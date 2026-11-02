#include "pch.h"
#include "DirectoryCollectionView.xaml.h"
#if __has_include("DirectoryCollectionView.g.cpp")
#include "DirectoryCollectionView.g.cpp"
#endif

#include "RowProperty.h"

#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>

#include <filesystem>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Storage::Streams;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        std::filesystem::path AppDirectory()
        {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0 || length == MAX_PATH)
                return {};
            return std::filesystem::path(buffer).parent_path();
        }

        ImageSource SharedSlideCarrierImage()
        {
            static ImageSource s_image{ nullptr };
            if (!s_image)
            {
                const auto path = AppDirectory() / L"Assets" / L"SlideCarrier.png";
                std::error_code ec;
                if (!path.empty() && std::filesystem::exists(path, ec) && !ec)
                    s_image = BitmapImage(Uri(hstring(L"file:///" + path.wstring())));
            }
            return s_image;
        }

        ImageSource SharedMofFallbackImage()
        {
            static ImageSource s_image{ nullptr };
            if (!s_image)
            {
                const auto path = AppDirectory() / L"Assets" / L"MOF.png";
                std::error_code ec;
                if (!path.empty() && std::filesystem::exists(path, ec) && !ec)
                    s_image = BitmapImage(Uri(hstring(L"file:///" + path.wstring())));
            }
            return s_image;
        }

        Brush SelectedBorderBrush()
        {
            // Cocoa ProjectSlideBorderView: NSColor(calibratedRed: 0.0, green: 0.5, blue: 1.0, alpha: 1.0)
            static Brush s_brush = SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 0, 128, 255));
            return s_brush;
        }

        Brush HoverBorderBrush()
        {
            // Cocoa ProjectSlideBorderView .forSelection: NSColor.orange
            static Brush s_brush = SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 128, 0));
            return s_brush;
        }

        Brush TransparentBorderBrush()
        {
            static Brush s_brush = SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0));
            return s_brush;
        }

        struct DirectorySlideItem : implements<DirectorySlideItem,
                                               ICustomPropertyProvider,
                                               INotifyPropertyChanged,
                                               IStringable>
        {
            std::shared_ptr<ProjectTreeNode> m_node;
            hstring m_title;
            ImageSource m_thumbnail{ nullptr };
            bool m_thumbnailInitialized{ false };
            bool m_selected{ false };
            bool m_hovered{ false };
            winrt::event<PropertyChangedEventHandler> m_propertyChanged;

            DirectorySlideItem(std::shared_ptr<ProjectTreeNode> node)
                : m_node(std::move(node))
            {
                if (m_node)
                    m_title = hstring(m_node->displayName().toStdWString());
            }

            event_token PropertyChanged(PropertyChangedEventHandler const& handler)
            {
                return m_propertyChanged.add(handler);
            }
            void PropertyChanged(event_token const& token) noexcept
            {
                m_propertyChanged.remove(token);
            }

            void SetSelected(bool selected)
            {
                if (m_selected == selected)
                    return;
                m_selected = selected;
                m_propertyChanged(*this, PropertyChangedEventArgs(L"HighlightBorderBrush"));
            }

            void SetHovered(bool hovered)
            {
                if (m_hovered == hovered)
                    return;
                m_hovered = hovered;
                m_propertyChanged(*this, PropertyChangedEventArgs(L"HighlightBorderBrush"));
            }

            Brush HighlightBorderBrushValue() const
            {
                if (m_selected)
                    return SelectedBorderBrush();
                if (m_hovered)
                    return HoverBorderBrush();
                return TransparentBorderBrush();
            }

            ImageSource ThumbnailImageValue()
            {
                if (!m_thumbnailInitialized)
                {
                    m_thumbnailInitialized = true;
                    if (m_node && !m_node->thumbnailData().empty())
                    {
                        try
                        {
                            auto const& bytes = m_node->thumbnailData();
                            InMemoryRandomAccessStream stream;
                            DataWriter writer(stream);
                            writer.WriteBytes(winrt::array_view<uint8_t const>(bytes.data(), static_cast<uint32_t>(bytes.size())));
                            writer.StoreAsync();
                            writer.DetachStream();
                            stream.Seek(0);
                            BitmapImage bmp;
                            bmp.SetSource(stream);
                            m_thumbnail = bmp;
                        }
                        catch (...)
                        {
                            m_thumbnail = SharedMofFallbackImage();
                        }
                    }
                    else
                    {
                        m_thumbnail = SharedMofFallbackImage();
                    }
                }
                return m_thumbnail;
            }

            ICustomProperty GetCustomProperty(hstring const& name)
            {
                if (name == L"Title")
                {
                    return make<RowProperty>(L"Title", xaml_typename<hstring>(),
                        [](IInspectable const& t)
                        {
                            auto s = t.try_as<DirectorySlideItem>();
                            return box_value(s ? s->m_title : hstring{});
                        });
                }
                if (name == L"SlideCarrierImage")
                {
                    return make<RowProperty>(L"SlideCarrierImage", xaml_typename<ImageSource>(),
                        [](IInspectable const&) { return SharedSlideCarrierImage(); });
                }
                if (name == L"ThumbnailImage")
                {
                    return make<RowProperty>(L"ThumbnailImage", xaml_typename<ImageSource>(),
                        [](IInspectable const& t)
                        {
                            auto s = t.try_as<DirectorySlideItem>();
                            return s ? s->ThumbnailImageValue() : nullptr;
                        });
                }
                if (name == L"HighlightBorderBrush")
                {
                    return make<RowProperty>(L"HighlightBorderBrush", xaml_typename<Brush>(),
                        [](IInspectable const& t)
                        {
                            auto s = t.try_as<DirectorySlideItem>();
                            return s ? s->HighlightBorderBrushValue() : nullptr;
                        });
                }
                return nullptr;
            }

            ICustomProperty GetIndexedProperty(hstring const&, winrt::Windows::UI::Xaml::Interop::TypeName const&)
            {
                return nullptr;
            }

            winrt::Windows::UI::Xaml::Interop::TypeName Type()
            {
                return xaml_typename<IInspectable>();
            }

            hstring GetStringRepresentation() const
            {
                return m_title;
            }

            hstring ToString()
            {
                return m_title;
            }
        };

        com_ptr<DirectorySlideItem> SlideItemFromInspectable(IInspectable const& obj)
        {
            return obj ? obj.try_as<DirectorySlideItem>() : nullptr;
        }
    }

    DirectoryCollectionView::DirectoryCollectionView()
    {
        InitializeComponent();

        m_items = single_threaded_observable_vector<IInspectable>();
        GridList().ItemsSource(m_items);

        // Register pointer/double-tap handlers with handledEventsToo=true so the
        // ItemContainer does not swallow clicks/double-clicks on tiles.
        GridList().AddHandler(UIElement::PointerPressedEvent(),
                              box_value(PointerEventHandler(this, &DirectoryCollectionView::OnGridPointerPressed)),
                              true);
        GridList().AddHandler(UIElement::DoubleTappedEvent(),
                              box_value(DoubleTappedEventHandler(this, &DirectoryCollectionView::OnGridDoubleTapped)),
                              true);
    }

    void DirectoryCollectionView::SetPredicateEvaluator(
        std::function<bool(std::shared_ptr<iRASPAProject> const&)> evaluator)
    {
        m_predicateEvaluator = std::move(evaluator);
    }

    void DirectoryCollectionView::SetNodeActivatedCallback(
        std::function<void(std::shared_ptr<ProjectTreeNode> const&)> onActivate)
    {
        m_onActivate = std::move(onActivate);
    }

    void DirectoryCollectionView::SetNodeDragStartedCallback(
        std::function<void(std::shared_ptr<ProjectTreeNode> const&)> onDragStart)
    {
        m_onDragStart = std::move(onDragStart);
    }

    void DirectoryCollectionView::SetProxyProject(std::shared_ptr<ProjectTreeNode> const& proxyProject)
    {
        m_proxyProject = proxyProject;
        ReloadData();
    }

    // Cocoa DirectoryCollectionViewController.reloadData():
    //   filteredAndSortedNodes = proxyProject?.descendantLeafNodes().filter{
    //       ($0.matchesFilter) && (self.predicate.evaluate(with: $0.representedObject))
    //   }
    void DirectoryCollectionView::ReloadData()
    {
        if (!m_items)
            return;

        m_filteredNodes.clear();
        m_selectedItem = nullptr;

        std::vector<IInspectable> nextItems;
        if (m_proxyProject)
        {
            const auto leaves = m_proxyProject->descendantLeafNodes();
            nextItems.reserve(leaves.size());
            m_filteredNodes.reserve(leaves.size());

            for (auto const& leaf : leaves)
            {
                if (!leaf || !leaf->matchesFilter())
                    continue;
                const auto iraspa = leaf->representedObject();
                if (!iraspa)
                    continue;
                if (m_predicateEvaluator && !m_predicateEvaluator(iraspa))
                    continue;

                m_filteredNodes.push_back(leaf);
                auto slide = make_self<DirectorySlideItem>(leaf);
                nextItems.push_back(*slide);
            }
        }

        m_items.ReplaceAll(nextItems);

        const size_t count = m_filteredNodes.size();
        const std::wstring label = (count == 1)
            ? L"1 item"
            : (std::to_wstring(count) + L" items");
        ItemCountLabel().Text(hstring(label));
    }

    void DirectoryCollectionView::OnGridSelectionChanged(
        [[maybe_unused]] ItemsView const& sender,
        [[maybe_unused]] ItemsViewSelectionChangedEventArgs const& e)
    {
        if (auto prev = SlideItemFromInspectable(m_selectedItem))
            prev->SetSelected(false);

        m_selectedItem = GridList().SelectedItem();
        if (auto curr = SlideItemFromInspectable(m_selectedItem))
            curr->SetSelected(true);
    }

    void DirectoryCollectionView::OnGridPointerPressed(
        [[maybe_unused]] IInspectable const&,
        PointerRoutedEventArgs const& e)
    {
        if (!m_items)
            return;

        try
        {
            auto local = e.GetCurrentPoint(GridList()).Position();
            auto toRoot = GridList().TransformToVisual(nullptr);
            auto rootPt = toRoot.TransformPoint(local);
            com_ptr<DirectorySlideItem> hitItem;
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, GridList()))
            {
                if (auto fe = el.try_as<FrameworkElement>())
                {
                    if (auto candidate = fe.DataContext().try_as<DirectorySlideItem>())
                    {
                        hitItem = candidate;
                        break;
                    }
                }
            }
            if (!hitItem || !hitItem->m_node)
                return;

            // Arm drag source on pointer press as well as DragStarting
            if (m_onDragStart)
                m_onDragStart(hitItem->m_node);

            if (e.GetCurrentPoint(GridList()).Properties().IsLeftButtonPressed())
            {
                const uint32_t count = m_items.Size();
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (SlideItemFromInspectable(m_items.GetAt(i)) == hitItem)
                    {
                        GridList().Select(static_cast<int32_t>(i));
                        break;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    // Cocoa ProjectCollectionViewItem.mouseDown (event.clickCount == 2):
    // selects the leaf ProjectTreeNode and switches back to structure visualization.
    void DirectoryCollectionView::OnGridDoubleTapped(
        [[maybe_unused]] IInspectable const&,
        DoubleTappedRoutedEventArgs const& e)
    {
        try
        {
            auto rootPt = GridList().TransformToVisual(nullptr)
                                    .TransformPoint(e.GetPosition(GridList()));
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, GridList()))
            {
                if (auto fe = el.try_as<FrameworkElement>())
                {
                    if (auto item = fe.DataContext().try_as<DirectorySlideItem>(); item && item->m_node)
                    {
                        if (m_onActivate)
                            m_onActivate(item->m_node);
                        e.Handled(true);
                        return;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    void DirectoryCollectionView::OnTileDragStarting(
        UIElement const& sender,
        DragStartingEventArgs const& e)
    {
        if (auto fe = sender.try_as<FrameworkElement>())
        {
            if (auto item = fe.DataContext().try_as<DirectorySlideItem>(); item && item->m_node)
            {
                if (m_onDragStart)
                    m_onDragStart(item->m_node);
                e.Data().RequestedOperation(DataPackageOperation::Copy | DataPackageOperation::Move);
                e.Data().SetText(item->m_title);
            }
        }
    }

    void DirectoryCollectionView::OnTilePointerEntered(
        IInspectable const& sender,
        [[maybe_unused]] PointerRoutedEventArgs const&)
    {
        if (auto fe = sender.try_as<FrameworkElement>())
        {
            if (auto item = fe.DataContext().try_as<DirectorySlideItem>())
                item->SetHovered(true);
        }
    }

    void DirectoryCollectionView::OnTilePointerExited(
        IInspectable const& sender,
        [[maybe_unused]] PointerRoutedEventArgs const&)
    {
        if (auto fe = sender.try_as<FrameworkElement>())
        {
            if (auto item = fe.DataContext().try_as<DirectorySlideItem>())
                item->SetHovered(false);
        }
    }
}
