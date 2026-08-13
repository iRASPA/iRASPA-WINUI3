#pragma once

#include "directxrenderer.h"
#include "dx12devicecontext.h"
#include "dx12input.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Dispatching.h>

#include <functional>
#include <memory>

/// WinUI SwapChainPanel host. Matches Cocoa MTKView: paused by default, redraw
/// only when RequestRedraw / setNeedsDisplay is called (event-driven).
class Dx12SwapChainPanelHost
{
public:
    Dx12SwapChainPanelHost() = default;
    ~Dx12SwapChainPanelHost();

    Dx12SwapChainPanelHost(const Dx12SwapChainPanelHost&) = delete;
    Dx12SwapChainPanelHost& operator=(const Dx12SwapChainPanelHost&) = delete;

    void SetPanel(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
    /// XAML Canvas layered on top of the SwapChainPanel; used to draw the
    /// rubber-band selection rectangle (Cocoa CAShapeLayer overlay equivalent).
    void SetOverlayCanvas(winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas);
    void Initialize();
    void Shutdown();

    /// Coalesced redraw request (Cocoa setNeedsDisplay). Safe to call often.
    void RequestRedraw();

    /// Raised when the right button is released without having dragged, with the
    /// position in panel DIPs. Right-drag stays a camera pan, so the menu is only
    /// offered once the gesture is known not to be one.
    void SetContextMenuHandler(std::function<void(winrt::Windows::Foundation::Point const&)> handler);

    DirectXRenderer* renderer() const { return m_renderer.get(); }

private:
    void OnSizeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                       winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
    void OnCompositionScaleChanged(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& sender,
                                   winrt::Windows::Foundation::IInspectable const& args);
    void OnPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
                          winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
    void OnPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
    void OnPointerReleased(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
    void OnPointerWheelChanged(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

    void ResizeFromPanel();
    bool HasValidPanelSize() const;
    void RenderNow();
    Dx12Input::PointerEvent ToPointerEvent(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e) const;
    Dx12Input::Modifier CurrentModifiers() const;

    // Rubber-band selection (Shift = new selection, Ctrl = add to selection).
    enum class RubberBandMode { none, newSelection, addSelection };
    void EnsureRubberBandVisual();
    void UpdateRubberBandRect(winrt::Windows::Foundation::Point const& current);
    void HideRubberBand();

    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel m_panel{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Canvas m_overlayCanvas{ nullptr };
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle m_rubberBand{ nullptr };
    RubberBandMode m_rubberBandMode = RubberBandMode::none;
    bool m_rubberBandActive = false;
    winrt::Windows::Foundation::Point m_rubberBandStart{};

    // A right press is ambiguous until it either moves (pan) or is released (menu).
    std::function<void(winrt::Windows::Foundation::Point const&)> m_contextMenuHandler;
    bool m_rightPressMayBeMenu = false;
    winrt::Windows::Foundation::Point m_rightPressStart{};

    std::unique_ptr<DirectXRenderer> m_renderer;

    winrt::event_token m_sizeChangedToken{};
    winrt::event_token m_scaleChangedToken{};
    winrt::event_token m_pointerPressedToken{};
    winrt::event_token m_pointerMovedToken{};
    winrt::event_token m_pointerReleasedToken{};
    winrt::event_token m_pointerWheelToken{};

    // Outlives the host in queued dispatcher lambdas; flipped to false in
    // Shutdown so a pending redraw never touches a destroyed host.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    bool m_initialized = false;
    bool m_redrawQueued = false;
    bool m_inResize = false;
    bool m_inRender = false;
    UINT m_lastPixelWidth = 0;
    UINT m_lastPixelHeight = 0;
};
