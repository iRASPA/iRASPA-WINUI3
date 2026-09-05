#pragma once

#include "CellDetailView.g.h"
#include "DetailControls.h"
#include "DocumentController.h"

#include "structuralpropertyviewer.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // The Cell tab of the inspector. The form is CellDetailView.xaml; this fills
    // it from the first selected object and writes every edit through to all of
    // them, via the document controller.
    struct CellDetailView : CellDetailViewT<CellDetailView>, CellPanePresenter
    {
        CellDetailView();

        void SetController(DocumentController* controller) { m_controller = controller; }

        // CellPanePresenter: also how the pane catches up after an undo.
        void Reload() override;

        void OnStructureTypeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnUnitCellChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                               winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnReplicaChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                              winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnRotationDeltaChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnEulerBoxChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                               winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnEulerSliderChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e);
        void OnRotateClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnOriginChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnFlipToggled(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnShiftChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                            winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnApplyContentShift(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnMaterialSelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnMaterialTextSubmitted(winrt::Microsoft::UI::Xaml::Controls::ComboBox const& sender,
                                     winrt::Microsoft::UI::Xaml::Controls::ComboBoxTextSubmittedEventArgs const& e);
        void OnForceFieldChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnStructuralChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnProbeMoleculeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnProbeParameterChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                     winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnComputeVoidFraction(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnComputeSurfaceArea(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnComputeWellSurfaceArea(winrt::Windows::Foundation::IInspectable const& sender,
                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnComputeGeometricSurfaceArea(winrt::Windows::Foundation::IInspectable const& sender,
                                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnComputeVanDerWaalsGeometricSurfaceArea(winrt::Windows::Foundation::IInspectable const& sender,
                                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnLoadBlockingPockets(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSpaceGroupNumberChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                       winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnQualifierChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnHallNumberChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnPrecisionChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);

    private:
        // One structural-property row: which value of the StructuralPropertyEditor
        // interface the box shows, and how to write it back. Keeps the thirteen
        // rows to one handler and one reload loop.
        struct StructuralField
        {
            winrt::Microsoft::UI::Xaml::Controls::NumberBox box{ nullptr };
            std::function<double(StructuralPropertyViewer&)> read;
            std::function<void(StructuralPropertyEditor&, double)> write;
        };

        void BuildStructureTypeItems();
        void BuildStaticComboItems();
        void BuildFieldTables();
        // Cocoa's number formatters show a fixed number of decimals.
        void ConfigureNumber(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& box,
                             double minimum, double maximum, double step, int decimals);

        void ReloadBox();
        // The read-only values, which follow every edit rather than being edited.
        void ReloadReadouts();
        void ReloadTransform();
        void ReloadStructural();
        void ReloadSymmetry();
        void ReloadBlockingPockets();
        // Cocoa applyStructureForceField(named:): LJ set for voids / areas / PES.
        void ApplyStructureForceField(RKString const& name);
        // Flip checkboxes live in a collapsed Expander; re-push visuals after
        // Reload and when a section opens (same WinUI Indeterminate stickiness
        // as Appearance).
        void RefreshAllCheckVisuals();
        void WireSectionExpanders();

        // Every row asks the selection as a whole, and shows "Multiple Values"
        // when the structures do not agree. The cell and symmetry rows concern
        // only the structures that have a cell or a space group.
        template <class Read>
        auto AgreedObject(Read const& read) const
            -> std::optional<std::decay_t<decltype(read(*std::declval<Object*>()))>>
        {
            if (!m_controller)
                return std::nullopt;
            return m_controller->AgreedValue<Object>(
                [&read](std::shared_ptr<Object> const& object) { return read(*object); });
        }

        template <class Read>
        auto AgreedCell(Read const& read) const
            -> std::optional<std::decay_t<decltype(read(*std::declval<SKCell*>()))>>
        {
            using Value = std::decay_t<decltype(read(*std::declval<SKCell*>()))>;
            if (!m_controller)
                return std::nullopt;
            return m_controller->AgreedPartialValue<Object>(
                [&read](std::shared_ptr<Object> const& object) -> std::optional<Value>
                {
                    auto cell = object->cell();
                    if (!cell)
                        return std::nullopt;
                    return read(*cell);
                });
        }

        // The object the form is showing: the first of the selection, as Cocoa's
        // detail views do.
        std::shared_ptr<Object> FirstObject() const;
        int AxisOf(winrt::Windows::Foundation::IInspectable const& sender,
                   std::array<winrt::Microsoft::UI::Xaml::Controls::NumberBox, 3> const& boxes) const;

        DocumentController* m_controller{ nullptr };
        // Set while the form is being filled in, so writing a value back into a
        // control does not look like the user editing it.
        bool m_suppressEvents{ false };

        std::array<winrt::Microsoft::UI::Xaml::Controls::NumberBox, 3> m_eulerBoxes{ nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::Slider, 3> m_eulerSliders{ nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::NumberBox, 3> m_originBoxes{ nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::NumberBox, 3> m_shiftBoxes{ nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::CheckBox, 3> m_flipChecks{ nullptr, nullptr, nullptr };
        std::vector<StructuralField> m_structuralFields;

        // A slider drag emits a stream of values, so the whole drag becomes one
        // undo entry: the state is captured when it starts and registered when
        // the slider loses the pointer.
        std::vector<DocumentController::CellUndoState> m_sliderBefore;
        bool m_sliderDragging{ false };
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct CellDetailView : CellDetailViewT<CellDetailView, implementation::CellDetailView>
    {
    };
}
