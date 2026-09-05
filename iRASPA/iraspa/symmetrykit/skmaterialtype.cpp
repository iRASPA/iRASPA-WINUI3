/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skmaterialtype.h"
#include "skstructure.h"
#include "skasymmetricatom.h"

#include <algorithm>
#include <set>
#include <string>

namespace
{
  /// H, noble gases.
  const std::set<int> kIgnoredAtomicNumbers = {1, 2, 10, 18, 36, 54, 86};
  /// Alkali and alkaline-earth extra-framework cations (Be is a T-atom, not listed).
  const std::set<int> kExtraFrameworkAtomicNumbers = {3, 11, 12, 19, 20, 37, 38, 55, 56};
  const std::set<int> kHalogenAtomicNumbers = {9, 17, 35, 53, 85};
  /// Typical ZIF nodes: Ni, Co, Zn, Cd.
  const std::set<int> kZifMetalAtomicNumbers = {27, 28, 30, 48};
  /// Not counted as MOF/ZIF framework metals.
  const std::set<int> kNonmetalAtomicNumbers = {
    1, 2, 5, 6, 7, 8, 9, 10, 14, 15, 16, 17, 18, 33, 34, 35, 36, 52, 53, 54, 85, 86
  };

  bool caseInsensitiveEqual(const RKString &a, const RKString &b)
  {
    return a.toLower() == b.toLower();
  }

  std::string joinedUpper(const std::vector<RKString> &names)
  {
    std::string joined;
    for (const RKString &name : names)
    {
      if (name.isEmpty())
        continue;
      if (!joined.empty())
        joined.push_back(' ');
      joined += name.toUpper().toStdString();
    }
    return joined;
  }

  std::set<int> frameworkMetals(const std::set<int> &present)
  {
    std::set<int> metals;
    for (int z : present)
    {
      if (kNonmetalAtomicNumbers.find(z) == kNonmetalAtomicNumbers.end())
        metals.insert(z);
    }
    return metals;
  }

  bool hasNonAlFrameworkMetal(const std::set<int> &present)
  {
    for (int z : present)
    {
      if (z != 13 &&
          kNonmetalAtomicNumbers.find(z) == kNonmetalAtomicNumbers.end() &&
          kExtraFrameworkAtomicNumbers.find(z) == kExtraFrameworkAtomicNumbers.end() &&
          kHalogenAtomicNumbers.find(z) == kHalogenAtomicNumbers.end())
      {
        return true;
      }
    }
    return false;
  }

  bool isAllSilicaComposition(const std::vector<int> &elementIdentifiers)
  {
    std::set<int> present;
    for (int z : elementIdentifiers)
    {
      if (z > 0 && kIgnoredAtomicNumbers.find(z) == kIgnoredAtomicNumbers.end())
        present.insert(z);
    }
    return present.count(14) && present.count(8) && !present.count(13) && !present.count(15);
  }

  /// MCM-41, SBA-15 and related ordered mesoporous silicas - not IZA zeolites.
  bool isMesoporousSilicaName(const std::vector<RKString> &names)
  {
    const std::string joined = joinedUpper(names);
    if (joined.empty())
      return false;
    static const char *tokens[] = {
      "MCM", "SBA", "FSM", "HMS", "KIT-6", "KIT6", "FDU", "TUD-1", "MESOPOROUS"
    };
    for (const char *token : tokens)
    {
      if (joined.find(token) != std::string::npos)
        return true;
    }
    return false;
  }

  /// Graphite, CNT, C60, graphene: carbon-dominated, little or no hydrogen.
  /// Benzene and other hydrocarbons (H comparable to C) are molecular crystals.
  bool isCarbonMaterial(const std::vector<int> &elementIdentifiers)
  {
    std::vector<int> heavy;
    for (int z : elementIdentifiers)
    {
      if (z > 1 && kIgnoredAtomicNumbers.find(z) == kIgnoredAtomicNumbers.end())
        heavy.push_back(z);
    }
    std::set<int> present(heavy.begin(), heavy.end());
    const std::set<int> allowed = {6, 8, 9, 17};
    if (!present.count(6))
      return false;
    for (int z : present)
    {
      if (!allowed.count(z))
        return false;
    }
    const int nC = static_cast<int>(std::count(elementIdentifiers.begin(), elementIdentifiers.end(), 6));
    const int nH = static_cast<int>(std::count(elementIdentifiers.begin(), elementIdentifiers.end(), 1));
    return nH == 0 || nC >= nH * 2;
  }

