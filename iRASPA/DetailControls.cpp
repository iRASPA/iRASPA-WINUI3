#include "pch.h"
#include "DetailControls.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::DetailControls
{
    winrt::Windows::UI::Color ToUiColor(RKColor const& color)
    {
        return winrt::Windows::UI::Color{
            static_cast<uint8_t>(color.alpha()), static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.green()), static_cast<uint8_t>(color.blue()) };
    }

    RKColor FromUiColor(winrt::Windows::UI::Color const& color)
    {
        return RKColor(color.R / 255.0, color.G / 255.0, color.B / 255.0, color.A / 255.0);
    }

    void SyncSliderAndBox(Slider const& slider, NumberBox const& box, std::function<void(double)> apply)
    {
        if (!slider || !box || !apply)
            return;
        auto guard = std::make_shared<bool>(false);
        slider.ValueChanged([apply, guard, weakBox = make_weak(box)]
                            (IInspectable const&, RangeBaseValueChangedEventArgs const& e)
        {
            if (*guard)
                return;
            const double value = e.NewValue();
            *guard = true;
            if (auto b = weakBox.get())
                b.Value(value);
            *guard = false;
            apply(value);
        });
        box.ValueChanged([apply, guard, weakSlider = make_weak(slider)]
                         (NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
        {
            if (*guard)
                return;
            const double value = sender.Value();
            if (!std::isfinite(value))
                return;
            *guard = true;
            if (auto s = weakSlider.get())
                s.Value((std::clamp)(value, s.Minimum(), s.Maximum()));
            *guard = false;
            apply(value);
        });
    }

    void SetSliderAndBox(Slider const& slider, NumberBox const& box, double value)
    {
        if (!std::isfinite(value))
            value = 0.0;
        if (box)
            box.Value(value);
        if (slider)
            slider.Value((std::clamp)(value, slider.Minimum(), slider.Maximum()));
    }

    winrt::hstring const& MultipleValuesText()
    {
        static const winrt::hstring text{ L"Multiple Values" };
        return text;
    }

    namespace
    {
        // A popup is filled either with plain strings or with ComboBoxItems from
        // markup, and both kinds are read here.
        winrt::hstring ItemText(IInspectable const& item)
        {
            if (!item)
                return {};
            if (auto element = item.try_as<ComboBoxItem>())
                return unbox_value_or<winrt::hstring>(element.Content(), winrt::hstring{});
            return unbox_value_or<winrt::hstring>(item, winrt::hstring{});
        }

        constexpr float kUnbounded = std::numeric_limits<float>::infinity();

        double TextScale()
        {
            static const double scale = []
            {
                try
                {
                    const double value = winrt::Windows::UI::ViewManagement::UISettings().TextScaleFactor();
                    return value > 1.0 ? value : 1.0;
                }
                catch (...)
                {
                    return 1.0;
                }
            }();
            return scale;
        }

        void Walk(IInspectable const& node, std::function<void(FrameworkElement const&)> const& visit)
        {
            if (!node)
                return;
            if (auto element = node.try_as<FrameworkElement>())
                visit(element);
            if (auto panel = node.try_as<Panel>())
            {
                for (auto const& child : panel.Children())
                    Walk(child, visit);
            }
            else if (auto border = node.try_as<Border>())
            {
                Walk(border.Child(), visit);
            }
            else if (auto user = node.try_as<UserControl>())
            {
                Walk(user.Content(), visit);
            }
            else if (auto content = node.try_as<ContentControl>())
            {
                // Expanders and the like; a Content that is text ends the walk.
                Walk(content.Content().try_as<UIElement>(), visit);
            }
        }

        // What the labels standing in one column of a grid ask for. Measuring is
        // the only way to know: the text size in force, the font and the margins
        // decide it together. Only a label that cannot wrap is counted, since that
        // is the one a narrow column cuts in half; a wrapping label takes another
        // line instead and needs no more width. Fields are left out too: they are
        // the part that has to give way for the label to be read.
        double NeededWidth(Grid const& grid, int column)
        {
            double needed = 0.0;
            for (auto const& child : grid.Children())
            {
                auto label = child.try_as<TextBlock>();
                if (!label || label.TextWrapping() != TextWrapping::NoWrap)
                    continue;
                if (Grid::GetColumn(label) != column || Grid::GetColumnSpan(label) != 1)
                    continue;
                try
                {
                    label.Measure(Size{ kUnbounded, kUnbounded });
                    needed = (std::max)(needed, static_cast<double>(label.DesiredSize().Width));
                }
                catch (...)
                {
                }
            }
            return needed;
        }

        double MeasuredTextWidth(winrt::hstring const& text, Control const& like)
        {
            TextBlock sample;
            if (like)
            {
                sample.FontFamily(like.FontFamily());
                sample.FontSize(like.FontSize());
                sample.FontWeight(like.FontWeight());
            }
            sample.Text(text);
            sample.Measure(Size{ kUnbounded, kUnbounded });
            return sample.DesiredSize().Width;
        }

        // "Multiple Values" does not fit a number field once the text is enlarged,
        // and a field too narrow even for that says as little as it must. The words
        // in full are on the tip.
        void ShowMultipleInNumberBox(NumberBox const& box)
        {
            if (!box || !std::isnan(box.Value()))
                return;
            // The spin buttons and the padding take about this much of the field.
            const double room = box.ActualWidth() - 30.0;
            winrt::hstring shown = MultipleValuesText();
            for (auto const& candidate : { MultipleValuesText(), winrt::hstring{ L"Mult. Val." },
                                           winrt::hstring{ L"M.V." } })
            {
                shown = candidate;
                if (MeasuredTextWidth(candidate, box) <= room)
                    break;
            }
            box.PlaceholderText(shown);
            ToolTipService::SetToolTip(box, shown == MultipleValuesText()
                                                ? nullptr
                                                : box_value(MultipleValuesText()));
        }
    }

    void FitFixedColumns(FrameworkElement const& root)
    {
        if (!root || TextScale() <= 1.0)
            return;

        // The forms are laid out in grids of one row each, so the columns of the
        // same declared width belong together even across grids: they are the one
        // column of the form as it reads.
        std::vector<Grid> grids;
        std::vector<CheckBox> checks;
        try
        {
            Walk(root, [&](FrameworkElement const& element)
            {
                if (auto grid = element.try_as<Grid>())
                    grids.push_back(grid);
                else if (auto check = element.try_as<CheckBox>())
                    checks.push_back(check);
            });
        }
        catch (...)
        {
            return;
        }

        // A check keeps its box at the size the glyph is drawn for, which leaves
        // its label smaller than the labels it stands among; the label alone is
        // put back to the size the setting asks for.
        for (auto const& check : checks)
        {
            try
            {
                check.FontSize(check.FontSize() * TextScale());
            }
            catch (...)
            {
            }
        }

        std::map<double, double> needed;
        for (auto const& grid : grids)
        {
            auto columns = grid.ColumnDefinitions();
            for (uint32_t i = 0; i < columns.Size(); ++i)
            {
                const auto width = columns.GetAt(i).Width();
                if (width.GridUnitType != GridUnitType::Pixel || width.Value <= 0.0)
                    continue;
                auto& room = needed[width.Value];
                room = (std::max)(room, NeededWidth(grid, static_cast<int>(i)));
            }
        }

        for (auto const& grid : grids)
        {
            auto columns = grid.ColumnDefinitions();
            for (uint32_t i = 0; i < columns.Size(); ++i)
            {
                auto column = columns.GetAt(i);
                const auto width = column.Width();
                if (width.GridUnitType != GridUnitType::Pixel || width.Value <= 0.0)
                    continue;
                const double room = needed[width.Value];
                if (room > width.Value)
                    column.Width(GridLength{ room, GridUnitType::Pixel });
            }
        }
    }

    void SetNumberOrMultiple(NumberBox const& box, std::optional<double> const& value)
    {
        if (!box)
            return;
        if (value)
        {
            box.PlaceholderText(L"");
            ToolTipService::SetToolTip(box, nullptr);
            box.Value(std::isfinite(*value) ? *value : 0.0);
            return;
        }
        // A NumberBox holds numbers only, so the words go in the placeholder and
        // the value is cleared to let it show.
        box.Value(std::numeric_limits<double>::quiet_NaN());
        ShowMultipleInNumberBox(box);
        if (box.ActualWidth() <= 0.0)
        {
            // The pane is filled in before it is laid out the first time, when how
            // much room the field has is not yet known.
            box.DispatcherQueue().TryEnqueue([weak = make_weak(box)]
            {
                if (auto b = weak.get())
                    ShowMultipleInNumberBox(b);
            });
        }
    }

    void SetSliderAndBoxOrMultiple(Slider const& slider, NumberBox const& box,
                                   std::optional<double> const& value)
    {
        SetNumberOrMultiple(box, value);
        if (slider && value)
        {
            const double sane = std::isfinite(*value) ? *value : 0.0;
            slider.Value((std::clamp)(sane, slider.Minimum(), slider.Maximum()));
        }
    }

    void SetTextOrMultiple(TextBox const& box, std::optional<winrt::hstring> const& value)
    {
        if (!box)
            return;
        if (value)
        {
            box.PlaceholderText(L"");
            box.Text(*value);
            return;
        }
        box.PlaceholderText(MultipleValuesText());
        box.Text(L"");
    }

    void SetCheckOrMultiple(CheckBox const& check, std::optional<bool> const& value)
    {
        if (!check)
            return;
        if (value)
        {
            // Always drop three-state first: a prior mixed selection otherwise
            // leaves the Indeterminate visual until a pointer hover redraws it.
            check.IsThreeState(false);
            // Flip then set so the template leaves Indeterminate even when the
            // bool already matches (common after a collapsed Expander opens).
            check.IsChecked(!*value);
            check.IsChecked(*value);
            VisualStateManager::GoToState(check, L"Normal", false);
            VisualStateManager::GoToState(check, *value ? L"Checked" : L"Unchecked", false);
            return;
        }
        check.IsThreeState(true);
        check.IsChecked(nullptr);
        VisualStateManager::GoToState(check, L"Indeterminate", false);
    }

    void ResolveCheck(CheckBox const& check)
    {
        if (!check)
            return;
        if (check.IsThreeState())
            check.IsThreeState(false);
        auto const state = check.IsChecked();
        const bool value = state && state.Value();
        check.IsChecked(value);
        VisualStateManager::GoToState(check, L"Normal", false);
        VisualStateManager::GoToState(check, value ? L"Checked" : L"Unchecked", false);
    }

    void RefreshCheckVisual(CheckBox const& check)
    {
        if (!check)
            return;
        auto const state = check.IsChecked();
        if (!state)
        {
            if (check.IsThreeState())
            {
                VisualStateManager::GoToState(check, L"Indeterminate", false);
                return;
            }
            // Non-three-state null is the recycle/collapse glitch — treat as on.
            check.IsChecked(true);
            VisualStateManager::GoToState(check, L"Normal", false);
            VisualStateManager::GoToState(check, L"Checked", false);
            return;
        }
        check.IsThreeState(false);
        VisualStateManager::GoToState(check, L"Normal", false);
        VisualStateManager::GoToState(check, state.Value() ? L"Checked" : L"Unchecked", false);
    }

    namespace
    {
        DependencyProperty CheckSyncWiredProperty()
        {
            static auto prop = DependencyProperty::RegisterAttached(
                L"CheckSyncWired", xaml_typename<bool>(), xaml_typename<FrameworkElement>(),
                PropertyMetadata{ box_value(false) });
            return prop;
        }

        DependencyProperty CheckSyncBusyProperty()
        {
            static auto prop = DependencyProperty::RegisterAttached(
                L"CheckSyncBusy", xaml_typename<bool>(), xaml_typename<FrameworkElement>(),
                PropertyMetadata{ box_value(false) });
            return prop;
        }

        DependencyProperty CheckSyncPropertyNameProperty()
        {
            static auto prop = DependencyProperty::RegisterAttached(
                L"CheckSyncPropertyName", xaml_typename<hstring>(),
                xaml_typename<FrameworkElement>(), PropertyMetadata{ box_value(hstring{}) });
            return prop;
        }

        DependencyProperty CheckSyncAllowIndeterminateProperty()
        {
            static auto prop = DependencyProperty::RegisterAttached(
                L"CheckSyncAllowIndeterminate", xaml_typename<bool>(),
                xaml_typename<FrameworkElement>(), PropertyMetadata{ box_value(false) });
            return prop;
        }

        // ApplyCheckState writes IsChecked as a local value, which wins over the OneWay binding.
        // After that, Raise(Visible) / Raise(ShowsAtoms) on the row item no longer moves the box —
        // the children of a group that just hid its atoms stay looking checked. This watch hears the
        // row's PropertyChanged and pushes the model back onto the control.
        DependencyProperty CheckSyncSourceWatchProperty()
        {
            static auto prop = DependencyProperty::RegisterAttached(
                L"CheckSyncSourceWatch", xaml_typename<IInspectable>(),
                xaml_typename<FrameworkElement>(), PropertyMetadata{ nullptr });
            return prop;
        }

        void ApplyCheckFromDataContext(ToggleButton const& control, hstring const& propertyName,
                                       bool allowIndeterminate);

        struct CheckSyncSourceWatch : implements<CheckSyncSourceWatch, IInspectable>
        {
            weak_ref<ToggleButton> control{};
            hstring propertyName{};
            bool allowIndeterminate = false;
            INotifyPropertyChanged source{ nullptr };
            event_token token{};

            ~CheckSyncSourceWatch()
            {
                Detach();
            }

            void Detach()
            {
                if (source)
                {
                    source.PropertyChanged(token);
                    source = nullptr;
                    token = {};
                }
            }

            void Attach(IInspectable const& ctx)
            {
                Detach();
                source = ctx.try_as<INotifyPropertyChanged>();
                if (!source)
                    return;
                token = source.PropertyChanged(
                    { get_weak(), &CheckSyncSourceWatch::OnSourcePropertyChanged });
            }

            void OnSourcePropertyChanged(IInspectable const&,
                                         PropertyChangedEventArgs const& args)
            {
                const auto changed = args.PropertyName();
                if (!changed.empty() && changed != propertyName)
                    return;
                if (auto c = control.get())
                    ApplyCheckFromDataContext(c, propertyName, allowIndeterminate);
            }
        };

        std::optional<bool> ReadOptionalBoolProperty(IInspectable const& target,
                                                     hstring const& name)
        {
            auto provider = target.try_as<ICustomPropertyProvider>();
            if (!provider)
                return true;
            auto prop = provider.GetCustomProperty(name);
            if (!prop)
                return true;
            auto value = prop.GetValue(target);
            if (!value)
                return std::nullopt;
            if (auto boxed = value.try_as<IReference<bool>>())
                return boxed.Value();
            return unbox_value_or<bool>(value, true);
        }

        void ApplyCheckState(ToggleButton const& control, std::optional<bool> state,
                             bool allowIndeterminate)
        {
            if (!control)
                return;
            if (unbox_value_or<bool>(control.GetValue(CheckSyncBusyProperty()), false))
                return;
            control.SetValue(CheckSyncBusyProperty(), box_value(true));
            if (!state && allowIndeterminate)
            {
                control.IsThreeState(true);
                control.IsChecked(IReference<bool>{ nullptr });
                VisualStateManager::GoToState(control, L"Indeterminate", false);
            }
            else
            {
                const bool value = state.value_or(true);
                // Recycled rows often keep IsThreeState / Indeterminate from the
                // previous item; skipping IsChecked when the bool already matches
                // leaves the '-' glyph until hover. Always coerce both.
                control.IsThreeState(false);
                control.IsChecked(value);
                VisualStateManager::GoToState(control, L"Normal", false);
                VisualStateManager::GoToState(control, value ? L"Checked" : L"Unchecked", false);
            }
            control.SetValue(CheckSyncBusyProperty(), box_value(false));
        }

        void ApplyCheckFromDataContext(ToggleButton const& control, hstring const& propertyName,
                                       bool allowIndeterminate)
        {
            if (!control)
                return;
            auto ctx = control.DataContext();
            if (!ctx)
            {
                if (!allowIndeterminate)
                    ApplyCheckState(control, true, false);
                return;
            }
            auto state = ReadOptionalBoolProperty(ctx, propertyName);
            if (!allowIndeterminate && !state)
                state = true;
            ApplyCheckState(control, state, allowIndeterminate);
        }

        void BindCheckSyncSource(ToggleButton const& control, hstring const& propertyName,
                                 bool allowIndeterminate)
        {
            com_ptr<CheckSyncSourceWatch> watch{ nullptr };
            if (auto stored = control.GetValue(CheckSyncSourceWatchProperty()))
                watch.copy_from(get_self<CheckSyncSourceWatch>(stored.as<IInspectable>()));
            if (!watch)
            {
                watch = make_self<CheckSyncSourceWatch>();
                control.SetValue(CheckSyncSourceWatchProperty(), watch.as<IInspectable>());
            }
            watch->control = control;
            watch->propertyName = propertyName;
            watch->allowIndeterminate = allowIndeterminate;
            watch->Attach(control.DataContext());
        }

        void WireCheckSync(ToggleButton const& control, hstring const& propertyName,
                           bool allowIndeterminate)
        {
            if (!control)
                return;
            control.SetValue(CheckSyncPropertyNameProperty(), box_value(propertyName));
            control.SetValue(CheckSyncAllowIndeterminateProperty(), box_value(allowIndeterminate));
            if (unbox_value_or<bool>(control.GetValue(CheckSyncWiredProperty()), false))
                return;
            control.SetValue(CheckSyncWiredProperty(), box_value(true));
            control.RegisterPropertyChangedCallback(
                ToggleButton::IsCheckedProperty(),
                [](DependencyObject const& sender, DependencyProperty const&)
                {
                    auto control = sender.try_as<ToggleButton>();
                    if (!control ||
                        unbox_value_or<bool>(control.GetValue(CheckSyncBusyProperty()), false))
                        return;
                    const auto name = unbox_value_or<hstring>(
                        control.GetValue(CheckSyncPropertyNameProperty()), hstring{});
                    const bool allow = unbox_value_or<bool>(
                        control.GetValue(CheckSyncAllowIndeterminateProperty()), false);
                    auto checked = control.IsChecked();
                    if (!checked)
                    {
                        if (!allow)
                            ApplyCheckFromDataContext(control, name, false);
                        else
                        {
                            control.IsThreeState(true);
                            VisualStateManager::GoToState(control, L"Indeterminate", false);
                        }
                        return;
                    }
                    control.IsThreeState(false);
                    VisualStateManager::GoToState(control, L"Normal", false);
                    VisualStateManager::GoToState(
                        control, checked.Value() ? L"Checked" : L"Unchecked", false);
                });
        }
    }

    void SyncCheckFromDataContext(ToggleButton const& control, hstring const& propertyName,
                                  bool allowIndeterminate)
    {
        if (!control)
            return;
        WireCheckSync(control, propertyName, allowIndeterminate);
        BindCheckSyncSource(control, propertyName, allowIndeterminate);
        ApplyCheckFromDataContext(control, propertyName, allowIndeterminate);
        // Binding and the visual state machine settle on a later tick after recycle;
        // one deferred pass covers expand/collapse inserts that still look wrong.
        auto weak = make_weak(control);
        control.DispatcherQueue().TryEnqueue(
            [weak, propertyName, allowIndeterminate]()
            {
                if (auto control = weak.get())
                {
                    BindCheckSyncSource(control, propertyName, allowIndeterminate);
                    ApplyCheckFromDataContext(control, propertyName, allowIndeterminate);
                }
            });
    }

    void SelectOrMultiple(ComboBox const& combo, std::optional<int> const& index)
    {
        if (!combo)
            return;
        auto items = combo.Items();
        const uint32_t count = items.Size();
        const bool hasEntry = count > 0 && ItemText(items.GetAt(count - 1)) == MultipleValuesText();
        if (index)
        {
            if (hasEntry)
                items.RemoveAtEnd();
            const int last = static_cast<int>(items.Size()) - 1;
            combo.SelectedIndex(last < 0 ? -1 : (std::clamp)(*index, 0, last));
            return;
        }
        if (!hasEntry)
            items.Append(box_value(MultipleValuesText()));
        combo.SelectedIndex(static_cast<int>(items.Size()) - 1);
    }

    bool IsMultipleValuesSelected(ComboBox const& combo)
    {
        if (!combo)
            return false;
        return ItemText(combo.SelectedItem()) == MultipleValuesText();
    }

    void SetColorWell(Border const& swatch, RKColor const& color)
    {
        if (swatch)
            swatch.Background(SolidColorBrush(ToUiColor(color)));
    }

    void SetColorWellOrMultiple(Border const& swatch, std::optional<RKColor> const& color)
    {
        SetColorWell(swatch, color ? *color : RKColor::fromRgb(211, 211, 211));
    }

    void AttachColorWell(DropDownButton const& button, Border const& swatch,
                         std::function<void(RKColor)> apply)
    {
        if (!button || !swatch || !apply)
            return;
        Flyout flyout;
        flyout.Placement(FlyoutPlacementMode::Bottom);
        flyout.Opening([apply = std::move(apply), weakSwatch = make_weak(swatch),
                        weakFlyout = make_weak(flyout)](IInspectable const&, IInspectable const&)
        {
            auto fly = weakFlyout.get();
            auto sw = weakSwatch.get();
            if (!fly || !sw)
                return;
            const auto shown = sw.Background().as<SolidColorBrush>().Color();
            if (auto existing = fly.Content().try_as<ColorPicker>())
            {
                existing.Color(shown);
                return;
            }
            ColorPicker picker;
            picker.IsAlphaEnabled(true);
            picker.IsMoreButtonVisible(true);
            picker.Color(shown);
            picker.ColorChanged([apply, weakSwatch](ColorPicker const&, ColorChangedEventArgs const& args)
            {
                const auto color = args.NewColor();
                if (auto s = weakSwatch.get())
                    s.Background(SolidColorBrush(color));
                apply(FromUiColor(color));
            });
            fly.Content(picker);
        });
        button.Flyout(flyout);
    }
}
