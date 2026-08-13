#include "pch.h"
#include "FrameView.xaml.h"

#if __has_include("FrameView.g.cpp")
#include "FrameView.g.cpp"
#endif

#include "ObjectTypeMenu.h"
#include "RowProperty.h"

#include "iraspaobject.h"
#include "movie.h"
#include "scenelist.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <optional>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        std::wstring FrameDisplayName(std::shared_ptr<iRASPAObject> const& frame, size_t index)
        {
            // Cocoa's frame list shows only the object's display name.
            if (frame && frame->object())
            {
                auto name = frame->object()->displayName().trimmed();
                if (!name.isEmpty())
                    return name.toStdWString();
            }
            return L"Frame " + std::to_wstring(index + 1);
        }

        // Stand-in for Cocoa's colorful per-material icons (crystalIcon,
        // molecularCrystalIcon, ...): a small rounded chip, colored by material
        // type, with a short type abbreviation.
        struct MaterialChip
        {
            wchar_t const* label;
            winrt::Windows::UI::Color color;
        };

        MaterialChip MaterialChipFor(ObjectType type)
        {
            wchar_t const* label = L"?";
            uint8_t r = 128, g = 128, b = 128;
            switch (type)
            {
            case ObjectType::crystal:
            case ObjectType::crystalSolvent:
                label = L"C"; r = 0x4A; g = 0x90; b = 0xD9; break;
            case ObjectType::molecularCrystal:
            case ObjectType::molecularCrystalSolvent:
                label = L"MC"; r = 0x50; g = 0xB8; b = 0x48; break;
            case ObjectType::molecule:
                label = L"M"; r = 0xF5; g = 0xA6; b = 0x23; break;
            case ObjectType::protein:
                label = L"P"; r = 0xE0; g = 0x52; b = 0x4D; break;
            case ObjectType::proteinCrystal:
            case ObjectType::proteinCrystalSolvent:
                label = L"PC"; r = 0x9B; g = 0x59; b = 0xB6; break;
            case ObjectType::crystalEllipsoidPrimitive:
            case ObjectType::crystalCylinderPrimitive:
            case ObjectType::crystalPolygonalPrismPrimitive:
            case ObjectType::ellipsoidPrimitive:
            case ObjectType::cylinderPrimitive:
            case ObjectType::polygonalPrismPrimitive:
                label = L"PR"; r = 0x6D; g = 0x82; b = 0x90; break;
            case ObjectType::gridVolume:
            case ObjectType::RASPADensityVolume:
            case ObjectType::VTKDensityVolume:
            case ObjectType::VASPDensityVolume:
            case ObjectType::GaussianCubeVolume:
                label = L"V"; r = 0x34; g = 0x49; b = 0x5E; break;
            default:
                break;
            }
            return MaterialChip{ label, winrt::Windows::UI::Color{ 255, r, g, b } };
        }

        // A frame row. Data rows (rather than ready-made panels) are what make the
        // list multi-selectable and drag-reorderable: the list moves entries of the
        // bound collection, and each entry carries its frame.
        struct FrameRowItem : implements<FrameRowItem,
                                        ICustomPropertyProvider,
                                        INotifyPropertyChanged,
                                        IStringable>
        {
            hstring m_title;
            hstring m_chipLabel;
            winrt::Windows::UI::Color m_chipColor{ 255, 128, 128, 128 };
            bool m_placeholder{ false };
            // Cocoa TableListNameTextField: this row's name field is in edit mode.
            bool m_editing{ false };
            std::shared_ptr<iRASPAObject> m_frame;
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
                m_propertyChanged(*this, PropertyChangedEventArgs(L"TitleVisibility"));
                m_propertyChanged(*this, PropertyChangedEventArgs(L"EditVisibility"));
            }

            ICustomProperty GetCustomProperty(hstring const& name)
            {
                auto self = [](IInspectable const& target) { return target.try_as<FrameRowItem>(); };

                if (name == L"Title")
                {
                    return make<RowProperty>(name, xaml_typename<hstring>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s ? s->m_title : hstring{});
                        });
                }
                if (name == L"ChipLabel")
                {
                    return make<RowProperty>(name, xaml_typename<hstring>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s ? s->m_chipLabel : hstring{});
                        });
                }
                if (name == L"ChipBrush")
                {
                    return make<RowProperty>(name, xaml_typename<Brush>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return SolidColorBrush(s ? s->m_chipColor
                                                     : winrt::Windows::UI::Color{ 255, 128, 128, 128 });
                        });
                }
                // The "(no frames)" row keeps the chip hidden and dims its text.
                if (name == L"ChipVisibility")
                {
                    return make<RowProperty>(name, xaml_typename<Visibility>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && s->m_placeholder ? Visibility::Collapsed
                                                                   : Visibility::Visible);
                        });
                }
                if (name == L"RowOpacity")
                {
                    return make<RowProperty>(name, xaml_typename<double>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && s->m_placeholder ? 0.6 : 1.0);
                        });
                }
                // The name label steps aside for the editor; the chip stays.
                if (name == L"TitleVisibility")
                {
                    return make<RowProperty>(name, xaml_typename<Visibility>(),
                        [self](IInspectable const& t) -> IInspectable
                        {
                            auto s = self(t);
                            return box_value(s && s->m_editing ? Visibility::Collapsed
                                                               : Visibility::Visible);
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

        IInspectable MakeFrameRow(std::shared_ptr<iRASPAObject> const& frame, size_t index)
        {
            auto item = make_self<FrameRowItem>();
            item->m_frame = frame;
            item->m_title = winrt::hstring(FrameDisplayName(frame, index));
            const ObjectType type = (frame && frame->object())
                ? frame->object()->structureType() : ObjectType::none;
            const MaterialChip chip = MaterialChipFor(type);
            item->m_chipLabel = chip.label;
            item->m_chipColor = chip.color;
            return *item;
        }

        IInspectable MakePlaceholderRow()
        {
            auto item = make_self<FrameRowItem>();
            item->m_placeholder = true;
            item->m_title = L"(no frames)";
            return *item;
        }

        // A press inside an open name editor belongs to the editor, not to the
        // list's selection/drag handling (Cocoa TableListNameTextField only
        // forwards mouseDown to the list while it is not renaming).
        bool PointIsOnRenameEditor(ItemsView const& list, Point const& localPoint)
        {
            if (!list)
                return false;
            auto rootPoint = list.TransformToVisual(nullptr).TransformPoint(localPoint);
            for (auto const& element : VisualTreeHelper::FindElementsInHostCoordinates(rootPoint, list))
            {
                if (element.try_as<TextBox>())
                    return true;
            }
            return false;
        }

        struct FrameHit
        {
            com_ptr<FrameRowItem> item;
            uint32_t index{ 0 };
            FrameworkElement container{ nullptr };
        };

        // The row (and its container, for the above/below decision) under a point
        // given in the list's own coordinates.
        FrameHit FrameRowAt(ItemsView const& list,
                            IObservableVector<IInspectable> const& items,
                            Point const& localPoint)
        {
            FrameHit hit;
            if (!list || !items)
                return hit;

            auto rootPoint = list.TransformToVisual(nullptr).TransformPoint(localPoint);
            // Innermost first, so the last match is the row's own container.
            for (auto const& element : VisualTreeHelper::FindElementsInHostCoordinates(rootPoint, list))
            {
                auto fe = element.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto row = fe.DataContext().try_as<FrameRowItem>(); row && row->m_frame)
                {
                    hit.item = row;
                    hit.container = fe;
                }
            }
            if (!hit.item)
                return hit;

            const uint32_t count = items.Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (items.GetAt(i).try_as<FrameRowItem>().get() == hit.item.get())
                {
                    hit.index = i;
                    return hit;
                }
            }
            return FrameHit{};
        }
    }

    FrameView::FrameView()
    {
        InitializeComponent();

        AddButton().ContextFlyout(BuildObjectTypeMenu(false, [this](ObjectType type)
        {
            if (m_controller)
                m_controller->AddMovieFrame(type);
        }));

        // handledEventsToo throughout: the CanDrag row content captures the pointer
        // and the ItemContainer marks the press and the double-tap handled before
        // either can bubble up to the list.
        List().AddHandler(UIElement::PointerPressedEvent(),
                          box_value(PointerEventHandler{ this, &FrameView::OnListPointerPressed }),
                          true);
        List().AddHandler(UIElement::PointerReleasedEvent(),
                          box_value(PointerEventHandler{ this, &FrameView::OnListPointerReleased }),
                          true);
        // Cocoa frameTableViewDoubleClick: rename the clicked frame.
        List().AddHandler(UIElement::DoubleTappedEvent(),
                          box_value(DoubleTappedEventHandler{ this, &FrameView::OnListDoubleTapped }),
                          true);
    }

    void FrameView::SetController(DocumentController* controller)
    {
        m_controller = controller;
        if (m_controller)
            m_controller->SetFrameList(this);
    }

    std::shared_ptr<iRASPAObject> FrameView::FrameAtRow(size_t row) const
    {
        return row < m_frames.size() ? m_frames[row] : nullptr;
    }

    void FrameView::RefreshRows()
    {
        m_suppressSelectionEvents = true;

        if (!m_rows)
        {
            m_rows = single_threaded_observable_vector<IInspectable>();
            List().ItemsSource(m_rows);
        }
        m_frames.clear();

        auto movie = m_controller ? m_controller->SelectedMovie() : nullptr;
        std::vector<IInspectable> rows;

        if (!movie || movie->frames().empty())
        {
            rows.push_back(MakePlaceholderRow());
        }
        else
        {
            for (size_t i = 0; i < movie->frames().size(); ++i)
            {
                auto frame = movie->frames()[i];
                m_frames.push_back(frame);
                rows.push_back(MakeFrameRow(frame, i));
            }
        }

        try
        {
            m_rows.ReplaceAll(rows);
        }
        catch (...)
        {
        }

        m_suppressSelectionEvents = false;
        if (!m_frames.empty())
            ReloadRowSelection();
    }

    // Cocoa reloadSelection(): the model owns the selection, the rows only show it.
    // The selection is filled in first, so the list is never left empty.
    void FrameView::ReloadRowSelection()
    {
        if (!m_rows || m_frames.empty() || !m_controller)
            return;
        auto movie = m_controller->SelectedMovie();
        auto sceneList = m_controller->SceneListOf();
        if (!movie || !sceneList)
            return;

        auto indices = movie->selectedFramesIndexSet();
        if (indices.empty())
        {
            size_t primary = sceneList->selectedFrameIndex();
            if (primary >= m_frames.size())
                primary = 0;
            indices.insert(primary);
            movie->setSelection(indices);
        }

        m_suppressSelectionEvents = true;
        try
        {
            List().DeselectAll();
            for (size_t index : indices)
            {
                if (index < m_rows.Size())
                    List().Select(static_cast<int32_t>(index));
            }
        }
        catch (...)
        {
        }
        m_suppressSelectionEvents = false;
    }

    void FrameView::OnListSelectionChanged([[maybe_unused]] ItemsView const&,
                                           [[maybe_unused]] ItemsViewSelectionChangedEventArgs const&)
    {
        if (m_suppressSelectionEvents)
            return;
        CommitSelectionFromRows();
    }

    void FrameView::CommitSelectionFromRows()
    {
        if (!m_rows || !m_controller)
            return;
        auto movie = m_controller->SelectedMovie();
        auto sceneList = m_controller->SceneListOf();
        if (!movie || !sceneList || m_frames.empty())
            return;

        FrameSelectionIndexSet selection;
        try
        {
            const uint32_t count = (std::min)(m_rows.Size(), static_cast<uint32_t>(m_frames.size()));
            for (uint32_t i = 0; i < count; ++i)
            {
                if (List().IsSelected(static_cast<int32_t>(i)))
                    selection.insert(static_cast<size_t>(i));
            }
        }
        catch (...)
        {
            return;
        }

        // Cocoa selectionIndexesForProposedSelection: an empty proposal is refused,
        // the previous selection stays.
        if (selection.empty())
        {
            ReloadRowSelection();
            return;
        }

        // Cocoa keeps a primary frame (selectedFrame) next to the selection: it
        // stays put while it remains selected, otherwise the newly clicked row takes
        // over.
        size_t primary = sceneList->selectedFrameIndex();
        if (selection.count(primary) == 0)
        {
            const auto previous = movie->selectedFramesIndexSet();
            primary = *selection.begin();
            for (size_t index : selection)
            {
                if (previous.count(index) == 0)
                {
                    primary = index;
                    break;
                }
            }
        }

        m_controller->SetFrameSelection(movie, primary, selection, L"Change Frame Selection");
    }

    void FrameView::OnListKeyDown([[maybe_unused]] IInspectable const&,
                                  KeyRoutedEventArgs const& e)
    {
        using winrt::Windows::System::VirtualKey;
        if (e.Key() != VirtualKey::Delete && e.Key() != VirtualKey::Back)
            return;
        if (m_controller)
            m_controller->DeleteSelectedFrames();
        e.Handled(true);
    }

    // Cocoa tableView(_:draggingSession:willBeginAt:forRowIndexes:): dragging a row
    // that is part of the selection drags the whole selection. The click itself is
    // handled here too, because the CanDrag row content captures the pointer and the
    // ItemContainer never sees the press.
    void FrameView::OnListPointerPressed([[maybe_unused]] IInspectable const&,
                                         PointerRoutedEventArgs const& e)
    {
        if (!m_rows || m_frames.empty())
            return;

        try
        {
            const auto local = e.GetCurrentPoint(List()).Position();
            if (PointIsOnRenameEditor(List(), local))
                return;
            auto hit = FrameRowAt(List(), m_rows, local);
            if (!hit.item)
                return;
            if (!e.GetCurrentPoint(List()).Properties().IsLeftButtonPressed())
                return;

            const int32_t rowIndex = static_cast<int32_t>(hit.index);
            const bool hitIsSelected = List().IsSelected(rowIndex);

            m_dragActive = false;
            m_dragFrames.clear();
            if (hitIsSelected)
            {
                const uint32_t count = m_rows.Size();
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (!List().IsSelected(static_cast<int32_t>(i)))
                        continue;
                    if (auto row = m_rows.GetAt(i).try_as<FrameRowItem>(); row && row->m_frame)
                        m_dragFrames.push_back(row->m_frame);
                }
            }
            else
            {
                m_dragFrames.push_back(hit.item->m_frame);
            }

            // Replicate the Extended-selection behavior (plain / Ctrl / Shift click)
            // the ItemContainer would have applied. The intermediate states are not
            // pushed to the model, only the result is.
            using winrt::Windows::System::VirtualKeyModifiers;
            const auto mods = e.KeyModifiers();
            const bool ctrl = (mods & VirtualKeyModifiers::Control) == VirtualKeyModifiers::Control;
            const bool shift = (mods & VirtualKeyModifiers::Shift) == VirtualKeyModifiers::Shift;

            m_suppressSelectionEvents = true;
            if (shift)
            {
                const int32_t count = static_cast<int32_t>(m_rows.Size());
                int32_t anchor = rowIndex;
                if (m_selectionAnchor >= 0 && m_selectionAnchor < count)
                    anchor = m_selectionAnchor;
                List().DeselectAll();
                for (int32_t i = (std::min)(anchor, rowIndex); i <= (std::max)(anchor, rowIndex); ++i)
                    List().Select(i);
            }
            else if (ctrl)
            {
                // Cocoa never leaves the frame table without a selection.
                if (hitIsSelected && m_dragFrames.size() > 1)
                    List().Deselect(rowIndex);
                else if (!hitIsSelected)
                    List().Select(rowIndex);
                m_selectionAnchor = rowIndex;
            }
            else
            {
                // Pressing a row of a multi-selection keeps it so the whole
                // selection can be dragged; a click that never becomes a drag
                // collapses onto the row when the button is released.
                if (!hitIsSelected)
                {
                    List().DeselectAll();
                    List().Select(rowIndex);
                }
                else if (m_dragFrames.size() > 1)
                {
                    m_pendingCollapseRow = rowIndex;
                }
                m_selectionAnchor = rowIndex;
            }
            m_suppressSelectionEvents = false;
            CommitSelectionFromRows();
        }
        catch (...)
        {
            m_suppressSelectionEvents = false;
        }
    }

    void FrameView::OnListPointerReleased([[maybe_unused]] IInspectable const&,
                                          [[maybe_unused]] PointerRoutedEventArgs const&)
    {
        const int32_t row = m_pendingCollapseRow;
        m_pendingCollapseRow = -1;
        // The pointer-up that ends a drag also reaches the list, sometimes ahead of
        // the drop: only disarm when nothing was dragged.
        if (!m_dragActive)
            m_dragFrames.clear();
        if (row < 0)
            return;

        try
        {
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

    // Cocoa frameTableViewDoubleClick: the clicked frame's name goes into the field
    // editor.
    void FrameView::OnListDoubleTapped([[maybe_unused]] IInspectable const&,
                                       DoubleTappedRoutedEventArgs const& e)
    {
        if (!m_controller || !m_controller->IsSelectedProjectEditable())
            return;

        com_ptr<FrameRowItem> item;
        FrameworkElement row{ nullptr };
        try
        {
            auto rootPt = List().TransformToVisual(nullptr).TransformPoint(e.GetPosition(List()));
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List()))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto candidate = fe.DataContext().try_as<FrameRowItem>();
                    candidate && candidate->m_frame && candidate->m_frame->object())
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

        auto frame = item->m_frame;
        item->SetEditing(true);
        if (!m_rename.Begin(row, frame->object()->displayName().toStdWString(), DispatcherQueue(),
                            [this, item, frame](std::optional<std::wstring> text)
                            {
                                item->SetEditing(false);
                                if (text && m_controller)
                                    m_controller->SetFrameDisplayName(frame, *text);
                                // Pointer, not Programmatic: see the project list, a
                                // keyboard commit would otherwise draw a focus
                                // rectangle around the whole list.
                                List().Focus(FocusState::Pointer);
                            }))
        {
            item->SetEditing(false);
            return;
        }
        e.Handled(true);
    }

    void FrameView::OnListDragOver([[maybe_unused]] IInspectable const&, DragEventArgs const& e)
    {
        m_pendingCollapseRow = -1;
        if (m_dragFrames.empty())
        {
            e.AcceptedOperation(DataPackageOperation::None);
            return;
        }
        m_dragActive = true;
        e.AcceptedOperation(DataPackageOperation::Move);
        e.DragUIOverride().IsGlyphVisible(false);
        e.DragUIOverride().Caption(m_dragFrames.size() == 1 ? L"Move frame" : L"Move frames");
    }

    // Cocoa internalDrop: the dragged frames land at the drop row, the ones below
    // shift down.
    void FrameView::OnListDrop([[maybe_unused]] IInspectable const&, DragEventArgs const& e)
    {
        auto dragged = m_dragFrames;
        m_dragFrames.clear();
        m_dragActive = false;
        if (dragged.empty() || !m_rows || !m_controller)
            return;

        auto movie = m_controller->SelectedMovie();
        if (!movie || movie->frames().empty())
            return;

        const std::vector<std::shared_ptr<iRASPAObject>> current = movie->frames();
        size_t row = current.size();
        try
        {
            auto hit = FrameRowAt(List(), m_rows, e.GetPosition(List()));
            if (hit.item && hit.container)
            {
                row = hit.index;
                // Below the middle of a row means "after it" (Cocoa drops between
                // rows, never onto one).
                const auto local = e.GetPosition(hit.container);
                if (local.Y > hit.container.ActualHeight() * 0.5)
                    row = hit.index + 1;
            }
        }
        catch (...)
        {
        }

        // The frames that stay, plus where the dragged block lands among them.
        std::vector<std::shared_ptr<iRASPAObject>> remaining;
        remaining.reserve(current.size());
        size_t insertAt = 0;
        for (size_t i = 0; i < current.size(); ++i)
        {
            const bool isDragged = std::find(dragged.begin(), dragged.end(), current[i]) != dragged.end();
            if (isDragged)
                continue;
            if (i < row)
                ++insertAt;
            remaining.push_back(current[i]);
        }

        std::vector<std::shared_ptr<iRASPAObject>> order;
        order.reserve(current.size());
        order.insert(order.end(), remaining.begin(), remaining.begin() + insertAt);
        order.insert(order.end(), dragged.begin(), dragged.end());
        order.insert(order.end(), remaining.begin() + insertAt, remaining.end());

        if (order.size() != current.size() || order == current)
        {
            ReloadRowSelection();
            return;
        }

        m_controller->ReorderMovieFrames(movie, order, L"Reorder frames");
    }

    void FrameView::OnAddClick([[maybe_unused]] IInspectable const&,
                               [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->AddFrameOfMovieType();
    }

    void FrameView::OnRemoveClick([[maybe_unused]] IInspectable const&,
                                  [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->DeleteSelectedFrames();
    }
}