  SKMaterialType inferComposition(const std::vector<int> &elementIdentifiers)
  {
    std::set<int> present;
    for (int z : elementIdentifiers)
    {
      if (z > 0 && kIgnoredAtomicNumbers.find(z) == kIgnoredAtomicNumbers.end())
        present.insert(z);
    }
    if (present.empty())
      return SKMaterialType::unspecified;

    const bool hasC = present.count(6) != 0;
    const bool hasN = present.count(7) != 0;
    const bool hasO = present.count(8) != 0;
    const bool hasB = present.count(5) != 0;
    const bool hasAl = present.count(13) != 0;
    const bool hasSi = present.count(14) != 0;
    const bool hasP = present.count(15) != 0;
    const bool hasGe = present.count(32) != 0;
    const bool hasGa = present.count(31) != 0;
    const bool hasAs = present.count(33) != 0;
    const bool hasBe = present.count(4) != 0;

    const bool isZeoliteFamily = hasO && (
      hasSi ||
      (hasAl && hasP) ||
      (hasAl && hasSi) ||
      hasGe || hasGa || hasAs || hasBe ||
      (hasB && !hasC)
    );

    if (isZeoliteFamily)
    {
      if (hasSi && hasAl && hasP)
        return SKMaterialType::silicoaluminophosphate;
      if (hasAl && hasP && !hasSi)
      {
        if (hasNonAlFrameworkMetal(present))
          return SKMaterialType::metallophosphate;
        return SKMaterialType::aluminophosphate;
      }
      if (hasSi && hasAl && !hasP)
        return SKMaterialType::aluminosilicate;
      if (hasSi && hasO && !hasAl && !hasP)
        return SKMaterialType::zeolite;
      if (hasP && hasO && hasNonAlFrameworkMetal(present))
        return SKMaterialType::metallophosphate;
      return SKMaterialType::zeolite;
    }

    if (hasC)
    {
      const std::set<int> metals = frameworkMetals(present);
      if (metals.empty())
      {
        if (hasB || hasN)
          return SKMaterialType::cof;
        if (isCarbonMaterial(elementIdentifiers))
          return SKMaterialType::carbon;
        return SKMaterialType::molecularCrystal;
      }
      bool hasZifMetal = false;
      for (int z : metals)
      {
        if (kZifMetalAtomicNumbers.count(z))
        {
          hasZifMetal = true;
          break;
        }
      }
      if (hasZifMetal && hasN && !hasSi && !hasAl && !hasP)
        return SKMaterialType::zif;
      return SKMaterialType::mof;
    }

    if (hasO && !frameworkMetals(present).empty())
      return SKMaterialType::oxide;

    // Ice and other non-metal molecular solids (H2O, ...).
    if (hasO)
      return SKMaterialType::molecularCrystal;

    return SKMaterialType::unspecified;
  }

  std::optional<SKMaterialType> inferFromNames(const std::vector<RKString> &names)
  {
    const std::string joined = joinedUpper(names);
    if (joined.empty())
      return std::nullopt;

    if (isMesoporousSilicaName(names))
      return SKMaterialType::silica;
    if (joined.find("ZIF") != std::string::npos)
      return SKMaterialType::zif;
    if (joined.find("COF") != std::string::npos)
      return SKMaterialType::cof;
    if (joined.find("MOF") != std::string::npos)
      return SKMaterialType::mof;
    if (joined.find("SAPO") != std::string::npos || joined.find("SILICOALUMINOPHOSPHATE") != std::string::npos)
      return SKMaterialType::silicoaluminophosphate;
    if (joined.find("ALPO") != std::string::npos || joined.find("ALUMINOPHOSPHATE") != std::string::npos)
      return SKMaterialType::aluminophosphate;
    if (joined.find("ZEOLITE") != std::string::npos)
      return SKMaterialType::zeolite;
    return std::nullopt;
  }
}

RKString SKMaterialTypeAPI::displayName(SKMaterialType type)
{
  switch (type)
  {
  case SKMaterialType::unspecified: return "Unspecified";
  case SKMaterialType::molecule: return "Molecule";
  case SKMaterialType::protein: return "Protein";
  case SKMaterialType::dnaRna: return "DNA/RNA";
  case SKMaterialType::molecularCrystal: return "Molecular crystal";
  case SKMaterialType::silica: return "Silica";
  case SKMaterialType::aluminosilicate: return "Aluminosilicate";
  case SKMaterialType::aluminophosphate: return "Aluminophosphate";
  case SKMaterialType::metallophosphate: return "Metallophosphate";
  case SKMaterialType::silicoaluminophosphate: return "Silicoaluminophosphate";
  case SKMaterialType::zeolite: return "Zeolite";
  case SKMaterialType::mof: return "MOF";
  case SKMaterialType::zif: return "ZIF";
  case SKMaterialType::cof: return "COF";
  case SKMaterialType::carbon: return "Carbon";
  case SKMaterialType::oxide: return "Oxide";
  case SKMaterialType::hof: return "HOF";
  case SKMaterialType::paf: return "PAF";
  case SKMaterialType::pim: return "PIM";
  case SKMaterialType::polymer: return "Polymer";
  case SKMaterialType::ionicLiquid: return "Ionic liquid";
  case SKMaterialType::clay: return "Clay";
  case SKMaterialType::perovskite: return "Perovskite";
  case SKMaterialType::alloy: return "Alloy";
  case SKMaterialType::glass: return "Glass";
  }
  return "Unspecified";
}

