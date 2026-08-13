#include "pch.h"
#include "Dx12SwapChainPanelHost.h"

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

using winrt::Microsoft::UI::Xaml::Controls::Canvas;
using winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel;
using winrt::Microsoft::UI::Xaml::SizeChangedEventArgs;
using winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs;
using winrt::Microsoft::UI::Dispatching::DispatcherQueue;

namespace
{
    UINT ToPixelSize(double dips, float scale)
    {
        const double px = dips * static_cast<double>(scale);
        return static_cast<UINT>((std::max)(1.0, std::ceil(px)));
    }

    // Drag must exceed this (in DIPs) before it counts as a rubber-band, not a click.
    constexpr float kRubberBandThresholdDips = 4.0f;

    // Likewise for a right press: beyond this it is a camera pan rather than a
    // request for the context menu.
    constexpr float kContextMenuThresholdDips = 4.0f;
}

Dx12SwapChainPanelHost::~Dx12SwapChainPanelHost()
{
    Shutdown();
}

void Dx12SwapChainPanelHost::SetPanel(SwapChainPanel const& panel)
{
    m_panel = panel;
}

void Dx12SwapChainPanelHost::SetOverlayCanvas(Canvas const& canvas)
{
    m_overlayCanvas = canvas;
}

void Dx12SwapChainPanelHost::SetContextMenuHandler(std::function<void(winrt::Windows::Foundation::Point const&)> handler)
{
    m_contextMenuHandler = std::move(handler);
}

void Dx12SwapChainPanelHost::Initialize()
{
    if (m_initialized || !m_panel)
    {
        return;
    }

    m_renderer = std::make_unique<DirectXRenderer>();

    const float scaleX = m_panel.CompositionScaleX();
    const float scaleY = m_panel.CompositionScaleY();
    const UINT width = ToPixelSize(m_panel.ActualWidth(), scaleX);
    const UINT height = ToPixelSize(m_panel.ActualHeight(), scaleY);

    if (!m_renderer->initializeComposition(width, height))
    {
        throw std::runtime_error("DirectXRenderer::initializeComposition failed");
    }

    m_lastPixelWidth = width;
    m_lastPixelHeight = height;
    m_renderer->deviceContext().setCompositionScale(scaleX, scaleY);

    // Cocoa MTKView enableSetNeedsDisplay: renderer asks host to redraw on change.
    m_renderer->setNeedsDisplayCallback([this]() { RequestRedraw(); });

    winrt::com_ptr<::ISwapChainPanelNative> panelNative;
    winrt::check_hresult(m_panel.as<::IUnknown>()->QueryInterface(panelNative.put()));
    HRESULT hr = panelNative->SetSwapChain(m_renderer->deviceContext().swapChain());
    if (FAILED(hr))
    {
        throw winrt::hresult_error(hr, L"ISwapChainPanelNative::SetSwapChain failed");
    }

    m_sizeChangedToken = m_panel.SizeChanged({ this, &Dx12SwapChainPanelHost::OnSizeChanged });
    m_scaleChangedToken = m_panel.CompositionScaleChanged({ this, &Dx12SwapChainPanelHost::OnCompositionScaleChanged });
    m_pointerPressedToken = m_panel.PointerPressed({ this, &Dx12SwapChainPanelHost::OnPointerPressed });
    m_pointerMovedToken = m_panel.PointerMoved({ this, &Dx12SwapChainPanelHost::OnPointerMoved });
    m_pointerReleasedToken = m_panel.PointerReleased({ this, &Dx12SwapChainPanelHost::OnPointerReleased });
    m_pointerWheelToken = m_panel.PointerWheelChanged({ this, &Dx12SwapChainPanelHost::OnPointerWheelChanged });

    m_initialized = true;

    // Only draw once the panel has a real layout size.
    if (HasValidPanelSize())
        RequestRedraw();
}

