#pragma once

#include "PredicateEditorView.g.h"
#include "iraspaproject.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa PredicateEditorViewController (DirectoryViewer.storyboard):
    // NSPredicateEditor over the 14 cached structural properties on iRASPAProject.
    struct PredicateEditorView : PredicateEditorViewT<PredicateEditorView>
    {
        enum class CompoundType : int32_t
        {
            Any = 0,  // OR
            All = 1,  // AND
            None = 2, // NOT (OR)
        };

        enum class PropertyKey : int32_t
        {
            VolumetricSurfaceArea = 0,
            GravimetricSurfaceArea = 1,
            HeliumVoidFraction = 2,
            LargestOverallCavityDiameter = 3,
            RestrictingPoreDiameter = 4,
            LargestDiameterAlongViablePath = 5,
            Density = 6,
            Mass = 7,
            SpecificVolume = 8,
            AccessiblePoreVolume = 9,
            NumberOfChannelSystems = 10,
            NumberOfInaccessiblePockets = 11,
            DimensionalityPoreSystem = 12,
            MaterialType = 13,
        };

        enum class NumericOperator : int32_t
        {
            Equal = 0,              // "is"
            NotEqual = 1,           // "is not"
            GreaterThan = 2,        // "is greater than"
            LessThanOrEqual = 3,    // "is less than or equal to"
            GreaterThanOrEqual = 4, // "is greater than or equal to"
            LessThan = 5,           // "is less than"
        };

        enum class StringOperator : int32_t
        {
            Matches = 0,  // "matches"
            IsLike = 1,   // "is like"
            Contains = 2, // "contains"
        };

        struct PredicateNode : std::enable_shared_from_this<PredicateNode>
        {
            bool isCompound = false;
            CompoundType compoundType = CompoundType::All;
            PropertyKey propertyKey = PropertyKey::HeliumVoidFraction;
            NumericOperator numericOp = NumericOperator::GreaterThanOrEqual;
            StringOperator stringOp = StringOperator::Matches;
            double numericValue = 0.0;
            std::wstring valueText = L"0.0";
            std::weak_ptr<PredicateNode> parent;
            std::vector<std::shared_ptr<PredicateNode>> children;

            bool IsStringProperty() const
            {
                return propertyKey == PropertyKey::MaterialType;
            }
        };

        PredicateEditorView();

        void SetPredicateChangedCallback(std::function<void()> callback);
        void ResetToDefaultPredicate();
        bool Evaluate(std::shared_ptr<iRASPAProject> const& project) const;

    private:
        void RebuildRowsUI();
        winrt::Microsoft::UI::Xaml::UIElement BuildRowElement(
            std::shared_ptr<PredicateNode> const& node,
            int32_t depth,
            bool canRemove);
        static bool EvaluateNode(std::shared_ptr<PredicateNode> const& node,
                                 std::shared_ptr<iRASPAProject> const& project);
        void NotifyPredicateChanged();

        std::shared_ptr<PredicateNode> m_root;
        std::function<void()> m_onChanged;
        bool m_updatingUI = false;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct PredicateEditorView : PredicateEditorViewT<PredicateEditorView, implementation::PredicateEditorView>
    {
    };
}
