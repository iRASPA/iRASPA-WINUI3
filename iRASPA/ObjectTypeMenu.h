#pragma once

#include "object.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <functional>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa "Add Structure Context Menu": the five structure types, and for the
    // scene list an Add Objects submenu of primitives after them. The scene and
    // frame lists hang the same menu off their [+] button and differ only in what
    // a pick does, so the type table lives here rather than in either view.
    inline winrt::Microsoft::UI::Xaml::Controls::MenuFlyout BuildObjectTypeMenu(
        bool includePrimitives,
        std::function<void(ObjectType)> invoke)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        struct Entry
        {
            wchar_t const* text;
            ObjectType type;
        };
        static constexpr Entry structures[] = {
            { L"Add Crystal", ObjectType::crystal },
            { L"Add Molecular crystal", ObjectType::molecularCrystal },
            { L"Add Molecule", ObjectType::molecule },
            { L"Add Protein", ObjectType::protein },
            { L"Add Protein crystal", ObjectType::proteinCrystal },
        };
        static constexpr Entry primitives[] = {
            { L"Crystal ellipsoid", ObjectType::crystalEllipsoidPrimitive },
            { L"Crystal Cylinder", ObjectType::crystalCylinderPrimitive },
            { L"Crystal polygonal prism", ObjectType::crystalPolygonalPrismPrimitive },
            { L"Ellipsoid", ObjectType::ellipsoidPrimitive },
            { L"Cylinder", ObjectType::cylinderPrimitive },
            { L"Polygonal prism", ObjectType::polygonalPrismPrimitive },
        };

        auto makeItem = [&invoke](Entry const& entry)
        {
            MenuFlyoutItem item;
            item.Text(entry.text);
            const ObjectType type = entry.type;
            item.Click([invoke, type](winrt::Windows::Foundation::IInspectable const&,
                                      RoutedEventArgs const&) { invoke(type); });
            return item;
        };

        MenuFlyout menu;
        for (auto const& entry : structures)
            menu.Items().Append(makeItem(entry));

        if (includePrimitives)
        {
            MenuFlyoutSubItem objects;
            objects.Text(L"Add Objects");
            for (auto const& entry : primitives)
                objects.Items().Append(makeItem(entry));
            menu.Items().Append(objects);
        }
        return menu;
    }
}