void Dx12SwapChainPanelHost::Shutdown()
{
    *m_alive = false;

    if (m_renderer)
    {
        m_renderer->setNeedsDisplayCallback(nullptr);
        m_renderer->setSelectionChangedCallback(nullptr);
    }

    // XAML calls throw if the core is already torn down (host destroyed after
    // window close); releasing our references is all that matters then.
    try
    {
        if (m_panel)
        {
            if (m_sizeChangedToken) { m_panel.SizeChanged(m_sizeChangedToken); m_sizeChangedToken = {}; }
            if (m_scaleChangedToken) { m_panel.CompositionScaleChanged(m_scaleChangedToken); m_scaleChangedToken = {}; }
            if (m_pointerPressedToken) { m_panel.PointerPressed(m_pointerPressedToken); m_pointerPressedToken = {}; }
            if (m_pointerMovedToken) { m_panel.PointerMoved(m_pointerMovedToken); m_pointerMovedToken = {}; }
            if (m_pointerReleasedToken) { m_panel.PointerReleased(m_pointerReleasedToken); m_pointerReleasedToken = {}; }
            if (m_pointerWheelToken) { m_panel.PointerWheelChanged(m_pointerWheelToken); m_pointerWheelToken = {}; }
        }
    }
    catch (...)
    {
    }

    if (m_renderer)
    {
        m_renderer->release();
        m_renderer.reset();
    }

    m_rubberBandMode = RubberBandMode::none;
    m_rubberBandActive = false;
    m_rubberBand = nullptr;
    m_overlayCanvas = nullptr;
    m_panel = nullptr;

    m_redrawQueued = false;
    m_lastPixelWidth = 0;
    m_lastPixelHeight = 0;
    m_initialized = false;
}

void Dx12SwapChainPanelHost::RequestRedraw()
{
    if (!m_initialized || !m_renderer || m_redrawQueued)
        return;

    auto queue = DispatcherQueue::GetForCurrentThread();
    if (!queue)
        return;

    m_redrawQueued = true;
    queue.TryEnqueue([this, alive = m_alive]()
    {
        if (!*alive)
            return;
        m_redrawQueued = false;
        RenderNow();
    });
}

void Dx12SwapChainPanelHost::RenderNow()
{
    if (!m_renderer || m_inResize || m_inRender)
        return;
    if (!HasValidPanelSize())
        return;

    m_inRender = true;
    try
    {
        m_renderer->renderFrame();
    }
    catch (...)
    {
        m_inRender = false;
        throw;
    }
    m_inRender = false;
}

bool Dx12SwapChainPanelHost::HasValidPanelSize() const
{
    if (!m_panel)
        return false;
    return m_panel.ActualWidth() > 0.0 && m_panel.ActualHeight() > 0.0;
}

void Dx12SwapChainPanelHost::ResizeFromPanel()
{
    if (!m_renderer || !m_panel || m_inResize || m_inRender)
        return;

    if (!HasValidPanelSize())
        return;

    const float scaleX = m_panel.CompositionScaleX();
    const float scaleY = m_panel.CompositionScaleY();
    const UINT width = ToPixelSize(m_panel.ActualWidth(), scaleX);
    const UINT height = ToPixelSize(m_panel.ActualHeight(), scaleY);

    m_renderer->deviceContext().setCompositionScale(scaleX, scaleY);

    if (width == m_lastPixelWidth && height == m_lastPixelHeight)
    {
        RequestRedraw();
        return;
    }

    m_inResize = true;
    m_lastPixelWidth = width;
    m_lastPixelHeight = height;
    try
    {
        m_renderer->resize(width, height);
        // Do not Present synchronously from SizeChanged — queue a redraw.
        RequestRedraw();
    }
    catch (...)
    {
        m_inResize = false;
        throw;
    }
    m_inResize = false;
}

void Dx12SwapChainPanelHost::OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, SizeChangedEventArgs const&)
{
    ResizeFromPanel();
}

void Dx12SwapChainPanelHost::OnCompositionScaleChanged(SwapChainPanel const&, winrt::Windows::Foundation::IInspectable const&)
{
    ResizeFromPanel();
}

Dx12Input::Modifier Dx12SwapChainPanelHost::CurrentModifiers() const
{
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        m |= static_cast<uint32_t>(Dx12Input::Modifier::shift);
    if (GetKeyState(VK_CONTROL) & 0x8000)
        m |= static_cast<uint32_t>(Dx12Input::Modifier::ctrl);
    if (GetKeyState(VK_MENU) & 0x8000)
        m |= static_cast<uint32_t>(Dx12Input::Modifier::alt);
    return static_cast<Dx12Input::Modifier>(m);
}