std::vector<RKString> SKMaterialTypeAPI::allDisplayNames()
{
  static const SKMaterialType kOrder[] = {
    SKMaterialType::unspecified,
    SKMaterialType::molecule,
    SKMaterialType::protein,
    SKMaterialType::dnaRna,
    SKMaterialType::molecularCrystal,
    SKMaterialType::silica,
    SKMaterialType::aluminosilicate,
    SKMaterialType::aluminophosphate,
    SKMaterialType::metallophosphate,
    SKMaterialType::silicoaluminophosphate,
    SKMaterialType::zeolite,
    SKMaterialType::mof,
    SKMaterialType::zif,
    SKMaterialType::cof,
    SKMaterialType::carbon,
    SKMaterialType::oxide,
    SKMaterialType::hof,
    SKMaterialType::paf,
    SKMaterialType::pim,
    SKMaterialType::polymer,
    SKMaterialType::ionicLiquid,
    SKMaterialType::clay,
    SKMaterialType::perovskite,
    SKMaterialType::alloy,
    SKMaterialType::glass
  };
  std::vector<RKString> names;
  names.reserve(std::size(kOrder));
  for (SKMaterialType type : kOrder)
    names.push_back(displayName(type));
  return names;
}

std::optional<SKMaterialType> SKMaterialTypeAPI::fromDisplayName(const RKString &name)
{
  const RKString trimmed = name.trimmed();
  if (caseInsensitiveEqual(trimmed, "Silicialuminophosphate"))
    return SKMaterialType::silicoaluminophosphate;

  for (int i = static_cast<int>(SKMaterialType::unspecified);
       i <= static_cast<int>(SKMaterialType::glass);
       ++i)
  {
    const auto type = static_cast<SKMaterialType>(i);
    if (caseInsensitiveEqual(trimmed, displayName(type)))
      return type;
  }
  return std::nullopt;
}

bool SKMaterialTypeAPI::usesAluminosilicateForceField(SKMaterialType type)
{
  switch (type)
  {
  case SKMaterialType::zeolite:
  case SKMaterialType::silica:
  case SKMaterialType::aluminosilicate:
  case SKMaterialType::aluminophosphate:
  case SKMaterialType::metallophosphate:
  case SKMaterialType::silicoaluminophosphate:
    return true;
  default:
    return false;
  }
}

SKMaterialType SKMaterialTypeAPI::infer(const std::vector<int> &elementIdentifiers,
                                        int64_t kind,
                                        const std::vector<RKString> &names)
{
  const auto structureKind = static_cast<SKStructure::Kind>(kind);
  switch (structureKind)
  {
  case SKStructure::Kind::protein:
  case SKStructure::Kind::proteinCrystal:
    return SKMaterialType::protein;
  case SKStructure::Kind::dna:
  case SKStructure::Kind::dnaCrystal:
    return SKMaterialType::dnaRna;
  case SKStructure::Kind::molecule:
    return SKMaterialType::molecule;
  case SKStructure::Kind::molecularCrystal:
  case SKStructure::Kind::molecularCrystalSolvent:
    return SKMaterialType::molecularCrystal;
  default:
    break;
  }

  if (isMesoporousSilicaName(names) && isAllSilicaComposition(elementIdentifiers))
    return SKMaterialType::silica;

  const SKMaterialType composition = inferComposition(elementIdentifiers);
  if (composition != SKMaterialType::unspecified)
    return composition;

  if (const auto fromName = inferFromNames(names))
    return *fromName;
  return SKMaterialType::unspecified;
}

void SKStructure::applyInferredMaterialType(const std::vector<RKString> &extraNames,
                                            std::optional<Kind> kindOverride)
{
  std::vector<RKString> names = extraNames;
  if (displayName && !displayName->isEmpty())
    names.push_back(*displayName);

  std::vector<int> elementIdentifiers;
  elementIdentifiers.reserve(atoms.size());
  for (const std::shared_ptr<SKAsymmetricAtom> &atom : atoms)
    elementIdentifiers.push_back(static_cast<int>(atom->elementIdentifier()));

  const Kind kind = kindOverride.value_or(this->kind);
  materialType = SKMaterialTypeAPI::infer(elementIdentifiers, static_cast<int64_t>(kind), names);
}