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
#include "binaryarchive.h"
#include <foundationkit.h>
#include <map>
#include <mathkit.h>
#include "rkcolor.h"

class SKColorSet
{
public:
  SKColorSet() {}

  enum class ColorScheme: int64_t
  {
    jmol = 0, rasmol_modern = 1, rasmol = 2, vesta = 3,
    crystalMaker = 4, mercury = 5, pubChem = 6, pymol = 7, vmdCpk = 8,
    multiple_values = 9
  };

  enum class ColorSchemeOrder: int64_t
  {
    elementOnly = 0, forceFieldFirst = 1, forceFieldOnly = 2, multiple_values = 3
  };

  SKColorSet(RKString name, SKColorSet& from, bool editable);
  SKColorSet(ColorScheme scheme);
  const RKString displayName() const {return _displayName;}
  RKColor& operator[] (const RKString colorName) {return _colors[colorName];}
  const RKColor* operator[] (RKString colorName) const;
  void remove(const RKString& colorName) {_colors.erase(colorName);}
  bool editable() {return _editable;}
private:
  int64_t _versionNumber{1};

  RKString _displayName;
  bool _editable = false;
  std::map<RKString, RKColor> _colors{};

  static std::map<RKString, RKColor> jMol;
  static std::map<RKString, RKColor> rasmol;
  static std::map<RKString, RKColor> rasmolModern;
  static std::map<RKString, RKColor> vesta;
  static std::map<RKString, RKColor> crystalMaker;
  static std::map<RKString, RKColor> mercury;
  static std::map<RKString, RKColor> pubChem;
  static std::map<RKString, RKColor> pymol;
  static std::map<RKString, RKColor> vmdCpk;

  friend BinaryArchive &operator<<(BinaryArchive & stream, const std::map<RKString, RKColor>& table);
  friend BinaryArchive &operator>>(BinaryArchive & stream, std::map<RKString, RKColor>& table);

  friend BinaryArchive &operator<<(BinaryArchive &, const SKColorSet &);
  friend BinaryArchive &operator>>(BinaryArchive &, SKColorSet &);
};

