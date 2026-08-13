#pragma once

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <functional>
#include <utility>

namespace winrt::iRASPA_WinUI::implementation
{
    // The row view-models of the project, scene and frame lists are plain C++
    // objects rather than projected runtime classes, so XAML reaches their
    // properties through ICustomPropertyProvider. This is the one property
    // implementation all of them hand back: a name, a type and a getter, plus a
    // setter for the few two-way bindings (the movie visibility checkbox).
    struct RowProperty : implements<RowProperty, winrt::Microsoft::UI::Xaml::Data::ICustomProperty>
    {
        using Getter = std::function<winrt::Windows::Foundation::IInspectable(
            winrt::Windows::Foundation::IInspectable const&)>;
        using Setter = std::function<void(winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Windows::Foundation::IInspectable const&)>;

        RowProperty(hstring name,
                    winrt::Windows::UI::Xaml::Interop::TypeName type,
                    Getter getter,
                    Setter setter = nullptr)
            : m_name(std::move(name)), m_type(type),
              m_getter(std::move(getter)), m_setter(std::move(setter)) {}

        winrt::Windows::UI::Xaml::Interop::TypeName Type() const { return m_type; }
        hstring Name() const { return m_name; }
        bool CanRead() const { return true; }
        bool CanWrite() const { return static_cast<bool>(m_setter); }

        winrt::Windows::Foundation::IInspectable GetValue(
            winrt::Windows::Foundation::IInspectable const& target) const
        {
            return m_getter(target);
        }
        void SetValue(winrt::Windows::Foundation::IInspectable const& target,
                      winrt::Windows::Foundation::IInspectable const& value) const
        {
            if (!m_setter)
                throw hresult_not_implemented();
            m_setter(target, value);
        }
        winrt::Windows::Foundation::IInspectable GetIndexedValue(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&) const
        {
            throw hresult_not_implemented();
        }
        void SetIndexedValue(winrt::Windows::Foundation::IInspectable const&,
                             winrt::Windows::Foundation::IInspectable const&,
                             winrt::Windows::Foundation::IInspectable const&) const
        {
            throw hresult_not_implemented();
        }

    private:
        hstring m_name;
        winrt::Windows::UI::Xaml::Interop::TypeName m_type;
        Getter m_getter;
        Setter m_setter;
    };
}
