#include "pch.h"
#include "AtomsDetailView.xaml.h"
#if __has_include("AtomsDetailView.g.cpp")
#include "AtomsDetailView.g.cpp"
#endif

#include "DetailControls.h"
#include "iraspaobject.h"
#include "proteinribbonsegmentsupport.h"
#include "skasymmetricatom.h"
#include "skatomtreecontroller.h"
#include "skatomtreenode.h"
#include "skelement.h"
#include "spacegroupviewer.h"
#include "structure.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include "rkstring.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <optional>
#include <set>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        hstring FormatFixed(double v)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"%.3f", v);
            return hstring(buf);
        }

        std::optional<double> ParseDouble(hstring const& text)
        {
            std::wstring s(text);
            try
            {
                size_t consumed = 0;
                const double v = std::stod(s, &consumed);
                while (consumed < s.size() && iswspace(s[consumed]))
                    ++consumed;
                if (consumed != s.size())
                    return std::nullopt;
                return v;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        hstring ElementSymbolFor(std::shared_ptr<SKAsymmetricAtom> const& atom)
        {
            if (!atom)
                return L"";
            const auto z = static_cast<size_t>(atom->elementIdentifier());
            if (z < PredefinedElements::predefinedElements.size())
                return hstring(PredefinedElements::predefinedElements[z]._chemicalSymbol.toStdWString());
            return L"?";
        }

        std::optional<int> AtomicNumberFor(hstring const& text)
        {
            RKString symbol = RKString::fromStdWString(std::wstring(text)).trimmed();
            auto it = PredefinedElements::atomicNumberData.find(symbol);
            if (it != PredefinedElements::atomicNumberData.end())
                return it->second;
            for (size_t z = 0; z < PredefinedElements::predefinedElements.size(); ++z)
            {
                if (PredefinedElements::predefinedElements[z]._chemicalSymbol.toLower() ==
                        symbol.toLower())
                    return static_cast<int>(z);
            }
            return std::nullopt;
        }
    }

    // {Binding} property backing for AtomFlatItem (a DataTemplate cannot use
    // x:Bind against a plain C++ class, which has no binding metadata).
    struct AtomItemProperty : implements<AtomItemProperty, ICustomProperty>
    {
        using Getter = std::function<IInspectable(IInspectable const&)>;
        using Setter = std::function<void(IInspectable const&, IInspectable const&)>;

        AtomItemProperty(hstring name, winrt::Windows::UI::Xaml::Interop::TypeName type,
                         Getter getter, Setter setter = nullptr)
            : m_name(std::move(name)), m_type(type),
              m_getter(std::move(getter)), m_setter(std::move(setter)) {}

        winrt::Windows::UI::Xaml::Interop::TypeName Type() const { return m_type; }
        hstring Name() const { return m_name; }
        bool CanRead() const { return true; }
        bool CanWrite() const { return static_cast<bool>(m_setter); }
        IInspectable GetValue(IInspectable const& target) const { return m_getter(target); }
        void SetValue(IInspectable const& target, IInspectable const& value) const
        {
            if (m_setter)
                m_setter(target, value);
            else
                throw hresult_not_implemented();
        }
        IInspectable GetIndexedValue(IInspectable const&, IInspectable const&) const { throw hresult_not_implemented(); }
        void SetIndexedValue(IInspectable const&, IInspectable const&, IInspectable const&) const { throw hresult_not_implemented(); }

    private:
        hstring m_name;
        winrt::Windows::UI::Xaml::Interop::TypeName m_type;
        Getter m_getter;
        Setter m_setter;
    };

    // Flat row view model for the virtualized atom list. The nested
    // SKAtomTreeNode hierarchy is flattened into one ObservableVector; Indent
    // tracks depth and is surfaced as IndentMargin for the template. Every field
    // is edited in place through TwoWay bindings (Cocoa NSOutlineView style), and
    // a committed edit is handed to the view, which owns the write.
    struct AtomFlatItem : implements<AtomFlatItem,
                                     ICustomPropertyProvider,
                                     INotifyPropertyChanged,
                                     IStringable>
    {
        int32_t m_indent{ 0 };
        bool m_expanded{ false };
        bool m_hasChildren{ false };
        std::shared_ptr<SKAtomTreeNode> m_node;
        AtomsDetailView* m_view{ nullptr };
        winrt::event<PropertyChangedEventHandler> m_propertyChanged;

        std::shared_ptr<SKAsymmetricAtom> Atom() const
        {
            return m_node ? m_node->representedObject() : nullptr;
        }

        bool IsGroup() const { return m_node && m_node->isGroup(); }

        void Raise(wchar_t const* prop)
        {
            m_propertyChanged(*this, PropertyChangedEventArgs(prop));
        }

        // --- display-only values ---

        hstring ExpanderGlyphText() const
        {
            if (!m_hasChildren)
                return L" ";
            return m_expanded ? L"\u25BE" : L"\u25B8"; // ▾ expanded, ▸ collapsed
        }

        Thickness IndentMarginValue() const
        {
            // Cocoa isGroupItem rows: only groups indent. Leaf atoms stay at the
            // column origin so el/x/y/z line up with the header for FAU and
            // proteins alike.
            if (!IsGroup())
                return ThicknessHelper::FromUniformLength(0);
            return ThicknessHelper::FromLengths(static_cast<double>(m_indent) * 20.0, 0, 0, 0);
        }

        // Leaf atoms keep the compact name column; group rows (Cocoa's
        // atomGroupRow) get a wide field because the numeric columns are hidden.
        double NameWidthValue() const
        {
            return IsGroup() ? 540.0 : 96.0;
        }

        hstring IdText() const
        {
            auto atom = Atom();
            if (!atom || IsGroup())
                return L"";
            return hstring(std::to_wstring(atom->tag()));
        }

        hstring CountText() const
        {
            if (!IsGroup() || !m_node)
                return L"";
            return hstring(L"(" + std::to_wstring(m_node->childCount()) + L")");
        }

        Visibility FieldsVisibilityValue() const
        {
            // Groups only expose name + visibility; numeric fields are hidden.
            return IsGroup() ? Visibility::Collapsed : Visibility::Visible;
        }

        void RaiseExpanderChanged() { Raise(L"ExpanderGlyph"); }

        // --- editable values ---

        // A chain, a segment and a residue answer two questions and get the segmented control; every
        // other row answers one and keeps the box.
        bool IsRibbonGroup() const
        {
            return ProteinRibbonSegmentSupport::isProteinHierarchyGroupNode(m_node);
        }

        Visibility CheckVisibilityValue() const
        {
            return IsRibbonGroup() ? Visibility::Collapsed : Visibility::Visible;
        }

        Visibility SegmentVisibilityValue() const
        {
            return IsRibbonGroup() ? Visibility::Visible : Visibility::Collapsed;
        }

        bool ReadVisible() const
        {
            if (auto atom = Atom())
                return atom->isVisible();
            return true;
        }

        // The box on an atom row and the one on a hand-made group mean different things, and the two
        // sit in the same column, so each row says which one it is.
        hstring ReadVisibleTooltip() const
        {
            if (!IsGroup())
                return L"Show this atom";
            return L"Show this group and everything under it";
        }

        void WriteVisible(bool visible)
        {
            auto atom = Atom();
            if (!atom || atom->isVisible() == visible)
                return;
            // A hand-made group carries its contents with it, and those rows each draw a box of
            // their own, so they have to hear about it.
            if (IsGroup())
                ProteinRibbonSegmentSupport::setGroupVisibility(m_node, visible);
            else
                atom->setVisibility(visible);
            Raise(L"Visible");
            if (m_view)
            {
                if (IsGroup())
                    m_view->RefreshVisibilityUnder(m_node);
                m_view->RefreshGroupsAbove(m_node);
                m_view->AtomVisibilityChanged();
            }
        }

        // The group stores nothing of its own here: it says of its atoms only what they say of
        // themselves. All on, all off, or the indeterminate glyph when they disagree, which is a
        // reading the user is shown but cannot click into.
        std::optional<bool> ShowsAtomsState() const
        {
            if (!m_node)
                return true;
            bool sawVisible = false;
            bool sawHidden = false;
            for (auto const& leaf : m_node->descendantLeafNodes())
            {
                if (!leaf || !leaf->representedObject())
                    continue;
                if (leaf->representedObject()->isVisible())
                    sawVisible = true;
                else
                    sawHidden = true;
                if (sawVisible && sawHidden)
                    return std::nullopt;
            }
            return sawVisible || !sawHidden;
        }

        IInspectable ReadShowsAtoms() const
        {
            const auto state = ShowsAtomsState();
            return state ? box_value(*state) : IInspectable{};
        }

        void WriteShowsAtoms(bool visible)
        {
            if (!m_node)
                return;
            ProteinRibbonSegmentSupport::setGroupAtomsVisibility(m_node, visible);
            Raise(L"ShowsAtoms");
            if (m_view)
            {
                m_view->RefreshVisibilityUnder(m_node);
                m_view->RefreshGroupsAbove(m_node);
                m_view->AtomVisibilityChanged();
            }
        }

        bool ReadShowsRibbon() const
        {
            auto atom = Atom();
            return atom ? atom->isVisible() : true;
        }

        void WriteShowsRibbon(bool visible)
        {
            if (!m_node || ReadShowsRibbon() == visible)
                return;
            ProteinRibbonSegmentSupport::setGroupRibbonVisibility(m_node, visible);
            Raise(L"ShowsRibbon");
            if (m_view)
                m_view->AtomVisibilityChanged();
        }

        hstring ReadName() const
        {
            if (IsGroup())
                return m_node ? hstring(m_node->displayName().toStdWString()) : hstring{};
            auto atom = Atom();
            return atom ? hstring(atom->displayName().toStdWString()) : hstring{};
        }

        bool ReadFixed(int axis) const
        {
            auto atom = Atom();
            if (!atom)
                return false;
            const bool3 f = atom->isFixed();
            return (axis == 0) ? f.x : (axis == 1) ? f.y : f.z;
        }

        // Cocoa's NSLabelSegmentedControl segments toggle isFixed x/y/z.
        void WriteFixed(int axis, bool value)
        {
            if (!m_view || ReadFixed(axis) == value)
                return;
            m_view->CommitField(m_node,
                                (axis == 0) ? AtomField::fixedX
                                            : (axis == 1) ? AtomField::fixedY : AtomField::fixedZ,
                                value);
        }

        hstring ReadFfId() const
        {
            auto atom = Atom();
            return atom ? hstring(atom->uniqueForceFieldName().toStdWString()) : hstring{};
        }

        void WriteFfId(hstring const& text)
        {
            if (!m_view || text == ReadFfId())
                return;
            m_view->CommitField(m_node, AtomField::forceFieldType, std::wstring(text));
        }

        void WriteName(hstring const& text)
        {
            if (!m_view || text == ReadName())
                return;
            m_view->CommitField(m_node, AtomField::name, std::wstring(text));
        }

        void WriteElement(hstring const& text)
        {
            auto atom = Atom();
            if (!atom)
                return;
            auto z = AtomicNumberFor(text);
            if (!m_view || !z || *z < 0 ||
                *z >= static_cast<int>(PredefinedElements::predefinedElements.size()) ||
                *z == static_cast<int>(atom->elementIdentifier()))
            {
                Raise(L"Element"); // unknown symbol or no-op: revert the field
                return;
            }
            m_view->CommitField(m_node, AtomField::element, std::wstring(text));
        }

        // Shared commit path for the numeric fields: parse, ignore no-ops, hand
        // the value to the view, which writes it and re-raises so the display
        // re-formats to %.3f.
        void CommitDouble(hstring const& text, wchar_t const* prop, double current,
                          AtomField field)
        {
            auto v = ParseDouble(text);
            if (!m_view || !v || !std::isfinite(*v) || std::abs(*v - current) < 1e-12)
            {
                Raise(prop); // invalid or unchanged: restore formatted value
                return;
            }
            m_view->CommitField(m_node, field, *v);
        }

        // INotifyPropertyChanged
        event_token PropertyChanged(PropertyChangedEventHandler const& handler)
        {
            return m_propertyChanged.add(handler);
        }
        void PropertyChanged(event_token const& token) noexcept
        {
            m_propertyChanged.remove(token);
        }

        // ICustomPropertyProvider
        ICustomProperty GetCustomProperty(hstring const& name)
        {
            auto self = [](IInspectable const& target) { return target.try_as<AtomFlatItem>(); };

            if (name == L"Visible")
            {
                return make<AtomItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadVisible() : true);
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteVisible(unbox_value_or<bool>(v, true));
                    });
            }
            if (name == L"ShowsAtoms")
            {
                return make<AtomItemProperty>(name, xaml_typename<Windows::Foundation::IReference<bool>>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return s ? s->ReadShowsAtoms() : box_value(true);
                    });
            }
            if (name == L"ShowsRibbon")
            {
                return make<AtomItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadShowsRibbon() : true);
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteShowsRibbon(unbox_value_or<bool>(v, true));
                    });
            }
            if (name == L"CheckVisibility")
            {
                return make<AtomItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->CheckVisibilityValue() : Visibility::Visible);
                    });
            }
            if (name == L"SegmentVisibility")
            {
                return make<AtomItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->SegmentVisibilityValue() : Visibility::Collapsed);
                    });
            }
            if (name == L"VisibleTooltip")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadVisibleTooltip() : hstring{});
                    });
            }
            if (name == L"Name")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadName() : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteName(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"Element")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? ElementSymbolFor(s->Atom()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteElement(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"X" || name == L"Y" || name == L"Z")
            {
                const int axis = (name == L"X") ? 0 : (name == L"Y") ? 1 : 2;
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self, axis](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        if (!atom)
                            return box_value(hstring{});
                        auto pos = atom->position();
                        const double v = (axis == 0) ? pos.x : (axis == 1) ? pos.y : pos.z;
                        return box_value(FormatFixed(v));
                    },
                    [self, axis](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        if (!atom)
                            return;
                        auto pos = atom->position();
                        const double current = (axis == 0) ? pos.x : (axis == 1) ? pos.y : pos.z;
                        wchar_t const* prop = (axis == 0) ? L"X" : (axis == 1) ? L"Y" : L"Z";
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), prop, current,
                                        (axis == 0) ? AtomField::positionX
                                                    : (axis == 1) ? AtomField::positionY
                                                                  : AtomField::positionZ);
                    });
            }
            if (name == L"Charge")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        return box_value(atom ? FormatFixed(atom->charge()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        if (!atom)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"Charge",
                                        atom->charge(), AtomField::charge);
                    });
            }
            if (name == L"Occupancy")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        return box_value(atom ? FormatFixed(atom->occupancy()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto atom = s ? s->Atom() : nullptr;
                        if (!atom)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"Occupancy",
                                        atom->occupancy(), AtomField::occupancy);
                    });
            }
            if (name == L"FfId")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadFfId() : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteFfId(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"FixedX" || name == L"FixedY" || name == L"FixedZ")
            {
                const int axis = (name == L"FixedX") ? 0 : (name == L"FixedY") ? 1 : 2;
                return make<AtomItemProperty>(name, xaml_typename<bool>(),
                    [self, axis](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadFixed(axis) : false);
                    },
                    [self, axis](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteFixed(axis, unbox_value_or<bool>(v, false));
                    });
            }
            if (name == L"IdText")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->IdText() : hstring{});
                    });
            }
            if (name == L"CountText")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->CountText() : hstring{});
                    });
            }
            if (name == L"FieldsVisibility")
            {
                return make<AtomItemProperty>(name, xaml_typename<Visibility>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->FieldsVisibilityValue() : Visibility::Visible);
                    });
            }
            if (name == L"ExpanderGlyph")
            {
                return make<AtomItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ExpanderGlyphText() : hstring{ L" " });
                    });
            }
            if (name == L"IndentMargin")
            {
                return make<AtomItemProperty>(name, xaml_typename<Thickness>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->IndentMarginValue()
                                           : ThicknessHelper::FromUniformLength(0));
                    });
            }
            if (name == L"NameWidth")
            {
                return make<AtomItemProperty>(name, xaml_typename<double>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->NameWidthValue() : 96.0);
                    });
            }
            return nullptr;
        }
        ICustomProperty GetIndexedProperty(hstring const&, winrt::Windows::UI::Xaml::Interop::TypeName const&)
        {
            return nullptr;
        }
        hstring GetStringRepresentation() { return ReadName(); }
        winrt::Windows::UI::Xaml::Interop::TypeName Type()
        {
            return xaml_typename<IInspectable>();
        }

        // IStringable
        hstring ToString() { return ReadName(); }
    };

    namespace
    {
        // Every node is an atom row; an expander glyph appears whenever the node
        // has children of its own (atoms can nest under atoms, not just groups).
        IInspectable MakeFlatItem(std::shared_ptr<SKAtomTreeNode> const& node, int32_t indent,
                                  AtomsDetailView* view)
        {
            auto item = make_self<AtomFlatItem>();
            item->m_node = node;
            item->m_indent = indent;
            item->m_view = view;
            item->m_hasChildren = node && node->childCount() > 0;
            return *item;
        }

        bool FindFlatItemIndex(IObservableVector<IInspectable> const& items,
                               AtomFlatItem* target, uint32_t& index)
        {
            const uint32_t size = items.Size();
            for (uint32_t i = 0; i < size; ++i)
            {
                if (auto it = items.GetAt(i).try_as<AtomFlatItem>(); it && it.get() == target)
                {
                    index = i;
                    return true;
                }
            }
            return false;
        }

        // Insert the item's children directly below it in the flat collection.
        void ExpandFlatItem(IObservableVector<IInspectable> const& items,
                            com_ptr<AtomFlatItem> const& item, AtomsDetailView* view)
        {
            if (!item || item->m_expanded || !item->m_hasChildren || !item->m_node)
                return;
            uint32_t index = 0;
            if (!FindFlatItemIndex(items, item.get(), index))
                return;

            uint32_t insertPos = index + 1;
            for (auto const& child : item->m_node->childNodes())
                items.InsertAt(insertPos++, MakeFlatItem(child, item->m_indent + 1, view));

            item->m_expanded = true;
            item->RaiseExpanderChanged();
        }

        // Remove every following row that is deeper than the item (children,
        // including any expanded grandchildren).
        void CollapseFlatItem(IObservableVector<IInspectable> const& items,
                              com_ptr<AtomFlatItem> const& item)
        {
            if (!item || !item->m_expanded)
                return;
            uint32_t index = 0;
            if (!FindFlatItemIndex(items, item.get(), index))
                return;

            while (index + 1 < items.Size())
            {
                auto next = items.GetAt(index + 1).try_as<AtomFlatItem>();
                if (!next || next->m_indent <= item->m_indent)
                    break;
                items.RemoveAt(index + 1);
            }

            item->m_expanded = false;
            item->RaiseExpanderChanged();
        }

        // What a row command needs before it is offered, which is how Cocoa gates
        // the items of its atom context menu.
        enum class CommandGate
        {
            Always,
            Structure,   // the frame's object is a Structure
            SpaceGroup,  // ... and edits its space group
            Selection,   // at least one atom is selected
        };

        CommandGate GateOf(std::wstring const& command)
        {
            // Cocoa validateMenuItem: re-tiling the cell and taking the symmetry off both need a
            // space group to work from, whereas flattening only rearranges the tree and is offered
            // for a molecule or a protein as much as for a crystal.
            if (command == L"supercell" || command == L"removesymmetry" || command == L"wrap")
                return CommandGate::SpaceGroup;
            if (command == L"flatten" || command == L"primitive" || command == L"niggli" ||
                command == L"impose" || command.rfind(L"export", 0) == 0)
                return CommandGate::Structure;
            if (command == L"copytomovie" || command == L"movetomovie" ||
                command == L"scrollfirst" || command == L"scrolllast")
                return CommandGate::Selection;
            return CommandGate::Always;
        }

        // The markup nests the commands in sub-menus, so both the wiring and the
        // gating walk the whole tree.
        void ForEachMenuItem(IVector<MenuFlyoutItemBase> const& items,
                             std::function<void(MenuFlyoutItem const&)> const& fn)
        {
            for (auto const& entry : items)
            {
                if (auto item = entry.try_as<MenuFlyoutItem>())
                    fn(item);
                else if (auto sub = entry.try_as<MenuFlyoutSubItem>())
                    ForEachMenuItem(sub.Items(), fn);
            }
        }

        std::wstring TagOf(MenuFlyoutItem const& item)
        {
            return std::wstring(unbox_value_or<hstring>(item.Tag(), L""));
        }
    }

    AtomsDetailView::AtomsDetailView()
    {
        InitializeComponent();

        m_items = single_threaded_observable_vector<IInspectable>();
        List().ItemsSource(m_items);

        // handledEventsToo: the TextBoxes and the ItemContainer eat the pointer
        // before it reaches the list.
        List().AddHandler(UIElement::PointerPressedEvent(),
                          box_value(PointerEventHandler{ this, &AtomsDetailView::OnListPointerPressed }),
                          true);
        List().AddHandler(UIElement::RightTappedEvent(),
                          box_value(RightTappedEventHandler{ this, &AtomsDetailView::OnListRightTapped }),
                          true);

        m_rowMenu = Resources().Lookup(box_value(hstring(L"RowMenu"))).as<MenuFlyout>();
        ForEachMenuItem(m_rowMenu.Items(), [this](MenuFlyoutItem const& item)
        {
            const std::wstring command = TagOf(item);
            item.Click([this, command](IInspectable const&, RoutedEventArgs const&)
            {
                RunRowCommand(command);
            });
        });
    }

    void AtomsDetailView::Reload()
    {
        m_activeItem = nullptr;
        m_dragNodes.clear();
        m_selectionAnchor = -1;
        m_frame.reset();

        auto frame = m_controller ? m_controller->AtomsFrame() : nullptr;
        auto tree = m_controller ? m_controller->AtomTree(frame) : nullptr;
        const bool haveAtoms = frame && tree;

        Hint().Visibility(haveAtoms ? Visibility::Collapsed : Visibility::Visible);
        Header().Visibility(haveAtoms ? Visibility::Visible : Visibility::Collapsed);
        List().Visibility(haveAtoms ? Visibility::Visible : Visibility::Collapsed);
        Footer().Visibility(haveAtoms ? Visibility::Visible : Visibility::Collapsed);
        if (!haveAtoms)
        {
            m_items.Clear();
            return;
        }

        m_frame = frame;
        // The atom id column shows the tags, which follow the tree order.
        tree->setTags();
        Populate();
        RefreshNetCharge();
    }

    void AtomsDetailView::Clear()
    {
        m_frame.reset();
        m_activeItem = nullptr;
        m_dragNodes.clear();
        m_selectionAnchor = -1;
        if (m_items)
            m_items.Clear();
    }

    // Flatten the root nodes; children are inserted and removed on expand and
    // collapse. A non-empty search filter switches to a flat list of the leaf
    // atoms whose name or element matches (Cocoa's filterContent /
    // updateFilteredNodes); it survives a reload, as the Cocoa field does.
    void AtomsDetailView::Populate()
    {
        auto tree = m_controller ? m_controller->AtomTree(m_frame) : nullptr;
        if (!tree || !m_items)
            return;

        // Replacing the rows drops whatever the ItemsView had selected, and its
        // SelectionChanged for that only arrives after this layout pass; keep it
        // from writing the emptied selection into the model (which owns the atom
        // selection and would register an undo entry for it) and put the model's
        // own selection back on the new rows instead. That also picks up a
        // selection made in the 3D view before this tab was opened.
        m_suppressSelectionEvents = true;
        m_items.Clear();
        std::wstring needle(SearchBox().Text());
        if (needle.empty())
        {
            for (auto const& node : tree->rootNodes())
                m_items.Append(MakeFlatItem(node, 0, this));
        }
        else
        {
            std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
            for (auto const& node : tree->flattenedLeafNodes())
            {
                if (!node || !node->representedObject())
                    continue;
                std::wstring name = node->representedObject()->displayName().toStdWString();
                std::wstring symbol(ElementSymbolFor(node->representedObject()));
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::towlower);
                if (name.find(needle) != std::wstring::npos ||
                    symbol.find(needle) != std::wstring::npos)
                    m_items.Append(MakeFlatItem(node, 0, this));
            }
        }
        DispatcherQueue().TryEnqueue([this]() { ReloadRowSelection(); });
    }

    void AtomsDetailView::OnSearchTextChanged([[maybe_unused]] IInspectable const&,
                                              [[maybe_unused]] TextChangedEventArgs const&)
    {
        Populate();
    }

    void AtomsDetailView::CommitField(std::shared_ptr<SKAtomTreeNode> const& node,
                                      AtomField field, AtomFieldValue const& value)
    {
        if (m_controller)
            m_controller->SetAtomField(node, field, value);
    }

    void AtomsDetailView::AtomVisibilityChanged()
    {
        if (m_controller)
            m_controller->ReloadRendererInvalidatingAmbientOcclusion();
    }

    // The rows are the tree flattened in its own order, so the contents of a group are the rows that
    // follow it until the indent falls back, and the groups holding it are the rows before it whose
    // indent keeps stepping back. A collapsed part of the tree has no rows and needs none: it is
    // read afresh when it is opened.
    uint32_t AtomsDetailView::RowOf(std::shared_ptr<SKAtomTreeNode> const& node)
    {
        const uint32_t size = m_items ? m_items.Size() : 0;
        for (uint32_t row = 0; row < size; ++row)
        {
            auto item = m_items.GetAt(row).try_as<AtomFlatItem>();
            if (item && item->m_node == node)
                return row;
        }
        return size;
    }

    void AtomsDetailView::RefreshVisibilityUnder(std::shared_ptr<SKAtomTreeNode> const& node)
    {
        if (!m_items || !node)
            return;
        const uint32_t size = m_items.Size();
        const uint32_t row = RowOf(node);
        if (row >= size)
            return;

        const int32_t indent = m_items.GetAt(row).try_as<AtomFlatItem>()->m_indent;
        for (uint32_t i = row + 1; i < size; ++i)
        {
            auto item = m_items.GetAt(i).try_as<AtomFlatItem>();
            if (!item || item->m_indent <= indent)
                break;
            item->Raise(L"Visible");
            item->Raise(L"ShowsAtoms");
        }
    }

    // Switching one atom, or one group, can leave every group above it no longer all-on or all-off,
    // and each of them draws that on its own row.
    void AtomsDetailView::RefreshGroupsAbove(std::shared_ptr<SKAtomTreeNode> const& node)
    {
        if (!m_items || !node)
            return;
        const uint32_t row = RowOf(node);
        if (row >= m_items.Size())
            return;

        int32_t indent = m_items.GetAt(row).try_as<AtomFlatItem>()->m_indent;
        for (uint32_t i = row; i-- > 0 && indent > 0;)
        {
            auto item = m_items.GetAt(i).try_as<AtomFlatItem>();
            if (!item || item->m_indent >= indent)
                continue;
            indent = item->m_indent;
            item->Raise(L"ShowsAtoms");
        }
    }

    // Cocoa reloads the edited row and column after a field write; the row has to
    // be told even when the write came from an undo instead of from its own
    // editor.
    void AtomsDetailView::RefreshField(std::shared_ptr<SKAtomTreeNode> const& node, AtomField field)
    {
        if (!m_items || !node)
            return;
        const uint32_t size = m_items.Size();
        for (uint32_t i = 0; i < size; ++i)
        {
            auto item = m_items.GetAt(i).try_as<AtomFlatItem>();
            if (!item || item->m_node != node)
                continue;
            switch (field)
            {
            case AtomField::name:
                item->Raise(L"Name");
                break;
            case AtomField::element:
                item->Raise(L"Element");
                item->Raise(L"Name");
                item->Raise(L"FfId");
                break;
            case AtomField::forceFieldType:
                item->Raise(L"FfId");
                break;
            case AtomField::occupancy:
                item->Raise(L"Occupancy");
                break;
            case AtomField::positionX:
                item->Raise(L"X");
                break;
            case AtomField::positionY:
                item->Raise(L"Y");
                break;
            case AtomField::positionZ:
                item->Raise(L"Z");
                break;
            case AtomField::charge:
                item->Raise(L"Charge");
                break;
            case AtomField::fixedX:
                item->Raise(L"FixedX");
                break;
            case AtomField::fixedY:
                item->Raise(L"FixedY");
                break;
            case AtomField::fixedZ:
                item->Raise(L"FixedZ");
                break;
            }
            break;
        }
    }

    void AtomsDetailView::RefreshNetCharge()
    {
        auto tree = m_controller ? m_controller->AtomTree(m_frame) : nullptr;
        NetChargeText().Text(FormatFixed(tree ? tree->netCharge() : 0.0));
    }

    std::shared_ptr<SKAtomTreeNode> AtomsDetailView::NodeAt(uint32_t index)
    {
        if (!m_items || index >= m_items.Size())
            return nullptr;
        auto item = m_items.GetAt(index).try_as<AtomFlatItem>();
        return item ? item->m_node : nullptr;
    }

    std::vector<std::shared_ptr<SKAtomTreeNode>> AtomsDetailView::ActedOnNodes()
    {
        std::vector<std::shared_ptr<SKAtomTreeNode>> nodes;
        if (auto selected = List().SelectedItems())
        {
            for (auto const& entry : selected)
            {
                if (auto item = entry.try_as<AtomFlatItem>(); item && item->m_node)
                    nodes.push_back(item->m_node);
            }
        }
        if (nodes.empty() && m_activeItem)
        {
            if (auto item = m_activeItem.try_as<AtomFlatItem>(); item && item->m_node)
                nodes.push_back(item->m_node);
        }
        return nodes;
    }

    // Hit-test handler for the flat rows:
    //   expander glyph → insert/remove children in the flat collection,
    //   inside a field → leave it alone (the TextBox places its own caret),
    //   anywhere else  → replicate the Extended selection the ItemsView would do
    //                    itself, which the drag-enabled row content prevents it
    //                    from seeing.
    // Every row hit is remembered as the active row for the [+] and [-] buttons.
    void AtomsDetailView::OnListPointerPressed([[maybe_unused]] IInspectable const&,
                                               PointerRoutedEventArgs const& e)
    {
        if (!m_items)
            return;

        try
        {
            auto list = List();
            auto local = e.GetCurrentPoint(list).Position();
            auto rootPt = list.TransformToVisual(nullptr).TransformPoint(local);
            auto elements = VisualTreeHelper::FindElementsInHostCoordinates(rootPt, list);

            com_ptr<AtomFlatItem> hitItem;
            bool onExpander = false;
            bool onField = false;
            for (auto const& el : elements)
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto tb = fe.try_as<TextBlock>();
                    tb && unbox_value_or<hstring>(tb.Tag(), L"") == L"expander")
                    onExpander = true;
                if (fe.try_as<TextBox>() || fe.try_as<Primitives::ToggleButton>() ||
                    fe.try_as<ComboBox>())
                    onField = true;
                if (!hitItem)
                {
                    if (auto it = fe.DataContext().try_as<AtomFlatItem>(); it && it->m_node)
                        hitItem = it;
                }
            }
            if (!hitItem)
                return;

            m_activeItem = *hitItem;

            if (onExpander)
            {
                if (hitItem->m_hasChildren)
                {
                    // Inserting or removing rows shifts the index-based selection
                    // and raises SelectionChanged, which would write the reduced
                    // selection back into the model; suppress it and re-apply the
                    // model selection afterwards (Cocoa's
                    // outlineViewItemDidExpand -> restoreSelectedItems).
                    m_suppressSelectionEvents = true;
                    if (hitItem->m_expanded)
                        CollapseFlatItem(m_items, hitItem);
                    else
                        ExpandFlatItem(m_items, hitItem, this);
                    m_suppressSelectionEvents = false;
                    ReloadRowSelection();
                    e.Handled(true);
                }
                return;
            }

            // Clicks inside an editable field are handled by the control itself;
            // leave the selection and the drag state alone.
            if (onField)
                return;

            if (!e.GetCurrentPoint(list).Properties().IsLeftButtonPressed())
                return;

            uint32_t rowIndex = 0;
            if (!FindFlatItemIndex(m_items, hitItem.get(), rowIndex))
                return;

            // Cocoa outline view: dragging a row that is part of the current
            // selection drags the whole selection; otherwise just that row.
            const bool hitIsSelected = list.IsSelected(static_cast<int32_t>(rowIndex));
            m_dragNodes.clear();
            if (hitIsSelected)
            {
                const uint32_t count = m_items.Size();
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (!list.IsSelected(static_cast<int32_t>(i)))
                        continue;
                    if (auto node = NodeAt(i))
                        m_dragNodes.push_back(node);
                }
            }
            else
            {
                m_dragNodes.push_back(hitItem->m_node);
            }

            // The CanDrag row content captures the pointer for drag detection, so
            // the ItemContainer never receives this press; replicate the
            // Extended-selection behavior (plain / Ctrl / Shift click) here.
            using winrt::Windows::System::VirtualKeyModifiers;
            const auto mods = e.KeyModifiers();
            const bool ctrl = (mods & VirtualKeyModifiers::Control) == VirtualKeyModifiers::Control;
            const bool shift = (mods & VirtualKeyModifiers::Shift) == VirtualKeyModifiers::Shift;
            if (shift)
            {
                const uint32_t count = m_items.Size();
                uint32_t anchor = rowIndex;
                if (m_selectionAnchor >= 0 && m_selectionAnchor < static_cast<int32_t>(count))
                    anchor = static_cast<uint32_t>(m_selectionAnchor);
                list.DeselectAll();
                const uint32_t lo = (std::min)(anchor, rowIndex);
                const uint32_t hi = (std::max)(anchor, rowIndex);
                for (uint32_t i = lo; i <= hi; ++i)
                    list.Select(static_cast<int32_t>(i));
            }
            else if (ctrl)
            {
                if (hitIsSelected)
                    list.Deselect(static_cast<int32_t>(rowIndex));
                else
                    list.Select(static_cast<int32_t>(rowIndex));
                m_selectionAnchor = static_cast<int32_t>(rowIndex);
            }
            else
            {
                // Keep an existing multi-selection intact when pressing one of its
                // rows, so the whole selection can be dragged (Cocoa).
                if (!hitIsSelected)
                {
                    list.DeselectAll();
                    list.Select(static_cast<int32_t>(rowIndex));
                }
                m_selectionAnchor = static_cast<int32_t>(rowIndex);
            }
        }
        catch (...)
        {
        }
    }

    void AtomsDetailView::OnVisibilityClick(IInspectable const& sender,
                                            [[maybe_unused]] RoutedEventArgs const&)
    {
        auto box = sender.try_as<CheckBox>();
        if (!box)
            return;

        auto item = box.DataContext().try_as<AtomFlatItem>();
        if (!item)
            return;

        // The box has already toggled itself, and not being three-state it cannot
        // have landed on null.
        const auto value = box.IsChecked();
        item->WriteVisible(value ? value.Value() : true);
    }

    void AtomsDetailView::OnVisibilityBoxDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        if (auto box = sender.try_as<ToggleButton>())
            DetailControls::SyncCheckFromDataContext(box, L"Visible");
    }

    void AtomsDetailView::OnShowsAtomsClick(IInspectable const& sender,
                                            [[maybe_unused]] RoutedEventArgs const&)
    {
        auto button = sender.try_as<ToggleButton>();
        if (!button)
            return;
        auto item = button.DataContext().try_as<AtomFlatItem>();
        if (!item)
            return;

        // Not being three-state, a click from the indeterminate reading lands on checked, so a group
        // whose atoms disagree is switched all on by one press and all off by the next.
        const auto value = button.IsChecked();
        item->WriteShowsAtoms(value ? value.Value() : true);
    }

    void AtomsDetailView::OnShowsRibbonClick(IInspectable const& sender,
                                             [[maybe_unused]] RoutedEventArgs const&)
    {
        auto button = sender.try_as<ToggleButton>();
        if (!button)
            return;
        if (auto item = button.DataContext().try_as<AtomFlatItem>())
        {
            const auto value = button.IsChecked();
            item->WriteShowsRibbon(value ? value.Value() : true);
        }
    }

    void AtomsDetailView::OnSegmentDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        auto button = sender.try_as<ToggleButton>();
        if (!button)
            return;
        const bool isRibbonHalf = unbox_value_or<hstring>(button.Tag(), hstring{}) == L"ribbon";
        DetailControls::SyncCheckFromDataContext(
            button, isRibbonHalf ? L"ShowsRibbon" : L"ShowsAtoms", !isRibbonHalf);
    }

    void AtomsDetailView::OnBinaryCheckDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        auto button = sender.try_as<ToggleButton>();
        if (!button)
            return;
        auto name = unbox_value_or<hstring>(button.Tag(), hstring{});
        if (!name.empty())
            DetailControls::SyncCheckFromDataContext(button, name);
    }

    void AtomsDetailView::OnListDragOver([[maybe_unused]] IInspectable const&,
                                         DragEventArgs const& e)
    {
        if (m_dragNodes.empty())
        {
            e.AcceptedOperation(DataPackageOperation::None);
            return;
        }
        e.AcceptedOperation(DataPackageOperation::Move);
        e.DragUIOverride().IsGlyphVisible(false);
        e.DragUIOverride().Caption(m_dragNodes.size() == 1 ? L"Move atom" : L"Move atoms");
    }

    void AtomsDetailView::OnListDrop([[maybe_unused]] IInspectable const&, DragEventArgs const& e)
    {
        auto dragged = m_dragNodes;
        m_dragNodes.clear();
        if (dragged.empty() || !m_items || !m_controller)
            return;

        // The row under the drop point decides the target parent and index.
        std::shared_ptr<SKAtomTreeNode> target;
        try
        {
            auto list = List();
            auto rootPt = list.TransformToVisual(nullptr).TransformPoint(e.GetPosition(list));
            for (auto const& el : VisualTreeHelper::FindElementsInHostCoordinates(rootPt, list))
            {
                auto fe = el.try_as<FrameworkElement>();
                if (!fe)
                    continue;
                if (auto it = fe.DataContext().try_as<AtomFlatItem>(); it && it->m_node)
                {
                    target = it->m_node;
                    break;
                }
            }
        }
        catch (...)
        {
            return;
        }

        if (m_controller->MoveAtomNodes(m_frame, dragged, target))
            e.Handled(true);
    }

    // Mirror the row selection into the atom tree, which owns the selection
    // (Cocoa-style), and refresh the selection glow in the 3D view.
    void AtomsDetailView::OnSelectionChanged(
        [[maybe_unused]] ItemsView const&,
        [[maybe_unused]] ItemsViewSelectionChangedEventArgs const&)
    {
        if (m_suppressSelectionEvents || !m_controller || !m_frame.lock())
            return;

        std::set<std::shared_ptr<SKAtomTreeNode>> selection;
        if (auto selected = List().SelectedItems())
        {
            for (auto const& entry : selected)
            {
                if (auto item = entry.try_as<AtomFlatItem>(); item && item->m_node)
                    selection.insert(item->m_node);
            }
        }
        m_controller->SetAtomSelection(m_frame, selection, L"Change Atom Selection");
    }

    // Inverse of OnSelectionChanged: after a 3D click-pick, a rubber-band drag or
    // an undo changed the atom tree's selection, highlight the matching rows.
    // Rows of atoms hidden inside collapsed groups stay unshown (Cocoa parity).
    void AtomsDetailView::ReloadRowSelection()
    {
        auto tree = m_controller ? m_controller->AtomTree(m_frame) : nullptr;
        if (!tree || !m_items)
        {
            m_suppressSelectionEvents = false;
            return;
        }

        auto selected = tree->selectedTreeNodes();
        auto list = List();
        m_suppressSelectionEvents = true;
        list.DeselectAll();
        const uint32_t count = m_items.Size();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (auto node = NodeAt(i); node && selected.count(node) > 0)
                list.Select(static_cast<int32_t>(i));
        }
        m_suppressSelectionEvents = false;
    }

    void AtomsDetailView::OnFieldGotFocus([[maybe_unused]] IInspectable const&,
                                          RoutedEventArgs const& e)
    {
        auto tb = e.OriginalSource().try_as<TextBox>();
        if (!tb || !tb.DataContext().try_as<AtomFlatItem>())
            return;
        // The built-in select-all runs after GotFocus; fix it up afterwards.
        auto weak = make_weak(tb);
        DispatcherQueue().TryEnqueue([weak]()
        {
            auto box = weak.get();
            if (!box)
                return;
            const auto len = static_cast<int32_t>(box.Text().size());
            if (len > 0 && box.SelectionLength() == len)
            {
                box.SelectionStart(len);
                box.SelectionLength(0);
            }
        });
    }

    void AtomsDetailView::OnAddClick([[maybe_unused]] IInspectable const&,
                                     [[maybe_unused]] RoutedEventArgs const&)
    {
        AddNode(false);
    }

    void AtomsDetailView::OnAddAtomMenuClick([[maybe_unused]] IInspectable const&,
                                             [[maybe_unused]] RoutedEventArgs const&)
    {
        AddNode(false);
    }

    void AtomsDetailView::OnAddAtomGroupMenuClick([[maybe_unused]] IInspectable const&,
                                                  [[maybe_unused]] RoutedEventArgs const&)
    {
        AddNode(true);
    }

    // The last selected row is where the insert goes; fall back to the last row
    // the user clicked, since clicking into a field leaves the selection alone.
    void AtomsDetailView::AddNode(bool group)
    {
        if (!m_controller)
            return;
        std::shared_ptr<SKAtomTreeNode> target;
        if (auto selected = List().SelectedItems(); selected && selected.Size() > 0)
        {
            if (auto item = selected.GetAt(selected.Size() - 1).try_as<AtomFlatItem>())
                target = item->m_node;
        }
        if (!target && m_activeItem)
        {
            if (auto item = m_activeItem.try_as<AtomFlatItem>())
                target = item->m_node;
        }
        m_controller->AddAtomNode(m_frame, target, group);
    }

    void AtomsDetailView::OnRemoveClick([[maybe_unused]] IInspectable const&,
                                        [[maybe_unused]] RoutedEventArgs const&)
    {
        if (!m_controller)
            return;
        auto nodes = ActedOnNodes();
        if (nodes.empty())
        {
            m_controller->Log(L"Select an atom to remove");
            return;
        }
        // Nodes whose ancestor is also selected go away with that ancestor, so
        // they do not count towards what the log reports.
        const size_t removed = static_cast<size_t>(std::count_if(nodes.begin(), nodes.end(),
            [&nodes](std::shared_ptr<SKAtomTreeNode> const& node)
            {
                return std::none_of(nodes.begin(), nodes.end(),
                    [&node](std::shared_ptr<SKAtomTreeNode> const& other)
                    {
                        return other != node && node->isDescendantOfNode(other);
                    });
            }));

        m_controller->RemoveAtomNodes(m_frame, nodes, L"Delete Atoms");
        m_controller->Log(removed == 1 ? std::wstring(L"Atom removed")
                                       : std::to_wstring(removed) + L" atoms removed");
    }

    void AtomsDetailView::OnListRightTapped([[maybe_unused]] IInspectable const&,
                                            RightTappedRoutedEventArgs const& e)
    {
        auto frame = m_frame.lock();
        if (!frame || !m_rowMenu)
            return;

        const bool isStructure = std::dynamic_pointer_cast<Structure>(frame->object()) != nullptr;
        const bool isSpaceGroupEditor =
            std::dynamic_pointer_cast<SpaceGroupEditor>(frame->object()) != nullptr;
        bool hasSelection = false;
        if (auto tree = m_controller ? m_controller->AtomTree(m_frame) : nullptr)
            hasSelection = !tree->selectedTreeNodes().empty();

        ForEachMenuItem(m_rowMenu.Items(), [&](MenuFlyoutItem const& item)
        {
            switch (GateOf(TagOf(item)))
            {
            case CommandGate::Structure:  item.IsEnabled(isStructure); break;
            case CommandGate::SpaceGroup: item.IsEnabled(isSpaceGroupEditor); break;
            case CommandGate::Selection:  item.IsEnabled(hasSelection); break;
            case CommandGate::Always:     item.IsEnabled(true); break;
            }
        });

        try
        {
            m_rowMenu.ShowAt(List(), e.GetPosition(List()));
        }
        catch (...)
        {
        }
        e.Handled(true);
    }

    void AtomsDetailView::RunRowCommand(std::wstring const& command)
    {
        if (!m_controller)
            return;
        using Operation = DocumentController::AtomStructureOperation;
        using Search = DocumentController::AtomSymmetrySearch;
        using Format = DocumentController::AtomExportFormat;

        if (command == L"add")
            AddNode(false);
        else if (command == L"addgroup")
            AddNode(true);
        else if (command == L"flatten")
            m_controller->RunAtomStructureOperation(m_frame, Operation::FlattenHierarchy);
        else if (command == L"supercell")
            m_controller->RunAtomStructureOperation(m_frame, Operation::SuperCell);
        else if (command == L"removesymmetry")
            m_controller->RunAtomStructureOperation(m_frame, Operation::RemoveSymmetry);
        else if (command == L"wrap")
            m_controller->RunAtomStructureOperation(m_frame, Operation::WrapAtomsToCell);
        else if (command == L"primitive")
            m_controller->FindAtomSymmetry(m_frame, Search::Primitive);
        else if (command == L"niggli")
            m_controller->FindAtomSymmetry(m_frame, Search::Niggli);
        else if (command == L"impose")
            m_controller->FindAtomSymmetry(m_frame, Search::Impose);
        else if (command == L"invertselection")
            m_controller->InvertAtomSelection(m_frame);
        else if (command == L"copytomovie")
            m_controller->AtomSelectionToNewMovie(m_frame, false);
        else if (command == L"movetomovie")
            m_controller->AtomSelectionToNewMovie(m_frame, true);
        else if (command == L"visibilitymatch")
            m_controller->SetAtomVisibilityFromSelection(m_frame, true);
        else if (command == L"visibilityinvert")
            m_controller->SetAtomVisibilityFromSelection(m_frame, false);
        else if (command == L"scrolltop")
            ScrollTo(0);
        else if (command == L"scrollbottom")
            ScrollTo(1);
        else if (command == L"scrollfirst")
            ScrollTo(2);
        else if (command == L"scrolllast")
            ScrollTo(3);
        else if (command == L"exportpdb")
            m_controller->ExportAtoms(m_frame, Format::PDB);
        else if (command == L"exportmmcif")
            m_controller->ExportAtoms(m_frame, Format::mmCIF);
        else if (command == L"exportcif")
            m_controller->ExportAtoms(m_frame, Format::CIF);
        else if (command == L"exportxyz")
            m_controller->ExportAtoms(m_frame, Format::XYZ);
        else if (command == L"exportposcar")
            m_controller->ExportAtoms(m_frame, Format::POSCAR);
    }

    void AtomsDetailView::ScrollTo(int mode)
    {
        if (!m_items)
            return;
        const int32_t count = static_cast<int32_t>(m_items.Size());
        if (count == 0)
            return;

        int32_t index = -1;
        if (mode == 0)
            index = 0;
        else if (mode == 1)
            index = count - 1;
        else
        {
            std::set<std::shared_ptr<SKAtomTreeNode>> selected;
            if (auto tree = m_controller ? m_controller->AtomTree(m_frame) : nullptr)
                selected = tree->selectedTreeNodes();
            for (int32_t i = 0; i < count; ++i)
            {
                if (auto node = NodeAt(static_cast<uint32_t>(i)); node && selected.count(node) > 0)
                {
                    index = i;
                    if (mode == 2)
                        break;
                }
            }
        }
        if (index < 0)
            return;
        try
        {
            List().StartBringItemIntoView(index, nullptr);
        }
        catch (...)
        {
        }
    }
}
