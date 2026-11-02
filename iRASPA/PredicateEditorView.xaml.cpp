#include "pch.h"
#include "PredicateEditorView.xaml.h"
#if __has_include("PredicateEditorView.g.cpp")
#include "PredicateEditorView.g.cpp"
#endif

#include <algorithm>
#include <cmath>
#include <cwctype>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        constexpr wchar_t const* kCompoundLabels[] = {
            L"Any",
            L"All",
            L"None",
        };

        // Exact Cocoa NSPredicateEditor row templates from DirectoryViewer.storyboard:
        constexpr wchar_t const* kPropertyLabels[] = {
            L"Volumetric nitrogen surface area",
            L"Geometric nitrogen surface area",
            L"Helium void fraction",
            L"Largest cavity diameter",
            L"Restricting pore diameter",
            L"Largest diameter along path",
            L"Framework density",
            L"Framework mass",
            L"Framework specific volume",
            L"Framework accessible volume",
            L"Number of channels",
            L"Number of pockets",
            L"Dimensionality",
            L"Type",
        };

        constexpr wchar_t const* kNumericOperatorLabels[] = {
            L"is",
            L"is not",
            L"is greater than",
            L"is less than or equal to",
            L"is greater than or equal to",
            L"is less than",
        };

        constexpr wchar_t const* kStringOperatorLabels[] = {
            L"matches",
            L"is like",
            L"contains",
        };

        std::wstring ToLower(std::wstring s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return s;
        }

        // Case-insensitive wildcard match ('*' and '?') for NSPredicate LIKE[c].
        bool WildcardMatch(std::wstring const& text, std::wstring const& pattern)
        {
            const std::wstring s = ToLower(text);
            const std::wstring p = ToLower(pattern);
            size_t si = 0, pi = 0;
            size_t starIdx = std::wstring::npos, matchIdx = 0;
            while (si < s.size())
            {
                if (pi < p.size() && (p[pi] == L'?' || p[pi] == s[si]))
                {
                    ++si;
                    ++pi;
                }
                else if (pi < p.size() && p[pi] == L'*')
                {
                    starIdx = pi++;
                    matchIdx = si;
                }
                else if (starIdx != std::wstring::npos)
                {
                    pi = starIdx + 1;
                    si = ++matchIdx;
                }
                else
                {
                    return false;
                }
            }
            while (pi < p.size() && p[pi] == L'*')
                ++pi;
            return pi == p.size();
        }
    }

    PredicateEditorView::PredicateEditorView()
    {
        InitializeComponent();
        ResetToDefaultPredicate();
    }

    void PredicateEditorView::SetPredicateChangedCallback(std::function<void()> callback)
    {
        m_onChanged = std::move(callback);
    }

    // Cocoa PredicateEditorViewController.viewDidLoad default predicate:
    //   AND(heliumVoidFraction >= 0.0,
    //       gravimetricSurfaceArea >= 0.0,
    //       restrictingPoreDiameter >= 0.0)
    void PredicateEditorView::ResetToDefaultPredicate()
    {
        m_root = std::make_shared<PredicateNode>();
        m_root->isCompound = true;
        m_root->compoundType = CompoundType::All;

        auto makeRule = [&](PropertyKey key, NumericOperator op, double val, wchar_t const* text)
        {
            auto child = std::make_shared<PredicateNode>();
            child->isCompound = false;
            child->propertyKey = key;
            child->numericOp = op;
            child->numericValue = val;
            child->valueText = text;
            child->parent = m_root;
            m_root->children.push_back(child);
        };

        makeRule(PropertyKey::HeliumVoidFraction, NumericOperator::GreaterThanOrEqual, 0.0, L"0.0");
        makeRule(PropertyKey::GravimetricSurfaceArea, NumericOperator::GreaterThanOrEqual, 0.0, L"0.0");
        makeRule(PropertyKey::RestrictingPoreDiameter, NumericOperator::GreaterThanOrEqual, 0.0, L"0.0");

        RebuildRowsUI();
    }

    bool PredicateEditorView::Evaluate(std::shared_ptr<iRASPAProject> const& project) const
    {
        if (!project)
            return false;
        project->ensureCachedPropertiesLoaded();
        if (!m_root)
            return true;
        return EvaluateNode(m_root, project);
    }

    bool PredicateEditorView::EvaluateNode(std::shared_ptr<PredicateNode> const& node,
                                           std::shared_ptr<iRASPAProject> const& project)
    {
        if (!node || !project)
            return true;

        if (node->isCompound)
        {
            if (node->children.empty())
                return true;

            switch (node->compoundType)
            {
            case CompoundType::All:
                for (auto const& child : node->children)
                {
                    if (!EvaluateNode(child, project))
                        return false;
                }
                return true;

            case CompoundType::Any:
                for (auto const& child : node->children)
                {
                    if (EvaluateNode(child, project))
                        return true;
                }
                return false;

            case CompoundType::None:
                for (auto const& child : node->children)
                {
                    if (EvaluateNode(child, project))
                        return false;
                }
                return true;
            }
            return true;
        }

        if (node->IsStringProperty())
        {
            const std::wstring target = project->materialType().toStdWString();
            const std::wstring query = node->valueText;
            switch (node->stringOp)
            {
            case StringOperator::Matches:
                return ToLower(target) == ToLower(query);
            case StringOperator::IsLike:
                if (query.find_first_of(L"*?") != std::wstring::npos)
                    return WildcardMatch(target, query);
                return ToLower(target) == ToLower(query);
            case StringOperator::Contains:
                if (query.empty())
                    return true;
                return ToLower(target).find(ToLower(query)) != std::wstring::npos;
            }
            return true;
        }

        double lhs = 0.0;
        switch (node->propertyKey)
        {
        case PropertyKey::VolumetricSurfaceArea:
            lhs = project->volumetricSurfaceArea();
            break;
        case PropertyKey::GravimetricSurfaceArea:
            lhs = project->gravimetricSurfaceArea();
            break;
        case PropertyKey::HeliumVoidFraction:
            lhs = project->heliumVoidFraction();
            break;
        case PropertyKey::LargestOverallCavityDiameter:
            lhs = project->largestOverallCavityDiameter();
            break;
        case PropertyKey::RestrictingPoreDiameter:
            lhs = project->restrictingPoreDiameter();
            break;
        case PropertyKey::LargestDiameterAlongViablePath:
            lhs = project->largestDiameterAlongViablePath();
            break;
        case PropertyKey::Density:
            lhs = project->density();
            break;
        case PropertyKey::Mass:
            lhs = project->mass();
            break;
        case PropertyKey::SpecificVolume:
            lhs = project->specificVolume();
            break;
        case PropertyKey::AccessiblePoreVolume:
            lhs = project->accessiblePoreVolume();
            break;
        case PropertyKey::NumberOfChannelSystems:
            lhs = static_cast<double>(project->numberOfChannelSystems());
            break;
        case PropertyKey::NumberOfInaccessiblePockets:
            lhs = static_cast<double>(project->numberOfInaccesiblePockets());
            break;
        case PropertyKey::DimensionalityPoreSystem:
            lhs = static_cast<double>(project->dimensionalityPoreSystem());
            break;
        default:
            break;
        }

        const double rhs = node->numericValue;
        constexpr double kEps = 1e-9;
        switch (node->numericOp)
        {
        case NumericOperator::Equal:
            return std::abs(lhs - rhs) <= kEps;
        case NumericOperator::NotEqual:
            return std::abs(lhs - rhs) > kEps;
        case NumericOperator::GreaterThan:
            return lhs > rhs;
        case NumericOperator::LessThanOrEqual:
            return lhs <= rhs;
        case NumericOperator::GreaterThanOrEqual:
            return lhs >= rhs;
        case NumericOperator::LessThan:
            return lhs < rhs;
        }
        return true;
    }

    void PredicateEditorView::NotifyPredicateChanged()
    {
        if (!m_updatingUI && m_onChanged)
            m_onChanged();
    }

    void PredicateEditorView::RebuildRowsUI()
    {
        m_updatingUI = true;
        auto panel = RowsPanel();
        panel.Children().Clear();

        if (m_root)
        {
            std::function<void(std::shared_ptr<PredicateNode> const&, int32_t, bool)> visit =
                [&](std::shared_ptr<PredicateNode> const& node, int32_t depth, bool canRemove)
            {
                panel.Children().Append(BuildRowElement(node, depth, canRemove));
                if (node->isCompound)
                {
                    const bool childrenRemovable = node->children.size() > 1 || depth > 0;
                    for (auto const& child : node->children)
                        visit(child, depth + 1, childrenRemovable);
                }
            };
            visit(m_root, 0, /*canRemove=*/false);
        }
        m_updatingUI = false;
    }

    UIElement PredicateEditorView::BuildRowElement(
        std::shared_ptr<PredicateNode> const& node,
        int32_t depth,
        bool canRemove)
    {
        Grid row;
        row.Padding(Thickness{ static_cast<double>(depth * 14), 2.0, 2.0, 2.0 });

        ColumnDefinition contentCol;
        contentCol.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        ColumnDefinition buttonsCol;
        buttonsCol.Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(contentCol);
        row.ColumnDefinitions().Append(buttonsCol);

        StackPanel left;
        left.Orientation(Orientation::Horizontal);
        left.Spacing(4.0);
        left.VerticalAlignment(VerticalAlignment::Center);

        if (node->isCompound)
        {
            ComboBox compoundCombo;
            compoundCombo.MinWidth(72.0);
            compoundCombo.Padding(Thickness{ 6, 2, 6, 2 });
            compoundCombo.FontSize(12.0);
            compoundCombo.IsTextScaleFactorEnabled(false);
            for (auto const* label : kCompoundLabels)
                compoundCombo.Items().Append(box_value(label));
            compoundCombo.SelectedIndex(static_cast<int32_t>(node->compoundType));
            compoundCombo.SelectionChanged(
                [this, node](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    if (m_updatingUI)
                        return;
                    if (auto cb = sender.try_as<ComboBox>())
                    {
                        const int32_t idx = cb.SelectedIndex();
                        if (idx >= 0 && idx <= 2)
                        {
                            node->compoundType = static_cast<CompoundType>(idx);
                            NotifyPredicateChanged();
                        }
                    }
                });
            left.Children().Append(compoundCombo);

            TextBlock label;
            label.Text(L"of the following are true");
            label.FontSize(12.0);
            label.IsTextScaleFactorEnabled(false);
            label.VerticalAlignment(VerticalAlignment::Center);
            left.Children().Append(label);
        }
        else
        {
            ComboBox propCombo;
            propCombo.MinWidth(175.0);
            propCombo.MaxWidth(210.0);
            propCombo.Padding(Thickness{ 6, 2, 6, 2 });
            propCombo.FontSize(11.5);
            propCombo.IsTextScaleFactorEnabled(false);
            for (auto const* label : kPropertyLabels)
                propCombo.Items().Append(box_value(label));
            propCombo.SelectedIndex(static_cast<int32_t>(node->propertyKey));
            propCombo.SelectionChanged(
                [this, node](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    if (m_updatingUI)
                        return;
                    if (auto cb = sender.try_as<ComboBox>())
                    {
                        const int32_t idx = cb.SelectedIndex();
                        if (idx >= 0 && idx <= 13)
                        {
                            const bool wasString = node->IsStringProperty();
                            node->propertyKey = static_cast<PropertyKey>(idx);
                            const bool nowString = node->IsStringProperty();
                            if (wasString != nowString)
                            {
                                if (nowString)
                                {
                                    node->stringOp = StringOperator::Matches;
                                    node->valueText = L"Zeolite";
                                }
                                else
                                {
                                    node->numericOp = NumericOperator::GreaterThanOrEqual;
                                    node->numericValue = 0.0;
                                    node->valueText = L"0.0";
                                }
                                RebuildRowsUI();
                            }
                            NotifyPredicateChanged();
                        }
                    }
                });
            left.Children().Append(propCombo);

            ComboBox opCombo;
            opCombo.MinWidth(135.0);
            opCombo.MaxWidth(170.0);
            opCombo.Padding(Thickness{ 6, 2, 6, 2 });
            opCombo.FontSize(11.5);
            opCombo.IsTextScaleFactorEnabled(false);
            if (node->IsStringProperty())
            {
                for (auto const* label : kStringOperatorLabels)
                    opCombo.Items().Append(box_value(label));
                opCombo.SelectedIndex(static_cast<int32_t>(node->stringOp));
            }
            else
            {
                for (auto const* label : kNumericOperatorLabels)
                    opCombo.Items().Append(box_value(label));
                opCombo.SelectedIndex(static_cast<int32_t>(node->numericOp));
            }
            opCombo.SelectionChanged(
                [this, node](IInspectable const& sender, SelectionChangedEventArgs const&)
                {
                    if (m_updatingUI)
                        return;
                    if (auto cb = sender.try_as<ComboBox>())
                    {
                        const int32_t idx = cb.SelectedIndex();
                        if (node->IsStringProperty())
                        {
                            if (idx >= 0 && idx <= 2)
                            {
                                node->stringOp = static_cast<StringOperator>(idx);
                                NotifyPredicateChanged();
                            }
                        }
                        else
                        {
                            if (idx >= 0 && idx <= 5)
                            {
                                node->numericOp = static_cast<NumericOperator>(idx);
                                NotifyPredicateChanged();
                            }
                        }
                    }
                });
            left.Children().Append(opCombo);

            TextBox valueBox;
            valueBox.MinWidth(56.0);
            valueBox.Width(68.0);
            valueBox.MinHeight(0.0);
            valueBox.Padding(Thickness{ 4, 2, 4, 2 });
            valueBox.FontSize(11.5);
            valueBox.IsTextScaleFactorEnabled(false);
            valueBox.Text(node->valueText);
            valueBox.TextChanged(
                [this, node](IInspectable const& sender, TextChangedEventArgs const&)
                {
                    if (m_updatingUI)
                        return;
                    if (auto tb = sender.try_as<TextBox>())
                    {
                        node->valueText = std::wstring(tb.Text());
                        if (!node->IsStringProperty())
                        {
                            try
                            {
                                size_t pos = 0;
                                const double v = std::stod(node->valueText, &pos);
                                if (std::isfinite(v))
                                    node->numericValue = v;
                            }
                            catch (...)
                            {
                                node->numericValue = 0.0;
                            }
                        }
                        NotifyPredicateChanged();
                    }
                });
            left.Children().Append(valueBox);
        }

        Grid::SetColumn(left, 0);
        row.Children().Append(left);

        StackPanel right;
        right.Orientation(Orientation::Horizontal);
        right.Spacing(2.0);
        right.VerticalAlignment(VerticalAlignment::Center);

        Button removeBtn;
        removeBtn.Content(box_value(L"\u2212")); // minus sign
        removeBtn.Width(24.0);
        removeBtn.Height(24.0);
        removeBtn.Padding(Thickness{ 0, 0, 0, 0 });
        removeBtn.FontSize(12.0);
        removeBtn.IsTextScaleFactorEnabled(false);
        removeBtn.IsEnabled(canRemove);
        ToolTipService::SetToolTip(removeBtn, box_value(L"Remove row"));
        removeBtn.Click(
            [this, node](IInspectable const&, RoutedEventArgs const&)
            {
                if (auto parent = node->parent.lock())
                {
                    auto& siblings = parent->children;
                    auto it = std::find(siblings.begin(), siblings.end(), node);
                    if (it != siblings.end())
                    {
                        siblings.erase(it);
                        RebuildRowsUI();
                        NotifyPredicateChanged();
                    }
                }
            });
        right.Children().Append(removeBtn);

        Button addBtn;
        addBtn.Content(box_value(L"+"));
        addBtn.Width(24.0);
        addBtn.Height(24.0);
        addBtn.Padding(Thickness{ 0, 0, 0, 0 });
        addBtn.FontSize(12.0);
        addBtn.IsTextScaleFactorEnabled(false);
        ToolTipService::SetToolTip(addBtn, box_value(L"Add row"));
        addBtn.Click(
            [this, node](IInspectable const&, RoutedEventArgs const&)
            {
                auto newRule = std::make_shared<PredicateNode>();
                newRule->isCompound = false;
                newRule->propertyKey = PropertyKey::HeliumVoidFraction;
                newRule->numericOp = NumericOperator::GreaterThanOrEqual;
                newRule->numericValue = 0.0;
                newRule->valueText = L"0.0";

                if (node->isCompound)
                {
                    newRule->parent = node;
                    node->children.insert(node->children.begin(), newRule);
                }
                else if (auto parent = node->parent.lock())
                {
                    newRule->parent = parent;
                    auto& siblings = parent->children;
                    auto it = std::find(siblings.begin(), siblings.end(), node);
                    if (it != siblings.end())
                        siblings.insert(it + 1, newRule);
                    else
                        siblings.push_back(newRule);
                }

                RebuildRowsUI();
                NotifyPredicateChanged();
            });
        right.Children().Append(addBtn);

        Button compoundBtn;
        compoundBtn.Content(box_value(L"\u22EF")); // midline horizontal ellipsis
        compoundBtn.Width(24.0);
        compoundBtn.Height(24.0);
        compoundBtn.Padding(Thickness{ 0, 0, 0, 0 });
        compoundBtn.FontSize(11.0);
        compoundBtn.IsTextScaleFactorEnabled(false);
        ToolTipService::SetToolTip(compoundBtn, box_value(L"Add compound predicate (Any / All / None)"));
        compoundBtn.Click(
            [this, node](IInspectable const&, RoutedEventArgs const&)
            {
                auto group = std::make_shared<PredicateNode>();
                group->isCompound = true;
                group->compoundType = CompoundType::All;

                auto innerRule = std::make_shared<PredicateNode>();
                innerRule->isCompound = false;
                innerRule->propertyKey = PropertyKey::HeliumVoidFraction;
                innerRule->numericOp = NumericOperator::GreaterThanOrEqual;
                innerRule->numericValue = 0.0;
                innerRule->valueText = L"0.0";
                innerRule->parent = group;
                group->children.push_back(innerRule);

                if (node->isCompound)
                {
                    group->parent = node;
                    node->children.push_back(group);
                }
                else if (auto parent = node->parent.lock())
                {
                    group->parent = parent;
                    auto& siblings = parent->children;
                    auto it = std::find(siblings.begin(), siblings.end(), node);
                    if (it != siblings.end())
                        siblings.insert(it + 1, group);
                    else
                        siblings.push_back(group);
                }

                RebuildRowsUI();
                NotifyPredicateChanged();
            });
        right.Children().Append(compoundBtn);

        Grid::SetColumn(right, 1);
        row.Children().Append(right);

        return row;
    }
}