Dx12Input::PointerEvent Dx12SwapChainPanelHost::ToPointerEvent(PointerRoutedEventArgs const& e) const
{
    Dx12Input::PointerEvent pe{};
    const auto pt = e.GetCurrentPoint(m_panel);
    const float sx = m_panel ? m_panel.CompositionScaleX() : 1.f;
    const float sy = m_panel ? m_panel.CompositionScaleY() : 1.f;
    pe.x = static_cast<float>(pt.Position().X * sx);
    pe.y = static_cast<float>(pt.Position().Y * sy);

    const auto props = pt.Properties();
    if (props.IsLeftButtonPressed()) pe.button = Dx12Input::Button::left;
    else if (props.IsRightButtonPressed()) pe.button = Dx12Input::Button::right;
    else if (props.IsMiddleButtonPressed()) pe.button = Dx12Input::Button::middle;

    pe.modifiers = CurrentModifiers();
    return pe;
}

void Dx12SwapChainPanelHost::EnsureRubberBandVisual()
{
    using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
    using winrt::Windows::UI::Color;

    if (!m_overlayCanvas)
        return;

    if (!m_rubberBand)
    {
        m_rubberBand = winrt::Microsoft::UI::Xaml::Shapes::Rectangle();
        m_rubberBand.StrokeThickness(1.5);
        m_rubberBand.Stroke(SolidColorBrush(Color{ 255, 255, 255, 255 }));
        m_rubberBand.Fill(SolidColorBrush(Color{ 70, 160, 200, 255 }));
        m_rubberBand.IsHitTestVisible(false);
        m_overlayCanvas.Children().Append(m_rubberBand);
    }

    // Cocoa: solid outline for a new selection, dashed for add-to-selection.
    winrt::Microsoft::UI::Xaml::Media::DoubleCollection dashes;
    if (m_rubberBandMode == RubberBandMode::addSelection)
    {
        dashes.Append(4.0);
        dashes.Append(2.0);
    }
    m_rubberBand.StrokeDashArray(dashes);

    m_rubberBand.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
}

void Dx12SwapChainPanelHost::UpdateRubberBandRect(winrt::Windows::Foundation::Point const& current)
{
    if (!m_rubberBand)
        return;

    const double left = (std::min)(m_rubberBandStart.X, current.X);
    const double top = (std::min)(m_rubberBandStart.Y, current.Y);
    const double width = std::abs(current.X - m_rubberBandStart.X);
    const double height = std::abs(current.Y - m_rubberBandStart.Y);

    Canvas::SetLeft(m_rubberBand, left);
    Canvas::SetTop(m_rubberBand, top);
    m_rubberBand.Width(width);
    m_rubberBand.Height(height);
}

void Dx12SwapChainPanelHost::HideRubberBand()
{
    if (m_rubberBand)
        m_rubberBand.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
}

void Dx12SwapChainPanelHost::OnPointerPressed(winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const& e)
{
    if (!m_renderer)
        return;

    // Shift/Ctrl + left starts a rubber-band selection handled here in the
    // host (the XAML overlay draws the rectangle); everything else goes to
    // the renderer (rotate / pan / click-pick).
    const auto pt = e.GetCurrentPoint(m_panel);
    const uint32_t mods = static_cast<uint32_t>(CurrentModifiers());
    const bool shift = (mods & static_cast<uint32_t>(Dx12Input::Modifier::shift)) != 0;
    const bool ctrl = (mods & static_cast<uint32_t>(Dx12Input::Modifier::ctrl)) != 0;

    if (m_overlayCanvas && pt.Properties().IsLeftButtonPressed() && (shift || ctrl))
    {
        m_rubberBandMode = shift ? RubberBandMode::newSelection : RubberBandMode::addSelection;
        m_rubberBandActive = false;
        m_rubberBandStart = pt.Position();
        m_panel.CapturePointer(e.Pointer());
        return;
    }

    // The renderer still starts a pan here, as before; whether this turns out to be
    // a pan or a context-menu click is only decided on move or release.
    m_rightPressMayBeMenu = pt.Properties().IsRightButtonPressed() && m_contextMenuHandler != nullptr;
    if (m_rightPressMayBeMenu)
        m_rightPressStart = pt.Position();

    m_renderer->onPointerPressed(ToPointerEvent(e));
}

