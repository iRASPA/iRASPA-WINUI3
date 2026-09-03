#pragma once

#include "AppearanceDetailView.g.h"

#include "DocumentController.h"
#include "rkcolor.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // The Appearance tab: Cocoa StructureAppearanceDetailViewController, whose
    // groups edit the atom, bond, primitive, volumetric and annotation
    // interfaces of the selected structures.
    //
    // None of it is undoable, as in Cocoa, so the form talks to the model
    // directly through the controller's selection rather than through an edit
    // method: every change ends in a renderer reload. Each control is bound once
    // to the property it writes, so there is no tag-to-property switch.
    struct AppearanceDetailView : AppearanceDetailViewT<AppearanceDetailView>
    {
        AppearanceDetailView();

        void SetController(DocumentController* controller) { m_controller = controller; }
        // Fill every group from the selection, as Cocoa reloads the outline view.
        void Reload();

    private:
        using Border = winrt::Microsoft::UI::Xaml::Controls::Border;
        using Button = winrt::Microsoft::UI::Xaml::Controls::Button;
        using CheckBox = winrt::Microsoft::UI::Xaml::Controls::CheckBox;
        using ComboBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox;
        using DropDownButton = winrt::Microsoft::UI::Xaml::Controls::DropDownButton;
        using NumberBox = winrt::Microsoft::UI::Xaml::Controls::NumberBox;
        using Slider = winrt::Microsoft::UI::Xaml::Controls::Slider;
        using TextBlock = winrt::Microsoft::UI::Xaml::Controls::TextBlock;
        using Panel = winrt::Microsoft::UI::Xaml::Controls::Panel;
        using Expander = winrt::Microsoft::UI::Xaml::Controls::Expander;

        // Binding: the range and the property a control writes, given once. The
        // apply runs on every edit unless the form is filling itself in.
        void BindNumber(NumberBox const& box, double minV, double maxV, double step,
                        std::function<void(double)> apply);
        void BindSlider(Slider const& slider, NumberBox const& box,
                        double minV, double maxV, double step,
                        std::function<void(double)> apply);
        void BindCheck(CheckBox const& check, std::function<void(bool)> apply);
        // The index and the shown text, because the annotation font is stored by
        // name rather than by position.
        void BindCombo(ComboBox const& combo, std::function<void(int, hstring const&)> apply);
        void BindWell(DropDownButton const& button, Border const& swatch,
                      std::function<void(RKColor)> apply);

        // Cocoa re-derives the representation style after every atom or bond
        // edit, so the style shown falls to "Custom" as soon as one setting no
        // longer matches a predefined style. Bindings made while the flag is set
        // do that once they have written the model.
        void RecheckAtomStyle();

        // The same idea for the ribbon, which keeps its own style separate from the
        // atom and bond one.
        void RecheckRibbonStyle();

        // Filling in: a value the whole selection agrees on, or nothing, which
        // the control shows as Cocoa's "Multiple Values". A plain value converts,
        // so rows read from a single object still read as before.
        void SetNumber(NumberBox const& box, std::optional<double> const& value);
        void SetSlider(Slider const& slider, NumberBox const& box,
                       std::optional<double> const& value);
        void SetCheck(CheckBox const& check, std::optional<bool> const& value);
        // The isocontour range comes from the grid data, so it is not known until
        // a structure is on screen.
        void SetRange(Slider const& slider, NumberBox const& box,
                      double minV, double maxV, double step);
        void FillCombo(ComboBox const& combo, std::vector<hstring> const& items,
                       std::optional<int> const& selected);
        // A group whose interface the selection does not implement shows the hint
        // instead of its rows. Primitive and Ribbon omit the whole section (Cocoa
        // drops those outline groups when the selection has none).
        static void ShowBody(TextBlock const& hint, Panel const& body, bool available);
        static void ShowSection(Expander const& section, bool available);

        void WirePrimitive();
        void WireAtoms();
        void WireBonds();
        void WireRibbon();
        void WireUnitCell();
        void WireLocalAxes();
        void WireVolumetric();
        void WireBlockingPockets();
        void WireAnnotation();

        void ReloadPrimitive();
        void ReloadAtoms();
        void ReloadBonds();
        void ReloadRibbon();
        void ReloadUnitCell();
        void ReloadLocalAxes();
        void ReloadVolumetric();
        void ReloadBlockingPockets();
        void ReloadAnnotation();
        // Cocoa outline sections are Expanders; re-push checkbox visuals after
        // Reload and when a collapsed section opens.
        void RefreshAllCheckVisuals();
        void WireSectionExpanders();

        // The rotate buttons and the Euler readouts move together: a rotation
        // changes the orientation the boxes show, and the step size their titles.
        void ReloadPrimitiveOrientation();
        void RetitleRotateButtons(double delta);
        void RotatePrimitive(int direction);

        DocumentController* m_controller{ nullptr };
        bool m_suppress{ false };
        bool m_recheckStyle{ false };
        // Cocoa order: yaw +/-, pitch +/-, roll +/-.
        std::array<Button, 6> m_rotate{
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct AppearanceDetailView : AppearanceDetailViewT<AppearanceDetailView, implementation::AppearanceDetailView>
    {
    };
}
