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

#include "mathkit.h"
#include <foundationkit.h>
#include "binaryarchive.h"

class ForceFieldType
{
public:
  ForceFieldType();
  ForceFieldType(const ForceFieldType& forceFieldType);
  ForceFieldType& operator=(const ForceFieldType& rhs) = default;
  ForceFieldType(RKString forceFieldStringIdentifier, int64_t atomicNumber, double2 potentialParameters, double mass, double userDefinedRadius, bool editable);
  RKString forceFieldStringIdentifier() const {return _forceFieldStringIdentifier;}
  void setForceFieldStringIdentifier(RKString forceFieldStringIdentifier) {_forceFieldStringIdentifier = forceFieldStringIdentifier;}
  double2 potentialParameters() const {return _potentialParameters;}
  int64_t atomicNumber() const {return _atomicNumber;}
  void setAtomicNumber(int atomicNumber) {_atomicNumber = atomicNumber;}
  double userDefinedRadius() const {return _userDefinedRadius;}
  double mass() const {return _mass;}
  void setEpsilonPotentialParameter(double epsilon)  {_potentialParameters = double2(epsilon,_potentialParameters.y);}
  void setSigmaPotentialParameter(double sigma)  {_potentialParameters = double2(_potentialParameters.x, sigma);}
  void setMass(double mass) {_mass = mass;}
  void setUserDefinedRadius(double radius) {_userDefinedRadius = radius;}
  bool editable() const {return _editable;}
  void setEditable(bool value) {_editable = value;}
  bool isVisible() const {return _isVisible;}
  void setIsVisible(bool value) {_isVisible = value;}
private:
  int64_t _versionNumber{2};
  bool _editable = false;
  int64_t _atomicNumber{6};
  RKString _forceFieldStringIdentifier;
  double2 _potentialParameters = double2(0.0,0.0);
  double _mass = 0.0;
  double _userDefinedRadius = 0.0;
  bool _isVisible = true;

  friend BinaryArchive &operator<<(BinaryArchive &, const ForceFieldType &);
  friend BinaryArchive &operator>>(BinaryArchive &, ForceFieldType &);
};