void Dx12SwapChainPanelHost::OnPointerMoved(winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const& e)
{
    if (m_rubberBandMode != RubberBandMode::none)
    {
        const auto pos = e.GetCurrentPoint(m_panel).Position();
        if (!m_rubberBandActive)
        {
            const float dx = pos.X - m_rubberBandStart.X;
            const float dy = pos.Y - m_rubberBandStart.Y;
            if (dx * dx + dy * dy < kRubberBandThresholdDips * kRubberBandThresholdDips)
                return;
            m_rubberBandActive = true;
            EnsureRubberBandVisual();
        }
        UpdateRubberBandRect(pos);
        return;
    }

    if (m_rightPressMayBeMenu)
    {
        const auto pos = e.GetCurrentPoint(m_panel).Position();
        const float dx = pos.X - m_rightPressStart.X;
        const float dy = pos.Y - m_rightPressStart.Y;
        if (dx * dx + dy * dy >= kContextMenuThresholdDips * kContextMenuThresholdDips)
            m_rightPressMayBeMenu = false;
    }

    if (m_renderer) m_renderer->onPointerMoved(ToPointerEvent(e));
}

void Dx12SwapChainPanelHost::OnPointerReleased(winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const& e)
{
    if (m_rubberBandMode != RubberBandMode::none)
    {
        const bool extend = (m_rubberBandMode == RubberBandMode::addSelection);
        const bool wasDrag = m_rubberBandActive;
        m_rubberBandMode = RubberBandMode::none;
        m_rubberBandActive = false;
        HideRubberBand();
        m_panel.ReleasePointerCapture(e.Pointer());

        if (m_renderer)
        {
            const auto pos = e.GetCurrentPoint(m_panel).Position();
            const float sx = m_panel.CompositionScaleX();
            const float sy = m_panel.CompositionScaleY();
            if (wasDrag)
            {
                m_renderer->applyRectangleSelection(m_rubberBandStart.X * sx, m_rubberBandStart.Y * sy,
                                                    pos.X * sx, pos.Y * sy, extend);
            }
            else
            {
                // Modifier-click without a drag: keep the old behavior
                // (Ctrl toggles the picked atom, Shift replaces the selection).
                // Strip shift so the renderer treats it as a normal click-pick.
                Dx12Input::PointerEvent pe = ToPointerEvent(e);
                pe.button = Dx12Input::Button::left;
                pe.modifiers = static_cast<Dx12Input::Modifier>(
                    static_cast<uint32_t>(pe.modifiers) &
                    ~static_cast<uint32_t>(Dx12Input::Modifier::shift));
                m_renderer->onPointerPressed(pe);
                m_renderer->onPointerReleased(pe);
            }
        }
        return;
    }

    if (m_renderer) m_renderer->onPointerReleased(ToPointerEvent(e));

    if (m_rightPressMayBeMenu)
    {
        m_rightPressMayBeMenu = false;
        // Ending the renderer's pan first leaves its tracking state clean while the
        // menu is up. IsRightButtonPressed is already false by now, so the release
        // has to be identified by the update kind.
        const auto pt = e.GetCurrentPoint(m_panel);
        if (m_contextMenuHandler &&
            pt.Properties().PointerUpdateKind() == winrt::Microsoft::UI::Input::PointerUpdateKind::RightButtonReleased)
        {
            m_contextMenuHandler(pt.Position());
            e.Handled(true);
        }
    }
}

void Dx12SwapChainPanelHost::OnPointerWheelChanged(winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const& e)
{
    if (!m_renderer) return;
    const auto pt = e.GetCurrentPoint(m_panel);
    const float sx = m_panel ? m_panel.CompositionScaleX() : 1.f;
    const float sy = m_panel ? m_panel.CompositionScaleY() : 1.f;
    Dx12Input::WheelEvent we{};
    we.x = static_cast<float>(pt.Position().X * sx);
    we.y = static_cast<float>(pt.Position().Y * sy);
    we.delta = static_cast<float>(pt.Properties().MouseWheelDelta());
    we.modifiers = CurrentModifiers();
    m_renderer->onWheel(we);
}
