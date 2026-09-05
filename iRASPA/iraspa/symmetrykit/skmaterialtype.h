/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <cstdint>
#include <optional>
#include <vector>

class SKStructure;

/// Chemical class shown as Cell → Structural Properties → Material Type.
/// Distinct from `SKStructure::Kind` (crystal / molecule / protein).
/// Free-standing equivalent of Cocoa `SKStructure.MaterialType`.
enum class SKMaterialType : int
{
  unspecified = 0,
  silica = 1,
  aluminosilicate = 2,
  aluminophosphate = 3,
  metallophosphate = 4,
  silicoaluminophosphate = 5,
  zeolite = 6,
  mof = 7,
  cof = 8,
  zif = 9,
  molecule = 10,
  protein = 11,
  dnaRna = 12,
  molecularCrystal = 13,
  carbon = 14,
  oxide = 15,
  hof = 16,
  paf = 17,
  pim = 18,
  polymer = 19,
  ionicLiquid = 20,
  clay = 21,
  perovskite = 22,
  alloy = 23,
  glass = 24
};

namespace SKMaterialTypeAPI
{
  RKString displayName(SKMaterialType type);

  /// Combo-box order: kind-based types, porous oxides, frameworks, then manual refinements.
  std::vector<RKString> allDisplayNames();

  /// Resolves a combo-box / project string, including the historical typo
  /// `Silicialuminophosphate`.
  std::optional<SKMaterialType> fromDisplayName(const RKString &name);

  /// Zeolite-family types share the Calero/Auerbach aluminosilicate force field.
  bool usesAluminosilicateForceField(SKMaterialType type);

  /// Classification from Kind plus atom atomic numbers, with optional CIF / file-name hints.
  /// HOF, PAF, PIM, polymer, ionic liquid, clay, perovskite, alloy, and glass are
  /// not inferred — the user can select them in the combo box.
  /// `kind` is `SKStructure::Kind` as int64_t to avoid a circular header dependency.
  SKMaterialType infer(const std::vector<int> &elementIdentifiers,
                       int64_t kind,
                       const std::vector<RKString> &names = {});
}