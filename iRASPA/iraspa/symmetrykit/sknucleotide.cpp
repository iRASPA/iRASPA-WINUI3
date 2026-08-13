/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "sknucleotide.h"
#include <set>

namespace
{
  const std::set<RKString> &nucleotideResidueNames()
  {
    static const std::set<RKString> names = {
      RKStringLiteral("A"), RKStringLiteral("C"), RKStringLiteral("G"), RKStringLiteral("T"), RKStringLiteral("U"),
      RKStringLiteral("DA"), RKStringLiteral("DC"), RKStringLiteral("DG"), RKStringLiteral("DT"),
      RKStringLiteral("ADE"), RKStringLiteral("CYT"), RKStringLiteral("GUA"), RKStringLiteral("THY"), RKStringLiteral("URI"),
      RKStringLiteral("RA"), RKStringLiteral("RC"), RKStringLiteral("RG"), RKStringLiteral("RU"),
      RKStringLiteral("A5"), RKStringLiteral("C5"), RKStringLiteral("G5"), RKStringLiteral("U5"),
      RKStringLiteral("RA5"), RKStringLiteral("RC5"), RKStringLiteral("RG5"), RKStringLiteral("RU5")
    };
    return names;
  }
}

namespace SKNucleotide
{
  bool isNucleotideResidueName(const RKString &residueName)
  {
    return nucleotideResidueNames().count(residueName.trimmed().toUpper()) > 0;
  }

  RKString normalizedAtomName(const RKString &atomName)
  {
    RKString normalized = atomName.trimmed().toUpper();
    normalized.replace('*', '\'');
    return normalized;
  }
}
