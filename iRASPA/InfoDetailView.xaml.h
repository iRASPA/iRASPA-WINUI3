#pragma once

#include "InfoDetailView.g.h"
#include "DetailControls.h"
#include "DocumentController.h"

#include "infoviewer.h"
#include "object.h"
#include "rkdate.h"
#include "rkstring.h"

#include <functional>
#include <optional>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa StructureInfoDetailViewController: the Info tab of the inspector.
    // The form itself is in InfoDetailView.xaml; what lives here is the table
    // pairing each named field with the model property it shows, so reloading
    // and committing are one loop each rather than a setter per control.
    struct InfoDetailView : InfoDetailViewT<InfoDetailView>
    {
        InfoDetailView();

        // Not projected: handed over in C++ right after construction.
        void SetController(DocumentController* controller);
        // Cocoa reloads the inspector's fields whenever the selection changes;
        // the controls themselves stay, unlike the old rebuild-from-scratch.
        void Reload();

        void OnTextCommit(winrt::Windows::Foundation::IInspectable const& sender,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnEnumCommit(winrt::Windows::Foundation::IInspectable const& sender,
                          winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        // The country popup is filled from the OS geo list, so it is not in the
        // field table with the fixed-item ones.
        void OnCountryCommit(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnDateCommit(winrt::Microsoft::UI::Xaml::Controls::CalendarDatePicker const& sender,
                          winrt::Microsoft::UI::Xaml::Controls::CalendarDatePickerDateChangedEventArgs const& e);

    private:
        // A field of the form and the model property behind it. Properties that
        // are declared on InfoViewer/InfoEditor rather than on Object itself read
        // blank and ignore edits for objects implementing neither, which is what
        // the old per-field `viewer ? ... : hstring{}` did.
        struct TextField
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBox box{ nullptr };
            std::function<RKString(Object&)> read;
            std::function<void(Object&, RKString const&)> write;
        };
        struct EnumField
        {
            winrt::Microsoft::UI::Xaml::Controls::ComboBox box{ nullptr };
            std::function<int(Object&)> read;
            std::function<void(Object&, int)> write;
        };
        struct DateField
        {
            winrt::Microsoft::UI::Xaml::Controls::CalendarDatePicker picker{ nullptr };
            std::function<RKDate(Object&)> read;
            std::function<void(Object&, RKDate const&)> write;
        };

        void BuildFieldTable();

        void AddText(winrt::Microsoft::UI::Xaml::Controls::TextBox const& box,
                     RKString (Object::*get)(), void (Object::*set)(RKString));
        void AddText(winrt::Microsoft::UI::Xaml::Controls::TextBox const& box,
                     RKString (InfoViewer::*get)(), void (InfoEditor::*set)(RKString));
        void AddDate(winrt::Microsoft::UI::Xaml::Controls::CalendarDatePicker const& picker,
                     RKDate (Object::*get)(), void (Object::*set)(RKDate));
        void AddDate(winrt::Microsoft::UI::Xaml::Controls::CalendarDatePicker const& picker,
                     RKDate (InfoViewer::*get)(), void (InfoEditor::*set)(RKDate));

        // The popups all stand for an InfoViewer enum whose order matches the
        // items in markup, so one template covers them.
        template <class Enum>
        void AddEnum(winrt::Microsoft::UI::Xaml::Controls::ComboBox const& box,
                     Enum (InfoViewer::*get)(), void (InfoEditor::*set)(Enum))
        {
            m_enumFields.push_back(EnumField{
                box,
                [get](Object& o) -> int
                {
                    auto* viewer = dynamic_cast<InfoViewer*>(&o);
                    return viewer ? static_cast<int>((viewer->*get)()) : 0;
                },
                [set](Object& o, int value)
                {
                    if (auto* editor = dynamic_cast<InfoEditor*>(&o))
                        (editor->*set)(static_cast<Enum>(value));
                } });
        }

        // Cocoa's creationTabView: the method popup decides which sub-form shows.
        void ApplyMethodVisibility(int method);

        // The field table reads an Object, so the selection is asked through it.
        template <class T, class Read>
        std::optional<T> Agreed(Read const& read) const
        {
            if (!m_controller)
                return std::nullopt;
            return m_controller->AgreedValue<Object>(
                [&read](std::shared_ptr<Object> const& object) { return read(*object); });
        }

        DocumentController* m_controller{ nullptr };
        std::vector<TextField> m_textFields;
        std::vector<EnumField> m_enumFields;
        std::vector<DateField> m_dateFields;
        // Set while Reload fills the controls, so populating a field is not
        // mistaken for the user editing it.
        bool m_suppressEvents = false;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct InfoDetailView : InfoDetailViewT<InfoDetailView, implementation::InfoDetailView>
    {
    };
}
