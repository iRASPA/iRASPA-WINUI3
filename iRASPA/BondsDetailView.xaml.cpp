#include "pch.h"
#include "BondsDetailView.xaml.h"
#if __has_include("BondsDetailView.g.cpp")
#include "BondsDetailView.g.cpp"
#endif

#include "DetailControls.h"
#include "iraspaobject.h"
#include "skasymmetricatom.h"
#include "skasymmetricbond.h"
#include "skbondsetcontroller.h"
#include "skelement.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <cmath>
#include <cwctype>
#include <functional>
#include <optional>
#include <set>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        hstring FormatBondLength(double v)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"%.5f", v);
            return hstring(buf);
        }

        std::optional<double> ParseBondDouble(hstring const& text)
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

        hstring BondElementSymbol(std::shared_ptr<SKAsymmetricAtom> const& atom)
        {
            if (!atom)
                return L"";
            const auto z = static_cast<int>(atom->elementIdentifier());
            if (z >= 0 && z < static_cast<int>(PredefinedElements::predefinedElements.size()))
                return hstring(PredefinedElements::predefinedElements[static_cast<size_t>(z)]
                                   ._chemicalSymbol.toStdWString());
            return L"?";
        }
    }

    // {Binding} property backing for BondFlatItem: a DataTemplate binds by name
    // at runtime, and a plain C++ class carries no binding metadata.
    struct BondItemProperty : implements<BondItemProperty, ICustomProperty>
    {
        using Getter = std::function<IInspectable(IInspectable const&)>;
        using Setter = std::function<void(IInspectable const&, IInspectable const&)>;

        BondItemProperty(hstring name, winrt::Windows::UI::Xaml::Interop::TypeName type,
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

    // One row of the bond table. The visibility, the fixed toggles and the bond
    // type write straight to the model, as the Cocoa cells' actions do; the
    // length goes through the pane, because changing it moves both atoms and
    // regenerates the structure around them.
    struct BondFlatItem : implements<BondFlatItem,
                                     ICustomPropertyProvider,
                                     INotifyPropertyChanged,
                                     IStringable>
    {
        int32_t m_row{ 0 };
        std::shared_ptr<SKAsymmetricBond> m_bond;
        BondsDetailView* m_view{ nullptr };
        winrt::event<PropertyChangedEventHandler> m_propertyChanged;

        void Raise(wchar_t const* prop)
        {
            m_propertyChanged(*this, PropertyChangedEventArgs(prop));
        }

        void RaiseLengthChanged()
        {
            Raise(L"Length");
            Raise(L"LengthValue");
        }

        double CurrentLength() const
        {
            return (m_view && m_bond) ? m_view->LengthOf(m_bond) : 0.0;
        }

        bool ReadVisible() const
        {
            if (!m_bond)
                return true;
            auto a1 = m_bond->atom1();
            auto a2 = m_bond->atom2();
            return m_bond->isVisible() && (!a1 || a1->isVisible()) && (!a2 || a2->isVisible());
        }

        void WriteVisible(bool visible)
        {
            // No equality early-out: the readout also folds in the two atom
            // visibilities, so the bond flag may differ from what is shown.
            if (!m_bond)
                return;
            m_bond->setIsVisible(visible);
            Raise(L"Visible");
            if (m_view)
                m_view->BondChanged();
        }

        bool ReadFixed(int atomIndex, int axis) const
        {
            auto atom = (atomIndex == 0) ? (m_bond ? m_bond->atom1() : nullptr)
                                         : (m_bond ? m_bond->atom2() : nullptr);
            if (!atom)
                return false;
            const bool3 f = atom->isFixed();
            return (axis == 0) ? f.x : (axis == 1) ? f.y : f.z;
        }

        void WriteFixed(int atomIndex, int axis, bool value)
        {
            auto atom = (atomIndex == 0) ? (m_bond ? m_bond->atom1() : nullptr)
                                         : (m_bond ? m_bond->atom2() : nullptr);
            if (!atom)
                return;
            bool3 f = atom->isFixed();
            bool& channel = (axis == 0) ? f.x : (axis == 1) ? f.y : f.z;
            if (channel == value)
                return;
            channel = value;
            atom->setIsFixed(f);
        }

        int32_t ReadTypeIndex() const
        {
            return m_bond ? static_cast<int32_t>(m_bond->getBondType()) : 0;
        }

        void WriteTypeIndex(int32_t index)
        {
            if (!m_bond || index < 0 || index > 3 || index == ReadTypeIndex())
                return;
            m_bond->setBondType(static_cast<SKAsymmetricBond::SKBondType>(index));
            Raise(L"TypeIndex");
            if (m_view)
                m_view->BondChanged();
        }

        void WriteLength(hstring const& text)
        {
            auto v = ParseBondDouble(text);
            if (!v || !std::isfinite(*v) || *v <= 0.0 ||
                std::abs(*v - CurrentLength()) < 1e-9)
            {
                RaiseLengthChanged(); // invalid or unchanged: restore the readout
                return;
            }
            if (m_view && m_bond)
                m_view->CommitLength(m_bond, *v);
            RaiseLengthChanged();
        }

        void WriteLengthValue(double v)
        {
            if (!std::isfinite(v) || v <= 0.0 || std::abs(v - CurrentLength()) < 1e-6)
                return;
            if (m_view && m_bond)
                m_view->CommitLength(m_bond, v);
            Raise(L"Length");
        }

        hstring TagText(int atomIndex) const
        {
            auto atom = (atomIndex == 0) ? (m_bond ? m_bond->atom1() : nullptr)
                                         : (m_bond ? m_bond->atom2() : nullptr);
            return atom ? hstring(std::to_wstring(atom->tag())) : hstring{};
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
            auto self = [](IInspectable const& target) { return target.try_as<BondFlatItem>(); };

            if (name == L"Visible")
            {
                return make<BondItemProperty>(name, xaml_typename<bool>(),
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
            if (name == L"IdText")
            {
                return make<BondItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? hstring(std::to_wstring(s->m_row)) : hstring{});
                    });
            }
            if (name == L"Tag1" || name == L"Tag2")
            {
                const int atomIndex = (name == L"Tag1") ? 0 : 1;
                return make<BondItemProperty>(name, xaml_typename<hstring>(),
                    [self, atomIndex](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->TagText(atomIndex) : hstring{});
                    });
            }
            // A1FixedX..A2FixedZ: the six fixed x/y/z segment toggles.
            if (name == L"A1FixedX" || name == L"A1FixedY" || name == L"A1FixedZ" ||
                name == L"A2FixedX" || name == L"A2FixedY" || name == L"A2FixedZ")
            {
                const int atomIndex = (name[1] == L'1') ? 0 : 1;
                const int axis = (name[7] == L'X') ? 0 : (name[7] == L'Y') ? 1 : 2;
                return make<BondItemProperty>(name, xaml_typename<bool>(),
                    [self, atomIndex, axis](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadFixed(atomIndex, axis) : false);
                    },
                    [self, atomIndex, axis](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteFixed(atomIndex, axis, unbox_value_or<bool>(v, false));
                    });
            }
            if (name == L"TypeIndex")
            {
                return make<BondItemProperty>(name, xaml_typename<int32_t>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->ReadTypeIndex() : 0);
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteTypeIndex(unbox_value_or<int32_t>(v, 0));
                    });
            }
            if (name == L"ElA" || name == L"ElB")
            {
                const int atomIndex = (name == L"ElA") ? 0 : 1;
                return make<BondItemProperty>(name, xaml_typename<hstring>(),
                    [self, atomIndex](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        if (!s || !s->m_bond)
                            return box_value(hstring{});
                        auto atom = (atomIndex == 0) ? s->m_bond->atom1() : s->m_bond->atom2();
                        return box_value(BondElementSymbol(atom));
                    });
            }
            if (name == L"Length")
            {
                return make<BondItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? FormatBondLength(s->CurrentLength()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteLength(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"LengthValue")
            {
                return make<BondItemProperty>(name, xaml_typename<double>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->CurrentLength() : 0.0);
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteLengthValue(unbox_value_or<double>(v, 0.0));
                    });
            }
            return nullptr;
        }
        ICustomProperty GetIndexedProperty(hstring const&, winrt::Windows::UI::Xaml::Interop::TypeName const&)
        {
            return nullptr;
        }
        hstring GetStringRepresentation() { return TagText(0) + L"-" + TagText(1); }
        winrt::Windows::UI::Xaml::Interop::TypeName Type()
        {
            return xaml_typename<IInspectable>();
        }

        // IStringable
        hstring ToString() { return GetStringRepresentation(); }
    };

    BondsDetailView::BondsDetailView()
    {
        InitializeComponent();

        m_items = single_threaded_observable_vector<IInspectable>();
        List().ItemsSource(m_items);
    }

    void BondsDetailView::Reload()
    {
        m_frame.reset();

        auto frame = m_controller ? m_controller->BondsFrame() : nullptr;
        auto bondSet = m_controller ? m_controller->BondSet(frame) : nullptr;
        const bool haveBonds = frame && bondSet;

        Hint().Visibility(haveBonds ? Visibility::Collapsed : Visibility::Visible);
        Header().Visibility(haveBonds ? Visibility::Visible : Visibility::Collapsed);
        List().Visibility(haveBonds ? Visibility::Visible : Visibility::Collapsed);
        Footer().Visibility(haveBonds ? Visibility::Visible : Visibility::Collapsed);
        if (!haveBonds)
        {
            m_items.Clear();
            return;
        }

        m_frame = frame;
        // The id column and the two atom-id segments show the tags, which follow
        // the order of the bond set.
        bondSet->setTags();
        Populate();
    }

    void BondsDetailView::Clear()
    {
        m_frame.reset();
        if (m_items)
            m_items.Clear();
    }

    void BondsDetailView::Populate()
    {
        auto bondSet = m_controller ? m_controller->BondSet(m_frame) : nullptr;
        if (!bondSet || !m_items)
            return;

        // Replacing the rows drops whatever the ItemsView had selected, and its
        // SelectionChanged for that only arrives after this layout pass; keep it
        // from writing the emptied selection into the bond set, which owns the
        // selection.
        m_suppressSelectionEvents = true;
        m_items.Clear();
        int32_t row = 0;
        for (auto const& bond : bondSet->arrangedObjects())
        {
            auto item = make_self<BondFlatItem>();
            item->m_row = row++;
            item->m_bond = bond;
            item->m_view = this;
            m_items.Append(*item);
        }
        CountText().Text(std::to_wstring(bondSet->arrangedObjects().size()) + L" bonds");
        DispatcherQueue().TryEnqueue([this]() { m_suppressSelectionEvents = false; });
    }

    // computeBonds replaced the asymmetric bonds, so every row is holding one
    // that is no longer in the set. Match them up by their atom pair, which
    // survives the rebuild, rather than tearing the rows down (Cocoa reloads the
    // whole table here, losing the scroll position).
    void BondsDetailView::RebindBonds()
    {
        auto bondSet = m_controller ? m_controller->BondSet(m_frame) : nullptr;
        if (!bondSet || !m_items)
            return;

        auto const& bonds = bondSet->arrangedObjects();
        const uint32_t count = m_items.Size();
        for (uint32_t i = 0; i < count; ++i)
        {
            auto item = m_items.GetAt(i).try_as<BondFlatItem>();
            if (!item || !item->m_bond)
                continue;
            auto oldA1 = item->m_bond->atom1();
            auto oldA2 = item->m_bond->atom2();
            for (auto const& bond : bonds)
            {
                auto a1 = bond->atom1();
                auto a2 = bond->atom2();
                if ((a1 == oldA1 && a2 == oldA2) || (a1 == oldA2 && a2 == oldA1))
                {
                    item->m_bond = bond;
                    break;
                }
            }
            item->RaiseLengthChanged();
        }
    }

    double BondsDetailView::LengthOf(std::shared_ptr<SKAsymmetricBond> const& bond) const
    {
        return m_controller ? m_controller->BondLength(m_frame, bond) : 0.0;
    }

    void BondsDetailView::CommitLength(std::shared_ptr<SKAsymmetricBond> const& bond,
                                       double length)
    {
        if (m_controller)
            m_controller->SetBondLength(m_frame, bond, length);
    }

    void BondsDetailView::BondChanged()
    {
        if (m_controller)
            m_controller->ReloadRenderer();
    }

    void BondsDetailView::OnRecomputeClick([[maybe_unused]] IInspectable const&,
                                           [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_controller)
            m_controller->RecomputeBonds(m_frame);
    }

    void BondsDetailView::OnSelectionChanged(
        [[maybe_unused]] ItemsView const&,
        [[maybe_unused]] ItemsViewSelectionChangedEventArgs const&)
    {
        if (m_suppressSelectionEvents || !m_controller || !m_frame.lock())
            return;

        std::set<int64_t> rows;
        if (auto selected = List().SelectedItems())
        {
            for (auto const& entry : selected)
            {
                if (auto item = entry.try_as<BondFlatItem>())
                    rows.insert(item->m_row);
            }
        }
        m_controller->SetBondSelection(m_frame, rows);
    }

    void BondsDetailView::OnFieldGotFocus([[maybe_unused]] IInspectable const&,
                                          RoutedEventArgs const& e)
    {
        auto tb = e.OriginalSource().try_as<TextBox>();
        if (!tb || !tb.DataContext().try_as<BondFlatItem>())
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

    void BondsDetailView::OnVisibilityClick(IInspectable const& sender,
                                            [[maybe_unused]] RoutedEventArgs const&)
    {
        auto box = sender.try_as<CheckBox>();
        if (!box)
            return;

        auto item = box.DataContext().try_as<BondFlatItem>();
        if (!item)
            return;

        // The box has already toggled itself, and not being three-state it cannot
        // have landed on null.
        const auto value = box.IsChecked();
        item->WriteVisible(value ? value.Value() : true);
    }

    void BondsDetailView::OnVisibilityBoxDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        if (auto box = sender.try_as<ToggleButton>())
            DetailControls::SyncCheckFromDataContext(box, L"Visible");
    }
}
