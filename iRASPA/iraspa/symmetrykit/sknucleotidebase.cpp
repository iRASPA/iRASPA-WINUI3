/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "sknucleotidebase.h"
#include "sknucleotide.h"

namespace
{
  SKNucleotideBaseKind baseKindFromChar(char base)
  {
    switch (base)
    {
    case 'A': return SKNucleotideBaseKind::adenine;
    case 'C': return SKNucleotideBaseKind::cytosine;
    case 'G': return SKNucleotideBaseKind::guanine;
    case 'T': return SKNucleotideBaseKind::thymine;
    case 'U': return SKNucleotideBaseKind::uracil;
    default: return SKNucleotideBaseKind::unknown;
    }
  }
}

namespace SKNucleotideBase
{
  float vertexStructureTypeCode(SKNucleotideBaseKind baseKind, bool backbone)
  {
    if (backbone) return kNucleicBackboneStructureType;
    switch (baseKind)
    {
    case SKNucleotideBaseKind::adenine: return kNucleicAdenineStructureType;
    case SKNucleotideBaseKind::cytosine: return kNucleicCytosineStructureType;
    case SKNucleotideBaseKind::guanine: return kNucleicGuanineStructureType;
    case SKNucleotideBaseKind::thymine:
    case SKNucleotideBaseKind::uracil: return kNucleicThymineStructureType;
    case SKNucleotideBaseKind::unknown:
      break;
    }
    return kNucleicBackboneStructureType;
  }

  SKNucleotideBaseKind baseKindFromResidueName(const RKString &residueName)
  {
    const RKString name = residueName.trimmed().toUpper();
    if (name == RKStringLiteral("A") || name == RKStringLiteral("DA") || name == RKStringLiteral("ADE") ||
        name == RKStringLiteral("RA") || name == RKStringLiteral("A5") || name == RKStringLiteral("RA5"))
    {
      return SKNucleotideBaseKind::adenine;
    }
    if (name == RKStringLiteral("C") || name == RKStringLiteral("DC") || name == RKStringLiteral("CYT") ||
        name == RKStringLiteral("RC") || name == RKStringLiteral("C5") || name == RKStringLiteral("RC5"))
    {
      return SKNucleotideBaseKind::cytosine;
    }
    if (name == RKStringLiteral("G") || name == RKStringLiteral("DG") || name == RKStringLiteral("GUA") ||
        name == RKStringLiteral("RG") || name == RKStringLiteral("G5") || name == RKStringLiteral("RG5"))
    {
      return SKNucleotideBaseKind::guanine;
    }
    if (name == RKStringLiteral("T") || name == RKStringLiteral("DT") || name == RKStringLiteral("THY"))
    {
      return SKNucleotideBaseKind::thymine;
    }
    if (name == RKStringLiteral("U") || name == RKStringLiteral("RU") || name == RKStringLiteral("URI") ||
        name == RKStringLiteral("U5") || name == RKStringLiteral("RU5"))
    {
      return SKNucleotideBaseKind::uracil;
    }
    if (name.length() >= 2)
    {
      return baseKindFromChar(name[name.length() - 1]);
    }
    return SKNucleotideBaseKind::unknown;
  }

  bool areWatsonCrickComplementary(SKNucleotideBaseKind a, SKNucleotideBaseKind b)
  {
    if (a == SKNucleotideBaseKind::unknown || b == SKNucleotideBaseKind::unknown) return false;
    if (a == b) return false;
    const auto complements = [](SKNucleotideBaseKind x, SKNucleotideBaseKind y)
    {
      return (x == SKNucleotideBaseKind::adenine &&
              (y == SKNucleotideBaseKind::thymine || y == SKNucleotideBaseKind::uracil)) ||
             (y == SKNucleotideBaseKind::adenine &&
              (x == SKNucleotideBaseKind::thymine || x == SKNucleotideBaseKind::uracil)) ||
             (x == SKNucleotideBaseKind::guanine && y == SKNucleotideBaseKind::cytosine) ||
             (y == SKNucleotideBaseKind::guanine && x == SKNucleotideBaseKind::cytosine);
    };
    return complements(a, b);
  }

  bool areWatsonCrickComplementary(const RKString &residueNameA, const RKString &residueNameB)
  {
    return areWatsonCrickComplementary(baseKindFromResidueName(residueNameA), baseKindFromResidueName(residueNameB));
  }

  std::vector<RKString> riboseRingAtomNames()
  {
    return {RKStringLiteral("C1'"), RKStringLiteral("C2'"), RKStringLiteral("C3'"), RKStringLiteral("C4'"),
            RKStringLiteral("O4'")};
  }

  std::vector<RKString> baseRingAtomNames(SKNucleotideBaseKind baseKind)
  {
    switch (baseKind)
    {
    case SKNucleotideBaseKind::cytosine:
    case SKNucleotideBaseKind::thymine:
    case SKNucleotideBaseKind::uracil:
      return {RKStringLiteral("N1"), RKStringLiteral("C2"), RKStringLiteral("N3"),
              RKStringLiteral("C4"), RKStringLiteral("C5"), RKStringLiteral("C6")};
    case SKNucleotideBaseKind::adenine:
    case SKNucleotideBaseKind::guanine:
      return {RKStringLiteral("N9"), RKStringLiteral("C4"), RKStringLiteral("N3"), RKStringLiteral("C2"),
              RKStringLiteral("N1"), RKStringLiteral("C6"), RKStringLiteral("C5"), RKStringLiteral("N7"),
              RKStringLiteral("C8")};
    case SKNucleotideBaseKind::unknown:
      break;
    }
    return {};
  }

  RKString baseAnchorAtomName(SKNucleotideBaseKind baseKind)
  {
    switch (baseKind)
    {
    case SKNucleotideBaseKind::cytosine:
    case SKNucleotideBaseKind::thymine:
    case SKNucleotideBaseKind::uracil:
      return RKStringLiteral("N1");
    case SKNucleotideBaseKind::adenine:
    case SKNucleotideBaseKind::guanine:
      return RKStringLiteral("N9");
    case SKNucleotideBaseKind::unknown:
      break;
    }
    return RKString();
  }
}
