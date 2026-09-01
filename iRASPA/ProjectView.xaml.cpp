#include "pch.h"
#include "ProjectView.xaml.h"

#if __has_include("ProjectView.g.cpp")
#include "ProjectView.g.cpp"
#endif

#include "ProjectNodeUtil.h"

#include "iraspaobject.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <algorithm>
#include <cwctype>
#include <functional>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

// Virtualized project tree (same pattern as the Atoms ItemsView): the nested
// ProjectTreeNode hierarchy is flattened into one ObservableVector bound to a
// virtualized ItemsView; Indent tracks depth and becomes a left margin.
// Expand/collapse rebuilds the flat vector from the model in one ReplaceAll,
// which UI virtualization makes cheap — only visible rows are ever realized,
// unlike the WinUI TreeView which realizes a container per node.

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        struct ProjectItemProperty : implements<ProjectItemProperty, ICustomProperty>
        {
            using Getter = std::function<IInspectable(IInspectable const&)>;

            ProjectItemProperty(hstring name, winrt::Windows::UI::Xaml::Interop::TypeName type, Getter getter)
                : m_name(std::move(name)), m_type(type), m_getter(std::move(getter)) {}

            winrt::Windows::UI::Xaml::Interop::TypeName Type() const { return m_type; }
            hstring Name() const { return m_name; }
            bool CanRead() const { return true; }
            bool CanWrite() const { return false; }
            IInspectable GetValue(IInspectable const& target) const { return m_getter(target); }
            void SetValue(IInspectable const&, IInspectable const&) const { throw hresult_not_implemented(); }
            IInspectable GetIndexedValue(IInspectable const&, IInspectable const&) const { throw hresult_not_implemented(); }
            void SetIndexedValue(IInspectable const&, IInspectable const&, IInspectable const&) const { throw hresult_not_implemented(); }

        private:
            hstring m_name;
            winrt::Windows::UI::Xaml::Interop::TypeName m_type;
            Getter m_getter;
        };
    }

    // Flat row view model. Section roots (GALLERY / LOCAL PROJECTS /
    // DATABASES PUBLIC — Cocoa isGroupItem) render with the small bold
    // gray style, hide the expander, and are always expanded.
    struct ProjectFlatItem : implements<ProjectFlatItem,
                                       ICustomPropertyProvider,
                                       INotifyPropertyChanged,
                                       IStringable>
    {
        hstring m_title;
        hstring m_iconGlyph;
        int32_t m_indent{ 0 };
        bool m_expanded{ false };
        bool m_hasChildren{ false };
        bool m_isSectionRoot{ false };
        // Cocoa isReadOnlyLibraryNode: a row inside the gallery or the public
        // databases, which is shown locked because it can only be edited once it
        // has been dragged into LOCAL PROJECTS.
        bool m_readOnly{ false };
        hstring m_readOnlyToolTip;
        // Cocoa TableListNameTextField: the name field of this row is being
        // edited, so its label gives way to the editor.
        bool m_editing{ false };
        std::shared_ptr<ProjectTreeNode> m_node;
        winrt::event<PropertyChangedEventHandler> m_propertyChanged;

        event_token PropertyChanged(PropertyChangedEventHandler const& handler)
        {
            return m_propertyChanged.add(handler);
        }
        void PropertyChanged(event_token const& token) noexcept
        {
            m_propertyChanged.remove(token);
        }

        void Raise(wchar_t const* prop)
        {
            m_propertyChanged(*this, PropertyChangedEventArgs(prop));
        }

        void SetEditing(bool editing)
        {
            if (m_editing == editing)
                return;
            m_editing = editing;
            Raise(L"TitleVisibility");
            Raise(L"EditVisibility");
        }

        hstring ExpanderGlyphText() const
        {
            // Cocoa shouldShowOutlineCellForItem=false on section roots.
            if (m_isSectionRoot || !m_hasChildren)
                return L" ";
            return m_expanded ? L"\u25BE" : L"\u25B8"; // ▾ expanded, ▸ collapsed
        }

        Thickness IndentMarginValue() const
        {
            return ThicknessHelper::FromLengths(static_cast<double>(m_indent) * 16.0, 0, 0, 0);
        }

        Visibility SectionVisibilityValue() const
        {
            return m_isSectionRoot ? Visibility::Visible : Visibility::Collapsed;
        }

        Visibility NormalVisibilityValue() const
        {
            return m_isSectionRoot ? Visibility::Collapsed : Visibility::Visible;
        }

        Visibility TitleVisibilityValue() const
        {
            return (m_isSectionRoot || m_editing) ? Visibility::Collapsed : Visibility::Visible;
        }

        Visibility EditVisibilityValue() const
        {
            return m_editing ? Visibility::Visible : Visibility::Collapsed;
        }

        Visibility LockVisibilityValue() const
        {
            return m_readOnly ? Visibility::Visible : Visibility::Collapsed;
        }

        // Cocoa dims the name of a read-only row to secondaryLabelColor, which is
        // the label color at reduced alpha; an opacity says the same thing without
        // naming a color that only suits one theme.
        double TitleOpacityValue() const
        {
            return m_readOnly ? 0.55 : 1.0;
        }

        ICustomProperty GetCustomProperty(hstring const& name)
        {
            auto self = [](IInspectable const& target) { return target.try_as<ProjectFlatItem>(); };

            if (name == L"Title")
            {
                return make<ProjectItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->m_title : hstring{});
                    });
            }
            if (name == L"ExpanderGlyph")
            {
                return make<ProjectItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ExpanderGlyphText() : hstring{ L" " });
                    });
            }
            if (name == L"IconGlyph")
            {
                return make<ProjectItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->m_iconGlyph : hstring{});
                    });
            }
            if (name == L"IndentMargin")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Thickness>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->IndentMarginValue()
                                           : ThicknessHelper::FromUniformLength(0));
                    });
            }
            if (name == L"SectionVisibility")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->SectionVisibilityValue() : Visibility::Collapsed);
                    });
            }
            if (name == L"NormalVisibility")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->NormalVisibilityValue() : Visibility::Visible);
                    });
            }
            if (name == L"TitleVisibility")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->TitleVisibilityValue() : Visibility::Visible);
                    });
            }
            if (name == L"EditVisibility")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->EditVisibilityValue() : Visibility::Collapsed);
                    });
            }
            if (name == L"LockVisibility")
            {
                return make<ProjectItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->LockVisibilityValue() : Visibility::Collapsed);
                    });
            }
            if (name == L"TitleOpacity")
            {
                return make<ProjectItemProperty>(name, xaml_typename<double>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->TitleOpacityValue() : 1.0);
                    });
            }
            if (name == L"ReadOnlyToolTip")
            {
                return make<ProjectItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        // Nothing rather than an empty string: an empty tooltip still
                        // pops an empty box up over every local row.
                        if (!s || s->m_readOnlyToolTip.empty())
                            return nullptr;
                        return box_value(s->m_readOnlyToolTip);
                    });
            }
            return nullptr;
        }
        ICustomProperty GetIndexedProperty(hstring const&, winrt::Windows::UI::Xaml::Interop::TypeName const&)
        {
            return nullptr;
        }
        hstring GetStringRepresentation() { return m_title; }
        winrt::Windows::UI::Xaml::Interop::TypeName Type()
        {
            return xaml_typename<IInspectable>();
        }
        hstring ToString() { return m_title; }
    };

    namespace
    {
        std::shared_ptr<ProjectTreeNode> NodeFromItem(IInspectable const& item)
        {
            if (auto p = item.try_as<ProjectFlatItem>())
                return p->m_node;
            return nullptr;
        }
    }

    ProjectView::ProjectView()
    {
        InitializeComponent();

        // handledEventsToo: the ItemContainer eats these before they bubble, so
        // they cannot be attached from markup.
        List().AddHandler(UIElement::PointerPressedEvent(),
                          box_value(PointerEventHandler{ this, &ProjectView::OnListPointerPressed }),
                          true);
        List().AddHandler(UIElement::RightTappedEvent(),
                          box_value(RightTappedEventHandler{ this, &ProjectView::OnListRightTapped }),
                          true);
        // Cocoa projectOutlineViewDoubleClick: rename the clicked row.
        List().AddHandler(UIElement::DoubleTappedEvent(),
                          box_value(DoubleTappedEventHandler{ this, &ProjectView::OnListDoubleTapped }),
                          true);
    }

    void ProjectView::SetController(DocumentController* controller)
    {
        m_controller = controller;
        if (m_controller)
            m_controller->SetProjectList(this);
    }

    std::shared_ptr<ProjectTreeController> ProjectView::Tree() const
    {
        return m_controller ? m_controller->ProjectTree() : nullptr;
    }

    void ProjectView::Bind()
    {
        if (!Tree())
            return;

        // Already bound: never rebind the ItemsSource, only refresh its content.
        if (List().ItemsSource())
            return;

        try
        {
            m_flatItems = single_threaded_observable_vector<IInspectable>();
            List().ItemsSource(m_flatItems);

            // Cocoa: section roots (GALLERY / LOCAL PROJECTS / DATABASES PUBLIC)
            // and their folders (Gallery / Local Projects / Databases Public)
            // start expanded. This also builds the flat rows.
            ExpandSectionRoots();
        }
        catch (hresult_error const& ex)
        {
            m_suppressSelectionEvents = false;
            m_controller->Log(std::wstring(L"Project ItemsView bind error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            m_suppressSelectionEvents = false;
            m_controller->Log(L"Project ItemsView bind error");
        }
    }

    // Rebuild the flat mirror from the model, honoring the expanded-node set.
    // With UI virtualization only the visible rows get containers, so this is
    // cheap even for large galleries (the row objects themselves are trivial).
    void ProjectView::RefreshRows()
    {
        auto controller = Tree();
        if (!m_flatItems || !controller)
            return;

        // Drop expired entries (deleted nodes).
        for (auto it = m_expandedNodes.begin(); it != m_expandedNodes.end();)
            it = it->expired() ? m_expandedNodes.erase(it) : std::next(it);

        // Cocoa infoPanelIcon equivalents: folder for groups, cloud for
        // public-database entries, document for regular projects.
        auto iconFor = [](std::shared_ptr<ProjectTreeNode> const& node) -> wchar_t const*
        {
            if (NodeIsGroup(node))
                return L"\uE8B7"; // folder
            if (node->type() == ProjectTreeNode::Type::cloud)
                return L"\uE753"; // cloud
            return L"\uE7C3";     // document
        };

        // Cocoa readOnlyLibraryTooltip: which library a row belongs to, the gallery
        // or the public databases, since only those two are read-only. Taken from
        // where the row sits rather than from isEditable, which is also false on the
        // "Local Projects" folder itself.
        auto sectionOf = [](std::shared_ptr<ProjectTreeNode> const& folder)
        {
            // The folder's own section header, so that the header carries the
            // tooltip too, as it does in Cocoa.
            return folder ? folder->parent() : nullptr;
        };
        const auto galleryRoot = sectionOf(controller->galleryProjects());
        const auto databaseRoot = sectionOf(controller->icloudProjects());
        auto readOnlyToolTip = [&](std::shared_ptr<ProjectTreeNode> const& node) -> wchar_t const*
        {
            if (!node)
                return nullptr;
            if (galleryRoot && node->isDescendantOfNode(galleryRoot.get()))
                return L"Gallery projects are read-only. Drag them to Local Projects to edit.";
            if (databaseRoot && node->isDescendantOfNode(databaseRoot.get()))
                return L"Database projects are read-only. Drag them to Local Projects to edit.";
            return nullptr;
        };

        std::vector<IInspectable> rows;

        if (!m_filter.empty())
        {
            // Filter mode (like the Atoms tab search): a flat list of every
            // node whose name contains the filter, hierarchy ignored.
            std::wstring needle = m_filter;
            std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);

            std::function<void(std::shared_ptr<ProjectTreeNode> const&)> visit =
                [&](std::shared_ptr<ProjectTreeNode> const& node)
            {
                if (!node)
                    return;
                std::wstring name = node->displayName().toStdWString();
                std::wstring lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (!name.empty() && lower.find(needle) != std::wstring::npos)
                {
                    auto item = make_self<ProjectFlatItem>();
                    item->m_node = node;
                    item->m_title = hstring(name);
                    item->m_indent = 0;
                    item->m_isSectionRoot = false;
                    item->m_hasChildren = false; // no expanders in filter mode
                    item->m_expanded = false;
                    item->m_iconGlyph = iconFor(node);
                    if (wchar_t const* tip = readOnlyToolTip(node))
                    {
                        item->m_readOnly = true;
                        item->m_readOnlyToolTip = tip;
                    }
                    rows.push_back(*item);
                }
                for (auto const& child : node->childNodes())
                    visit(child);
            };
            // Only the nodes below the section roots, not the roots themselves.
            for (auto const& root : controller->rootNodes())
            {
                if (!root)
                    continue;
                for (auto const& child : root->childNodes())
                    visit(child);
            }
        }
        else
        {
            std::function<void(std::shared_ptr<ProjectTreeNode> const&, int32_t, bool)> append =
                [&](std::shared_ptr<ProjectTreeNode> const& node, int32_t indent, bool sectionRoot)
            {
                if (!node || node->displayName().trimmed().isEmpty())
                    return;
                // Section roots are always expanded (their expander is hidden).
                const bool expanded = sectionRoot || m_expandedNodes.count(node) > 0;
                auto item = make_self<ProjectFlatItem>();
                item->m_node = node;
                item->m_title = hstring(node->displayName().toStdWString());
                item->m_indent = indent;
                item->m_isSectionRoot = sectionRoot;
                item->m_hasChildren = NodeIsGroup(node) || node->childCount() > 0;
                item->m_expanded = expanded;
                if (!sectionRoot)
                    item->m_iconGlyph = iconFor(node);
                if (wchar_t const* tip = readOnlyToolTip(node))
                {
                    // The section headers explain the lock, they do not wear one.
                    item->m_readOnly = !sectionRoot;
                    item->m_readOnlyToolTip = tip;
                }
                rows.push_back(*item);
                if (expanded)
                {
                    for (auto const& child : node->childNodes())
                        append(child, indent + 1, false);
                }
            };
            for (auto const& root : controller->rootNodes())
                append(root, 0, /*sectionRoot=*/true);
        }

        m_suppressSelectionEvents = true;
        try
        {
            m_flatItems.ReplaceAll(rows);
        }
        catch (...)
        {
        }
        m_suppressSelectionEvents = false;

        ReloadRowSelection();
    }

    // Used when the search filter is cleared: keep the selected project
    // reachable by expanding its ancestors, then scroll its row into view.
    void ProjectView::RevealSelected()
    {
        std::shared_ptr<ProjectTreeNode> primary;
        if (auto controller = Tree())
            primary = controller->selectedTreeNode();

        if (primary)
        {
            for (auto p = primary->parent(); p; p = p->parent())
                m_expandedNodes.insert(p);
        }

        RefreshRows();

        if (!primary || !m_flatItems)
            return;
        const uint32_t count = m_flatItems.Size();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (NodeFromItem(m_flatItems.GetAt(i)) == primary)
            {
                try
                {
                    List().StartBringItemIntoView(static_cast<int32_t>(i), nullptr);
                }
                catch (...)
                {
                }
                break;
            }
        }
    }

    // Reveal a freshly inserted row: its whole ancestor chain has to be open
    // before the rebuild, or the row is not among the flattened ones.
    void ProjectView::RevealNode(std::shared_ptr<ProjectTreeNode> const& node)
    {
        for (auto p = node; p; p = p->parent())
            m_expandedNodes.insert(p);
        RefreshRows();
    }

    void ProjectView::ForgetNode(std::shared_ptr<ProjectTreeNode> const& node)
    {
        m_expandedNodes.erase(std::weak_ptr<ProjectTreeNode>(node));
    }

    void ProjectView::ExpandSectionRoots()
    {
        auto controller = Tree();
        if (!controller)
            return;

        // Mark the three section roots and their direct children
        // (Gallery / Local Projects / Databases Public) expanded.
        for (auto const& root : controller->rootNodes())
        {
            if (!root)
                continue;
            m_expandedNodes.insert(root);
            for (auto const& child : root->childNodes())
            {
                if (child)
                    m_expandedNodes.insert(child);
            }
        }

        RefreshRows();
    }

    // Full refresh: regenerate the flat rows from the model (expansion state
    // survives — it lives in m_expandedNodes, not in the rows).
    void ProjectView::Recreate()
    {
        m_refreshPending = false;
        m_dragNodes.clear();
        RefreshRows();
    }

    void ProjectView::ScheduleRefresh()
    {
        if (m_refreshPending)
            return;
        m_refreshPending = true;
        DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [this]()
            {
                try
                {
                    Recreate();
                }
                catch (...)
                {
                    m_refreshPending = false;
                    if (m_controller)
                        m_controller->Log(L"Project tree update error");
                }
            });
    }

    // Cocoa reloadSelection(): re-apply the controller selection to the view
    // after a mutation, without touching rows.
    void ProjectView::ReloadRowSelection()
    {
        auto controller = Tree();
        if (!m_flatItems || !controller)
            return;

        m_suppressSelectionEvents = true;
        try
        {
            auto primary = controller->selectedTreeNode();
            List().DeselectAll();
            if (primary)
            {
                const uint32_t count = m_flatItems.Size();
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (NodeFromItem(m_flatItems.GetAt(i)) == primary)
                    {
                        List().Select(static_cast<int32_t>(i));
                        break;
                    }
                }
            }
        }
        catch (...)
        {
            // Selection reload is cosmetic; the controller state stays valid.
        }
        m_suppressSelectionEvents = false;
    }

    // Cocoa outlineViewSelectionDidChange: push the view selection into the
    // controller; section roots are not selectable.
    void ProjectView::SyncSelectionFromView()
    {
        if (!Tree() || m_suppressSelectionEvents)
            return;

        std::shared_ptr<ProjectTreeNode> node;
        try
        {
            node = NodeFromItem(List().SelectedItem());

            // Cocoa selectionIndexesForProposedSelection: section roots
            // (LOCAL PROJECTS / GALLERY / ICLOUD) cannot be selected; keep
            // the previous selection instead.
            if (node && m_controller->IsSectionRoot(node))
            {
                ReloadRowSelection();
                return;
            }
        }
        catch (...)
        {
            m_suppressSelectionEvents = false;
            return;
        }

        // Null selection here comes from async collection resets (ReplaceAll)
        // or programmatic deselects, never from a user picking a project, so
        // leave the controller selection untouched.
        if (!node)
            return;

        m_controller->SetProjectSelection(node, { node }, L"Change Project Selection");
    }

    void ProjectView::OnListSelectionChanged([[maybe_unused]] ItemsView const& sender,
                                         [[maybe_unused]] ItemsViewSelectionChangedEventArgs const& args)
    {
        SyncSelectionFromView();
        // The selected project owns its own undo stack (Cocoa
        // windowWillReturnUndoManager), so the Edit menu follows the selection
        // even when the selection itself did not end up changing.
        if (m_controller)
            m_controller->RefreshEditMenuLabels();
    }

    // Expander clicks toggle the node in the expanded set and rebuild the flat
    // rows; any other press just records the node as a potential drag source
    // (the row template has CanDrag=True) and falls through to the ItemsView's
    // own selection handling.
    void ProjectView::OnListPointerPressed([[maybe_unused]] IInspectable const&,
                                       PointerRoutedEventArgs const& e)
    {
        if (!m_flatItems)
            return;

        try
        {
            auto local = e.GetCurrentPoint(List()).Position();
            auto toRoot = List().TransformToVisual(nullptr);
            auto rootPt = toRoot.TransformPoint(local);
            auto elements = VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List());

            com_ptr<ProjectFlatItem> hitItem;
            bool onExpander = false;
            bool onEditor = false;
            for (auto const& el : elements)
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto tb = fe.try_as<TextBlock>();
                    tb && unbox_value_or<hstring>(tb.Tag(), L"") == L"expander")
                    onExpander = true;
                if (fe.try_as<TextBox>())
                    onEditor = true;
                if (!hitItem)
                    hitItem = fe.DataContext().try_as<ProjectFlatItem>();
            }
            if (!hitItem || !hitItem->m_node)
                return;

            // A press inside the open name editor places its caret; it must not
            // re-select the row or arm a drag (Cocoa TableListNameTextField
            // forwards mouseDown to the list only while it is not renaming).
            if (onEditor)
                return;

            if (onExpander && !hitItem->m_isSectionRoot && hitItem->m_hasChildren)
            {
                auto node = hitItem->m_node;
                if (hitItem->m_expanded)
                    m_expandedNodes.erase(std::weak_ptr<ProjectTreeNode>(node));
                else
                    m_expandedNodes.insert(node);
                RefreshRows();
                e.Handled(true);
                return;
            }

            // Remember the pressed node: if this press turns into a drag, the
            // DragOver/Drop handlers use it as the drag source.
            m_dragNodes.clear();
            if (!hitItem->m_isSectionRoot)
                m_dragNodes.push_back(hitItem->m_node);

            // Select the row here: the CanDrag row content captures the pointer
            // for drag detection, so the ItemContainer never receives the click
            // and would not select on its own.
            if (!hitItem->m_isSectionRoot &&
                e.GetCurrentPoint(List()).Properties().IsLeftButtonPressed())
            {
                const uint32_t count = m_flatItems.Size();
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (NodeFromItem(m_flatItems.GetAt(i)) == hitItem->m_node)
                    {
                        List().Select(static_cast<int32_t>(i));
                        break;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    // Cocoa projectOutlineViewDoubleClick: a double-click on an editable row
    // hands its name field to the field editor.
    void ProjectView::OnListDoubleTapped([[maybe_unused]] IInspectable const&,
                                     DoubleTappedRoutedEventArgs const& e)
    {
        if (!m_controller)
            return;

        com_ptr<ProjectFlatItem> item;
        FrameworkElement row{ nullptr };
        try
        {
            auto rootPt = List().TransformToVisual(nullptr)
                              .TransformPoint(e.GetPosition(List()));
            // Innermost first, so the last match is the row's own container —
            // the one holding both the label and the editor.
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List()))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto candidate = fe.DataContext().try_as<ProjectFlatItem>(); candidate && candidate->m_node)
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

        if (!item || item->m_isSectionRoot || !m_controller->CanEditProjectNode(item->m_node))
            return;

        auto node = item->m_node;
        item->SetEditing(true);
        if (!m_rename.Begin(row, node->displayName().toStdWString(), DispatcherQueue(),
                            [this, item, node](std::optional<std::wstring> text)
                            {
                                item->SetEditing(false);
                                if (text && m_controller)
                                    m_controller->SetProjectDisplayName(node, *text);
                                // Keep the list focused so Edit > Undo still
                                // routes to the document stack. Pointer, not
                                // Programmatic: a programmatic focus adopts the
                                // last input mode, so committing with Enter
                                // would draw the keyboard focus rectangle around
                                // the whole list.
                                List().Focus(FocusState::Pointer);
                            }))
        {
            item->SetEditing(false);
            return;
        }
        e.Handled(true);
    }

    // Cocoa Project Context Menu (Master.storyboard, "Project Context Menu"):
    // Delete Selection | Add Structure project | Add Group project | Compute
    // properties for selection. The cloud/database items are hidden in Cocoa
    // and are not ported. Enabled states mirror validateMenuItem.
    void ProjectView::OnListRightTapped([[maybe_unused]] IInspectable const&,
                                    RightTappedRoutedEventArgs const& e)
    {
        auto tree = Tree();
        if (!tree)
            return;

        // Cocoa clickedRow: which row is under the pointer (may be none).
        std::shared_ptr<ProjectTreeNode> clicked;
        try
        {
            auto pos = e.GetPosition(List());
            auto toRoot = List().TransformToVisual(nullptr);
            auto rootPt = toRoot.TransformPoint(pos);
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List()))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto item = fe.DataContext().try_as<ProjectFlatItem>(); item && item->m_node)
                {
                    if (!item->m_isSectionRoot)
                        clicked = item->m_node;
                    break;
                }
            }
        }
        catch (...)
        {
        }

        const bool hasSelection =
            tree->selectedTreeNode() != nullptr || !tree->selectedTreeNodes().empty();
        // Cocoa validateMenuItem: adding is allowed on rows under LOCAL
        // PROJECTS and on empty space (falls back to the local root); it is
        // disabled on gallery/cloud rows.
        const bool canAdd = !clicked || m_controller->IsUnderLocalProjects(clicked);

        MenuFlyout menu;
        auto append = [&menu](wchar_t const* text, bool enabled, RoutedEventHandler const& handler)
        {
            MenuFlyoutItem item;
            item.Text(text);
            item.IsEnabled(enabled);
            item.Click(handler);
            menu.Items().Append(item);
        };
        append(L"Delete Selection", hasSelection,
               [this](IInspectable const&, RoutedEventArgs const&)
               { m_controller->DeleteSelectedProjects(); });
        append(L"Add Structure project", canAdd,
               [this, clicked](IInspectable const&, RoutedEventArgs const&)
               { m_controller->AddProjectNodeFromContext(false, clicked); });
        append(L"Add Group project", canAdd,
               [this, clicked](IInspectable const&, RoutedEventArgs const&)
               { m_controller->AddProjectNodeFromContext(true, clicked); });
        append(L"Compute properties for selection", hasSelection,
               [this](IInspectable const&, RoutedEventArgs const&)
               { m_controller->ComputePropertiesForProjectSelection(); });

        try
        {
            menu.ShowAt(List(), e.GetPosition(List()));
        }
        catch (...)
        {
        }
        e.Handled(true);
    }

    void ProjectView::OnDeleteAccelerator([[maybe_unused]] KeyboardAccelerator const&,
                                          KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (m_controller)
            m_controller->DeleteSelectedProjects();
        args.Handled(true);
    }

    void ProjectView::OnAddStructureClick([[maybe_unused]] IInspectable const&,
                                          [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->AddProjectFromToolbar(false);
    }

    void ProjectView::OnAddGroupClick([[maybe_unused]] IInspectable const&,
                                      [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->AddProjectFromToolbar(true);
    }

    void ProjectView::OnRemoveClick([[maybe_unused]] IInspectable const&,
                                    [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->DeleteSelectedProjects();
    }

    void ProjectView::OnSearchTextChanged(IInspectable const& sender,
                                          [[maybe_unused]] TextChangedEventArgs const&)
    {
        if (auto tb = sender.try_as<TextBox>())
        {
            const std::wstring next(tb.Text());
            const bool clearing = next.empty() && !m_filter.empty();
            m_filter = next;
            if (clearing)
                RevealSelected();
            else
                RefreshRows();
        }
    }

    // Cocoa validateDrop: only into LOCAL PROJECTS, drop-on requires a group,
    // never into the dragged node's own subtree.
    void ProjectView::OnListDragOver([[maybe_unused]] IInspectable const&, DragEventArgs const& e)
    {
        if (!m_controller || m_dragNodes.empty() || m_dragNodes.front() == nullptr)
        {
            e.AcceptedOperation(DataPackageOperation::None);
            return;
        }
        auto const& node = m_dragNodes.front();
        // Local editable nodes move; gallery/cloud nodes copy into local.
        const bool fromLocal = m_controller->IsUnderLocalProjects(node) &&
                               m_controller->CanEditProjectNode(node);
        e.AcceptedOperation(fromLocal ? DataPackageOperation::Move
                                      : DataPackageOperation::Copy);
    }

    void ProjectView::OnListDrop([[maybe_unused]] IInspectable const&, DragEventArgs const& e)
    {
        auto dragged = m_dragNodes;
        m_dragNodes.clear();
        if (dragged.empty() || !dragged.front() || !Tree())
            return;

        // Which row is under the drop point?
        std::shared_ptr<ProjectTreeNode> targetNode;
        try
        {
            auto pos = e.GetPosition(List());
            auto toRoot = List().TransformToVisual(nullptr);
            auto rootPt = toRoot.TransformPoint(pos);
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, List()))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto item = fe.DataContext().try_as<ProjectFlatItem>(); item && item->m_node)
                {
                    targetNode = item->m_node;
                    break;
                }
            }
        }
        catch (...)
        {
        }

        // The move/copy and its undo entry belong to the document.
        std::shared_ptr<ProjectTreeNode> newParent;
        auto inserted = m_controller->ApplyProjectDrop(dragged.front(), targetNode, newParent);
        if (!inserted)
            return;

        RevealNode(newParent);

        e.Handled(true);
        m_controller->Log(L"Project drop applied");
    }
}
