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

#include "rkstring.h"
#include <array>
#include <vector>
#include <map>
#include <optional>
#include <set>

enum class SKBackboneAtomRole
{
  nitrogen,
  alphaCarbon,
  carbonylCarbon,
  carbonylOxygen
};

struct SKElement
{
  RKString _chemicalSymbol = RKString("Undefined");
  int64_t _atomicNumber = 0;
  int64_t _group = 0;
  int64_t _period = 0;
  RKString _name = RKString("Undefined");
  double _mass = 1.0;
  double _atomRadius = 0.0;
  double _covalentRadius = 0.0;
  double _singleBondCovalentRadius = 0.0;
  double _doubleBondCovalentRadius = 0.0;
  double _tripleBondCovalentRadius = 0.0;
  double _VDWRadius = 1.0;
  std::vector<int> _possibleOxidationStates;
  int64_t _oxidationState = 0;
  double _atomicPolarizability = 0.0;
  SKElement();
  SKElement(RKString string, int64_t atomicNumber, int64_t group, int64_t period, RKString name, double mass, double atomRadius, double covalentRadius, double singleBondCovalentRadius,
            double doubleBondCovalentRadius, double tripleBondCovalentRadius, double vDWRadius, std::vector<int> possibleOxidationStates);
};

struct PredefinedElements
{
  static std::vector<SKElement> predefinedElements;
  static std::map<RKString, int> atomicNumberData;

  static std::set<RKString> residueDefinitions;
  static std::map<RKString, RKString> residueDefinitionsElement;
  static std::map<RKString, RKString> residueDefinitionsType;
  static std::map<RKString, std::vector<RKString>> residueDefinitionsBondedAtoms;

  static std::optional<SKBackboneAtomRole> backboneAtomRole(const RKString &residueName, const RKString &atomName);
  static std::optional<SKBackboneAtomRole> backboneAtomRoleForType(const RKString &type);
  static bool isBackboneAtomType(const RKString &type);
};
