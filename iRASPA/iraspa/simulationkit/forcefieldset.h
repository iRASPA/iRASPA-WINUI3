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
#include <map>
#include "rkstring.h"
#include <tuple>
#include <vector>
#include <set>
#include "forcefieldtype.h"
#include <iostream>

class ForceFieldSet
{
public:
  enum class ForceFieldSchemeOrder: int64_t
  {
    elementOnly = 0, forceFieldFirst = 1, forceFieldOnly = 2, multiple_values = 3
  };

  ForceFieldSet();
  ForceFieldSet(RKString name, ForceFieldSet& forcefieldset, bool editable=false);
  RKString displayName() const {return _displayName;}

  ForceFieldType* operator[] (const RKString name);
  ForceFieldType& operator[] (size_t sortNumber) {return _atomTypeList[sortNumber];}

  std::vector<ForceFieldType>& atomTypeList() {return _atomTypeList;}
  void insert(int index, ForceFieldType& forceFieldType) {_atomTypeList.insert(_atomTypeList.begin()+index, forceFieldType);}
  void remove(size_t index) {_atomTypeList.erase(_atomTypeList.begin()+index);}
  void duplicate(size_t index);
  bool editable() {return _editable;}
  RKString uniqueName(int atomicNumber);
  /** Whether the name is one of the built-in types, which stay read-only even
      in an editable set (Cocoa SKForceFieldSet.isDefaultForceFieldType). */
  static bool isDefaultForceFieldType(const RKString& uniqueForceFieldName);
private:
  int64_t _versionNumber{1};
  RKString _displayName = "Default";
  bool _editable = false;
  std::vector<ForceFieldType> _atomTypeList{};

  static std::vector<ForceFieldType> _defaultForceField;

  friend BinaryArchive &operator<<(BinaryArchive &, const ForceFieldSet &);
  friend BinaryArchive &operator>>(BinaryArchive &, ForceFieldSet &);

  friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<ForceFieldType>& val);
  friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<ForceFieldType>& val);
};

