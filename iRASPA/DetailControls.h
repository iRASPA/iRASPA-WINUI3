#pragma once

#include "rkcolor.h"

#include <functional>
#include <optional>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.h>

// The two composite controls the detail panes are built from, which XAML cannot
// express on its own: a slider tied to a number field showing the same value,
// and Cocoa's NSColorWell. The forms declare the pieces and name them; these
// give them their behaviour.
//
// Also here is how a control shows a mixed selection, for which the panes ask
// DocumentController::AgreedValue: Cocoa puts "Multiple Values" in place of the
// value when the selected structures do not agree on it.
namespace winrt::iRASPA_WinUI::DetailControls
{
    winrt::Windows::UI::Color ToUiColor(RKColor const& color);
    RKColor FromUiColor(winrt::Windows::UI::Color const& color);

    // Windows' text-size setting (Settings > Accessibility > Text size) makes the
    // text of every control larger without touching the column widths a form
    // declares, so a label column written for the default size clips its label at
    // 150% or 200%. Each fixed column of the form is widened here to what the
    // widest thing standing in it asks for, columns of the same declared width
    // together so that the rows stay lined up. Nothing moves while the setting is
    // off, which is what the forms are written for.
    void FitFixedColumns(winrt::Microsoft::UI::Xaml::FrameworkElement const& root);

    // What Cocoa puts in a field or a popup title for a mixed selection.
    winrt::hstring const& MultipleValuesText();

    // A number field shows nothing and says "Multiple Values" in its place; the
    // slider beside it keeps the value it had, as Cocoa leaves it alone.
    void SetNumberOrMultiple(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                             std::optional<double> const& value);
    void SetSliderAndBoxOrMultiple(winrt::Microsoft::UI::Xaml::Controls::Slider const& slider,
                                   winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                                   std::optional<double> const& value);
    void SetTextOrMultiple(winrt::Microsoft::UI::Xaml::Controls::TextBox const& box,
                           std::optional<winrt::hstring> const& value);
    // Cocoa's allowsMixedState: the box carries the third state only while the
    // selection is mixed, so a click cannot put it back into one.
    void SetCheckOrMultiple(winrt::Microsoft::UI::Xaml::Controls::CheckBox const& check,
                            std::optional<bool> const& value);
    void ResolveCheck(winrt::Microsoft::UI::Xaml::Controls::CheckBox const& check);
    // Re-push Checked / Unchecked / Indeterminate from the current IsChecked.
    // Needed when a CheckBox was filled while its Expander (Appearance outline)
    // was collapsed — the glyph can stay on '-' until a hover otherwise.
    void RefreshCheckVisual(winrt::Microsoft::UI::Xaml::Controls::CheckBox const& check);

    // WinUI's IsChecked is a nullable bool. Recycling an ItemsView/ItemsRepeater row
    // clears it to null, which paints the indeterminate glyph even when IsThreeState
    // is false, and the visual state machine often stays there until a pointer hover.
    // Call from DataContextChanged: re-read the named property off the row's
    // ICustomPropertyProvider, write IsChecked, and force Checked/Unchecked/
    // Indeterminate so the control does not wait for the mouse.
    // Also listens for the row's PropertyChanged: writing IsChecked is a local value
    // that shadows the OneWay binding, so Raise(Visible)/Raise(ShowsAtoms) alone would
    // leave child boxes stale after a parent group toggles its atoms.
    // allowIndeterminate: the property may legitimately be null (mixed atoms under a
    // group). Otherwise null is treated as a recycle glitch and replaced.
    void SyncCheckFromDataContext(
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& control,
        winrt::hstring const& propertyName,
        bool allowIndeterminate = false);

    // A popup gets a "Multiple Values" entry appended and selected, and loses it
    // again once the selection agrees. It is display-only: the panes' handlers
    // ignore it through IsMultipleValuesSelected.
    void SelectOrMultiple(winrt::Microsoft::UI::Xaml::Controls::ComboBox const& combo,
                          std::optional<int> const& index);
    bool IsMultipleValuesSelected(winrt::Microsoft::UI::Xaml::Controls::ComboBox const& combo);

    // Keeps a slider and a number box on the same value and calls apply once per
    // edit, whichever of the two the user moved. Without the guard the two would
    // drive each other and apply would run twice.
    void SyncSliderAndBox(winrt::Microsoft::UI::Xaml::Controls::Slider const& slider,
                          winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                          std::function<void(double)> apply);
    // Puts a value in both halves of such a row without calling apply, for the
    // reload path.
    void SetSliderAndBox(winrt::Microsoft::UI::Xaml::Controls::Slider const& slider,
                         winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                         double value);

    // Gives a drop-down button the swatch face and the colour-picker flyout of an
    // NSColorWell, applying continuously as the picker moves. The picker is built
    // when the flyout first opens: a pane with a dozen wells is slow to show if
    // they are all built up front.
    void AttachColorWell(winrt::Microsoft::UI::Xaml::Controls::DropDownButton const& button,
                         winrt::Microsoft::UI::Xaml::Controls::Border const& swatch,
                         std::function<void(RKColor)> apply);
    void SetColorWell(winrt::Microsoft::UI::Xaml::Controls::Border const& swatch,
                      RKColor const& color);
    // A well cannot spell out "Multiple Values", so Cocoa shows it light gray.
    void SetColorWellOrMultiple(winrt::Microsoft::UI::Xaml::Controls::Border const& swatch,
                                std::optional<RKColor> const& color);
}
