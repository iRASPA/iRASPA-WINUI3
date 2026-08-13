#include "pch.h"
#include "SceneView.xaml.h"

#if __has_include("SceneView.g.cpp")
#include "SceneView.g.cpp"
#endif

#include "DetailControls.h"
#include "ObjectTypeMenu.h"
#include "RowProperty.h"

#include "iraspaobject.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>

#include <algorithm>
#include <cwctype>
#include <optional>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        // One row of the flat list: either a scene group row or a movie row.
        struct SceneRowItem : implements<SceneRowItem,
                                        ICustomPropertyProvider,
                                        INotifyPropertyChanged,
                                        IStringable>
        {
            hstring m_title;
            hstring m_iconGlyph;
            bool m_isScene{ false };
            // Cocoa TableListNameTextField: this row's name field is in edit mode.
            bool m_editing{ false };
            std::shared_ptr<Scene> m_scene;
            std::shared_ptr<Movie> m_movie;
            winrt::event<PropertyChangedEventHandler> m_propertyChanged;

            event_token PropertyChanged(PropertyChangedEventHandler const& handler)
            {
                return m_propertyChanged.add(handler);
            }
            void PropertyChanged(event_token const& token) noexcept
            {
                m_propertyChanged.remove(token);
            }

            void SetEditing(bool editing)
            {
                if (m_editing == editing)
                    return;
                m_editing = editing;
                m_propertyChanged(*this, PropertyChangedEventArgs(L"SceneTitleVisibility"));
                m_propertyChanged(*this, PropertyChangedEventArgs(L"MovieTitleVisibility"));
                m_propertyChanged(*this, PropertyChangedEventArgs(L"EditVisibility"));
            }

            ICustomProperty GetCustomProperty(hstring const& name)
            {
                auto self = [](IInspectable const& target) { return target.try_as<SceneRowItem>(); };

                if (name == L"Title")
                {
                    return make<RowProperty>(name, xaml_typename<hstring>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s ? s->m_title : hstring{});
                        });
                }
                if (name == L"IconGlyph")
                {
                    return make<RowProperty>(name, xaml_typename<hstring>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s ? s->m_iconGlyph : hstring{});
                        });
                }
                // Movie rows are indented under their scene group row.
                if (name == L"IndentMargin")
                {
                    return make<RowProperty>(name, xaml_typename<Thickness>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            const bool isScene = s && s->m_isScene;
                            return box_value(ThicknessHelper::FromLengths(isScene ? 4.0 : 18.0, 0, 0, 0));
                        });
                }
                // The checkbox and the type icon belong to movie rows only, and
                // stay put while the row is being renamed.
                if (name == L"MovieVisibility")
                {
                    return make<RowProperty>(name, xaml_typename<Visibility>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && !s->m_isScene ? Visibility::Visible
                                                                : Visibility::Collapsed);
                        });
                }
                // The two name labels also step aside for the editor, unlike the
                // checkbox and the type icon which stay put while renaming.
                if (name == L"SceneTitleVisibility" || name == L"MovieTitleVisibility")
                {
                    const bool wantScene = (name == L"SceneTitleVisibility");
                    return make<RowProperty>(name, xaml_typename<Visibility>(),
                        [self, wantScene](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            const bool isScene = s && s->m_isScene;
                            const bool editing = s && s->m_editing;
                            return box_value(!editing && isScene == wantScene ? Visibility::Visible
                                                                             : Visibility::Collapsed);
                        });
                }
                if (name == L"EditVisibility")
                {
                    return make<RowProperty>(name, xaml_typename<Visibility>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && s->m_editing ? Visibility::Visible
                                                               : Visibility::Collapsed);
                        });
                }
                if (name == L"Visible")
                {
                    return make<RowProperty>(name, xaml_typename<bool>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && s->m_movie ? s->m_movie->isVisible() : true);
                        });
                }
                return nullptr;
            }
            ICustomProperty GetIndexedProperty(hstring const&,
                                               winrt::Windows::UI::Xaml::Interop::TypeName const&)
            {
                return nullptr;
            }
            hstring GetStringRepresentation() { return m_title; }
            winrt::Windows::UI::Xaml::Interop::TypeName Type() { return xaml_typename<IInspectable>(); }
            hstring ToString() { return m_title; }
        };

        IInspectable MakeMovieRow(std::shared_ptr<Scene> const& scene,
                                  std::shared_ptr<Movie> const& movie)
        {
            auto item = make_self<SceneRowItem>();
            item->m_scene = scene;
            item->m_movie = movie;
            RKString name = movie ? movie->displayName() : RKString("Movie");
            if (name.trimmed().isEmpty())
                name = RKString("Movie");
            item->m_title = winrt::hstring(name.toStdWString());
            // Cocoa movieSingleFrameIcon / movieMultipleFramesIcon.
            item->m_iconGlyph = (movie && movie->frames().size() > 1) ? L"\uE714" : L"\uE8B9";
            return *item;
        }

        IInspectable MakeSceneRow(std::shared_ptr<Scene> const& scene)
        {
            auto item = make_self<SceneRowItem>();
            item->m_scene = scene;
            item->m_isScene = true;
            RKString name = scene ? scene->displayName() : RKString("Scene");
            if (name.trimmed().isEmpty())
                name = RKString("Scene");
            // Cocoa shows scene group rows uppercased.
            std::wstring upper = name.toStdWString();
            std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
            item->m_title = winrt::hstring(upper);
            return *item;
        }

        IInspectable MakePlaceholderRow()
        {
            auto item = make_self<SceneRowItem>();
            item->m_isScene = true;
            item->m_title = L"(no scenes)";
            return *item;
        }

        // The movie row under a point given in the list's own coordinates, or -1.
        // The visibility box and an open name editor take their own clicks, so a
        // press on either counts as no row.
        int32_t MovieRowIndexAt(ItemsView const& list,
                                IObservableVector<IInspectable> const& items,
                                Point const& localPoint)
        {
            if (!list || !items)
                return -1;

            com_ptr<SceneRowItem> found;
            auto rootPoint = list.TransformToVisual(nullptr).TransformPoint(localPoint);
            // Innermost first, so the last match is the row's own container.
            for (auto const& element : VisualTreeHelper::FindElementsInHostCoordinates(rootPoint, list))
            {
                if (element.try_as<TextBox>() || element.try_as<CheckBox>())
                    return -1;
                auto fe = element.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto row = fe.DataContext().try_as<SceneRowItem>();
                    row && row->m_movie && !row->m_isScene)
                {
                    found = row;
                }
            }
            if (!found)
                return -1;

            const uint32_t count = items.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (items.GetAt(i).try_as<SceneRowItem>().get() == found.get())
                    return static_cast<int32_t>(i);
            }
            return -1;
        }
    }

    SceneView::SceneView()
    {
        InitializeComponent();

        AddButton().ContextFlyout(BuildObjectTypeMenu(true, [this](ObjectType type)
        {
            if (m_controller)
                m_controller->AddSceneMovie(type);
        }));

        // handledEventsToo for both: the ItemContainer marks the press and the
        // double-tap handled before either can bubble up to the list.
        List().AddHandler(UIElement::PointerPressedEvent(),
                          box_value(PointerEventHandler{ this, &SceneView::OnListPointerPressed }),
                          true);
        // Cocoa movieSceneListViewDoubleClick: rename the clicked scene or movie.
        List().AddHandler(UIElement::DoubleTappedEvent(),
                          box_value(DoubleTappedEventHandler{ this, &SceneView::OnListDoubleTapped }),
                          true);
    }

    void SceneView::SetController(DocumentController* controller)
    {
        m_controller = controller;
        if (m_controller)
            m_controller->SetSceneList(this);
    }

    void SceneView::RefreshRows()
    {
        m_suppressSelectionEvents = true;

        if (!m_rows)
        {
            m_rows = single_threaded_observable_vector<IInspectable>();
            List().ItemsSource(m_rows);
        }

        auto sceneList = m_controller ? m_controller->SceneListOf() : nullptr;
        if (!sceneList)
        {
            std::vector<IInspectable> single{ MakePlaceholderRow() };
            try
            {
                m_rows.ReplaceAll(single);
            }
            catch (...)
            {
            }
            m_suppressSelectionEvents = false;
            return;
        }

        // Nothing selected yet (a freshly loaded project): fall back to the first
        // scene and its first movie, as Cocoa does.
        if (!sceneList->selectedScene() && !sceneList->scenes().empty())
        {
            auto first = sceneList->scenes().front();
            sceneList->setSelectedScene(first);
            if (first && !first->movies().empty())
                first->setSelectedMovie(first->movies().front());
        }

        std::vector<IInspectable> rows;
        for (auto const& scene : sceneList->scenes())
        {
            if (!scene)
                continue;
            rows.push_back(MakeSceneRow(scene));
            for (auto const& movie : scene->movies())
            {
                if (movie)
                    rows.push_back(MakeMovieRow(scene, movie));
            }
        }
        if (rows.empty())
            rows.push_back(MakePlaceholderRow());

        try
        {
            m_rows.ReplaceAll(rows);
        }
        catch (...)
        {
        }

        m_suppressSelectionEvents = false;
        ReloadRowSelection();
    }

    // Re-apply the model's selected movies to the list (after a rebuild or a
    // refused selection).
    void SceneView::ReloadRowSelection()
    {
        if (!m_rows)
            return;

        m_suppressSelectionEvents = true;
        try
        {
            List().DeselectAll();
            const uint32_t count = m_rows.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                auto item = m_rows.GetAt(i).try_as<SceneRowItem>();
                if (!item || item->m_isScene || !item->m_scene || !item->m_movie)
                    continue;
                auto const& selected = item->m_scene->selectedMovies();
                if (selected.find(item->m_movie) != selected.end())
                    List().Select(static_cast<int32_t>(i));
            }
        }
        catch (...)
        {
        }
        m_suppressSelectionEvents = false;
    }

    void SceneView::OnListSelectionChanged([[maybe_unused]] ItemsView const&,
                                           [[maybe_unused]] ItemsViewSelectionChangedEventArgs const&)
    {
        if (m_suppressSelectionEvents)
            return;
        CommitSelectionFromRows();
    }

    // A press on a row that is already part of a multi-selection is left alone by
    // the ItemContainer, so the selection only ever grew: clicking one of several
    // selected movies did nothing at all. Cocoa's list narrows to the clicked row,
    // which is what happens here. Every other press (Ctrl, Shift, or a row outside
    // the selection) is left to the list, which handles those itself.
    void SceneView::OnListPointerPressed([[maybe_unused]] IInspectable const&,
                                         PointerRoutedEventArgs const& e)
    {
        if (!m_rows || !m_controller)
            return;

        try
        {
            auto point = e.GetCurrentPoint(List());
            if (!point.Properties().IsLeftButtonPressed())
                return;

            using winrt::Windows::System::VirtualKeyModifiers;
            const auto mods = e.KeyModifiers();
            if ((mods & VirtualKeyModifiers::Control) == VirtualKeyModifiers::Control ||
                (mods & VirtualKeyModifiers::Shift) == VirtualKeyModifiers::Shift)
                return;

            const int32_t row = MovieRowIndexAt(List(), m_rows, point.Position());
            if (row < 0 || !List().IsSelected(row))
                return;

            uint32_t selected = 0;
            const uint32_t count = m_rows.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (List().IsSelected(static_cast<int32_t>(i)))
                    ++selected;
            }
            if (selected < 2)
                return;

            m_suppressSelectionEvents = true;
            List().DeselectAll();
            List().Select(row);
            m_suppressSelectionEvents = false;
            CommitSelectionFromRows();
        }
        catch (...)
        {
            m_suppressSelectionEvents = false;
        }
    }

    void SceneView::CommitSelectionFromRows()
    {
        if (!m_rows || !m_controller)
            return;

        // Cocoa group rows refuse selection, so a range that swept over a scene
        // row keeps its movie rows and gives that one up.
        std::vector<std::pair<std::shared_ptr<Scene>, std::shared_ptr<Movie>>> picks;
        m_suppressSelectionEvents = true;
        try
        {
            const uint32_t count = m_rows.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!List().IsSelected(static_cast<int32_t>(i)))
                    continue;
                auto item = m_rows.GetAt(i).try_as<SceneRowItem>();
                if (!item || !item->m_scene)
                    continue;
                if (item->m_isScene || !item->m_movie)
                {
                    List().Deselect(static_cast<int32_t>(i));
                    continue;
                }
                picks.emplace_back(item->m_scene, item->m_movie);
            }
        }
        catch (...)
        {
        }
        m_suppressSelectionEvents = false;

        // An empty selection is refused, as in Cocoa's list: the previous one
        // stays, so the inspector always has something to edit. The check is
        // deferred, so that a list clearing its selection only to fill it in again
        // is not handed its old one in between.
        if (picks.empty())
        {
            DispatcherQueue().TryEnqueue([this]() { RestoreSelectionIfEmpty(); });
            return;
        }
        m_controller->SelectSceneMovies(picks);
    }

    void SceneView::RestoreSelectionIfEmpty()
    {
        if (!m_rows)
            return;
        try
        {
            const uint32_t count = m_rows.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (List().IsSelected(static_cast<int32_t>(i)))
                    return;
            }
        }
        catch (...)
        {
            return;
        }
        ReloadRowSelection();
    }

    // Cocoa movieSceneListViewDoubleClick: a double-click hands the clicked scene
    // or movie name to the field editor.
    void SceneView::OnListDoubleTapped([[maybe_unused]] IInspectable const&,
                                       DoubleTappedRoutedEventArgs const& e)
    {
        if (!m_controller || !m_controller->IsSelectedProjectEditable())
            return;

        com_ptr<SceneRowItem> item;
        FrameworkElement row{ nullptr };
        try
        {
            auto rootPt = List().TransformToVisual(nullptr).TransformPoint(e.GetPosition(List()));
            // Innermost first, so the last match is the row's own container.
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List()))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto candidate = fe.DataContext().try_as<SceneRowItem>(); candidate && candidate->m_scene)
                {
                    item = candidate;
                    row = fe;
                }
            }
        }
        catch (...)
        {
            return;
        }
        if (!item)
            return;

        // Scene group rows carry the scene name (shown uppercased, edited raw),
        // movie rows the movie name.
        auto scene = item->m_scene;
        auto movie = item->m_movie;
        const bool isScene = item->m_isScene;
        if (!isScene && !movie)
            return;
        const std::wstring current = isScene ? scene->displayName().toStdWString()
                                             : movie->displayName().toStdWString();

        item->SetEditing(true);
        if (!m_rename.Begin(row, current, DispatcherQueue(),
                            [this, item, scene, movie, isScene](std::optional<std::wstring> text)
                            {
                                item->SetEditing(false);
                                if (text && m_controller)
                                {
                                    if (isScene)
                                        m_controller->SetSceneDisplayName(scene, *text);
                                    else
                                        m_controller->SetMovieDisplayName(movie, *text);
                                }
                                // Pointer, not Programmatic: see the project list,
                                // a keyboard commit would otherwise draw a focus
                                // rectangle around the whole list.
                                List().Focus(FocusState::Pointer);
                            }))
        {
            item->SetEditing(false);
            return;
        }
        e.Handled(true);
    }

    void SceneView::OnAddCrystalClick([[maybe_unused]] IInspectable const&,
                                      [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->AddSceneMovie(ObjectType::crystal);
    }

    void SceneView::OnRemoveClick([[maybe_unused]] IInspectable const&,
                                  [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->RemoveSelectedSceneMovie();
    }

    // The box reports itself only when a hand moved it. A row that is rebuilt or handed
    // to another movie moves the box too, and that must not be read as the user asking
    // for the movie underneath to be switched off.
    void SceneView::OnMovieVisibilityClick(IInspectable const& sender,
                                           [[maybe_unused]] RoutedEventArgs const&)
    {
        auto box = sender.try_as<CheckBox>();
        if (!box || !m_controller)
            return;
        auto item = box.DataContext().try_as<SceneRowItem>();
        if (!item || !item->m_movie)
            return;
        auto state = box.IsChecked();
        m_controller->SetMovieVisibility(item->m_scene, item->m_movie, state ? state.Value() : true);
    }

    void SceneView::OnMovieVisibilityBoxDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        if (auto box = sender.try_as<ToggleButton>())
            DetailControls::SyncCheckFromDataContext(box, L"Visible");
    }
}
