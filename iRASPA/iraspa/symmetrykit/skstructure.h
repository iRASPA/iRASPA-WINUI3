/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
    D.Dubbeldam@uva.nl            https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html
    S.Calero@tue.nl               https://www.tue.nl/en/research/researchers/sofia-calero/
    t.j.h.vlugt@tudelft.nl        http://homepage.tudelft.nl/v9k6y

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************************************************************/

#pragma once

#include <zipreader.h>

#include "rkstring.h"
#include <string>
#include <mathkit.h>
#include <vector>
#include <set>
#include <optional>
#include "skasymmetricatom.h"
#include "skcell.h"
#include "skmaterialtype.h"

class SKStructure
{
public:
  enum class DataType {Uint8, Int8, Uint16, Int16, Uint32, Int32, Uint64, Int64, Float, Double};

  SKStructure(): cell(std::make_shared<SKCell>()) {};

  enum class Kind: int64_t
  {
    none = -1, object = 0, structure = 1,
    crystal = 2, molecularCrystal = 3, molecule = 4, protein = 5, proteinCrystal = 6,
    proteinCrystalSolvent = 7, crystalSolvent = 8, molecularCrystalSolvent = 9,
    crystalEllipsoidPrimitive = 10, crystalCylinderPrimitive = 11, crystalPolygonalPrismPrimitive = 12,
    ellipsoidPrimitive = 13, cylinderPrimitive = 14, polygonalPrismPrimitive = 15,
    gridVolume = 16, RASPADensityVolume = 17, VTKDensityVolume = 18, VASPDensityVolume = 19, GaussianCubeVolume = 20,
    dna = 21, dnaCrystal = 22
  };

  Kind kind = Kind::crystal;
  SKMaterialType materialType = SKMaterialType::unspecified;
  std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms;

  /// Sets `materialType` from `kind`, `atoms`, and `displayName`.
  /// Call after a parser has finished filling a frame.
  /// `extraNames` is for CIF `_chemical_name_*` (and similar) hints.
  /// `kindOverride` overrides `kind` for classification only (XYZ lattices use
  /// molecularCrystal as object type but still infer composition as a crystal).
  void applyInferredMaterialType(const std::vector<RKString> &extraNames = {},
                                 std::optional<Kind> kindOverride = std::nullopt);
  std::set<RKString> unknownAtoms;

  std::optional<RKString> displayName;
  std::shared_ptr<SKCell> cell;
  std::optional<int> spaceGroupHallNumber;
  bool drawUnitCell = false;
  bool periodic = false;

  std::optional<RKString> creationDate;
  std::optional<RKString> creationMethod;
  std::optional<RKString> chemicalFormulaSum;
  std::optional<RKString> chemicalFormulaStructural;
  std::optional<int> cellFormulaUnitsZ;

  std::optional<int> numberOfChannels;
  std::optional<int> numberOfPockets;
  std::optional<int> dimensionality;
  std::optional<double> Di;
  std::optional<double> Df;
  std::optional<double> Dif;

  int3 dimensions;
  double3 origin;
  double3 spacing;
  DataType dataType;
  std::pair<double,double> range;
  double average;
  double variance;
  RKByteArray byteData;
};
