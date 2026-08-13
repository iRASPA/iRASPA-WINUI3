#pragma once

#include "object.h"

#include <iterator>

// Qt cellStructureTypeComboBox: the entries are ordered by ObjectType, so the
// row index is the enum value. The types Qt greys out have no conversion
// constructor behind them. The Cell pane shows this list and the document layer
// names and converts from it, so it lives here rather than in either.
struct StructureTypeEntry
{
    ObjectType type;
    wchar_t const* name;
    bool convertible;
};

inline constexpr StructureTypeEntry kStructureTypes[] = {
    { ObjectType::object,                         L"Unknown",                   false },
    { ObjectType::structure,                      L"Structure",                 false },
    { ObjectType::crystal,                        L"Crystal",                   true  },
    { ObjectType::molecularCrystal,               L"Molecular Crystal",         true  },
    { ObjectType::molecule,                       L"Molecule",                  true  },
    { ObjectType::protein,                        L"Protein",                   true  },
    { ObjectType::proteinCrystal,                 L"Protein Crystal",           true  },
    { ObjectType::proteinCrystalSolvent,          L"Protein Crystal Solvent",   false },
    { ObjectType::crystalSolvent,                 L"Crystal Solvent",           false },
    { ObjectType::molecularCrystalSolvent,        L"Molecular Crystal Solvent", false },
    { ObjectType::crystalEllipsoidPrimitive,      L"Crystal Ellipsoid",         true  },
    { ObjectType::crystalCylinderPrimitive,       L"Crystal Cylinder",          true  },
    { ObjectType::crystalPolygonalPrismPrimitive, L"Crystal Polygonal Prism",   true  },
    { ObjectType::ellipsoidPrimitive,             L"Ellipsoid",                 true  },
    { ObjectType::cylinderPrimitive,              L"Cylinder",                  true  },
    { ObjectType::polygonalPrismPrimitive,        L"Polygonal",                 true  },
    { ObjectType::gridVolume,                     L"Grid Volumetric Data",      false },
    { ObjectType::RASPADensityVolume,             L"RASPA Volumetric Data",     true  },
    { ObjectType::VTKDensityVolume,               L"VTK Volumetric Data",       true  },
    { ObjectType::VASPDensityVolume,              L"VASP Volumetric Data",      true  },
    { ObjectType::GaussianCubeVolume,             L"Cube Volumetric Data",      true  },
};

inline int IndexOfStructureType(ObjectType type)
{
    for (int i = 0; i < static_cast<int>(std::size(kStructureTypes)); ++i)
        if (kStructureTypes[i].type == type)
            return i;
    return -1;
}

inline wchar_t const* StructureTypeName(ObjectType type)
{
    const int index = IndexOfStructureType(type);
    return index >= 0 ? kStructureTypes[index].name : L"Unknown";
}
