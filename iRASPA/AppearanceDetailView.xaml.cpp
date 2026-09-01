#include "pch.h"
#include "AppearanceDetailView.xaml.h"
#if __has_include("AppearanceDetailView.g.cpp")
#include "AppearanceDetailView.g.cpp"
#endif

#include "DetailControls.h"

#include "annotationviewer.h"
#include "atomstructureviewer.h"
#include "bondstructureviewer.h"
#include "documentdata.h"
#include "iraspaobject.h"
#include "mathkit.h"
#include "object.h"
#include "primitivestructureviewer.h"
#include "ribbonstructureeditor.h"
#include "rklocalaxes.h"
#include "rkrenderuniforms.h"
#include "rkstring.h"
#include "volumetricdataviewer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <type_traits>
#include <utility>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kDegreesPerRadian = 180.0 / kPi;
        constexpr double kRadiansPerDegree = kPi / 180.0;

        // Cocoa's selection-style popup and the three light-order popups, which
        // several groups share.
        std::vector<hstring> SelectionStyles()
        {
            return { L"None", L"Worley Noise 3D", L"Striped", L"Glow" };
        }

        std::vector<hstring> SchemeOrders()
        {
            return { L"Element", L"Force Field First", L"Force Field Only" };
        }

        // The cues of Tarini, Cignoni and Montani, in Cocoa's order and wording.
        std::vector<hstring> EdgeCueings()
        {
            return { L"None", L"Contour lines", L"Halos", L"Contour lines and halos" };
        }

        // The style popup lists the predefined styles, and "Custom" last. As in
        // Cocoa, "Custom" cannot be picked: it is what the popup shows once the
        // settings no longer match a predefined style.
        //
        // The order is Cocoa's rather than the enum's. QuteMol belongs beside the Fancy style whose
        // material it shares, while its raw value had to be appended after the styles that shipped
        // before it, so the two no longer coincide and the popup maps between them.
        constexpr AtomStructureViewer::RepresentationStyle kStyleItems[] = {
            AtomStructureViewer::RepresentationStyle::defaultStyle,
            AtomStructureViewer::RepresentationStyle::fancy,
            AtomStructureViewer::RepresentationStyle::quteMol,
            AtomStructureViewer::RepresentationStyle::licorice,
            AtomStructureViewer::RepresentationStyle::objects };
        constexpr int kCustomStyleItem = static_cast<int>(std::size(kStyleItems));

        std::vector<hstring> RepresentationStyles()
        {
            return { L"Default", L"Fancy", L"QuteMol", L"Licorice", L"Objects", L"Custom" };
        }

        int StyleItem(AtomStructureViewer::RepresentationStyle style)
        {
            const auto item = std::find(std::begin(kStyleItems), std::end(kStyleItems), style);
            return item == std::end(kStyleItems)
                       ? kCustomStyleItem
                       : static_cast<int>(item - std::begin(kStyleItems));
        }

        // The item of a popup whose order follows an enum, and nothing when the
        // selection does not agree on the value.
        template<typename Enum>
        std::optional<int> ItemOf(std::optional<Enum> const& value)
        {
            if (!value)
                return std::nullopt;
            return static_cast<int>(*value);
        }

        template<typename Style>
        std::optional<int> StyleItemOf(std::optional<Style> const& style)
        {
            if (!style)
                return std::nullopt;
            return StyleItem(*style);
        }

        hstring RotateTitle(bool plus, double delta)
        {
            wchar_t buffer[40];
            swprintf_s(buffer, L"Rotate %s%.4g", plus ? L"+" : L"-", delta);
            return hstring(buffer);
        }
        // Cocoa AppearanceRibbonHelpers: only primitive object types, and only
        // protein / proteinCrystal for ribbons (solvent crystals are excluded).
        bool ObjectTypeIsPrimitive(ObjectType type)
        {
            switch (type)
            {
            case ObjectType::ellipsoidPrimitive:
            case ObjectType::cylinderPrimitive:
            case ObjectType::polygonalPrismPrimitive:
            case ObjectType::crystalEllipsoidPrimitive:
            case ObjectType::crystalCylinderPrimitive:
            case ObjectType::crystalPolygonalPrismPrimitive:
                return true;
            default:
                return false;
            }
        }

        bool ObjectTypeIsProteinRibbon(ObjectType type)
        {
            return type == ObjectType::protein || type == ObjectType::proteinCrystal;
        }

        bool HasPrimitiveStructure(DocumentController* controller)
        {
            if (!controller)
                return false;
            for (auto const& iraspa : controller->AppearanceSectionStructures())
            {
                if (iraspa && ObjectTypeIsPrimitive(iraspa->type())
                    && std::dynamic_pointer_cast<PrimitiveEditor>(iraspa->object()))
                {
                    return true;
                }
            }
            return false;
        }

        bool HasProteinRibbonStructure(DocumentController* controller)
        {
            if (!controller)
                return false;
            for (auto const& iraspa : controller->AppearanceSectionStructures())
            {
                if (iraspa && ObjectTypeIsProteinRibbon(iraspa->type())
                    && std::dynamic_pointer_cast<ProteinRibbonStructureEditor>(iraspa->object()))
                {
                    return true;
                }
            }
            return false;
        }

        // Has* helpers above; FirstAs/ForEachAs/AgreedAs use AppearanceSectionStructures
        // for Primitive and Ribbon interfaces (see StructuresFor).
    }

    // The interfaces a group edits: the first structure of the selection fills
    // the fields, and every edit is written to all of them (Cocoa's inspector
    // applies to the whole selection). Primitive and Ribbon groups use the wider
    // Appearance section context (selected scenes / project), matching Cocoa's
    // outline-group visibility.
    template<typename T>
    static constexpr bool UsesAppearanceSectionContext()
    {
        return std::is_base_of_v<PrimitiveViewer, T>
            || std::is_same_v<T, ProteinRibbonStructureEditor>;
    }

    static bool IsPrimitiveObjectType(ObjectType type)
    {
        switch (type)
        {
        case ObjectType::ellipsoidPrimitive:
        case ObjectType::cylinderPrimitive:
        case ObjectType::polygonalPrismPrimitive:
        case ObjectType::crystalEllipsoidPrimitive:
        case ObjectType::crystalCylinderPrimitive:
        case ObjectType::crystalPolygonalPrismPrimitive:
            return true;
        default:
            return false;
        }
    }

    static bool IsProteinRibbonObjectType(ObjectType type)
    {
        return type == ObjectType::protein || type == ObjectType::proteinCrystal;
    }

    template<typename T>
    static bool SectionTypeAllows(std::shared_ptr<iRASPAObject> const& iraspa)
    {
        if (!iraspa)
            return false;
        if constexpr (std::is_base_of_v<PrimitiveViewer, T>)
            return IsPrimitiveObjectType(iraspa->type());
        if constexpr (std::is_same_v<T, ProteinRibbonStructureEditor>)
            return IsProteinRibbonObjectType(iraspa->type());
        return true;
    }

    template<typename T>
    static std::vector<std::shared_ptr<iRASPAObject>> StructuresFor(DocumentController* controller)
    {
        if (!controller)
            return {};
        if constexpr (UsesAppearanceSectionContext<T>())
            return controller->AppearanceSectionStructures();
        return controller->TargetStructures();
    }

    template<typename T>
    static std::shared_ptr<T> FirstAs(DocumentController* controller)
    {
        if (!controller)
            return nullptr;
        for (auto const& iraspa : StructuresFor<T>(controller))
        {
            if (!SectionTypeAllows<T>(iraspa))
                continue;
            if (auto typed = std::dynamic_pointer_cast<T>(iraspa->object()))
                return typed;
        }
        return nullptr;
    }

    template<typename T, typename Fn>
    static void ForEachAs(DocumentController* controller, Fn const& fn)
    {
        if (!controller)
            return;
        for (auto const& iraspa : StructuresFor<T>(controller))
        {
            if (!SectionTypeAllows<T>(iraspa))
                continue;
            if (auto typed = std::dynamic_pointer_cast<T>(iraspa->object()))
                fn(typed);
        }
    }

    // One property read across the selection: a value while the structures agree
    // on it, nothing once two of them differ, which is what the fields show as
    // "Multiple Values".
    template<typename T, typename Read>
    static auto AgreedAs(DocumentController* controller, Read const& read)
        -> std::optional<std::decay_t<decltype(read(std::shared_ptr<T>{}))>>
    {
        if (!controller)
            return std::nullopt;
        if constexpr (!UsesAppearanceSectionContext<T>())
            return controller->AgreedValue<T>(read);

        using Value = std::decay_t<decltype(read(std::shared_ptr<T>{}))>;
        std::optional<Value> agreed;
        for (auto const& iraspa : StructuresFor<T>(controller))
        {
            if (!SectionTypeAllows<T>(iraspa))
                continue;
            auto typed = std::dynamic_pointer_cast<T>(iraspa->object());
            if (!typed)
                continue;
            Value value = read(typed);
            if (!agreed)
                agreed = std::move(value);
            else if (!(*agreed == value))
                return std::nullopt;
        }
        return agreed;
    }

    // Cocoa's ribbon selection rows edit the atom selection settings — atoms,
    // bonds and the ribbon overlay all read one set of values — but only for the
    // structures of the selection that have a ribbon.
    template<typename Fn>
    static void ForEachRibbonSelection(DocumentController* controller, Fn const& fn)
    {
        ForEachAs<ProteinRibbonStructureEditor>(controller, [&fn](auto const& ribbon)
        {
            if (auto atoms = std::dynamic_pointer_cast<AtomStructureEditor>(ribbon))
                fn(atoms);
        });
    }

    template<typename Read>
    static auto AgreedRibbonSelection(DocumentController* controller, Read const& read)
        -> std::optional<std::decay_t<decltype(read(std::shared_ptr<AtomStructureViewer>{}))>>
    {
        using Value = std::decay_t<decltype(read(std::shared_ptr<AtomStructureViewer>{}))>;
        if (!controller)
            return std::nullopt;
        return controller->AgreedPartialValue<ProteinRibbonStructureEditor>(
            [&read](std::shared_ptr<ProteinRibbonStructureEditor> const& ribbon)
                -> std::optional<Value>
            {
                auto atoms = std::dynamic_pointer_cast<AtomStructureViewer>(ribbon);
                if (!atoms)
                    return std::nullopt;
                return std::optional<Value>(read(atoms));
            });
    }

    // Rotating or transforming a primitive moves the bounding box, so the camera
    // is refitted to it, as Cocoa does after an orientation change.
    static void RecomputeBoundingBoxes(DocumentController* controller)
    {
        if (!controller)
            return;
        for (auto const& iraspa : controller->TargetStructures())
        {
            if (auto object = iraspa->object())
                object->reComputeBoundingBox();
        }
    }

    AppearanceDetailView::AppearanceDetailView()
    {
        InitializeComponent();
        DetailControls::FitFixedColumns(*this);
        WirePrimitive();
        WireRibbon();
        WireAtoms();
        WireBonds();
        WireUnitCell();
        WireLocalAxes();
        WireVolumetric();
        WireAnnotation();
        WireSectionExpanders();
    }

    // ---- binding ---------------------------------------------------------

    void AppearanceDetailView::BindNumber(NumberBox const& box, double minV, double maxV, double step,
                                          std::function<void(double)> apply)
    {
        box.Minimum(minV);
        box.Maximum(maxV);
        box.SmallChange(step);
        box.LargeChange(step * 5.0);
        box.ValueChanged([this, apply = std::move(apply), recheck = m_recheckStyle]
                         (NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
        {
            if (m_suppress)
                return;
            const double value = sender.Value();
            if (!std::isfinite(value))
                return;
            apply(value);
            if (recheck)
                RecheckAtomStyle();
            // Scale, style and representation changes move the geometry the bake was done against.
            if (m_controller)
                m_controller->ReloadRendererInvalidatingAmbientOcclusion();
        });
    }

    // Cocoa pairs a continuous slider with an editable field. Both write the
    // same property; the guard inside the shared helper breaks the loop between
    // them.
    void AppearanceDetailView::BindSlider(Slider const& slider, NumberBox const& box,
                                          double minV, double maxV, double step,
                                          std::function<void(double)> apply)
    {
        SetRange(slider, box, minV, maxV, step);
        DetailControls::SyncSliderAndBox(slider, box,
            [this, apply = std::move(apply), recheck = m_recheckStyle](double value)
        {
            if (m_suppress)
                return;
            apply(value);
            if (recheck)
                RecheckAtomStyle();
            if (m_controller)
                m_controller->ReloadRendererInvalidatingAmbientOcclusion();
        });
    }

    void AppearanceDetailView::BindCheck(CheckBox const& check, std::function<void(bool)> apply)
    {
        auto handler = [this, apply = std::move(apply), recheck = m_recheckStyle]
                       (IInspectable const& sender, RoutedEventArgs const&)
        {
            if (m_suppress)
                return;
            auto box = sender.try_as<CheckBox>();
            if (!box)
                return;
            auto state = box.IsChecked();
            if (!state)
                return;
            // The box carried the mixed state until this click resolved it.
            DetailControls::ResolveCheck(box);
            apply(state.Value());
            if (recheck)
                RecheckAtomStyle();
            if (m_controller)
                m_controller->ReloadRendererInvalidatingAmbientOcclusion();
        };
        check.Checked(handler);
        check.Unchecked(handler);
    }

    void AppearanceDetailView::BindCombo(ComboBox const& combo,
                                         std::function<void(int, hstring const&)> apply)
    {
        combo.SelectionChanged([this, apply = std::move(apply), recheck = m_recheckStyle]
                               (IInspectable const& sender, SelectionChangedEventArgs const&)
        {
            if (m_suppress)
                return;
            auto box = sender.try_as<ComboBox>();
            if (!box || box.SelectedIndex() < 0)
                return;
            // The "Multiple Values" entry is there to be read, not picked.
            if (DetailControls::IsMultipleValuesSelected(box))
                return;
            hstring text;
            if (auto item = box.SelectedItem())
                text = unbox_value_or<hstring>(item, hstring{});
            apply(box.SelectedIndex(), text);
            if (recheck)
                RecheckAtomStyle();
            if (m_controller)
                m_controller->ReloadRendererInvalidatingAmbientOcclusion();
        });
    }

    void AppearanceDetailView::BindWell(DropDownButton const& button, Border const& swatch,
                                        std::function<void(RKColor)> apply)
    {
        DetailControls::AttachColorWell(button, swatch,
            [this, apply = std::move(apply), recheck = m_recheckStyle](RKColor color)
        {
            if (m_suppress)
                return;
            apply(color);
            if (recheck)
                RecheckAtomStyle();
            if (m_controller)
                m_controller->ReloadRendererInvalidatingAmbientOcclusion();
        });
    }

    // Cocoa's recheckRepresentationStyle plus its reload of the style row: the
    // style is re-derived from the settings of every structure in the selection,
    // and the popup follows the first of them.
    void AppearanceDetailView::RecheckAtomStyle()
    {
        ForEachAs<AtomStructureEditor>(m_controller, [](auto editor)
        {
            editor->recheckRepresentationStyle();
        });

        if (!FirstAs<AtomStructureViewer>(m_controller))
            return;
        const bool wasSuppressed = m_suppress;
        m_suppress = true;
        DetailControls::SelectOrMultiple(
            AtomStyle(),
            StyleItemOf(AgreedAs<AtomStructureViewer>(
                m_controller, [](auto const& a) { return a->atomRepresentationStyle(); })));
        m_suppress = wasSuppressed;
    }

    void AppearanceDetailView::RecheckRibbonStyle()
    {
        ForEachAs<ProteinRibbonStructureEditor>(m_controller, [](auto editor)
        {
            editor->recheckRibbonRepresentationStyle();
        });

        if (!FirstAs<ProteinRibbonStructureEditor>(m_controller))
            return;
        const bool wasSuppressed = m_suppress;
        m_suppress = true;
        DetailControls::SelectOrMultiple(
            RibbonStyle(),
            ItemOf(AgreedAs<ProteinRibbonStructureEditor>(
                m_controller, [](auto const& r) { return r->ribbonRepresentationStyle(); })));
        m_suppress = wasSuppressed;
    }

    void AppearanceDetailView::SetNumber(NumberBox const& box, std::optional<double> const& value)
    {
        if (value && std::isfinite(*value))
        {
            DetailControls::SetNumberOrMultiple(
                box, (std::clamp)(*value, box.Minimum(), box.Maximum()));
            return;
        }
        DetailControls::SetNumberOrMultiple(box, value ? std::optional<double>(0.0) : std::nullopt);
    }

    void AppearanceDetailView::SetSlider(Slider const& slider, NumberBox const& box,
                                         std::optional<double> const& value)
    {
        DetailControls::SetSliderAndBoxOrMultiple(slider, box, value);
    }

    void AppearanceDetailView::SetCheck(CheckBox const& check, std::optional<bool> const& value)
    {
        DetailControls::SetCheckOrMultiple(check, value);
    }

    void AppearanceDetailView::RefreshAllCheckVisuals()
    {
        CheckBox const checks[] = {
            PrimCapped(), PrimFrontHDR(), PrimBackHDR(),
            AtomDraw(), AtomHDR(), AtomAO(),
            BondDraw(), BondHDR(), BondAO(),
            RibbonDraw(), RibbonHDR(), RibbonAO(),
            UnitCellDraw(),
            VolDraw(), VolFrontHDR(), VolBackHDR(),
        };
        for (auto const& check : checks)
            DetailControls::RefreshCheckVisual(check);
    }

    void AppearanceDetailView::WireSectionExpanders()
    {
        // Cocoa outline groups are Expanders. Opening a section that was filled
        // while collapsed needs a visual-state push for its checkboxes.
        auto weak = get_weak();
        auto const onExpanding = [weak](Expander const&, ExpanderExpandingEventArgs const&)
        {
            if (auto self = weak.get())
            {
                self->DispatcherQueue().TryEnqueue([weak]()
                {
                    if (auto self = weak.get())
                        self->RefreshAllCheckVisuals();
                });
            }
        };

        std::function<void(DependencyObject const&)> walk;
        walk = [&](DependencyObject const& root)
        {
            if (!root)
                return;
            if (auto exp = root.try_as<Expander>())
                exp.Expanding(onExpanding);
            if (auto panel = root.try_as<Panel>())
            {
                for (auto const& child : panel.Children())
                    walk(child);
            }
            else if (auto content = root.try_as<ContentControl>())
            {
                if (auto inner = content.Content().try_as<DependencyObject>())
                    walk(inner);
            }
        };
        if (auto content = Content().try_as<DependencyObject>())
            walk(content);
        else
            walk(*this);
    }

    void AppearanceDetailView::SetRange(Slider const& slider, NumberBox const& box,
                                        double minV, double maxV, double step)
    {
        if (!(maxV > minV))
            maxV = minV + 1.0;
        slider.Minimum(minV);
        slider.Maximum(maxV);
        slider.StepFrequency(step);
        box.Minimum(minV);
        box.Maximum(maxV);
        box.SmallChange(step);
        box.LargeChange(step * 5.0);
    }

    void AppearanceDetailView::FillCombo(ComboBox const& combo, std::vector<hstring> const& items,
                                         std::optional<int> const& selected)
    {
        combo.Items().Clear();
        for (auto const& item : items)
            combo.Items().Append(box_value(item));
        if (items.empty())
        {
            combo.SelectedIndex(-1);
            return;
        }
        // A mixed selection appends the "Multiple Values" entry and lands on it.
        DetailControls::SelectOrMultiple(combo, selected);
    }

    void AppearanceDetailView::ShowBody(TextBlock const& hint, Panel const& body, bool available)
    {
        hint.Visibility(available ? Visibility::Collapsed : Visibility::Visible);
        body.Visibility(available ? Visibility::Visible : Visibility::Collapsed);
    }

    void AppearanceDetailView::ShowSection(Expander const& section, bool available)
    {
        if (section)
            section.Visibility(available ? Visibility::Visible : Visibility::Collapsed);
    }

    // ---- wiring ----------------------------------------------------------

    void AppearanceDetailView::WirePrimitive()
    {
        m_rotate[0] = PrimYawPlus();
        m_rotate[1] = PrimYawMinus();
        m_rotate[2] = PrimPitchPlus();
        m_rotate[3] = PrimPitchMinus();
        m_rotate[4] = PrimRollPlus();
        m_rotate[5] = PrimRollMinus();
        for (int i = 0; i < static_cast<int>(m_rotate.size()); ++i)
        {
            m_rotate[i].Click([this, i](IInspectable const&, RoutedEventArgs const&)
            {
                RotatePrimitive(i);
            });
        }

        // Cocoa retitles the rotate buttons when the step changes, so the
        // buttons say what they will do.
        BindNumber(PrimRotDelta(), 0.1, 180.0, 1.0, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveRotationDelta(value);
            });
            RetitleRotateButtons(value);
        });

        // Rows 1-3 of the Cocoa cell are yaw/x, pitch/z and roll/y, and y has
        // the narrower range.
        struct EulerRow { Slider slider; NumberBox box; int axis; double minV; double maxV; };
        const EulerRow rows[3] = {
            { PrimEulerXSlider(), PrimEulerXBox(), 0, -180.0, 180.0 },
            { PrimEulerZSlider(), PrimEulerZBox(), 2, -180.0, 180.0 },
            { PrimEulerYSlider(), PrimEulerYBox(), 1,  -90.0,  90.0 },
        };
        for (auto const& row : rows)
        {
            const int axis = row.axis;
            BindSlider(row.slider, row.box, row.minV, row.maxV, 1.0, [this, axis](double value)
            {
                const double radians = value * kRadiansPerDegree;
                ForEachAs<PrimitiveEditor>(m_controller, [axis, radians](auto editor)
                {
                    double3 euler = editor->primitiveOrientation().EulerAngles();
                    if (axis == 0)
                        euler.x = radians;
                    else if (axis == 1)
                        euler.y = radians;
                    else
                        euler.z = radians;
                    editor->setPrimitiveOrientation(simd_quatd(euler));
                });
                RecomputeBoundingBoxes(m_controller);
                if (m_controller)
                    m_controller->RefitCameraToBoundingBox();
            });
        }

        // The transformation matrix, in the UI's row-major order.
        const std::pair<NumberBox, int> cells[9] = {
            { PrimTransAX(), 0 }, { PrimTransBX(), 1 }, { PrimTransCX(), 2 },
            { PrimTransAY(), 3 }, { PrimTransBY(), 4 }, { PrimTransCY(), 5 },
            { PrimTransAZ(), 6 }, { PrimTransBZ(), 7 }, { PrimTransCZ(), 8 },
        };
        for (auto const& [box, index] : cells)
        {
            const int slot = index;
            BindNumber(box, -1000.0, 1000.0, 0.1, [this, slot](double value)
            {
                ForEachAs<PrimitiveEditor>(m_controller, [slot, value](auto editor)
                {
                    double3x3 matrix = editor->primitiveTransformationMatrix();
                    switch (slot)
                    {
                        case 0: matrix.ax = value; break;
                        case 1: matrix.bx = value; break;
                        case 2: matrix.cx = value; break;
                        case 3: matrix.ay = value; break;
                        case 4: matrix.by = value; break;
                        case 5: matrix.cy = value; break;
                        case 6: matrix.az = value; break;
                        case 7: matrix.bz = value; break;
                        default: matrix.cz = value; break;
                    }
                    editor->setPrimitiveTransformationMatrix(matrix);
                });
                RecomputeBoundingBoxes(m_controller);
                if (m_controller)
                    m_controller->RefitCameraToBoundingBox();
            });
        }

        BindCheck(PrimCapped(), [this](bool on)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [on](auto editor) { editor->setPrimitiveIsCapped(on); });
        });
        BindSlider(PrimOpacitySlider(), PrimOpacityBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor) { editor->setPrimitiveOpacity(value); });
        });
        BindSlider(PrimSidesSlider(), PrimSidesBox(), 2.0, 41.0, 1.0, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveNumberOfSides(static_cast<int>(value));
            });
        });

        BindCombo(PrimSelStyle(), [this](int index, hstring const&)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [index](auto editor)
            {
                editor->setPrimitiveSelectionStyle(static_cast<RKSelectionStyle>(index));
            });
        });
        BindNumber(PrimSelFreq(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveSelectionFrequency(value);
            });
        });
        BindNumber(PrimSelDensity(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveSelectionDensity(value);
            });
        });
        BindSlider(PrimSelIntensitySlider(), PrimSelIntensityBox(), 0.0, 2.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveSelectionIntensity(value);
            });
        });
        BindSlider(PrimSelScalingSlider(), PrimSelScalingBox(), 1.0, 2.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveSelectionScaling(value);
            });
        });

        BindSlider(PrimHueSlider(), PrimHueBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor) { editor->setPrimitiveHue(value); });
        });
        BindSlider(PrimSatSlider(), PrimSatBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor) { editor->setPrimitiveSaturation(value); });
        });
        BindSlider(PrimValSlider(), PrimValBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor) { editor->setPrimitiveValue(value); });
        });

        BindCheck(PrimFrontHDR(), [this](bool on)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [on](auto editor) { editor->setPrimitiveFrontSideHDR(on); });
        });
        BindSlider(PrimFrontHDRExpSlider(), PrimFrontHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveFrontSideHDRExposure(value);
            });
        });
        BindSlider(PrimFrontAmbSlider(), PrimFrontAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveFrontSideAmbientIntensity(value);
            });
        });
        BindWell(PrimFrontAmbWell(), PrimFrontAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveFrontSideAmbientColor(color);
            });
        });
        BindSlider(PrimFrontDiffSlider(), PrimFrontDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveFrontSideDiffuseIntensity(value);
            });
        });
        BindWell(PrimFrontDiffWell(), PrimFrontDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveFrontSideDiffuseColor(color);
            });
        });
        BindSlider(PrimFrontSpecSlider(), PrimFrontSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveFrontSideSpecularIntensity(value);
            });
        });
        BindWell(PrimFrontSpecWell(), PrimFrontSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveFrontSideSpecularColor(color);
            });
        });
        BindSlider(PrimFrontShininessSlider(), PrimFrontShininessBox(), 0.0, 256.0, 1.0, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveFrontSideShininess(value);
            });
        });

        BindCheck(PrimBackHDR(), [this](bool on)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [on](auto editor) { editor->setPrimitiveBackSideHDR(on); });
        });
        BindSlider(PrimBackHDRExpSlider(), PrimBackHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveBackSideHDRExposure(value);
            });
        });
        BindSlider(PrimBackAmbSlider(), PrimBackAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveBackSideAmbientIntensity(value);
            });
        });
        BindWell(PrimBackAmbWell(), PrimBackAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveBackSideAmbientColor(color);
            });
        });
        BindSlider(PrimBackDiffSlider(), PrimBackDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveBackSideDiffuseIntensity(value);
            });
        });
        BindWell(PrimBackDiffWell(), PrimBackDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveBackSideDiffuseColor(color);
            });
        });
        BindSlider(PrimBackSpecSlider(), PrimBackSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveBackSideSpecularIntensity(value);
            });
        });
        BindWell(PrimBackSpecWell(), PrimBackSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [color](auto editor)
            {
                editor->setPrimitiveBackSideSpecularColor(color);
            });
        });
        BindSlider(PrimBackShininessSlider(), PrimBackShininessBox(), 0.0, 256.0, 1.0, [this](double value)
        {
            ForEachAs<PrimitiveEditor>(m_controller, [value](auto editor)
            {
                editor->setPrimitiveBackSideShininess(value);
            });
        });
    }

    void AppearanceDetailView::WireAtoms()
    {
        // Every atom setting takes part in the representation style, so all of
        // these bindings recheck it after they have written the model.
        m_recheckStyle = true;

        BindCheck(AtomDraw(), [this](bool on)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [on](auto editor) { editor->setDrawAtoms(on); });
        });
        BindSlider(AtomScaleSlider(), AtomScaleBox(), 0.1, 2.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomScaleFactor(value);
            });
        });

        BindCombo(AtomType(), [this](int index, hstring const&)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setRepresentationType(static_cast<AtomStructureEditor::RepresentationType>(index));
            });
        });
        // A style writes most of the atom and bond settings, so the whole form is
        // reloaded afterwards, as Cocoa reloads the rows the style touched. The
        // "Custom" item writes nothing: the recheck that follows every binding
        // puts the popup back on the style the settings match.
        BindCombo(AtomStyle(), [this](int index, hstring const&)
        {
            if (!m_controller || !m_controller->Document() || index < 0 || index >= kCustomStyleItem)
                return;
            const auto style = kStyleItems[index];
            auto& colorSets = m_controller->Document()->colorSets();
            try
            {
                ForEachAs<AtomStructureEditor>(m_controller, [style, &colorSets](auto editor)
                {
                    editor->setRepresentationStyle(style, colorSets);
                });
            }
            catch (...)
            {
                m_controller->Log(L"Style change failed");
            }
            DispatcherQueue().TryEnqueue([this]() { Reload(); });
        });
        BindCombo(AtomColorScheme(), [this](int index, hstring const&)
        {
            if (!m_controller || !m_controller->Document())
                return;
            auto& colorSets = m_controller->Document()->colorSets();
            auto const& sets = colorSets.colorSets();
            if (index < 0 || index >= static_cast<int>(sets.size()))
                return;
            const RKString name = sets[static_cast<size_t>(index)].displayName();
            ForEachAs<AtomStructureEditor>(m_controller, [&name, &colorSets](auto editor)
            {
                editor->setRepresentationColorSchemeIdentifier(name, colorSets);
            });
        });
        BindCombo(AtomColorOrder(), [this](int index, hstring const&)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setColorSchemeOrder(static_cast<SKColorSet::ColorSchemeOrder>(index));
            });
        });
        BindCombo(AtomFF(), [this](int index, hstring const&)
        {
            if (!m_controller || !m_controller->Document())
                return;
            auto& forceFieldSets = m_controller->Document()->forceFieldSets();
            auto const& sets = forceFieldSets.forceFieldSets();
            if (index < 0 || index >= static_cast<int>(sets.size()))
                return;
            const RKString name = sets[static_cast<size_t>(index)].displayName();
            ForEachAs<AtomStructureEditor>(m_controller, [&name, &forceFieldSets](auto editor)
            {
                editor->setAtomForceFieldIdentifier(name, forceFieldSets);
            });
        });
        BindCombo(AtomFFOrder(), [this](int index, hstring const&)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setForceFieldSchemeOrder(static_cast<ForceFieldSet::ForceFieldSchemeOrder>(index));
            });
        });
        // The recheck that follows every binding in this group is what moves the style row between
        // Fancy and QuteMol, the two differing in nothing else.
        BindCombo(AtomEdgeCueing(), [this](int index, hstring const&)
        {
            if (index < 0 || index > static_cast<int>(RKEdgeCueing::contoursAndHalos))
                return;
            const auto cueing = static_cast<RKEdgeCueing>(index);
            ForEachAs<AtomStructureEditor>(m_controller, [cueing](auto editor)
            {
                editor->setAtomEdgeCueing(cueing);
            });
        });

        BindCombo(AtomSelStyle(), [this](int index, hstring const&)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setAtomSelectionStyle(static_cast<RKSelectionStyle>(index));
            });
        });
        BindNumber(AtomSelFreq(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionFrequency(value);
            });
        });
        BindNumber(AtomSelDensity(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionDensity(value);
            });
        });
        BindSlider(AtomSelIntensitySlider(), AtomSelIntensityBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setSelectionIntensity(value);
            });
        });
        BindSlider(AtomSelScalingSlider(), AtomSelScalingBox(), 1.0, 2.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionScaling(value);
            });
        });

        BindCheck(AtomHDR(), [this](bool on)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [on](auto editor) { editor->setAtomHDR(on); });
        });
        BindSlider(AtomHDRExpSlider(), AtomHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor) { editor->setAtomHDRExposure(value); });
        });
        BindSlider(AtomHueSlider(), AtomHueBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor) { editor->setAtomHue(value); });
        });
        BindSlider(AtomSatSlider(), AtomSatBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor) { editor->setAtomSaturation(value); });
        });
        BindSlider(AtomValSlider(), AtomValBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor) { editor->setAtomValue(value); });
        });

        BindCheck(AtomAO(), [this](bool on)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [on](auto editor)
            {
                editor->setAtomAmbientOcclusion(on);
            });
        });
        BindSlider(AtomAmbSlider(), AtomAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomAmbientIntensity(value);
            });
        });
        BindWell(AtomAmbWell(), AtomAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setAtomAmbientColor(color);
            });
        });
        BindSlider(AtomDiffSlider(), AtomDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomDiffuseIntensity(value);
            });
        });
        BindWell(AtomDiffWell(), AtomDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setAtomDiffuseColor(color);
            });
        });
        BindSlider(AtomSpecSlider(), AtomSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomSpecularIntensity(value);
            });
        });
        BindWell(AtomSpecWell(), AtomSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setAtomSpecularColor(color);
            });
        });
        BindSlider(AtomShininessSlider(), AtomShininessBox(), 0.1, 128.0, 1.0, [this](double value)
        {
            ForEachAs<AtomStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setAtomShininess(value);
            });
        });

        m_recheckStyle = false;
    }

    void AppearanceDetailView::WireBonds()
    {
        // The bond settings are part of the representation style as well.
        m_recheckStyle = true;

        BindCheck(BondDraw(), [this](bool on)
        {
            ForEachAs<BondStructureEditor>(m_controller, [on](auto editor) { editor->setDrawBonds(on); });
        });
        BindSlider(BondScaleSlider(), BondScaleBox(), 0.1, 1.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondScaleFactor(value);
            });
        });
        BindCombo(BondColorMode(), [this](int index, hstring const&)
        {
            ForEachAs<BondStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setBondColorMode(static_cast<RKBondColorMode>(index));
            });
        });

        BindCombo(BondSelStyle(), [this](int index, hstring const&)
        {
            ForEachAs<BondStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setBondSelectionStyle(static_cast<RKSelectionStyle>(index));
            });
        });
        BindNumber(BondSelFreq(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondSelectionFrequency(value);
            });
        });
        BindNumber(BondSelDensity(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondSelectionDensity(value);
            });
        });
        BindSlider(BondSelIntensitySlider(), BondSelIntensityBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondSelectionIntensity(value);
            });
        });
        BindSlider(BondSelScalingSlider(), BondSelScalingBox(), 1.0, 2.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondSelectionScaling(value);
            });
        });

        BindCheck(BondHDR(), [this](bool on)
        {
            ForEachAs<BondStructureEditor>(m_controller, [on](auto editor) { editor->setBondHDR(on); });
        });
        BindSlider(BondHDRExpSlider(), BondHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor) { editor->setBondHDRExposure(value); });
        });
        BindSlider(BondHueSlider(), BondHueBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor) { editor->setBondHue(value); });
        });
        BindSlider(BondSatSlider(), BondSatBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor) { editor->setBondSaturation(value); });
        });
        BindSlider(BondValSlider(), BondValBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor) { editor->setBondValue(value); });
        });

        BindCheck(BondAO(), [this](bool on)
        {
            ForEachAs<BondStructureEditor>(m_controller, [on](auto editor)
            {
                editor->setBondAmbientOcclusion(on);
            });
        });
        BindSlider(BondAmbSlider(), BondAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondAmbientIntensity(value);
            });
        });
        BindWell(BondAmbWell(), BondAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<BondStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setBondAmbientColor(color);
            });
        });
        BindSlider(BondDiffSlider(), BondDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondDiffuseIntensity(value);
            });
        });
        BindWell(BondDiffWell(), BondDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<BondStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setBondDiffuseColor(color);
            });
        });
        BindSlider(BondSpecSlider(), BondSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondSpecularIntensity(value);
            });
        });
        BindWell(BondSpecWell(), BondSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<BondStructureEditor>(m_controller, [color](auto editor)
            {
                editor->setBondSpecularColor(color);
            });
        });
        BindSlider(BondShininessSlider(), BondShininessBox(), 0.1, 128.0, 1.0, [this](double value)
        {
            ForEachAs<BondStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setBondShininess(value);
            });
        });

        m_recheckStyle = false;
    }

    void AppearanceDetailView::WireRibbon()
    {
        // Anything that changes the shape of the ribbon rather than its shading has to rebuild the
        // mesh, because the geometry is generated on the processor and only uploaded afterwards.
        auto rebuildMesh = [this]
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [](auto editor)
            {
                editor->rebuildRibbonMesh();
            });
        };

        // Everything that only changes shading still reclassifies the style, so a hand-edited
        // ribbon reads as "Custom" the way an atom or bond edit does.
        auto shade = [this](auto const& write)
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, write);
            RecheckRibbonStyle();
        };

        BindCheck(RibbonDraw(), [this](bool on)
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [on](auto editor)
            {
                editor->setDrawRibbon(on);
                // A ribbon switched off has no mesh, so switching it back on has to build one.
                if (on)
                    editor->rebuildBackbone();
            });
        });
        BindSlider(RibbonScaleSlider(), RibbonScaleBox(), 0.1, 3.0, 0.05,
                   [this, rebuildMesh](double value)
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [value](auto editor)
            {
                editor->setRibbonScaleFactor(value);
            });
            rebuildMesh();
        });
        BindCombo(RibbonSecondary(), [this](int index, hstring const&)
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setRibbonSecondaryStructureMethod(
                    static_cast<ProteinRibbonSecondaryStructureMethod>(index));
                // A different assigner means a different secondary structure, so the backbone is
                // reassigned and not just re-swept.
                editor->rebuildBackbone();
            });
        });
        BindCombo(RibbonSpline(), [this, rebuildMesh](int index, hstring const&)
        {
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [index](auto editor)
            {
                editor->setRibbonSplineType(static_cast<ProteinRibbonSplineType>(index));
            });
            rebuildMesh();
        });
        BindCombo(RibbonStyle(), [this](int index, hstring const&)
        {
            const auto style = static_cast<ProteinRibbonRepresentationStyle>(index);
            // "Custom" is what a hand-edited ribbon reports, not something to apply.
            if (style == ProteinRibbonRepresentationStyle::custom)
                return;
            ForEachAs<ProteinRibbonStructureEditor>(m_controller, [style](auto editor)
            {
                applyRibbonRepresentationStyle(*editor, style);
            });
            // A style writes many settings at once, so the whole group is read back. Reading it
            // back refills this very popup, which would raise a selection change and re-enter
            // here, so it happens through Reload, which suppresses the fields while it fills them.
            DispatcherQueue().TryEnqueue([this]() { Reload(); });
        });
        BindCombo(RibbonColorSet(), [shade](int index, hstring const&)
        {
            shade([index](auto editor)
            {
                editor->setRibbonColorSet(static_cast<ProteinRibbonColorSet>(index));
            });
        });
        // Cues on the Fancy material are the Illustrative style, so this row moves the style row as
        // the atom one does, through the recheck that shading a ribbon already performs.
        BindCombo(RibbonEdgeCueing(), [shade](int index, hstring const&)
        {
            if (index < 0 || index > static_cast<int>(RKEdgeCueing::contoursAndHalos))
                return;
            shade([index](auto editor)
            {
                editor->setRibbonEdgeCueing(static_cast<RKEdgeCueing>(index));
            });
        });

        // The overlay shaders read the atom selection settings, so these rows write those, and the
        // ribbon keeps whatever representation style it had.
        BindCombo(RibbonSelStyle(), [this](int index, hstring const&)
        {
            ForEachRibbonSelection(m_controller, [index](auto editor)
            {
                editor->setAtomSelectionStyle(static_cast<RKSelectionStyle>(index));
            });
        });
        BindNumber(RibbonSelFreq(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachRibbonSelection(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionFrequency(value);
            });
        });
        BindNumber(RibbonSelDensity(), 0.0, 50.0, 0.5, [this](double value)
        {
            ForEachRibbonSelection(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionDensity(value);
            });
        });
        BindSlider(RibbonSelIntensitySlider(), RibbonSelIntensityBox(), 0.0, 1.0, 0.01,
                   [this](double value)
        {
            ForEachRibbonSelection(m_controller, [value](auto editor)
            {
                editor->setSelectionIntensity(value);
            });
        });
        BindSlider(RibbonSelScalingSlider(), RibbonSelScalingBox(), 1.0, 2.0, 0.01,
                   [this](double value)
        {
            ForEachRibbonSelection(m_controller, [value](auto editor)
            {
                editor->setAtomSelectionScaling(value);
            });
        });

        BindCheck(RibbonHDR(), [shade](bool on)
        {
            shade([on](auto editor) { editor->setRibbonHDR(on); });
        });
        BindSlider(RibbonHDRExpSlider(), RibbonHDRExpBox(), 0.0, 3.0, 0.05, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonHDRExposure(value); });
        });
        BindSlider(RibbonHueSlider(), RibbonHueBox(), 0.0, 1.5, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonHue(value); });
        });
        BindSlider(RibbonSatSlider(), RibbonSatBox(), 0.0, 1.5, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonSaturation(value); });
        });
        BindSlider(RibbonValSlider(), RibbonValBox(), 0.0, 1.5, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonValue(value); });
        });

        // Switching this on bakes the occlusion atlas, which happens in the reload the shared
        // check handler already asks for.
        BindCheck(RibbonAO(), [shade](bool on)
        {
            shade([on](auto editor) { editor->setRibbonAmbientOcclusion(on); });
        });
        BindSlider(RibbonAmbSlider(), RibbonAmbBox(), 0.0, 1.0, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonAmbientIntensity(value); });
        });
        BindWell(RibbonAmbWell(), RibbonAmbSwatch(), [shade](RKColor color)
        {
            shade([color](auto editor) { editor->setRibbonAmbientColor(color); });
        });
        BindSlider(RibbonDiffSlider(), RibbonDiffBox(), 0.0, 1.0, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonDiffuseIntensity(value); });
        });
        BindWell(RibbonDiffWell(), RibbonDiffSwatch(), [shade](RKColor color)
        {
            shade([color](auto editor) { editor->setRibbonDiffuseColor(color); });
        });
        BindSlider(RibbonSpecSlider(), RibbonSpecBox(), 0.0, 1.0, 0.01, [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonSpecularIntensity(value); });
        });
        BindWell(RibbonSpecWell(), RibbonSpecSwatch(), [shade](RKColor color)
        {
            shade([color](auto editor) { editor->setRibbonSpecularColor(color); });
        });
        BindSlider(RibbonShininessSlider(), RibbonShininessBox(), 0.1, 128.0, 1.0,
                   [shade](double value)
        {
            shade([value](auto editor) { editor->setRibbonShininess(value); });
        });
    }

    void AppearanceDetailView::WireUnitCell()
    {
        BindCheck(UnitCellDraw(), [this](bool on)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([on](Object& object) { object.setDrawUnitCell(on); });
        });
        BindSlider(UnitCellScaleSlider(), UnitCellScaleBox(), 0.0, 2.0, 0.01, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.setUnitCellScaleFactor(value);
                });
        });
        BindSlider(UnitCellDiffSlider(), UnitCellDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.setUnitCellDiffuseIntensity(value);
                });
        });
        BindWell(UnitCellWell(), UnitCellSwatch(), [this](RKColor color)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([color](Object& object)
                {
                    object.setUnitCellDiffuseColor(color);
                });
        });
    }

    void AppearanceDetailView::WireLocalAxes()
    {
        BindCombo(LocalPos(), [this](int index, hstring const&)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([index](Object& object)
                {
                    object.renderLocalAxes().setPosition(static_cast<RKLocalAxes::Position>(index));
                });
        });
        BindCombo(LocalStyle(), [this](int index, hstring const&)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([index](Object& object)
                {
                    object.renderLocalAxes().setStyle(static_cast<RKLocalAxes::Style>(index));
                });
        });
        BindCombo(LocalScale(), [this](int index, hstring const&)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([index](Object& object)
                {
                    object.renderLocalAxes().setScalingType(static_cast<RKLocalAxes::ScalingType>(index));
                });
        });
        BindSlider(LocalLengthSlider(), LocalLengthBox(), 0.0, 10.0, 0.05, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.renderLocalAxes().setLength(value);
                });
        });
        BindSlider(LocalWidthSlider(), LocalWidthBox(), 0.0, 2.0, 0.01, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.renderLocalAxes().setWidth(value);
                });
        });
        BindNumber(LocalOffsetX(), -50.0, 50.0, 0.1, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.renderLocalAxes().setOffsetX(value);
                });
        });
        BindNumber(LocalOffsetY(), -50.0, 50.0, 0.1, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.renderLocalAxes().setOffsetY(value);
                });
        });
        BindNumber(LocalOffsetZ(), -50.0, 50.0, 0.1, [this](double value)
        {
            if (m_controller)
                m_controller->ForEachSelectedObject([value](Object& object)
                {
                    object.renderLocalAxes().setOffsetZ(value);
                });
        });
    }

    void AppearanceDetailView::WireVolumetric()
    {
        BindCheck(VolDraw(), [this](bool on)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [on](auto viewer)
            {
                viewer->setDrawAdsorptionSurface(on);
            });
            if (m_controller)
                m_controller->Log(on ? L"Draw adsorption surface: ON" : L"Draw adsorption surface: OFF");
        });
        BindCombo(VolMethod(), [this](int index, hstring const&)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [index](auto viewer)
            {
                viewer->setAdsorptionSurfaceRenderingMethod(static_cast<RKEnergySurfaceType>(index));
            });
            if (m_controller)
                m_controller->Log(index == 1 ? L"Volume rendering enabled (reloading grid…)"
                                             : L"Isosurface rendering selected");
        });
        BindCombo(VolProbe(), [this](int index, hstring const&)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [index](auto viewer)
            {
                viewer->setAdsorptionSurfaceProbeMolecule(static_cast<ProbeMolecule>(index));
            });
        });
        BindCombo(VolTF(), [this](int index, hstring const&)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [index](auto viewer)
            {
                viewer->setAdsorptionVolumeTransferFunction(
                    static_cast<RKPredefinedVolumeRenderingTransferFunction>(index));
            });
        });
        // Item order mirrors Cocoa: "Custom", then 2x2x2 .. 512x512x512, whose
        // index is the power of two. "Custom" is display-only.
        BindCombo(VolQuality(), [this](int index, hstring const&)
        {
            if (index < 1)
                return;
            ForEachAs<VolumetricDataEditor>(m_controller, [index](auto editor)
            {
                editor->setEncompassingPowerOfTwoCubicGridSize(index);
            });
        });
        BindNumber(VolStep(), 0.001, 1.0, 0.001, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionVolumeStepLength(value);
            });
        });
        // The isocontour range comes from the grid, so it is set in Reload.
        BindSlider(VolIsoSlider(), VolIsoBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceIsoValue(value);
            });
        });
        BindSlider(VolOpacitySlider(), VolOpacityBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceOpacity(value);
            });
        });
        BindSlider(VolThresholdSlider(), VolThresholdBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionTransparencyThreshold(value);
            });
        });

        BindSlider(VolHueSlider(), VolHueBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceHue(value);
            });
        });
        BindSlider(VolSatSlider(), VolSatBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceSaturation(value);
            });
        });
        BindSlider(VolValSlider(), VolValBox(), 0.0, 1.5, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceValue(value);
            });
        });

        BindCheck(VolFrontHDR(), [this](bool on)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [on](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideHDR(on);
            });
        });
        BindSlider(VolFrontHDRExpSlider(), VolFrontHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideHDRExposure(value);
            });
        });
        BindSlider(VolFrontAmbSlider(), VolFrontAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideAmbientIntensity(value);
            });
        });
        BindWell(VolFrontAmbWell(), VolFrontAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideAmbientColor(color);
            });
        });
        BindSlider(VolFrontDiffSlider(), VolFrontDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideDiffuseIntensity(value);
            });
        });
        BindWell(VolFrontDiffWell(), VolFrontDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideDiffuseColor(color);
            });
        });
        BindSlider(VolFrontSpecSlider(), VolFrontSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideSpecularIntensity(value);
            });
        });
        BindWell(VolFrontSpecWell(), VolFrontSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideSpecularColor(color);
            });
        });
        BindSlider(VolFrontShininessSlider(), VolFrontShininessBox(), 0.0, 256.0, 1.0, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceFrontSideShininess(value);
            });
        });

        BindCheck(VolBackHDR(), [this](bool on)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [on](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideHDR(on);
            });
        });
        BindSlider(VolBackHDRExpSlider(), VolBackHDRExpBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideHDRExposure(value);
            });
        });
        BindSlider(VolBackAmbSlider(), VolBackAmbBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideAmbientIntensity(value);
            });
        });
        BindWell(VolBackAmbWell(), VolBackAmbSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideAmbientColor(color);
            });
        });
        BindSlider(VolBackDiffSlider(), VolBackDiffBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideDiffuseIntensity(value);
            });
        });
        BindWell(VolBackDiffWell(), VolBackDiffSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideDiffuseColor(color);
            });
        });
        BindSlider(VolBackSpecSlider(), VolBackSpecBox(), 0.0, 1.0, 0.01, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideSpecularIntensity(value);
            });
        });
        BindWell(VolBackSpecWell(), VolBackSpecSwatch(), [this](RKColor color)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [color](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideSpecularColor(color);
            });
        });
        BindSlider(VolBackShininessSlider(), VolBackShininessBox(), 0.0, 256.0, 1.0, [this](double value)
        {
            ForEachAs<VolumetricDataViewer>(m_controller, [value](auto viewer)
            {
                viewer->setAdsorptionSurfaceBackSideShininess(value);
            });
        });
    }

    void AppearanceDetailView::WireAnnotation()
    {
        BindCombo(AnnType(), [this](int index, hstring const&)
        {
            ForEachAs<AnnotationEditor>(m_controller, [index](auto editor)
            {
                editor->setRenderTextType(static_cast<RKTextType>(index));
            });
        });
        // The font is stored by name, so the shown text is what is applied.
        BindCombo(AnnFont(), [this](int, hstring const& text)
        {
            if (text.empty())
                return;
            const RKString font = RKString::fromStdWString(std::wstring(text));
            ForEachAs<AnnotationEditor>(m_controller, [&font](auto editor)
            {
                editor->setRenderTextFont(font);
            });
        });
        BindCombo(AnnAlign(), [this](int index, hstring const&)
        {
            ForEachAs<AnnotationEditor>(m_controller, [index](auto editor)
            {
                editor->setRenderTextAlignment(static_cast<RKTextAlignment>(index));
            });
        });
        BindCombo(AnnStyle(), [this](int index, hstring const&)
        {
            ForEachAs<AnnotationEditor>(m_controller, [index](auto editor)
            {
                editor->setRenderTextStyle(static_cast<RKTextStyle>(index));
            });
        });
        BindWell(AnnColorWell(), AnnColorSwatch(), [this](RKColor color)
        {
            ForEachAs<AnnotationEditor>(m_controller, [color](auto editor)
            {
                editor->setRenderTextColor(color);
            });
        });
        BindSlider(AnnScalingSlider(), AnnScalingBox(), 0.0, 3.0, 0.01, [this](double value)
        {
            ForEachAs<AnnotationEditor>(m_controller, [value](auto editor)
            {
                editor->setRenderTextScaling(value);
            });
        });
        BindNumber(AnnOffsetX(), -10.0, 10.0, 0.1, [this](double value)
        {
            ForEachAs<AnnotationEditor>(m_controller, [value](auto editor)
            {
                editor->setRenderTextOffsetX(value);
            });
        });
        BindNumber(AnnOffsetY(), -10.0, 10.0, 0.1, [this](double value)
        {
            ForEachAs<AnnotationEditor>(m_controller, [value](auto editor)
            {
                editor->setRenderTextOffsetY(value);
            });
        });
        BindNumber(AnnOffsetZ(), -10.0, 10.0, 0.1, [this](double value)
        {
            ForEachAs<AnnotationEditor>(m_controller, [value](auto editor)
            {
                editor->setRenderTextOffsetZ(value);
            });
        });
    }

    // ---- filling in ------------------------------------------------------

    void AppearanceDetailView::Reload()
    {
        m_suppress = true;
        try
        {
            ReloadPrimitive();
            ReloadRibbon();
            ReloadAtoms();
            ReloadBonds();
            ReloadUnitCell();
            ReloadLocalAxes();
            ReloadVolumetric();
            ReloadAnnotation();
        }
        catch (...)
        {
            if (m_controller)
                m_controller->Log(L"Appearance inspector error");
        }
        m_suppress = false;
        // Expander content often applies IsChecked before its template is live;
        // one deferred pass clears stuck Indeterminate glyphs without a hover.
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]()
        {
            if (auto self = weak.get())
                self->RefreshAllCheckVisuals();
        });
    }

    void AppearanceDetailView::ReloadPrimitive()
    {
        // Cocoa omits the Primitive outline group unless the selection has one.
        const bool available = HasPrimitiveStructure(m_controller);
        ShowSection(PrimitiveSection(), available);
        auto primitive = FirstAs<PrimitiveViewer>(m_controller);
        ShowBody(PrimitiveHint(), PrimitiveBody(), available && primitive != nullptr);
        if (!available || !primitive)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<PrimitiveViewer>(m_controller, read);
        };

        const auto delta = agreed([](auto const& p) { return p->primitiveRotationDelta(); });
        SetNumber(PrimRotDelta(), delta);
        RetitleRotateButtons(delta ? *delta : primitive->primitiveRotationDelta());
        ReloadPrimitiveOrientation();

        // Each cell of the matrix stands on its own, so one of them can be mixed
        // while the others agree.
        auto cell = [&agreed](auto const& component)
        {
            return agreed([&component](auto const& p)
                          { return component(p->primitiveTransformationMatrix()); });
        };
        SetNumber(PrimTransAX(), cell([](double3x3 const& m) { return m.ax; }));
        SetNumber(PrimTransBX(), cell([](double3x3 const& m) { return m.bx; }));
        SetNumber(PrimTransCX(), cell([](double3x3 const& m) { return m.cx; }));
        SetNumber(PrimTransAY(), cell([](double3x3 const& m) { return m.ay; }));
        SetNumber(PrimTransBY(), cell([](double3x3 const& m) { return m.by; }));
        SetNumber(PrimTransCY(), cell([](double3x3 const& m) { return m.cy; }));
        SetNumber(PrimTransAZ(), cell([](double3x3 const& m) { return m.az; }));
        SetNumber(PrimTransBZ(), cell([](double3x3 const& m) { return m.bz; }));
        SetNumber(PrimTransCZ(), cell([](double3x3 const& m) { return m.cz; }));

        SetCheck(PrimCapped(), agreed([](auto const& p) { return p->primitiveIsCapped(); }));
        SetSlider(PrimOpacitySlider(), PrimOpacityBox(),
                  agreed([](auto const& p) { return p->primitiveOpacity(); }));
        SetSlider(PrimSidesSlider(), PrimSidesBox(),
                  agreed([](auto const& p)
                         { return static_cast<double>(p->primitiveNumberOfSides()); }));

        FillCombo(PrimSelStyle(), SelectionStyles(),
                  ItemOf(agreed([](auto const& p) { return p->primitiveSelectionStyle(); })));
        SetNumber(PrimSelFreq(), agreed([](auto const& p)
                                        { return p->primitiveSelectionFrequency(); }));
        SetNumber(PrimSelDensity(), agreed([](auto const& p)
                                           { return p->primitiveSelectionDensity(); }));
        SetSlider(PrimSelIntensitySlider(), PrimSelIntensityBox(),
                  agreed([](auto const& p) { return p->primitiveSelectionIntensity(); }));
        SetSlider(PrimSelScalingSlider(), PrimSelScalingBox(),
                  agreed([](auto const& p) { return p->primitiveSelectionScaling(); }));

        SetSlider(PrimHueSlider(), PrimHueBox(),
                  agreed([](auto const& p) { return p->primitiveHue(); }));
        SetSlider(PrimSatSlider(), PrimSatBox(),
                  agreed([](auto const& p) { return p->primitiveSaturation(); }));
        SetSlider(PrimValSlider(), PrimValBox(),
                  agreed([](auto const& p) { return p->primitiveValue(); }));

        SetCheck(PrimFrontHDR(), agreed([](auto const& p) { return p->primitiveFrontSideHDR(); }));
        SetSlider(PrimFrontHDRExpSlider(), PrimFrontHDRExpBox(),
                  agreed([](auto const& p) { return p->primitiveFrontSideHDRExposure(); }));
        SetSlider(PrimFrontAmbSlider(), PrimFrontAmbBox(),
                  agreed([](auto const& p) { return p->primitiveFrontSideAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimFrontAmbSwatch(),
            agreed([](auto const& p) { return p->primitiveFrontSideAmbientColor(); }));
        SetSlider(PrimFrontDiffSlider(), PrimFrontDiffBox(),
                  agreed([](auto const& p) { return p->primitiveFrontSideDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimFrontDiffSwatch(),
            agreed([](auto const& p) { return p->primitiveFrontSideDiffuseColor(); }));
        SetSlider(PrimFrontSpecSlider(), PrimFrontSpecBox(),
                  agreed([](auto const& p) { return p->primitiveFrontSideSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimFrontSpecSwatch(),
            agreed([](auto const& p) { return p->primitiveFrontSideSpecularColor(); }));
        SetSlider(PrimFrontShininessSlider(), PrimFrontShininessBox(),
                  agreed([](auto const& p) { return p->primitiveFrontSideShininess(); }));

        SetCheck(PrimBackHDR(), agreed([](auto const& p) { return p->primitiveBackSideHDR(); }));
        SetSlider(PrimBackHDRExpSlider(), PrimBackHDRExpBox(),
                  agreed([](auto const& p) { return p->primitiveBackSideHDRExposure(); }));
        SetSlider(PrimBackAmbSlider(), PrimBackAmbBox(),
                  agreed([](auto const& p) { return p->primitiveBackSideAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimBackAmbSwatch(),
            agreed([](auto const& p) { return p->primitiveBackSideAmbientColor(); }));
        SetSlider(PrimBackDiffSlider(), PrimBackDiffBox(),
                  agreed([](auto const& p) { return p->primitiveBackSideDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimBackDiffSwatch(),
            agreed([](auto const& p) { return p->primitiveBackSideDiffuseColor(); }));
        SetSlider(PrimBackSpecSlider(), PrimBackSpecBox(),
                  agreed([](auto const& p) { return p->primitiveBackSideSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            PrimBackSpecSwatch(),
            agreed([](auto const& p) { return p->primitiveBackSideSpecularColor(); }));
        SetSlider(PrimBackShininessSlider(), PrimBackShininessBox(),
                  agreed([](auto const& p) { return p->primitiveBackSideShininess(); }));
    }

    void AppearanceDetailView::ReloadPrimitiveOrientation()
    {
        auto primitive = FirstAs<PrimitiveViewer>(m_controller);
        if (!primitive)
            return;
        const bool wasSuppressed = m_suppress;
        m_suppress = true;
        auto angle = [this](auto const& component)
        {
            return AgreedAs<PrimitiveViewer>(m_controller, [&component](auto const& p)
            {
                return component(p->primitiveOrientation().EulerAngles()) * kDegreesPerRadian;
            });
        };
        SetSlider(PrimEulerXSlider(), PrimEulerXBox(),
                  angle([](double3 const& e) { return e.x; }));
        SetSlider(PrimEulerZSlider(), PrimEulerZBox(),
                  angle([](double3 const& e) { return e.z; }));
        SetSlider(PrimEulerYSlider(), PrimEulerYBox(),
                  angle([](double3 const& e) { return e.y; }));
        m_suppress = wasSuppressed;
    }

    void AppearanceDetailView::RetitleRotateButtons(double delta)
    {
        if (!std::isfinite(delta))
            return;
        for (size_t i = 0; i < m_rotate.size(); ++i)
        {
            if (m_rotate[i])
                m_rotate[i].Content(box_value(RotateTitle((i % 2) == 0, delta)));
        }
    }

    void AppearanceDetailView::RotatePrimitive(int direction)
    {
        if (!m_controller)
            return;
        ForEachAs<PrimitiveEditor>(m_controller, [direction](auto editor)
        {
            const double delta = editor->primitiveRotationDelta();
            simd_quatd change = simd_quatd(1.0, double3(0.0, 0.0, 0.0));
            switch (direction)
            {
                case 0: change = simd_quatd::yaw(delta); break;
                case 1: change = simd_quatd::yaw(-delta); break;
                case 2: change = simd_quatd::pitch(delta); break;
                case 3: change = simd_quatd::pitch(-delta); break;
                case 4: change = simd_quatd::roll(delta); break;
                case 5: change = simd_quatd::roll(-delta); break;
                default: break;
            }
            editor->setPrimitiveOrientation(editor->primitiveOrientation() * change);
        });
        RecomputeBoundingBoxes(m_controller);
        m_controller->RefitCameraToBoundingBox();
        // Cocoa reloads the row, so the Euler readouts follow the rotation.
        ReloadPrimitiveOrientation();
    }

    void AppearanceDetailView::ReloadAtoms()
    {
        auto atom = FirstAs<AtomStructureViewer>(m_controller);
        ShowBody(AtomsHint(), AtomsBody(), atom != nullptr);
        if (!atom)
            return;

        // Every row asks the selection as a whole: a value when the structures
        // agree, nothing when they do not.
        auto agreed = [this](auto const& read)
        {
            return AgreedAs<AtomStructureViewer>(m_controller, read);
        };

        SetCheck(AtomDraw(), agreed([](auto const& a) { return a->drawAtoms(); }));
        SetSlider(AtomScaleSlider(), AtomScaleBox(),
                  agreed([](auto const& a) { return a->atomScaleFactor(); }));

        FillCombo(AtomType(), { L"Ball and Stick", L"Van der Waals", L"Unity" },
                  ItemOf(agreed([](auto const& a) { return a->atomRepresentationType(); })));
        FillCombo(AtomStyle(), RepresentationStyles(),
                  StyleItemOf(agreed([](auto const& a) { return a->atomRepresentationStyle(); })));

        // The color sets and force fields are the document's, so both popups are
        // filled from it and matched by name.
        std::vector<hstring> colorSets;
        std::optional<int> colorIndex;
        std::vector<hstring> forceFields;
        std::optional<int> forceFieldIndex;
        const auto scheme = agreed([](auto const& a) { return a->atomColorSchemeIdentifier(); });
        const auto field = agreed([](auto const& a) { return a->atomForceFieldIdentifier(); });
        if (m_controller && m_controller->Document())
        {
            auto const& sets = m_controller->Document()->colorSets().colorSets();
            for (size_t i = 0; i < sets.size(); ++i)
            {
                const RKString name = sets[i].displayName();
                colorSets.push_back(hstring(name.toStdWString()));
                if (scheme && name.toLower() == scheme->toLower())
                    colorIndex = static_cast<int>(i);
            }

            auto const& fields = m_controller->Document()->forceFieldSets().forceFieldSets();
            for (size_t i = 0; i < fields.size(); ++i)
            {
                const RKString name = fields[i].displayName();
                forceFields.push_back(hstring(name.toStdWString()));
                if (field && name.toLower() == field->toLower())
                    forceFieldIndex = static_cast<int>(i);
            }
        }
        if (colorSets.empty())
            colorSets.push_back(L"Jmol");
        if (forceFields.empty())
            forceFields.push_back(L"Default");
        // A name the selection agrees on that is not one of the sets still lands
        // on the first item, as it did before.
        FillCombo(AtomColorScheme(), colorSets, scheme ? colorIndex.value_or(0) : colorIndex);
        FillCombo(AtomFF(), forceFields, field ? forceFieldIndex.value_or(0) : forceFieldIndex);
        FillCombo(AtomColorOrder(), SchemeOrders(),
                  ItemOf(agreed([](auto const& a) { return a->colorSchemeOrder(); })));
        FillCombo(AtomFFOrder(), SchemeOrders(),
                  ItemOf(agreed([](auto const& a) { return a->forceFieldSchemeOrder(); })));
        FillCombo(AtomEdgeCueing(), EdgeCueings(),
                  ItemOf(agreed([](auto const& a) { return a->atomEdgeCueing(); })));

        FillCombo(AtomSelStyle(), SelectionStyles(),
                  ItemOf(agreed([](auto const& a) { return a->atomSelectionStyle(); })));
        SetNumber(AtomSelFreq(), agreed([](auto const& a)
                                        { return a->atomSelectionFrequency(); }));
        SetNumber(AtomSelDensity(), agreed([](auto const& a)
                                           { return a->atomSelectionDensity(); }));
        SetSlider(AtomSelIntensitySlider(), AtomSelIntensityBox(),
                  agreed([](auto const& a) { return a->atomSelectionIntensity(); }));
        SetSlider(AtomSelScalingSlider(), AtomSelScalingBox(),
                  agreed([](auto const& a) { return a->atomSelectionScaling(); }));

        SetCheck(AtomHDR(), agreed([](auto const& a) { return a->atomHDR(); }));
        SetSlider(AtomHDRExpSlider(), AtomHDRExpBox(),
                  agreed([](auto const& a) { return a->atomHDRExposure(); }));
        SetSlider(AtomHueSlider(), AtomHueBox(),
                  agreed([](auto const& a) { return a->atomHue(); }));
        SetSlider(AtomSatSlider(), AtomSatBox(),
                  agreed([](auto const& a) { return a->atomSaturation(); }));
        SetSlider(AtomValSlider(), AtomValBox(),
                  agreed([](auto const& a) { return a->atomValue(); }));

        SetCheck(AtomAO(), agreed([](auto const& a) { return a->atomAmbientOcclusion(); }));
        SetSlider(AtomAmbSlider(), AtomAmbBox(),
                  agreed([](auto const& a) { return a->atomAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            AtomAmbSwatch(), agreed([](auto const& a) { return a->atomAmbientColor(); }));
        SetSlider(AtomDiffSlider(), AtomDiffBox(),
                  agreed([](auto const& a) { return a->atomDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            AtomDiffSwatch(), agreed([](auto const& a) { return a->atomDiffuseColor(); }));
        SetSlider(AtomSpecSlider(), AtomSpecBox(),
                  agreed([](auto const& a) { return a->atomSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            AtomSpecSwatch(), agreed([](auto const& a) { return a->atomSpecularColor(); }));
        SetSlider(AtomShininessSlider(), AtomShininessBox(),
                  agreed([](auto const& a) { return a->atomShininess(); }));
    }

    void AppearanceDetailView::ReloadBonds()
    {
        auto bond = FirstAs<BondStructureViewer>(m_controller);
        ShowBody(BondsHint(), BondsBody(), bond != nullptr);
        if (!bond)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<BondStructureViewer>(m_controller, read);
        };

        SetCheck(BondDraw(), agreed([](auto const& b) { return b->drawBonds(); }));
        SetSlider(BondScaleSlider(), BondScaleBox(),
                  agreed([](auto const& b) { return b->bondScaleFactor(); }));
        FillCombo(BondColorMode(), { L"Uniform", L"Split", L"Gradient" },
                  ItemOf(agreed([](auto const& b) { return b->bondColorMode(); })));

        FillCombo(BondSelStyle(), SelectionStyles(),
                  ItemOf(agreed([](auto const& b) { return b->bondSelectionStyle(); })));
        SetNumber(BondSelFreq(), agreed([](auto const& b)
                                        { return b->bondSelectionFrequency(); }));
        SetNumber(BondSelDensity(), agreed([](auto const& b)
                                           { return b->bondSelectionDensity(); }));
        SetSlider(BondSelIntensitySlider(), BondSelIntensityBox(),
                  agreed([](auto const& b) { return b->bondSelectionIntensity(); }));
        SetSlider(BondSelScalingSlider(), BondSelScalingBox(),
                  agreed([](auto const& b) { return b->bondSelectionScaling(); }));

        SetCheck(BondHDR(), agreed([](auto const& b) { return b->bondHDR(); }));
        SetSlider(BondHDRExpSlider(), BondHDRExpBox(),
                  agreed([](auto const& b) { return b->bondHDRExposure(); }));
        SetSlider(BondHueSlider(), BondHueBox(),
                  agreed([](auto const& b) { return b->bondHue(); }));
        SetSlider(BondSatSlider(), BondSatBox(),
                  agreed([](auto const& b) { return b->bondSaturation(); }));
        SetSlider(BondValSlider(), BondValBox(),
                  agreed([](auto const& b) { return b->bondValue(); }));

        SetCheck(BondAO(), agreed([](auto const& b) { return b->bondAmbientOcclusion(); }));
        SetSlider(BondAmbSlider(), BondAmbBox(),
                  agreed([](auto const& b) { return b->bondAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            BondAmbSwatch(), agreed([](auto const& b) { return b->bondAmbientColor(); }));
        SetSlider(BondDiffSlider(), BondDiffBox(),
                  agreed([](auto const& b) { return b->bondDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            BondDiffSwatch(), agreed([](auto const& b) { return b->bondDiffuseColor(); }));
        SetSlider(BondSpecSlider(), BondSpecBox(),
                  agreed([](auto const& b) { return b->bondSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            BondSpecSwatch(), agreed([](auto const& b) { return b->bondSpecularColor(); }));
        SetSlider(BondShininessSlider(), BondShininessBox(),
                  agreed([](auto const& b) { return b->bondShininess(); }));
    }

    void AppearanceDetailView::ReloadRibbon()
    {
        // Cocoa omits the Protein Ribbons outline group unless the selection has one.
        const bool available = HasProteinRibbonStructure(m_controller);
        ShowSection(RibbonSection(), available);
        auto ribbon = FirstAs<ProteinRibbonStructureEditor>(m_controller);
        ShowBody(RibbonHint(), RibbonBody(), available && ribbon != nullptr);
        if (!available || !ribbon)
            return;

        // Classify from lighting before filling the popup, so a stored "Default" label cannot
        // disagree with Fancy lighting the way a save/reload used to flip the popup.
        ForEachAs<ProteinRibbonStructureEditor>(m_controller, [](auto editor)
        {
            editor->recheckRibbonRepresentationStyle();
        });

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<ProteinRibbonStructureEditor>(m_controller, read);
        };

        SetCheck(RibbonDraw(), agreed([](auto const& r) { return r->drawRibbon(); }));
        SetSlider(RibbonScaleSlider(), RibbonScaleBox(),
                  agreed([](auto const& r) { return r->ribbonScaleFactor(); }));

        FillCombo(RibbonSecondary(),
                  { L"STRIDE", L"DSS", L"DSSP", L"P-SEA", L"Sequoia", L"SEGNO" },
                  ItemOf(agreed([](auto const& r) { return r->ribbonSecondaryStructureMethod(); })));
        FillCombo(RibbonSpline(), { L"B-Spline", L"Catmull-Rom" },
                  ItemOf(agreed([](auto const& r) { return r->ribbonSplineType(); })));
        FillCombo(RibbonStyle(), { L"Default", L"Fancy", L"Illustrative", L"Custom" },
                  ItemOf(agreed([](auto const& r) { return r->ribbonRepresentationStyle(); })));
        FillCombo(RibbonColorSet(),
                  { L"Standard Academic", L"Modern UI", L"Biophysical Properties", L"Infographic" },
                  ItemOf(agreed([](auto const& r) { return r->ribbonColorSet(); })));
        FillCombo(RibbonEdgeCueing(), EdgeCueings(),
                  ItemOf(agreed([](auto const& r) { return r->ribbonEdgeCueing(); })));

        auto agreedSelection = [this](auto const& read)
        {
            return AgreedRibbonSelection(m_controller, read);
        };
        FillCombo(RibbonSelStyle(), SelectionStyles(),
                  ItemOf(agreedSelection([](auto const& a) { return a->atomSelectionStyle(); })));
        SetNumber(RibbonSelFreq(), agreedSelection([](auto const& a)
                                                   { return a->atomSelectionFrequency(); }));
        SetNumber(RibbonSelDensity(), agreedSelection([](auto const& a)
                                                      { return a->atomSelectionDensity(); }));
        SetSlider(RibbonSelIntensitySlider(), RibbonSelIntensityBox(),
                  agreedSelection([](auto const& a) { return a->atomSelectionIntensity(); }));
        SetSlider(RibbonSelScalingSlider(), RibbonSelScalingBox(),
                  agreedSelection([](auto const& a) { return a->atomSelectionScaling(); }));

        SetCheck(RibbonHDR(), agreed([](auto const& r) { return r->ribbonHDR(); }));
        SetSlider(RibbonHDRExpSlider(), RibbonHDRExpBox(),
                  agreed([](auto const& r) { return r->ribbonHDRExposure(); }));
        SetSlider(RibbonHueSlider(), RibbonHueBox(),
                  agreed([](auto const& r) { return r->ribbonHue(); }));
        SetSlider(RibbonSatSlider(), RibbonSatBox(),
                  agreed([](auto const& r) { return r->ribbonSaturation(); }));
        SetSlider(RibbonValSlider(), RibbonValBox(),
                  agreed([](auto const& r) { return r->ribbonValue(); }));

        SetCheck(RibbonAO(), agreed([](auto const& r) { return r->ribbonAmbientOcclusion(); }));
        SetSlider(RibbonAmbSlider(), RibbonAmbBox(),
                  agreed([](auto const& r) { return r->ribbonAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            RibbonAmbSwatch(), agreed([](auto const& r) { return r->ribbonAmbientColor(); }));
        SetSlider(RibbonDiffSlider(), RibbonDiffBox(),
                  agreed([](auto const& r) { return r->ribbonDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            RibbonDiffSwatch(), agreed([](auto const& r) { return r->ribbonDiffuseColor(); }));
        SetSlider(RibbonSpecSlider(), RibbonSpecBox(),
                  agreed([](auto const& r) { return r->ribbonSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            RibbonSpecSwatch(), agreed([](auto const& r) { return r->ribbonSpecularColor(); }));
        SetSlider(RibbonShininessSlider(), RibbonShininessBox(),
                  agreed([](auto const& r) { return r->ribbonShininess(); }));
    }

    void AppearanceDetailView::ReloadUnitCell()
    {
        auto object = m_controller ? m_controller->FirstSelectedObject() : nullptr;
        ShowBody(UnitCellHint(), UnitCellBody(), object != nullptr);
        if (!object)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<Object>(m_controller, read);
        };

        SetCheck(UnitCellDraw(), agreed([](auto const& o) { return o->drawUnitCell(); }));
        SetSlider(UnitCellScaleSlider(), UnitCellScaleBox(),
                  agreed([](auto const& o) { return o->unitCellScaleFactor(); }));
        SetSlider(UnitCellDiffSlider(), UnitCellDiffBox(),
                  agreed([](auto const& o) { return o->unitCellDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            UnitCellSwatch(), agreed([](auto const& o) { return o->unitCellDiffuseColor(); }));
    }

    void AppearanceDetailView::ReloadLocalAxes()
    {
        auto object = m_controller ? m_controller->FirstSelectedObject() : nullptr;
        ShowBody(LocalAxesHint(), LocalAxesBody(), object != nullptr);
        if (!object)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<Object>(m_controller, [&read](auto const& o)
                                    { return read(o->renderLocalAxes()); });
        };

        FillCombo(LocalPos(),
                  { L"None", L"Origin", L"Origin Bounding-Box", L"Center", L"Center Bounding-Box" },
                  ItemOf(agreed([](RKLocalAxes const& a) { return a.position(); })));
        FillCombo(LocalStyle(), { L"Default", L"Default RGB", L"Cylinder", L"Cylinder RGB" },
                  ItemOf(agreed([](RKLocalAxes const& a) { return a.style(); })));
        FillCombo(LocalScale(), { L"Absolute", L"Relative" },
                  ItemOf(agreed([](RKLocalAxes const& a) { return a.scalingType(); })));
        SetSlider(LocalLengthSlider(), LocalLengthBox(),
                  agreed([](RKLocalAxes const& a) { return a.length(); }));
        SetSlider(LocalWidthSlider(), LocalWidthBox(),
                  agreed([](RKLocalAxes const& a) { return a.width(); }));
        SetNumber(LocalOffsetX(), agreed([](RKLocalAxes const& a) { return a.offsetX(); }));
        SetNumber(LocalOffsetY(), agreed([](RKLocalAxes const& a) { return a.offsetY(); }));
        SetNumber(LocalOffsetZ(), agreed([](RKLocalAxes const& a) { return a.offsetZ(); }));
    }

    void AppearanceDetailView::ReloadVolumetric()
    {
        auto volume = FirstAs<VolumetricDataViewer>(m_controller);
        ShowBody(VolumetricHint(), VolumetricBody(), volume != nullptr);
        if (!volume)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<VolumetricDataViewer>(m_controller, read);
        };

        SetCheck(VolDraw(), agreed([](auto const& v) { return v->drawAdsorptionSurface(); }));
        FillCombo(VolMethod(), { L"Isosurface", L"Volume Rendering" },
                  ItemOf(agreed([](auto const& v)
                                { return v->adsorptionSurfaceRenderingMethod(); })));
        FillCombo(VolProbe(),
                  { L"Helium", L"Methane", L"Nitrogen", L"Hydrogen", L"Water", L"CO\u2082",
                    L"Xenon", L"Krypton", L"Argon" },
                  ItemOf(agreed([](auto const& v)
                                { return v->adsorptionSurfaceProbeMolecule(); })));
        FillCombo(VolTF(),
                  { L"RASPA PES", L"Cool/Warm", L"XRay", L"Gray", L"Rainbow", L"Turbo",
                    L"Gnuplot2", L"Spectral", L"Cool", L"Viridis", L"Plasma", L"Inferno",
                    L"Magma", L"Cividis", L"Spring", L"Summer", L"Autumn", L"Winter",
                    L"Reds", L"Blues", L"Greens", L"Purples", L"Oranges" },
                  ItemOf(agreed([](auto const& v)
                                { return v->adsorptionVolumeTransferFunction(); })));
        SetNumber(VolStep(), agreed([](auto const& v)
                                    { return v->adsorptionVolumeStepLength(); }));

        // Cocoa derives the isocontour range from the grid data itself.
        const std::pair<double, double> range = volume->range();
        const double isoMin = (std::min)(range.first, range.second);
        const double isoMax = (std::max)(range.first, range.second);
        SetRange(VolIsoSlider(), VolIsoBox(), isoMin, isoMax,
                 (isoMax > isoMin) ? (isoMax - isoMin) / 1000.0 : 1.0);
        SetSlider(VolIsoSlider(), VolIsoBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceIsoValue(); }));

        SetSlider(VolOpacitySlider(), VolOpacityBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceOpacity(); }));
        SetSlider(VolThresholdSlider(), VolThresholdBox(),
                  agreed([](auto const& v) { return v->adsorptionTransparencyThreshold(); }));

        const auto power = agreed([](auto const& v)
                                  { return v->encompassingPowerOfTwoCubicGridSize(); });
        FillCombo(VolQuality(),
                  { L"Custom", L"2x2x2", L"4x4x4", L"8x8x8", L"16x16x16", L"32x32x32",
                    L"64x64x64", L"128x128x128", L"256x256x256", L"512x512x512" },
                  power ? std::optional<int>((*power >= 1 && *power <= 9) ? *power : 0)
                        : std::nullopt);

        SetSlider(VolHueSlider(), VolHueBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceHue(); }));
        SetSlider(VolSatSlider(), VolSatBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceSaturation(); }));
        SetSlider(VolValSlider(), VolValBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceValue(); }));

        SetCheck(VolFrontHDR(),
                 agreed([](auto const& v) { return v->adsorptionSurfaceFrontSideHDR(); }));
        SetSlider(VolFrontHDRExpSlider(), VolFrontHDRExpBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceFrontSideHDRExposure(); }));
        SetSlider(VolFrontAmbSlider(), VolFrontAmbBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceFrontSideAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolFrontAmbSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceFrontSideAmbientColor(); }));
        SetSlider(VolFrontDiffSlider(), VolFrontDiffBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceFrontSideDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolFrontDiffSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceFrontSideDiffuseColor(); }));
        SetSlider(VolFrontSpecSlider(), VolFrontSpecBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceFrontSideSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolFrontSpecSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceFrontSideSpecularColor(); }));
        SetSlider(VolFrontShininessSlider(), VolFrontShininessBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceFrontSideShininess(); }));

        SetCheck(VolBackHDR(),
                 agreed([](auto const& v) { return v->adsorptionSurfaceBackSideHDR(); }));
        SetSlider(VolBackHDRExpSlider(), VolBackHDRExpBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceBackSideHDRExposure(); }));
        SetSlider(VolBackAmbSlider(), VolBackAmbBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceBackSideAmbientIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolBackAmbSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceBackSideAmbientColor(); }));
        SetSlider(VolBackDiffSlider(), VolBackDiffBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceBackSideDiffuseIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolBackDiffSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceBackSideDiffuseColor(); }));
        SetSlider(VolBackSpecSlider(), VolBackSpecBox(),
                  agreed([](auto const& v)
                         { return v->adsorptionSurfaceBackSideSpecularIntensity(); }));
        DetailControls::SetColorWellOrMultiple(
            VolBackSpecSwatch(),
            agreed([](auto const& v) { return v->adsorptionSurfaceBackSideSpecularColor(); }));
        SetSlider(VolBackShininessSlider(), VolBackShininessBox(),
                  agreed([](auto const& v) { return v->adsorptionSurfaceBackSideShininess(); }));
    }

    void AppearanceDetailView::ReloadAnnotation()
    {
        auto annotation = FirstAs<AnnotationViewer>(m_controller);
        ShowBody(AnnotationHint(), AnnotationBody(), annotation != nullptr);
        if (!annotation)
            return;

        auto agreed = [this](auto const& read)
        {
            return AgreedAs<AnnotationViewer>(m_controller, read);
        };

        FillCombo(AnnType(),
                  { L"None", L"Display Name", L"Identifier", L"Chemical Element",
                    L"Force Field Type", L"Position", L"Charge" },
                  ItemOf(agreed([](auto const& a) { return a->renderTextType(); })));

        // The stored font need not be one of the offered ones, in which case it
        // is added so the popup can show what is actually in use.
        std::vector<hstring> fonts{ L"Segoe UI", L"Consolas", L"Arial",
                                    L"Times New Roman", L"Courier New" };
        const auto font = agreed([](auto const& a) { return a->renderTextFont(); });
        std::optional<int> fontIndex;
        if (font)
        {
            fontIndex = 0;
            bool found = false;
            for (size_t i = 0; i < fonts.size(); ++i)
            {
                if (font->toLower() == RKString::fromStdWString(std::wstring(fonts[i])).toLower())
                {
                    fontIndex = static_cast<int>(i);
                    found = true;
                }
            }
            if (!found && !font->isEmpty())
            {
                fonts.push_back(hstring(font->toStdWString()));
                fontIndex = static_cast<int>(fonts.size()) - 1;
            }
        }
        FillCombo(AnnFont(), fonts, fontIndex);

        FillCombo(AnnAlign(),
                  { L"Center", L"Left", L"Right", L"Top", L"Bottom",
                    L"Top-Left", L"Top-Right", L"Bottom-Left", L"Bottom-Right" },
                  ItemOf(agreed([](auto const& a) { return a->renderTextAlignment(); })));
        FillCombo(AnnStyle(), { L"Flat Billboard" },
                  ItemOf(agreed([](auto const& a) { return a->renderTextStyle(); })));

        DetailControls::SetColorWellOrMultiple(
            AnnColorSwatch(), agreed([](auto const& a) { return a->renderTextColor(); }));
        SetSlider(AnnScalingSlider(), AnnScalingBox(),
                  agreed([](auto const& a) { return a->renderTextScaling(); }));

        auto offset = [&agreed](auto const& component)
        {
            return agreed([&component](auto const& a)
                          { return component(a->renderTextOffset()); });
        };
        SetNumber(AnnOffsetX(), offset([](double3 const& o) { return o.x; }));
        SetNumber(AnnOffsetY(), offset([](double3 const& o) { return o.y; }));
        SetNumber(AnnOffsetZ(), offset([](double3 const& o) { return o.z; }));
    }
}
