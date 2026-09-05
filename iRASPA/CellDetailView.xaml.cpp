#include "pch.h"
#include "CellDetailView.xaml.h"
#if __has_include("CellDetailView.g.cpp")
#include "CellDetailView.g.cpp"
#endif

#include "StructureTypeTable.h"

#include "atomviewer.h"
#include "atomstructureviewer.h"
#include "bondviewer.h"
#include "documentdata.h"
#include "forcefieldsets.h"
#include "iraspaobject.h"
#include "primitive.h"
#include "spacegroupviewer.h"
#include "structure.h"
#include "skcell.h"
#include "skpointgroup.h"
#include "skspacegroup.h"
#include "skspacegroupdatabase.h"

#include "mathkit.h"
#include "rkstring.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>

#include <winrt/Windows.Globalization.NumberFormatting.h>
#include <winrt/Microsoft.UI.Dispatching.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        // Cocoa's editable material-name combo box in the structural group.
        constexpr wchar_t const* kMaterialTypes[] = {
            L"Unspecified", L"Molecule", L"Protein", L"DNA/RNA", L"Molecular crystal",
            L"Silica", L"Aluminosilicate", L"Aluminophosphate",
            L"Metallophosphate", L"Silicoaluminophosphate", L"Zeolite",
            L"MOF", L"ZIF", L"COF",
            L"Carbon", L"Oxide",
            L"HOF", L"PAF", L"PIM", L"Polymer", L"Ionic liquid",
            L"Clay", L"Perovskite", L"Alloy", L"Glass",
        };

        constexpr wchar_t const* kProbeMolecules[] = {
            L"Helium", L"Methane", L"Nitrogen", L"Hydrogen", L"Water",
            L"CO\u2082", L"Xenon", L"Krypton", L"Argon", L"Connolly", L"Custom",
        };

        hstring FormatDouble(double v, int decimals = 5)
        {
            if (!std::isfinite(v))
                v = 0.0;
            wchar_t buf[64];
            swprintf_s(buf, L"%.*f", decimals, v);
            return hstring(buf);
        }

        // The item of a popup whose order follows an enum, and nothing when the
        // selection does not agree on the value.
        template <class Enum>
        std::optional<int> ItemOf(std::optional<Enum> const& value)
        {
            if (!value)
                return std::nullopt;
            return static_cast<int>(*value);
        }

        wchar_t const* LaueGroupString(Laue laue)
        {
            switch (laue)
            {
            case Laue::laue_1:    return L"1";
            case Laue::laue_2m:   return L"2/m";
            case Laue::laue_mmm:  return L"mmm";
            case Laue::laue_4m:   return L"4/m";
            case Laue::laue_4mmm: return L"4/mmm";
            case Laue::laue_3:    return L"3";
            case Laue::laue_3m:   return L"3m";
            case Laue::laue_6m:   return L"6/m";
            case Laue::laue_6mmm: return L"6/mmm";
            case Laue::laue_m3:   return L"m-3";
            case Laue::laue_m3m:  return L"m-3m";
            default:              return L"";
            }
        }
    }

    CellDetailView::CellDetailView()
    {
        InitializeComponent();
        DetailControls::FitFixedColumns(*this);

        m_eulerBoxes = { EulerX(), EulerY(), EulerZ() };
        m_eulerSliders = { SliderX(), SliderY(), SliderZ() };
        m_originBoxes = { OriginX(), OriginY(), OriginZ() };
        m_shiftBoxes = { ShiftX(), ShiftY(), ShiftZ() };
        m_flipChecks = { FlipA(), FlipB(), FlipC() };

        BuildStructureTypeItems();
        BuildStaticComboItems();
        BuildFieldTables();
        WireSectionExpanders();

        // The lengths and angles Cocoa shows to four and three decimals.
        ConfigureNumber(LengthA(), 0.01, 100000.0, 0.1, 4);
        ConfigureNumber(LengthB(), 0.01, 100000.0, 0.1, 4);
        ConfigureNumber(LengthC(), 0.01, 100000.0, 0.1, 4);
        ConfigureNumber(AngleAlpha(), 0.01, 179.99, 1.0, 3);
        ConfigureNumber(AngleBeta(), 0.01, 179.99, 1.0, 3);
        ConfigureNumber(AngleGamma(), 0.01, 179.99, 1.0, 3);

        for (auto const& box : { MaxReplicaX(), MaxReplicaY(), MaxReplicaZ(),
                                 MinReplicaX(), MinReplicaY(), MinReplicaZ() })
            ConfigureNumber(box, -100.0, 100.0, 1.0, 0);

        ConfigureNumber(RotationDelta(), 0.1, 180.0, 0.5, 4);
        for (auto const& box : m_eulerBoxes)
            ConfigureNumber(box, -180.0, 180.0, 1.0, 6);
        for (auto const& box : m_originBoxes)
            ConfigureNumber(box, -100000.0, 100000.0, 0.1, 6);
        for (auto const& box : m_shiftBoxes)
            ConfigureNumber(box, -1.0, 1.0, 0.01, 6);
        ConfigureNumber(PrecisionBox(), 1e-6, 1.0, 1e-3, 6);

        // A drag has to be one undo entry, so the sliders are watched for the
        // pointer going down and being released. Handled events count, because
        // the slider marks them as it tracks the pointer.
        for (auto const& slider : m_eulerSliders)
        {
            slider.AddHandler(UIElement::PointerPressedEvent(),
                box_value(Input::PointerEventHandler([this](IInspectable const&,
                                                            Input::PointerRoutedEventArgs const&)
                {
                    if (!m_controller)
                        return;
                    m_sliderBefore = m_controller->SnapshotCellStates();
                    m_sliderDragging = true;
                })), true);
            slider.AddHandler(UIElement::PointerCaptureLostEvent(),
                box_value(Input::PointerEventHandler([this](IInspectable const&,
                                                            Input::PointerRoutedEventArgs const&)
                {
                    if (!m_sliderDragging || !m_controller)
                        return;
                    m_sliderDragging = false;
                    m_controller->RegisterCellUndo(L"Change Orientation", m_sliderBefore);
                    m_sliderBefore.clear();
                })), true);
        }
    }

    void CellDetailView::ConfigureNumber(NumberBox const& box, double minimum, double maximum,
                                         double step, int decimals)
    {
        box.Minimum(minimum);
        box.Maximum(maximum);
        box.SmallChange(step);
        box.LargeChange(step * 5.0);
        winrt::Windows::Globalization::NumberFormatting::DecimalFormatter formatter;
        formatter.IntegerDigits(1);
        formatter.FractionDigits(decimals);
        formatter.IsGrouped(false);
        box.NumberFormatter(formatter);
    }

    void CellDetailView::BuildStructureTypeItems()
    {
        for (auto const& entry : kStructureTypes)
        {
            ComboBoxItem item;
            item.Content(box_value(hstring(entry.name)));
            // The types Qt greys out have no conversion behind them.
            item.IsEnabled(entry.convertible);
            TypeCombo().Items().Append(item);
        }
    }

    void CellDetailView::BuildStaticComboItems()
    {
        for (auto const* name : kMaterialTypes)
            MaterialCombo().Items().Append(box_value(hstring(name)));
        for (auto const* name : kProbeMolecules)
            ProbeCombo().Items().Append(box_value(hstring(name)));

        // The space-group popups are the whole database, so they are filled once
        // and only their selection moves afterwards.
        for (size_t n = 1; n < SKSpaceGroupDataBase::spaceGroupHallData.size(); ++n)
        {
            const int firstHall = SKSpaceGroupDataBase::spaceGroupHallData[n].front();
            auto const& setting = SKSpaceGroupDataBase::spaceGroupData[firstHall];
            NumberCombo().Items().Append(box_value(hstring(std::to_wstring(n) + L": " +
                                                           setting.HMString().toStdWString())));
        }
        for (size_t h = 1; h < SKSpaceGroupDataBase::spaceGroupData.size(); ++h)
        {
            auto const& setting = SKSpaceGroupDataBase::spaceGroupData[h];
            HallCombo().Items().Append(box_value(hstring(std::to_wstring(h) + L": " +
                                                         setting.HallString().toStdWString())));
        }
    }

    void CellDetailView::BuildFieldTables()
    {
        auto add = [this](NumberBox const& box,
                          double (StructuralPropertyViewer::*get)() const,
                          void (StructuralPropertyEditor::*set)(double))
        {
            m_structuralFields.push_back({ box,
                [get](StructuralPropertyViewer& v) { return (v.*get)(); },
                [set](StructuralPropertyEditor& e, double value) { (e.*set)(value); } });
        };
        // The counts are whole numbers on the model side, so they round.
        auto addInt = [this](NumberBox const& box,
                             int (StructuralPropertyViewer::*get)() const,
                             void (StructuralPropertyEditor::*set)(int))
        {
            m_structuralFields.push_back({ box,
                [get](StructuralPropertyViewer& v) { return static_cast<double>((v.*get)()); },
                [set](StructuralPropertyEditor& e, double value)
                { (e.*set)(static_cast<int>(std::lround(value))); } });
        };

        add(MassBox(), &StructuralPropertyViewer::structureMass,
            &StructuralPropertyEditor::setStructureMass);
        add(DensityBox(), &StructuralPropertyViewer::structureDensity,
            &StructuralPropertyEditor::setStructureDensity);
        add(VoidFractionBox(), &StructuralPropertyViewer::structureHeliumVoidFraction,
            &StructuralPropertyEditor::setStructureHeliumVoidFraction);
        add(SpecificVolumeBox(), &StructuralPropertyViewer::structureSpecificVolume,
            &StructuralPropertyEditor::setStructureSpecificVolume);
        add(PoreVolumeBox(), &StructuralPropertyViewer::structureAccessiblePoreVolume,
            &StructuralPropertyEditor::setStructureAccessiblePoreVolume);
        add(VolumetricAreaBox(), &StructuralPropertyViewer::structureVolumetricNitrogenSurfaceArea,
            &StructuralPropertyEditor::setStructureVolumetricNitrogenSurfaceArea);
        add(GravimetricAreaBox(), &StructuralPropertyViewer::structureGravimetricNitrogenSurfaceArea,
            &StructuralPropertyEditor::setStructureGravimetricNitrogenSurfaceArea);
        add(VolumetricWellAreaBox(), &StructuralPropertyViewer::structureVolumetricWellSurfaceArea,
            &StructuralPropertyEditor::setStructureVolumetricWellSurfaceArea);
        add(GravimetricWellAreaBox(), &StructuralPropertyViewer::structureGravimetricWellSurfaceArea,
            &StructuralPropertyEditor::setStructureGravimetricWellSurfaceArea);
        add(VolumetricGeometricAreaBox(), &StructuralPropertyViewer::structureVolumetricGeometricSurfaceArea,
            &StructuralPropertyEditor::setStructureVolumetricGeometricSurfaceArea);
        add(GravimetricGeometricAreaBox(), &StructuralPropertyViewer::structureGravimetricGeometricSurfaceArea,
            &StructuralPropertyEditor::setStructureGravimetricGeometricSurfaceArea);
        add(VolumetricVDWGeometricAreaBox(), &StructuralPropertyViewer::structureVolumetricVanDerWaalsGeometricSurfaceArea,
            &StructuralPropertyEditor::setStructureVolumetricVanDerWaalsGeometricSurfaceArea);
        add(GravimetricVDWGeometricAreaBox(), &StructuralPropertyViewer::structureGravimetricVanDerWaalsGeometricSurfaceArea,
            &StructuralPropertyEditor::setStructureGravimetricVanDerWaalsGeometricSurfaceArea);
        add(ProbeEpsilonBox(), &StructuralPropertyViewer::frameworkProbeEpsilon,
            &StructuralPropertyEditor::setFrameworkProbeEpsilon);
        add(ProbeSigmaBox(), &StructuralPropertyViewer::frameworkProbeSigma,
            &StructuralPropertyEditor::setFrameworkProbeSigma);
        addInt(ChannelSystemsBox(), &StructuralPropertyViewer::structureNumberOfChannelSystems,
               &StructuralPropertyEditor::setStructureNumberOfChannelSystems);
        addInt(PocketsBox(), &StructuralPropertyViewer::structureNumberOfInaccessiblePockets,
               &StructuralPropertyEditor::setStructureNumberOfInaccessiblePockets);
        addInt(DimensionalityBox(), &StructuralPropertyViewer::structureDimensionalityOfPoreSystem,
               &StructuralPropertyEditor::setStructureDimensionalityOfPoreSystem);
        add(CavityDiameterBox(), &StructuralPropertyViewer::structureLargestCavityDiameter,
            &StructuralPropertyEditor::setStructureLargestCavityDiameter);
        add(PoreDiameterBox(), &StructuralPropertyViewer::structureRestrictingPoreLimitingDiameter,
            &StructuralPropertyEditor::setStructureRestrictingPoreLimitingDiameter);
        add(ViablePathDiameterBox(), &StructuralPropertyViewer::structureLargestCavityDiameterAlongAViablePath,
            &StructuralPropertyEditor::setStructureLargestCavityDiameterAlongAViablePath);

        // Cocoa's steps: masses and areas by one, the volumes and fractions
        // finer, the counts by one and whole.
        ConfigureNumber(MassBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(DensityBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(VoidFractionBox(), 0.0, 1.0e12, 0.01, 6);
        ConfigureNumber(SpecificVolumeBox(), 0.0, 1.0e12, 0.01, 6);
        ConfigureNumber(PoreVolumeBox(), 0.0, 1.0e12, 0.01, 6);
        ConfigureNumber(VolumetricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(GravimetricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(VolumetricWellAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(GravimetricWellAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(VolumetricGeometricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(GravimetricGeometricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(VolumetricVDWGeometricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(GravimetricVDWGeometricAreaBox(), 0.0, 1.0e12, 1.0, 6);
        ConfigureNumber(ProbeEpsilonBox(), 0.0, 1.0e12, 0.1, 6);
        ConfigureNumber(ProbeSigmaBox(), 0.0, 1.0e12, 0.01, 6);
        ConfigureNumber(ChannelSystemsBox(), 0.0, 1.0e12, 1.0, 0);
        ConfigureNumber(PocketsBox(), 0.0, 1.0e12, 1.0, 0);
        ConfigureNumber(DimensionalityBox(), 0.0, 1.0e12, 1.0, 0);
        ConfigureNumber(CavityDiameterBox(), 0.0, 1.0e12, 0.1, 6);
        ConfigureNumber(PoreDiameterBox(), 0.0, 1.0e12, 0.1, 6);
        ConfigureNumber(ViablePathDiameterBox(), 0.0, 1.0e12, 0.1, 6);
    }

    std::shared_ptr<Object> CellDetailView::FirstObject() const
    {
        if (!m_controller)
            return nullptr;
        for (auto const& target : m_controller->TargetStructures())
        {
            auto object = target ? target->object() : nullptr;
            if (object && object->cell())
                return object;
        }
        return nullptr;
    }

    int CellDetailView::AxisOf(IInspectable const& sender,
                               std::array<NumberBox, 3> const& boxes) const
    {
        for (int axis = 0; axis < 3; ++axis)
            if (boxes[axis] == sender)
                return axis;
        return -1;
    }

    void CellDetailView::RefreshAllCheckVisuals()
    {
        for (auto const& check : m_flipChecks)
            DetailControls::RefreshCheckVisual(check);
    }

    void CellDetailView::WireSectionExpanders()
    {
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

    void CellDetailView::Reload()
    {
        auto object = FirstObject();
        Hint().Visibility(object ? Visibility::Collapsed : Visibility::Visible);
        Sections().Visibility(object ? Visibility::Visible : Visibility::Collapsed);
        if (!object)
            return;

        m_suppressEvents = true;
        try
        {
            ReloadBox();
            ReloadReadouts();
            ReloadTransform();
            ReloadStructural();
            ReloadSymmetry();
            ReloadBlockingPockets();
        }
        catch (...)
        {
        }
        m_suppressEvents = false;
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]()
        {
            if (auto self = weak.get())
                self->RefreshAllCheckVisuals();
        });
    }

    void CellDetailView::ReloadBox()
    {
        // The structure type of the selection as a whole.
        const auto type = AgreedObject([](Object& o) { return o.structureType(); });
        DetailControls::SelectOrMultiple(
            TypeCombo(), type ? std::optional<int>(IndexOfStructureType(*type)) : std::nullopt);

        auto cell = [this](auto const& read)
        {
            return AgreedCell(read);
        };
        DetailControls::SetNumberOrMultiple(LengthA(), cell([](SKCell& c) { return c.a(); }));
        DetailControls::SetNumberOrMultiple(LengthB(), cell([](SKCell& c) { return c.b(); }));
        DetailControls::SetNumberOrMultiple(LengthC(), cell([](SKCell& c) { return c.c(); }));
        DetailControls::SetNumberOrMultiple(
            AngleAlpha(), cell([](SKCell& c) { return c.alpha() * 180.0 / M_PI; }));
        DetailControls::SetNumberOrMultiple(
            AngleBeta(), cell([](SKCell& c) { return c.beta() * 180.0 / M_PI; }));
        DetailControls::SetNumberOrMultiple(
            AngleGamma(), cell([](SKCell& c) { return c.gamma() * 180.0 / M_PI; }));

        DetailControls::SetNumberOrMultiple(
            MaxReplicaX(), cell([](SKCell& c) { return double(c.maximumReplicaX()); }));
        DetailControls::SetNumberOrMultiple(
            MaxReplicaY(), cell([](SKCell& c) { return double(c.maximumReplicaY()); }));
        DetailControls::SetNumberOrMultiple(
            MaxReplicaZ(), cell([](SKCell& c) { return double(c.maximumReplicaZ()); }));
        DetailControls::SetNumberOrMultiple(
            MinReplicaX(), cell([](SKCell& c) { return double(c.minimumReplicaX()); }));
        DetailControls::SetNumberOrMultiple(
            MinReplicaY(), cell([](SKCell& c) { return double(c.minimumReplicaY()); }));
        DetailControls::SetNumberOrMultiple(
            MinReplicaZ(), cell([](SKCell& c) { return double(c.minimumReplicaZ()); }));

        DetailControls::SetNumberOrMultiple(
            m_originBoxes[0], AgreedObject([](Object& o) { return o.origin().x; }));
        DetailControls::SetNumberOrMultiple(
            m_originBoxes[1], AgreedObject([](Object& o) { return o.origin().y; }));
        DetailControls::SetNumberOrMultiple(
            m_originBoxes[2], AgreedObject([](Object& o) { return o.origin().z; }));
    }

    void CellDetailView::ReloadReadouts()
    {
        auto object = FirstObject();
        if (!object)
            return;
        auto cell = object->cell();
        if (!cell)
            return;

        const bool previous = m_suppressEvents;
        m_suppressEvents = true;

        // The readouts are text, so they can spell out the words themselves.
        auto readout = [](TextBox const& box, std::optional<double> const& value, int decimals = 5)
        {
            box.Text(value ? FormatDouble(*value, decimals)
                           : DetailControls::MultipleValuesText());
        };
        auto bound = [this](auto const& component)
        {
            return AgreedObject([&component](Object& o)
                                { return component(o.boundingBox()); });
        };
        readout(BbMin0(), bound([](SKBoundingBox const& b) { return b.minimum().x; }), 3);
        readout(BbMin1(), bound([](SKBoundingBox const& b) { return b.minimum().y; }), 3);
        readout(BbMin2(), bound([](SKBoundingBox const& b) { return b.minimum().z; }), 3);
        readout(BbMax0(), bound([](SKBoundingBox const& b) { return b.maximum().x; }), 3);
        readout(BbMax1(), bound([](SKBoundingBox const& b) { return b.maximum().y; }), 3);
        readout(BbMax2(), bound([](SKBoundingBox const& b) { return b.maximum().z; }), 3);

        auto matrix = [this](auto const& component)
        {
            return AgreedCell([&component](SKCell& c) { return component(c.unitCell()); });
        };
        readout(MatAX(), matrix([](double3x3 const& m) { return m.ax; }));
        readout(MatBX(), matrix([](double3x3 const& m) { return m.bx; }));
        readout(MatCX(), matrix([](double3x3 const& m) { return m.cx; }));
        readout(MatAY(), matrix([](double3x3 const& m) { return m.ay; }));
        readout(MatBY(), matrix([](double3x3 const& m) { return m.by; }));
        readout(MatCY(), matrix([](double3x3 const& m) { return m.cy; }));
        readout(MatAZ(), matrix([](double3x3 const& m) { return m.az; }));
        readout(MatBZ(), matrix([](double3x3 const& m) { return m.bz; }));
        readout(MatCZ(), matrix([](double3x3 const& m) { return m.cz; }));

        readout(VolumeBox(), AgreedCell([](SKCell& c) { return c.volume(); }));
        auto width = [this](auto const& component)
        {
            return AgreedCell([&component](SKCell& c)
                              { return component(c.perpendicularWidths()); });
        };
        readout(Perp0(), width([](double3 const& w) { return w.x; }));
        readout(Perp1(), width([](double3 const& w) { return w.y; }));
        readout(Perp2(), width([](double3 const& w) { return w.z; }));

        auto angle = [this](auto const& component)
        {
            return AgreedObject([&component](Object& o)
                                { return component(o.orientation().EulerAngles()) * 180.0 / M_PI; });
        };
        const std::optional<double> degrees[3] = {
            angle([](double3 const& e) { return e.x; }),
            angle([](double3 const& e) { return e.y; }),
            angle([](double3 const& e) { return e.z; }) };
        for (int axis = 0; axis < 3; ++axis)
        {
            DetailControls::SetNumberOrMultiple(m_eulerBoxes[axis], degrees[axis]);
            if (degrees[axis])
                m_eulerSliders[axis].Value((std::clamp)(*degrees[axis], -180.0, 180.0));
        }

        // Cocoa labels the rotate buttons with the current rotation angle.
        const auto agreedDelta = AgreedObject([](Object& o) { return o.rotationDelta(); });
        DetailControls::SetNumberOrMultiple(RotationDelta(), agreedDelta);
        const double delta = agreedDelta.value_or(object->rotationDelta());
        wchar_t plus[32];
        wchar_t minus[32];
        swprintf_s(plus, L"Rotate (%g\u00B0)", delta);
        swprintf_s(minus, L"Rotate (-%g\u00B0)", delta);
        for (auto const& button : { RotatePlusX(), RotatePlusY(), RotatePlusZ() })
            button.Content(box_value(hstring(plus)));
        for (auto const& button : { RotateMinusX(), RotateMinusY(), RotateMinusZ() })
            button.Content(box_value(hstring(minus)));

        m_suppressEvents = previous;
    }

    void CellDetailView::ReloadTransform()
    {
        auto object = FirstObject();
        auto cell = object ? object->cell() : nullptr;
        TransformHint().Visibility(cell ? Visibility::Collapsed : Visibility::Visible);
        TransformBody().Visibility(cell ? Visibility::Visible : Visibility::Collapsed);
        if (!cell)
            return;

        const std::optional<bool> flips[3] = {
            AgreedCell([](SKCell& c) { return c.contentFlip().x; }),
            AgreedCell([](SKCell& c) { return c.contentFlip().y; }),
            AgreedCell([](SKCell& c) { return c.contentFlip().z; }) };
        const std::optional<double> shifts[3] = {
            AgreedCell([](SKCell& c) { return c.contentShift().x; }),
            AgreedCell([](SKCell& c) { return c.contentShift().y; }),
            AgreedCell([](SKCell& c) { return c.contentShift().z; }) };
        for (int axis = 0; axis < 3; ++axis)
        {
            DetailControls::SetCheckOrMultiple(m_flipChecks[axis], flips[axis]);
            DetailControls::SetNumberOrMultiple(m_shiftBoxes[axis], shifts[axis]);
        }
    }

    void CellDetailView::ReloadStructural()
    {
        std::shared_ptr<StructuralPropertyViewer> viewer;
        if (m_controller)
        {
            for (auto const& target : m_controller->TargetStructures())
            {
                if (!target)
                    continue;
                if (auto v = std::dynamic_pointer_cast<StructuralPropertyViewer>(target->object()))
                {
                    viewer = v;
                    break;
                }
            }
        }
        StructuralHint().Visibility(viewer ? Visibility::Collapsed : Visibility::Visible);
        StructuralBody().Visibility(viewer ? Visibility::Visible : Visibility::Collapsed);
        if (!viewer)
            return;

        auto agreed = [this](auto const& read)
        {
            return m_controller->AgreedValue<StructuralPropertyViewer>(read);
        };

        for (auto const& field : m_structuralFields)
        {
            DetailControls::SetNumberOrMultiple(
                field.box,
                agreed([&field](std::shared_ptr<StructuralPropertyViewer> const& v)
                       { return field.read(*v); }));
        }

        const auto material = agreed([](auto const& v) { return v->structureMaterialType(); });
        MaterialCombo().SelectedIndex(-1);
        if (material)
        {
            const std::wstring name = material->toStdWString();
            for (int i = 0; i < static_cast<int>(std::size(kMaterialTypes)); ++i)
            {
                if (name == kMaterialTypes[i])
                {
                    MaterialCombo().SelectedIndex(i);
                    break;
                }
            }
            // Whatever the name is, typed or picked, it shows in the editable field.
            MaterialCombo().Text(hstring(name));
        }
        else
        {
            // The material popup is editable, so Cocoa puts the words in the text.
            MaterialCombo().Text(DetailControls::MultipleValuesText());
        }

        {
            auto probe = agreed([](auto const& v) { return v->frameworkProbeMolecule(); });
            std::optional<int> item;
            if (probe)
            {
                const int index = selectableProbeMoleculeIndex(*probe);
                if (index >= 0)
                    item = index;
            }
            DetailControls::SelectOrMultiple(ProbeCombo(), item);
        }

        // Document force-field sets, matched by name like Cocoa's Cell popup.
        std::vector<hstring> forceFields;
        std::optional<int> forceFieldIndex;
        const auto field = m_controller->AgreedValue<AtomStructureViewer>(
            [](std::shared_ptr<AtomStructureViewer> const& a) { return a->atomForceFieldIdentifier(); });
        if (m_controller->Document())
        {
            auto const& fields = m_controller->Document()->forceFieldSets().forceFieldSets();
            for (size_t i = 0; i < fields.size(); ++i)
            {
                const RKString name = fields[i].displayName();
                forceFields.push_back(hstring(name.toStdWString()));
                if (field && name.toLower() == field->toLower())
                    forceFieldIndex = static_cast<int>(i);
            }
        }
        if (forceFields.empty())
            forceFields.push_back(L"Default");
        ForceFieldCombo().Items().Clear();
        for (auto const& item : forceFields)
            ForceFieldCombo().Items().Append(box_value(item));
        DetailControls::SelectOrMultiple(ForceFieldCombo(),
                                         field ? forceFieldIndex.value_or(0) : forceFieldIndex);
    }

    // Cocoa lists the pockets of the first selected structure, read-only: they are
    // replaced wholesale by reading a file, never edited row by row.
    void CellDetailView::ReloadBlockingPockets()
    {
        std::shared_ptr<Structure> structure;
        if (m_controller)
        {
            for (auto const& target : m_controller->TargetStructures())
            {
                if (auto s = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr)
                {
                    structure = s;
                    break;
                }
            }
        }
        BlockingHint().Visibility(structure ? Visibility::Collapsed : Visibility::Visible);
        BlockingBody().Visibility(structure ? Visibility::Visible : Visibility::Collapsed);
        if (!structure)
            return;

        auto const pockets = m_controller->BlockingPockets();
        BlockingCountLabel().Text(hstring(std::to_wstring(pockets.size()) + L" blocking pockets"));

        auto rows = BlockingRows();
        rows.Children().Clear();
        for (size_t i = 0; i < pockets.size(); ++i)
        {
            auto const& pocket = pockets[i];
            Grid row;
            for (double width : { 40.0, 0.0, 0.0, 0.0, 0.0 })
            {
                ColumnDefinition column;
                column.Width(width > 0.0 ? GridLengthHelper::FromPixels(width)
                                         : GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
                row.ColumnDefinitions().Append(column);
            }

            const double values[5] = { static_cast<double>(i + 1), pocket.x, pocket.y, pocket.z,
                                       pocket.w };
            for (int column = 0; column < 5; ++column)
            {
                wchar_t text[32];
                if (column == 0)
                    swprintf_s(text, L"%d", static_cast<int>(values[column]));
                else
                    swprintf_s(text, L"%.4f", values[column]);
                TextBlock cell;
                cell.Text(hstring(text));
                cell.FontSize(11.0);
                Grid::SetColumn(cell, column);
                row.Children().Append(cell);
            }
            rows.Children().Append(row);
        }
    }

    void CellDetailView::ReloadSymmetry()
    {
        std::shared_ptr<SpaceGroupViewer> viewer;
        if (m_controller)
        {
            for (auto const& target : m_controller->TargetStructures())
            {
                if (!target)
                    continue;
                if (auto v = std::dynamic_pointer_cast<SpaceGroupViewer>(target->object()))
                {
                    viewer = v;
                    break;
                }
            }
        }
        SymmetryHint().Visibility(viewer ? Visibility::Collapsed : Visibility::Visible);
        SymmetryBody().Visibility(viewer ? Visibility::Visible : Visibility::Collapsed);
        if (!viewer)
            return;

        // One space group for the whole selection, or none: everything below is
        // that group's, so it all shows the words together.
        const auto agreedHall = m_controller->AgreedValue<SpaceGroupViewer>(
            [](auto const& v)
            { return static_cast<int>(v->spaceGroup().spaceGroupSetting().HallNumber()); });
        auto const& text = DetailControls::MultipleValuesText();

        TextBox groupBoxes[] = { HolohedryBox(), LaueBox(), PointGroupBox(), SchoenfliesBox(),
                                 CentrosymmetricBox(), EnantiomorphicBox(), CenteringBox(),
                                 Lattice0(), Lattice1(), Lattice2(), Lattice3(), InversionBox(),
                                 InversionCenterBox(), SymmorphicityBox(), ElementsBox() };
        if (!agreedHall)
        {
            DetailControls::SelectOrMultiple(NumberCombo(), std::nullopt);
            DetailControls::SelectOrMultiple(HallCombo(), std::nullopt);
            QualifierCombo().Items().Clear();
            DetailControls::SelectOrMultiple(QualifierCombo(), std::nullopt);
            for (auto const& box : groupBoxes)
                box.Text(text);
            DetailControls::SetNumberOrMultiple(
                PrecisionBox(), AgreedCell([](SKCell& c) { return c.precision(); }));
            return;
        }

        const int hall = *agreedHall;
        auto const& setting = viewer->spaceGroup().spaceGroupSetting();
        const int number = static_cast<int>(setting.number());

        DetailControls::SelectOrMultiple(NumberCombo(), number - 1);
        DetailControls::SelectOrMultiple(HallCombo(), hall - 1);

        // The qualifier list is the settings of the current number, so it is
        // rebuilt whenever that changes.
        QualifierCombo().Items().Clear();
        int qualifierIndex = 0;
        if (number >= 1 && number < static_cast<int>(SKSpaceGroupDataBase::spaceGroupHallData.size()))
        {
            auto const& halls = SKSpaceGroupDataBase::spaceGroupHallData[number];
            for (size_t i = 0; i < halls.size(); ++i)
            {
                // Cocoa: ((ext > 0) ? "\(ext):" : "") + qualifier, so Fd-3m's
                // two origin choices read as "1:abc" / "2:abc" rather than both
                // as bare "abc".
                auto const& settingForHall = SKSpaceGroupDataBase::spaceGroupData[halls[i]];
                std::wstring qualifier = settingForHall.qualifier().toStdWString();
                if (qualifier.empty())
                    qualifier = L"-";
                if (settingForHall.extension() > 0)
                    qualifier = std::to_wstring(static_cast<unsigned char>(settingForHall.extension())) + L":" + qualifier;
                QualifierCombo().Items().Append(box_value(hstring(qualifier)));
                if (halls[i] == hall)
                    qualifierIndex = static_cast<int>(i);
            }
        }
        QualifierCombo().SelectedIndex(qualifierIndex);

        const int pointGroupNumber = static_cast<int>(setting.pointGroupNumber());
        if (pointGroupNumber >= 0 && pointGroupNumber < static_cast<int>(SKPointGroup::pointGroupData.size()))
        {
            SKPointGroup& group = SKPointGroup::pointGroupData[pointGroupNumber];
            HolohedryBox().Text(hstring(group.holohedryString().toStdWString()));
            LaueBox().Text(hstring(LaueGroupString(group.laue())));
            PointGroupBox().Text(hstring(group.symbol().toStdWString()));
            CentrosymmetricBox().Text(group.centrosymmetric() ? L"yes" : L"no");
            EnantiomorphicBox().Text(group.enantiomorphic() ? L"yes" : L"no");
        }
        // Cocoa's Schoenflies field is the space-group symbol (Oh^7 for Fd-3m),
        // not the point-group one (Oh).
        SchoenfliesBox().Text(hstring(setting.schoenflies().toStdWString()));

        CenteringBox().Text(hstring(setting.centringString().toStdWString()));
        const std::vector<RKString> translations = SKSpaceGroup::latticeTranslationStrings(hall);
        TextBox lattice[4] = { Lattice0(), Lattice1(), Lattice2(), Lattice3() };
        for (auto& box : lattice)
            box.Text(L"");
        for (size_t i = 0; i < 4 && i < translations.size(); ++i)
            lattice[i].Text(hstring(translations[i].toStdWString()));

        InversionBox().Text(setting.inversionAtOrigin() ? L"yes" : L"no");
        InversionCenterBox().Text(hstring(SKSpaceGroup::inversionCenterString(hall).toStdWString()));
        SymmorphicityBox().Text(hstring(setting.symmorphicityString().toStdWString()));
        ElementsBox().Text(hstring(std::to_wstring(setting.fullSeitzMatrices().size())));

        DetailControls::SetNumberOrMultiple(
            PrecisionBox(), AgreedCell([](SKCell& c) { return c.precision(); }));
    }

    // ---- edits ------------------------------------------------------------

    void CellDetailView::OnStructureTypeChanged(IInspectable const& sender,
                                                SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo)
            return;
        const int index = combo.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(std::size(kStructureTypes)))
            return;
        m_controller->ChangeStructureType(kStructureTypes[index].type);
        Reload();
    }

    void CellDetailView::OnUnitCellChanged(NumberBox const& sender,
                                           NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value) || value <= 0.0)
            return;

        // Which of the six the box is; the angles are stored in radians.
        auto apply = [value](SKCell& cell, int which)
        {
            switch (which)
            {
            case 0: cell.setLengthA(value); break;
            case 1: cell.setLengthB(value); break;
            case 2: cell.setLengthC(value); break;
            case 3: cell.setAlphaAngle(value * M_PI / 180.0); break;
            case 4: cell.setBetaAngle(value * M_PI / 180.0); break;
            case 5: cell.setGammaAngle(value * M_PI / 180.0); break;
            default: break;
            }
        };
        int which = -1;
        if (sender == LengthA()) which = 0;
        else if (sender == LengthB()) which = 1;
        else if (sender == LengthC()) which = 2;
        else if (sender == AngleAlpha()) which = 3;
        else if (sender == AngleBeta()) which = 4;
        else if (sender == AngleGamma()) which = 5;
        if (which < 0)
            return;

        m_controller->EditCells(L"Change Unit Cell", [&](Object& object)
        {
            if (auto cell = object.cell())
            {
                apply(*cell, which);
                object.reComputeBoundingBox();
            }
        }, DocumentController::CellReload::CameraAndRenderer);
        ReloadReadouts();
    }

    void CellDetailView::OnReplicaChanged(NumberBox const& sender,
                                          NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double raw = sender.Value();
        if (!std::isfinite(raw))
            return;
        const int value = static_cast<int>(std::lround(raw));

        int axis = -1;
        bool isMinimum = false;
        if (sender == MaxReplicaX()) { axis = 0; }
        else if (sender == MaxReplicaY()) { axis = 1; }
        else if (sender == MaxReplicaZ()) { axis = 2; }
        else if (sender == MinReplicaX()) { axis = 0; isMinimum = true; }
        else if (sender == MinReplicaY()) { axis = 1; isMinimum = true; }
        else if (sender == MinReplicaZ()) { axis = 2; isMinimum = true; }
        if (axis < 0)
            return;

        // A replica range cannot cross itself, so each end clamps to the other.
        m_controller->EditCells(L"Change Replicas", [&](Object& object)
        {
            auto cell = object.cell();
            if (!cell)
                return;
            switch (axis)
            {
            case 0: isMinimum ? cell->setMinimumReplicaX((std::min)(value, cell->maximumReplicaX()))
                              : cell->setMaximumReplicaX((std::max)(value, cell->minimumReplicaX())); break;
            case 1: isMinimum ? cell->setMinimumReplicaY((std::min)(value, cell->maximumReplicaY()))
                              : cell->setMaximumReplicaY((std::max)(value, cell->minimumReplicaY())); break;
            case 2: isMinimum ? cell->setMinimumReplicaZ((std::min)(value, cell->maximumReplicaZ()))
                              : cell->setMaximumReplicaZ((std::max)(value, cell->minimumReplicaZ())); break;
            }
            object.reComputeBoundingBox();
        }, DocumentController::CellReload::CameraAndRenderer);
        ReloadReadouts();
    }

    void CellDetailView::OnRotationDeltaChanged(NumberBox const& sender,
                                                NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value) || value <= 0.0)
            return;
        m_controller->EditCells(L"Change Rotation Angle", [value](Object& object)
        {
            object.setRotationDelta(value);
        }, DocumentController::CellReload::None);
        ReloadReadouts();
    }

    void CellDetailView::OnEulerBoxChanged(NumberBox const& sender,
                                           NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        const int axis = AxisOf(sender, m_eulerBoxes);
        if (axis < 0)
            return;
        m_controller->EditCells(L"Change Orientation", [axis, value](Object& object)
        {
            double3 euler = object.orientation().EulerAngles();
            if (axis == 0) euler.x = value * M_PI / 180.0;
            if (axis == 1) euler.y = value * M_PI / 180.0;
            if (axis == 2) euler.z = value * M_PI / 180.0;
            object.setOrientation(simd_quatd(euler));
            object.reComputeBoundingBox();
        }, DocumentController::CellReload::CameraAndRenderer);
        ReloadReadouts();
    }

    void CellDetailView::OnEulerSliderChanged(IInspectable const& sender,
                                              Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        int axis = -1;
        for (int i = 0; i < 3; ++i)
            if (m_eulerSliders[i] == sender)
                axis = i;
        if (axis < 0)
            return;

        const double degrees = e.NewValue();
        auto rotate = [axis, degrees](Object& object)
        {
            double3 euler = object.orientation().EulerAngles();
            if (axis == 0) euler.x = degrees * M_PI / 180.0;
            if (axis == 1) euler.y = degrees * M_PI / 180.0;
            if (axis == 2) euler.z = degrees * M_PI / 180.0;
            object.setOrientation(simd_quatd(euler));
            object.reComputeBoundingBox();
        };

        if (m_sliderDragging)
        {
            // Mid-drag: the undo entry for the whole drag is registered when the
            // slider lets the pointer go.
            m_controller->EditCellsWithoutUndo(rotate,
                                               DocumentController::CellReload::CameraAndRenderer);
        }
        else
        {
            m_controller->EditCells(L"Change Orientation", rotate,
                                    DocumentController::CellReload::CameraAndRenderer);
        }
        ReloadReadouts();
    }

    void CellDetailView::OnRotateClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        int axis = -1;
        double sign = 1.0;
        if (sender == RotatePlusX()) { axis = 0; }
        else if (sender == RotatePlusY()) { axis = 1; }
        else if (sender == RotatePlusZ()) { axis = 2; }
        else if (sender == RotateMinusX()) { axis = 0; sign = -1.0; }
        else if (sender == RotateMinusY()) { axis = 1; sign = -1.0; }
        else if (sender == RotateMinusZ()) { axis = 2; sign = -1.0; }
        if (axis < 0)
            return;

        // Cocoa turns the object by the rotation angle about the axis the button
        // sits on, on top of the orientation it already has.
        m_controller->EditCells(L"Rotate Object", [axis, sign](Object& object)
        {
            const double angle = sign * object.rotationDelta() * M_PI / 180.0;
            const double3 axisVector(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0);
            const simd_quatd delta = simd_quatd::fromAxisAngle(angle, axisVector);
            object.setOrientation((delta * object.orientation()).normalized());
            object.reComputeBoundingBox();
        }, DocumentController::CellReload::CameraAndRenderer);
        ReloadReadouts();
    }

    void CellDetailView::OnOriginChanged(NumberBox const& sender,
                                         NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        const int axis = AxisOf(sender, m_originBoxes);
        if (axis < 0)
            return;
        m_controller->EditCells(L"Change Origin", [axis, value](Object& object)
        {
            if (axis == 0) object.setOriginX(value);
            if (axis == 1) object.setOriginY(value);
            if (axis == 2) object.setOriginZ(value);
        }, DocumentController::CellReload::CameraAndRenderer);
    }

    void CellDetailView::OnFlipToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto check = sender.try_as<CheckBox>();
        if (!check || !check.IsChecked())
            return;
        int axis = -1;
        for (int i = 0; i < 3; ++i)
            if (m_flipChecks[i] == check)
                axis = i;
        if (axis < 0)
            return;
        // The box carried the mixed state until this click resolved it.
        DetailControls::ResolveCheck(check);
        const bool value = check.IsChecked().Value();
        m_controller->EditCells(L"Change Content Flip", [axis, value](Object& object)
        {
            auto cell = object.cell();
            if (!cell)
                return;
            if (axis == 0) cell->setContentFlipX(value);
            if (axis == 1) cell->setContentFlipY(value);
            if (axis == 2) cell->setContentFlipZ(value);
        }, DocumentController::CellReload::Renderer);
    }

    void CellDetailView::OnShiftChanged(NumberBox const& sender,
                                        NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        const int axis = AxisOf(sender, m_shiftBoxes);
        if (axis < 0)
            return;
        m_controller->EditCells(L"Change Content Shift", [axis, value](Object& object)
        {
            auto cell = object.cell();
            if (!cell)
                return;
            if (axis == 0) cell->setContentShiftX(value);
            if (axis == 1) cell->setContentShiftY(value);
            if (axis == 2) cell->setContentShiftZ(value);
        }, DocumentController::CellReload::Renderer);
    }

    void CellDetailView::OnApplyContentShift(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ApplyCellContentShift();
        Reload();
    }

    void CellDetailView::OnMaterialSelectionChanged(IInspectable const& sender,
                                                    SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo)
            return;
        const int index = combo.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(std::size(kMaterialTypes)))
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        const RKString value = RKString::fromStdWString(kMaterialTypes[index]);
        m_controller->EditCells(L"Change Structural Material Type", [&value](Object& object)
        {
            if (auto editor = dynamic_cast<StructuralPropertyEditor*>(&object))
                editor->setStructureMaterialType(value);
        }, DocumentController::CellReload::None);
        // Cocoa applies the force field suggested by the material type.
        ApplyStructureForceField(ForceFieldSets::suggestedDisplayName(value));
        Reload();
    }

    void CellDetailView::OnMaterialTextSubmitted(ComboBox const& sender,
                                                 ComboBoxTextSubmittedEventArgs const& e)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        // The words standing for a mixed selection are not a material name.
        if (e.Text() == DetailControls::MultipleValuesText())
            return;
        const RKString value = RKString::fromStdWString(std::wstring(e.Text()));
        m_controller->EditCells(L"Change Structural Material Type", [&value](Object& object)
        {
            if (auto editor = dynamic_cast<StructuralPropertyEditor*>(&object))
                editor->setStructureMaterialType(value);
        }, DocumentController::CellReload::None);
        ApplyStructureForceField(ForceFieldSets::suggestedDisplayName(value));
        Reload();
    }

    void CellDetailView::OnForceFieldChanged(IInspectable const& sender,
                                             SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        if (!m_controller->Document())
            return;
        auto const& sets = m_controller->Document()->forceFieldSets().forceFieldSets();
        const int index = combo.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(sets.size()))
            return;
        ApplyStructureForceField(sets[static_cast<size_t>(index)].displayName());
    }

    void CellDetailView::ApplyStructureForceField(RKString const& name)
    {
        if (!m_controller || !m_controller->Document())
            return;
        auto& forceFieldSets = m_controller->Document()->forceFieldSets();
        for (auto const& target : m_controller->TargetStructures())
        {
            auto editor = target ? std::dynamic_pointer_cast<AtomStructureEditor>(target->object()) : nullptr;
            if (!editor)
                continue;
            editor->setAtomForceFieldIdentifier(name, forceFieldSets);
            editor->recheckRepresentationStyle();
        }
        // Cocoa invalidates the energy surface: Lennard-Jones parameters drive the grid.
        m_controller->ReloadRendererInvalidatingIsosurfaces();
    }

    void CellDetailView::OnStructuralChanged(NumberBox const& sender,
                                             NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        auto field = std::find_if(m_structuralFields.begin(), m_structuralFields.end(),
                                  [&sender](StructuralField const& f) { return f.box == sender; });
        if (field == m_structuralFields.end())
            return;
        auto write = field->write;
        m_controller->EditCells(L"Change Structural Property", [&write, value](Object& object)
        {
            if (auto editor = dynamic_cast<StructuralPropertyEditor*>(&object))
                write(*editor, value);
        }, DocumentController::CellReload::None);
    }

    void CellDetailView::OnProbeMoleculeChanged(IInspectable const& sender,
                                                SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        const int index = combo.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(kSelectableProbeMoleculeCount))
            return;
        const auto probe = kSelectableProbeMolecules[index];
        m_controller->EditCells(L"Change Probe Molecule", [probe](Object& object)
        {
            if (auto editor = dynamic_cast<StructuralPropertyEditor*>(&object))
                editor->applyFrameworkProbeMolecule(probe);
        }, DocumentController::CellReload::None);
        Reload();
    }

    void CellDetailView::OnProbeParameterChanged(NumberBox const& sender,
                                                 NumberBoxValueChangedEventArgs const& e)
    {
        // Same path as the other structural NumberBoxes; regenerates the probe enum from ε/σ.
        OnStructuralChanged(sender, e);
    }

    void CellDetailView::OnComputeVoidFraction(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ComputeHeliumVoidFraction();
        Reload();
    }

    void CellDetailView::OnComputeSurfaceArea(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ComputeNitrogenSurfaceArea();
        Reload();
    }

    void CellDetailView::OnComputeWellSurfaceArea(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ComputeWellSurfaceArea();
        Reload();
    }

    void CellDetailView::OnComputeGeometricSurfaceArea(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ComputeGeometricSurfaceArea();
        Reload();
    }

    void CellDetailView::OnComputeVanDerWaalsGeometricSurfaceArea(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        m_controller->ComputeVanDerWaalsGeometricSurfaceArea();
        Reload();
    }

    void CellDetailView::OnLoadBlockingPockets(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        // The picker runs on the window; the rows follow once it has read a file.
        m_controller->LoadBlockingPocketsFile();
    }

    void CellDetailView::OnSpaceGroupNumberChanged(IInspectable const& sender,
                                                   SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        // The first setting of the number, which is what Cocoa's popup selects.
        const int number = combo.SelectedIndex() + 1;
        if (number >= static_cast<int>(SKSpaceGroupDataBase::spaceGroupHallData.size()))
            return;
        m_controller->SetSpaceGroupHallNumber(SKSpaceGroupDataBase::spaceGroupHallData[number].front());
        Reload();
    }

    void CellDetailView::OnQualifierChanged(IInspectable const& sender,
                                            SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        auto object = FirstObject();
        auto viewer = std::dynamic_pointer_cast<SpaceGroupViewer>(object);
        if (!viewer)
            return;
        const int number = static_cast<int>(viewer->spaceGroup().spaceGroupSetting().number());
        if (number < 1 || number >= static_cast<int>(SKSpaceGroupDataBase::spaceGroupHallData.size()))
            return;
        auto const& halls = SKSpaceGroupDataBase::spaceGroupHallData[number];
        const int index = combo.SelectedIndex();
        if (index >= static_cast<int>(halls.size()))
            return;
        m_controller->SetSpaceGroupHallNumber(halls[index]);
        Reload();
    }

    void CellDetailView::OnHallNumberChanged(IInspectable const& sender,
                                             SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        m_controller->SetSpaceGroupHallNumber(combo.SelectedIndex() + 1);
        Reload();
    }

    void CellDetailView::OnPrecisionChanged(NumberBox const& sender,
                                            NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller || !sender)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value) || value <= 0.0)
            return;
        m_controller->EditCells(L"Change Precision", [value](Object& object)
        {
            if (auto cell = object.cell())
                cell->setPrecision(value);
        }, DocumentController::CellReload::None);
    }
}
