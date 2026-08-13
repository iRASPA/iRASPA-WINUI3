#include "pch.h"
#include "ElementsDetailView.xaml.h"
#if __has_include("ElementsDetailView.g.cpp")
#include "ElementsDetailView.g.cpp"
#endif

#include "DetailControls.h"
#include "forcefieldset.h"
#include "forcefieldsets.h"
#include "forcefieldtype.h"
#include "skcolorset.h"
#include "skcolorsets.h"
#include "skelement.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include "rkcolor.h"
#include "rkstring.h"

#include <cmath>
#include <functional>
#include <optional>
#include <sstream>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::UI;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        // What an in-place edit affects, so the pane can refresh the right things.
        enum class ElementEditKind { Visibility, Color, Parameters, Mass };
        using ElementEditedFn = std::function<void(ElementEditKind)>;

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

        std::optional<int> AtomicNumberForSymbol(hstring const& text)
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

        Color RKColorToWinColor(RKColor const& c)
        {
            return Color{ static_cast<uint8_t>(c.alpha()),
                          static_cast<uint8_t>(c.red()),
                          static_cast<uint8_t>(c.green()),
                          static_cast<uint8_t>(c.blue()) };
        }

        RKColor WinColorToRKColor(Color const& c)
        {
            return RKColor::fromRgb(c.R, c.G, c.B, c.A);
        }
    }

    // {Binding} property backing for ElementCardItem: a DataTemplate in a
    // resource dictionary cannot use x:Bind against a plain C++ class, which has
    // no binding metadata of its own.
    struct ElementItemProperty : implements<ElementItemProperty, ICustomProperty>
    {
        using Getter = std::function<IInspectable(IInspectable const&)>;
        using Setter = std::function<void(IInspectable const&, IInspectable const&)>;

        ElementItemProperty(hstring name, winrt::Windows::UI::Xaml::Interop::TypeName type,
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

    // Card view model for one force-field atom type (one row of the Cocoa
    // NSTableView). Fields write straight through to the ForceFieldType /
    // SKColorSet via TwoWay bindings, like the Cocoa cell's target/actions.
    struct ElementCardItem : implements<ElementCardItem,
                                       ICustomPropertyProvider,
                                       INotifyPropertyChanged,
                                       IStringable>
    {
        ForceFieldSet* m_set{ nullptr };
        SKColorSet* m_colorSet{ nullptr };
        int m_row{ -1 };
        bool m_editable{ false };
        // The built-in types keep their identity even in an editable set, as in
        // Cocoa: their name, element and mass are what the structures resolve
        // their colors and radii by.
        bool m_typeEditable{ false };
        bool m_colorEditable{ false };
        ElementEditedFn m_onEdited;
        // The name is unique across the set and doubles as the key of the color
        // sets, so renaming is the document's to do, not the card's.
        ElementsDetailView* m_view{ nullptr };
        winrt::event<PropertyChangedEventHandler> m_propertyChanged;

        ForceFieldType* AtomType() const
        {
            if (!m_set || m_row < 0 ||
                m_row >= static_cast<int>(m_set->atomTypeList().size()))
                return nullptr;
            return &m_set->atomTypeList()[static_cast<size_t>(m_row)];
        }

        SKElement const* Element() const
        {
            auto* type = AtomType();
            if (!type)
                return nullptr;
            const int z = static_cast<int>(type->atomicNumber());
            if (z < 0 || z >= static_cast<int>(PredefinedElements::predefinedElements.size()))
                return nullptr;
            return &PredefinedElements::predefinedElements[static_cast<size_t>(z)];
        }

        void Raise(wchar_t const* prop)
        {
            m_propertyChanged(*this, PropertyChangedEventArgs(prop));
        }

        // Everything derived from the atomic number (element identity changed).
        void RaiseElementDerived()
        {
            for (auto* p : { L"ElementName", L"Symbol", L"AtomicNumber", L"GroupText",
                             L"PeriodText", L"AtomicRadius", L"CovalentRadius",
                             L"VDWRadius", L"TripleBondRadius", L"Oxidation" })
                Raise(p);
        }

        // --- reads ---

        hstring SortIndexText() const { return hstring(std::to_wstring(m_row)); }

        hstring BigSymbolText() const
        {
            auto* type = AtomType();
            return type ? hstring(type->forceFieldStringIdentifier().toStdWString())
                        : hstring{};
        }

        hstring ElementNameText() const
        {
            auto const* el = Element();
            return el ? hstring(el->_name.toStdWString()) : hstring(L"Unknown");
        }

        RKColor CurrentColor() const
        {
            RKColor color = RKColor::fromRgb(128, 128, 128);
            auto* type = AtomType();
            if (type && m_colorSet)
            {
                if (const RKColor* c = static_cast<const SKColorSet&>(*m_colorSet)
                        [type->forceFieldStringIdentifier()])
                    color = *c;
            }
            return color;
        }

        hstring OxidationText() const
        {
            auto const* el = Element();
            if (!el)
                return L"";
            std::wostringstream oss;
            bool first = true;
            for (int state : el->_possibleOxidationStates)
            {
                if (!first)
                    oss << L",";
                oss << state;
                first = false;
            }
            return hstring(oss.str());
        }

        // --- writes ---

        void WriteVisible(bool visible)
        {
            auto* type = AtomType();
            if (!type || !m_editable || type->isVisible() == visible)
                return;
            type->setIsVisible(visible);
            Raise(L"Visible");
            if (m_onEdited)
                m_onEdited(ElementEditKind::Visibility);
        }

        // Cocoa changeUniqueForceFieldName: the document takes it, because the
        // color sets are keyed by this name; the card only re-reads afterwards.
        void WriteTypeName(hstring const& text);

        void WriteAtomicNumber(hstring const& text)
        {
            auto* type = AtomType();
            if (!type || !m_typeEditable)
            {
                Raise(L"AtomicNumber");
                return;
            }
            auto v = ParseDouble(text);
            const int maxZ = static_cast<int>(PredefinedElements::predefinedElements.size()) - 1;
            if (!v || *v < 0 || *v > maxZ)
            {
                Raise(L"AtomicNumber"); // invalid: restore
                return;
            }
            const int z = static_cast<int>(*v);
            if (z == static_cast<int>(type->atomicNumber()))
            {
                Raise(L"AtomicNumber");
                return;
            }
            type->setAtomicNumber(z);
            RaiseElementDerived();
            if (m_onEdited)
                m_onEdited(ElementEditKind::Parameters);
        }

        void WriteSymbol(hstring const& text)
        {
            auto* type = AtomType();
            if (!type || !m_typeEditable)
            {
                Raise(L"Symbol");
                return;
            }
            auto z = AtomicNumberForSymbol(text);
            if (!z || *z == static_cast<int>(type->atomicNumber()))
            {
                Raise(L"Symbol"); // unknown symbol: revert the field
                return;
            }
            type->setAtomicNumber(*z);
            RaiseElementDerived();
            if (m_onEdited)
                m_onEdited(ElementEditKind::Parameters);
        }

        // Shared commit path for the numeric fields: parse, ignore no-ops,
        // write through, re-raise so the display re-formats to %.3f. The mass
        // belongs to the type rather than to the set, so it needs the stricter
        // gate; the radius and the potential parameters do not, as in Cocoa.
        void CommitDouble(hstring const& text, wchar_t const* prop, double current,
                          std::function<void(double)> const& apply, ElementEditKind kind,
                          bool typeGated = false)
        {
            auto* type = AtomType();
            if (!type || !(typeGated ? m_typeEditable : m_editable))
            {
                Raise(prop);
                return;
            }
            auto v = ParseDouble(text);
            if (!v || !std::isfinite(*v) || std::abs(*v - current) < 1e-12)
            {
                Raise(prop); // invalid or unchanged: restore formatted value
                return;
            }
            apply(*v);
            Raise(prop);
            if (m_onEdited)
                m_onEdited(kind);
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
            auto self = [](IInspectable const& target) { return target.try_as<ElementCardItem>(); };

            auto readOnlyString = [&](std::function<hstring(ElementCardItem&)> read)
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self, read](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? read(*s.get()) : hstring{});
                    });
            };

            if (name == L"SortIndex")
                return readOnlyString([](ElementCardItem& s) { return s.SortIndexText(); });
            if (name == L"BigSymbol")
                return readOnlyString([](ElementCardItem& s) { return s.BigSymbolText(); });
            if (name == L"ElementName")
                return readOnlyString([](ElementCardItem& s) { return s.ElementNameText(); });
            if (name == L"Oxidation")
                return readOnlyString([](ElementCardItem& s) { return s.OxidationText(); });
            if (name == L"GroupText")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? hstring(std::to_wstring(el->_group)) : hstring{};
                });
            }
            if (name == L"PeriodText")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? hstring(std::to_wstring(el->_period)) : hstring{};
                });
            }
            if (name == L"AtomicRadius")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? FormatFixed(el->_atomRadius) : hstring{};
                });
            }
            if (name == L"CovalentRadius")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? FormatFixed(el->_covalentRadius) : hstring{};
                });
            }
            if (name == L"VDWRadius")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? FormatFixed(el->_VDWRadius) : hstring{};
                });
            }
            if (name == L"TripleBondRadius")
            {
                return readOnlyString([](ElementCardItem& s)
                {
                    auto const* el = s.Element();
                    return el ? FormatFixed(el->_tripleBondCovalentRadius) : hstring{};
                });
            }
            if (name == L"Swatch")
            {
                return make<ElementItemProperty>(name, xaml_typename<Brush>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        RKColor c = s ? s->CurrentColor() : RKColor::fromRgb(128, 128, 128);
                        return SolidColorBrush(RKColorToWinColor(c));
                    });
            }
            if (name == L"Visible")
            {
                return make<ElementItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type ? type->isVisible() : true);
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        if (!s)
                            return;
                        // ItemsView recycle clears IsChecked to null and the
                        // TwoWay binding writes that back here. Refuse it and
                        // push the real value so the box cannot stick on '-'.
                        if (!v)
                        {
                            s->Raise(L"Visible");
                            return;
                        }
                        s->WriteVisible(unbox_value_or<bool>(v, true));
                    });
            }
            if (name == L"AtomicNumber")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type
                            ? hstring(std::to_wstring(static_cast<int>(type->atomicNumber())))
                            : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteAtomicNumber(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"Symbol")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto const* el = s ? s->Element() : nullptr;
                        return box_value(el ? hstring(el->_chemicalSymbol.toStdWString())
                                            : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteSymbol(unbox_value_or<hstring>(v, L""));
                    });
            }
            if (name == L"Mass")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type ? FormatFixed(type->mass()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        if (!type)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"Mass", type->mass(),
                            [type](double nv) { type->setMass(nv); },
                            ElementEditKind::Mass, true);
                    });
            }
            if (name == L"UserRadius")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type ? FormatFixed(type->userDefinedRadius()) : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        if (!type)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"UserRadius",
                                        type->userDefinedRadius(),
                            [type](double nv) { type->setUserDefinedRadius(nv); },
                            ElementEditKind::Parameters);
                    });
            }
            if (name == L"Epsilon")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type ? FormatFixed(type->potentialParameters().x)
                                              : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        if (!type)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"Epsilon",
                                        type->potentialParameters().x,
                            [type](double nv) { type->setEpsilonPotentialParameter(nv); },
                            ElementEditKind::Parameters);
                    });
            }
            if (name == L"Sigma")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        return box_value(type ? FormatFixed(type->potentialParameters().y)
                                              : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        auto s = self(t);
                        auto* type = s ? s->AtomType() : nullptr;
                        if (!type)
                            return;
                        s->CommitDouble(unbox_value_or<hstring>(v, L""), L"Sigma",
                                        type->potentialParameters().y,
                            [type](double nv) { type->setSigmaPotentialParameter(nv); },
                            ElementEditKind::Parameters);
                    });
            }
            if (name == L"IsEditable")
            {
                return make<ElementItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->m_editable : false);
                    });
            }
            if (name == L"ReadOnly")
            {
                return make<ElementItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? !s->m_editable : true);
                    });
            }
            if (name == L"TypeReadOnly")
            {
                return make<ElementItemProperty>(name, xaml_typename<bool>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? !s->m_typeEditable : true);
                    });
            }
            if (name == L"TypeName")
            {
                return make<ElementItemProperty>(name, xaml_typename<hstring>(),
                    [self](IInspectable const& t) -> IInspectable
                    {
                        auto s = self(t);
                        return box_value(s ? s->BigSymbolText() : hstring{});
                    },
                    [self](IInspectable const& t, IInspectable const& v)
                    {
                        if (auto s = self(t))
                            s->WriteTypeName(unbox_value_or<hstring>(v, L""));
                    });
            }
            return nullptr;
        }
        ICustomProperty GetIndexedProperty(hstring const&, winrt::Windows::UI::Xaml::Interop::TypeName const&)
        {
            return nullptr;
        }
        hstring GetStringRepresentation() { return BigSymbolText(); }
        winrt::Windows::UI::Xaml::Interop::TypeName Type()
        {
            return xaml_typename<IInspectable>();
        }

        // IStringable
        hstring ToString() { return BigSymbolText(); }
    };

    void ElementCardItem::WriteTypeName(hstring const& text)
    {
        if (!m_typeEditable || !m_view || !m_view->RenameType(m_row, std::wstring(text)))
        {
            // The box has just pushed the name that was refused, and a TwoWay
            // binding does not re-read its source inside the commit that wrote
            // it: put the name that stuck back once the commit has unwound.
            if (m_view)
            {
                auto lifetime = get_strong();
                m_view->DispatcherQueue().TryEnqueue([lifetime] { lifetime->Raise(L"TypeName"); });
            }
            return;
        }
        Raise(L"TypeName");
        Raise(L"Swatch"); // the color sets are keyed by the name
        if (m_onEdited)
            m_onEdited(ElementEditKind::Parameters);
    }

    ElementsDetailView::ElementsDetailView()
    {
        InitializeComponent();

        m_cards = single_threaded_observable_vector<IInspectable>();
        List().ItemsSource(m_cards);
        List().AddHandler(UIElement::PointerPressedEvent(),
                          box_value(PointerEventHandler{ this, &ElementsDetailView::OnListPointerPressed }),
                          true);
        m_rowMenu = Resources().Lookup(box_value(hstring(L"RowMenu"))).as<MenuFlyout>();
    }

    void ElementsDetailView::Reload()
    {
        const bool haveDocument = m_controller && m_controller->Document();
        NoDocumentHint().Visibility(haveDocument ? Visibility::Collapsed : Visibility::Visible);
        List().Visibility(haveDocument ? Visibility::Visible : Visibility::Collapsed);
        BottomBar().Visibility(haveDocument ? Visibility::Visible : Visibility::Collapsed);
        if (!haveDocument)
        {
            Clear();
            return;
        }

        try
        {
            FillSetCombos();
            ReloadCards();
        }
        catch (hresult_error const& ex)
        {
            m_controller->Log(std::wstring(L"Elements inspector error: ") +
                              std::wstring(ex.message()));
        }
        catch (...)
        {
            m_controller->Log(L"Elements inspector error");
        }
    }

    void ElementsDetailView::Clear()
    {
        if (m_cards)
            m_cards.Clear();
    }

    // The sets come from the document, so the combos are refilled rather than
    // kept: a newly opened document has its own.
    void ElementsDetailView::FillSetCombos()
    {
        auto& document = *m_controller->Document();

        m_suppress = true;
        ForceFieldCombo().Items().Clear();
        for (auto const& set : document.forceFieldSets().forceFieldSets())
            ForceFieldCombo().Items().Append(box_value(hstring(set.displayName().toStdWString())));
        if (ForceFieldCombo().Items().Size() > 0)
        {
            if (m_forceFieldSetIndex < 0 ||
                m_forceFieldSetIndex >= static_cast<int>(ForceFieldCombo().Items().Size()))
                m_forceFieldSetIndex = 0;
            ForceFieldCombo().SelectedIndex(m_forceFieldSetIndex);
            // The combo is editable, so whatever was typed has to be replaced by
            // the name of the set actually shown.
            ForceFieldCombo().Text(unbox_value<hstring>(
                ForceFieldCombo().Items().GetAt(static_cast<uint32_t>(m_forceFieldSetIndex))));
        }

        ColorCombo().Items().Clear();
        for (auto const& set : document.colorSets().colorSets())
            ColorCombo().Items().Append(box_value(hstring(set.displayName().toStdWString())));
        if (ColorCombo().Items().Size() > 0)
        {
            if (m_colorSetIndex < 0 ||
                m_colorSetIndex >= static_cast<int>(ColorCombo().Items().Size()))
                m_colorSetIndex = 0;
            ColorCombo().SelectedIndex(m_colorSetIndex);
            ColorCombo().Text(unbox_value<hstring>(
                ColorCombo().Items().GetAt(static_cast<uint32_t>(m_colorSetIndex))));
        }
        m_suppress = false;
    }

    void ElementsDetailView::ReloadCards()
    {
        if (!m_cards || !m_controller || !m_controller->Document())
            return;

        m_cards.Clear();
        auto& document = *m_controller->Document();

        auto& forceFieldSets = document.forceFieldSets();
        if (m_forceFieldSetIndex < 0 ||
            m_forceFieldSetIndex >= static_cast<int>(forceFieldSets.forceFieldSets().size()))
            return;
        auto& forceFieldSet = forceFieldSets[m_forceFieldSetIndex];

        auto& colorSets = document.colorSets();
        if (m_colorSetIndex < 0 ||
            m_colorSetIndex >= static_cast<int>(colorSets.colorSets().size()))
            m_colorSetIndex = 0;
        auto& colorSet = colorSets[static_cast<size_t>(m_colorSetIndex)];

        // The cards mutate the document data themselves; what is left is pushing
        // the change at the structures. Mass is model-only, as in Cocoa
        // changeAtomMass.
        ElementEditedFn onEdited = [this](ElementEditKind kind)
        {
            if (kind != ElementEditKind::Mass && m_controller)
                m_controller->ReapplyForceFieldAndColors();
        };

        const bool setEditable = forceFieldSet.editable();
        const size_t count = forceFieldSet.atomTypeList().size();
        for (size_t i = 0; i < count; ++i)
        {
            auto item = make_self<ElementCardItem>();
            item->m_set = &forceFieldSet;
            item->m_colorSet = &colorSet;
            item->m_row = static_cast<int>(i);
            item->m_editable = setEditable;
            item->m_typeEditable = setEditable && !ForceFieldSet::isDefaultForceFieldType(
                forceFieldSet.atomTypeList()[i].forceFieldStringIdentifier());
            item->m_colorEditable = colorSet.editable();
            item->m_onEdited = onEdited;
            item->m_view = this;
            m_cards.Append(*item);
        }

        // Cocoa selectionIndexesForProposedSelection: a row can only be picked
        // in a set that may be edited, there being nothing to do with it
        // otherwise. The [+] and [-] work off that selection.
        List().SelectionMode(setEditable ? ItemsViewSelectionMode::Single
                                         : ItemsViewSelectionMode::None);
        UpdateTypeButtons();

        // Forking a set rebuilds every card and recycles the containers: any
        // visibility box that landed on null during that pass is coerced once
        // the new DataContexts are in place.
        auto lifetime = get_strong();
        DispatcherQueue().TryEnqueue([lifetime] { lifetime->CoerceVisibilityCheckBoxes(); });
    }

    void ElementsDetailView::CoerceVisibilityCheckBoxes()
    {
        std::function<void(DependencyObject const&)> walk;
        walk = [&](DependencyObject const& node)
        {
            if (auto box = node.try_as<ToggleButton>())
                DetailControls::SyncCheckFromDataContext(box, L"Visible");
            const int n = VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < n; ++i)
                walk(VisualTreeHelper::GetChild(node, i));
        };
        walk(List());
    }

    void ElementsDetailView::OnVisibilityBoxDataContextChanged(
        FrameworkElement const& sender,
        [[maybe_unused]] DataContextChangedEventArgs const&)
    {
        if (auto box = sender.try_as<ToggleButton>())
            DetailControls::SyncCheckFromDataContext(box, L"Visible");
    }

    void ElementsDetailView::OnVisibilityClick(
        IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const&)
    {
        auto box = sender.try_as<CheckBox>();
        if (!box)
            return;

        auto item = box.DataContext().try_as<ElementCardItem>();
        if (!item)
            return;

        auto* type = item->AtomType();
        if (!type || !item->m_editable)
        {
            box.IsChecked(type ? type->isVisible() : true);
            return;
        }

        // The box has already toggled itself; take that as the wanted value, and
        // if it landed on null (recycle), fall back to flipping the model.
        auto state = box.IsChecked();
        const bool next = state ? state.Value() : !type->isVisible();
        item->WriteVisible(next);
        box.IsChecked(type->isVisible());
    }

    // The row the type operations act on, which is the selected card: Cocoa
    // works off selectedRow for the buttons and clickedRow for the menu.
    int ElementsDetailView::SelectedRow()
    {
        if (auto card = List().SelectedItem().try_as<ElementCardItem>())
            return card->m_row;
        return -1;
    }

    void ElementsDetailView::UpdateTypeButtons()
    {
        const int row = SelectedRow();
        AddTypeButton().IsEnabled(row >= 0);
        RemoveTypeButton().IsEnabled(row >= 0 && CanRemoveRow(row));
    }

    bool ElementsDetailView::CanRemoveRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(m_cards.Size()))
            return false;
        auto card = m_cards.GetAt(static_cast<uint32_t>(row)).try_as<ElementCardItem>();
        return card && card->m_typeEditable;
    }

    void ElementsDetailView::OnSelectionChanged(
        [[maybe_unused]] ItemsView const&,
        [[maybe_unused]] ItemsViewSelectionChangedEventArgs const&)
    {
        UpdateTypeButtons();
    }

    bool ElementsDetailView::RenameType(int row, std::wstring const& name)
    {
        if (!m_controller)
            return false;
        return m_controller->RenameForceFieldType(m_forceFieldSetIndex, row, name);
    }

    // Insert and delete both renumber the rows below, so the cards are rebuilt
    // and the row acted on stays selected (Cocoa reloads the whole column 0).
    void ElementsDetailView::InsertTypeAt(int row)
    {
        if (!m_controller || !m_controller->InsertForceFieldType(m_forceFieldSetIndex, row))
            return;
        ReloadCards();
        SelectRow(row + 1);
    }

    void ElementsDetailView::RemoveTypeAt(int row)
    {
        if (!m_controller || !m_controller->RemoveForceFieldType(m_forceFieldSetIndex, row))
            return;
        ReloadCards();
        SelectRow(row < static_cast<int>(m_cards.Size()) ? row : row - 1);
        if (m_controller)
            m_controller->ReapplyForceFieldAndColors();
    }

    void ElementsDetailView::SelectRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(m_cards.Size()))
        {
            UpdateTypeButtons();
            return;
        }
        List().Select(row);
        List().StartBringItemIntoView(row, BringIntoViewOptions());
        UpdateTypeButtons();
    }

    void ElementsDetailView::OnAddTypeClick([[maybe_unused]] IInspectable const&,
                                            [[maybe_unused]] RoutedEventArgs const&)
    {
        InsertTypeAt(SelectedRow());
    }

    void ElementsDetailView::OnRemoveTypeClick([[maybe_unused]] IInspectable const&,
                                               [[maybe_unused]] RoutedEventArgs const&)
    {
        RemoveTypeAt(SelectedRow());
    }

    // Cocoa's elementContextMenu, gated as its validateMenuItem does: both items
    // need an editable set and a row, and deleting also needs that row not to be
    // one of the built-in types.
    void ElementsDetailView::OnListRightTapped([[maybe_unused]] IInspectable const&,
                                               RightTappedRoutedEventArgs const& e)
    {
        if (!m_rowMenu)
            return;

        int row = -1;
        auto element = e.OriginalSource().try_as<DependencyObject>();
        while (element)
        {
            if (auto fe = element.try_as<FrameworkElement>())
            {
                if (auto card = fe.DataContext().try_as<ElementCardItem>())
                {
                    row = card->m_row;
                    break;
                }
            }
            element = VisualTreeHelper::GetParent(element);
        }
        if (row < 0)
            return;

        auto card = m_cards.GetAt(static_cast<uint32_t>(row)).try_as<ElementCardItem>();
        if (!card || !card->m_editable)
            return;

        m_menuRow = row;
        InsertTypeItem().IsEnabled(true);
        DeleteTypeItem().IsEnabled(CanRemoveRow(row));
        e.Handled(true);
        m_rowMenu.ShowAt(List(), e.GetPosition(List()));
    }

    void ElementsDetailView::OnInsertTypeMenu([[maybe_unused]] IInspectable const&,
                                              [[maybe_unused]] RoutedEventArgs const&)
    {
        InsertTypeAt(m_menuRow);
    }

    void ElementsDetailView::OnDeleteTypeMenu([[maybe_unused]] IInspectable const&,
                                              [[maybe_unused]] RoutedEventArgs const&)
    {
        RemoveTypeAt(m_menuRow);
    }

    // Hit-test the card's color well (Tag='colorwell'): a DataTemplate cannot
    // wire a Click handler, so the picker opens from the routed PointerPressed.
    void ElementsDetailView::OnListPointerPressed([[maybe_unused]] IInspectable const&,
                                                  PointerRoutedEventArgs const& e)
    {
        try
        {
            auto element = e.OriginalSource().try_as<DependencyObject>();
            while (element)
            {
                if (auto fe = element.try_as<FrameworkElement>())
                {
                    if (auto tag = fe.Tag())
                    {
                        if (auto tagText = tag.try_as<hstring>(); tagText && *tagText == L"colorwell")
                        {
                            if (auto item = fe.DataContext().try_as<ElementCardItem>())
                            {
                                e.Handled(true);
                                PickColorAsync(*item.get());
                            }
                            return;
                        }
                    }
                }
                element = VisualTreeHelper::GetParent(element);
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget ElementsDetailView::PickColorAsync(IInspectable item)
    {
        auto lifetime = get_strong();

        auto card = item.try_as<ElementCardItem>();
        if (!card)
            co_return;
        auto* type = card->AtomType();
        if (!type || !card->m_colorSet)
            co_return;
        if (!card->m_colorEditable)
        {
            if (m_controller)
                m_controller->Log(L"Selected color set is not editable");
            co_return;
        }

        RKColor current = card->CurrentColor();

        auto dlg = ContentDialog();
        dlg.XamlRoot(XamlRoot());
        dlg.Title(box_value(L"Element color"));
        dlg.PrimaryButtonText(L"OK");
        dlg.CloseButtonText(L"Cancel");

        auto picker = ColorPicker();
        picker.Color(RKColorToWinColor(current));
        picker.IsMoreButtonVisible(true);
        picker.IsColorSliderVisible(true);
        picker.IsColorChannelTextInputVisible(true);
        picker.IsHexInputVisible(true);
        dlg.Content(picker);

        auto result = co_await dlg.ShowAsync();
        if (result != ContentDialogResult::Primary)
            co_return;

        RKColor chosen = WinColorToRKColor(picker.Color());
        (*card->m_colorSet)[type->forceFieldStringIdentifier()] = chosen;
        card->Raise(L"Swatch");
        if (m_controller)
        {
            m_controller->ReapplyForceFieldAndColors();
            m_controller->Log(L"Element color updated");
        }
    }

    void ElementsDetailView::OnForceFieldSetChanged([[maybe_unused]] IInspectable const&,
                                                    [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        if (m_suppress)
            return;
        const int index = ForceFieldCombo().SelectedIndex();
        if (index < 0)
            return;
        m_forceFieldSetIndex = index;
        ReloadCards();
    }

    void ElementsDetailView::OnColorSetChanged([[maybe_unused]] IInspectable const&,
                                               [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        if (m_suppress)
            return;
        const int index = ColorCombo().SelectedIndex();
        if (index < 0)
            return;
        m_colorSetIndex = index;
        ReloadCards();
    }

    void ElementsDetailView::OnForceFieldSetSubmitted(
        [[maybe_unused]] ComboBox const&, ComboBoxTextSubmittedEventArgs const& e)
    {
        if (!m_controller || !m_controller->Document())
            return;
        // The typed name is not one of the items, so the combo would put the
        // previous one back; the selection is ours to set here.
        e.Handled(true);

        const int index =
            m_controller->AddForceFieldSet(std::wstring(e.Text()), m_forceFieldSetIndex);
        if (index >= 0)
            m_forceFieldSetIndex = index;
        FillSetCombos();
        ReloadCards();
        // As Cocoa hands first responder back to the table, so that what is
        // typed next goes to the cards and not into the combo.
        List().Focus(FocusState::Programmatic);
    }

    void ElementsDetailView::OnColorSetSubmitted(
        [[maybe_unused]] ComboBox const&, ComboBoxTextSubmittedEventArgs const& e)
    {
        if (!m_controller || !m_controller->Document())
            return;
        e.Handled(true);

        const int index = m_controller->AddColorSet(std::wstring(e.Text()), m_colorSetIndex);
        if (index >= 0)
            m_colorSetIndex = index;
        FillSetCombos();
        ReloadCards();
        List().Focus(FocusState::Programmatic);
    }
}
