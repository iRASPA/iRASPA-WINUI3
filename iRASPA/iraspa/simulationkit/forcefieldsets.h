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
#include <vector>
#include "rkstring.h"
#include "forcefieldset.h"

class ForceFieldSets
{
public:
  ForceFieldSets();
  ForceFieldSet& operator[] (const int index) {return _forceFieldSets[index];}
  ForceFieldSet* operator[] (const RKString name);
  ForceFieldSet* operator[] (const RKString name) const;
  std::vector<ForceFieldSet>& forceFieldSets() {return _forceFieldSets;}
  void append(ForceFieldSet forceField) {_forceFieldSets.push_back(forceField);}
  /** Whether any set still has a type of this name, which decides whether the
      color sets may drop its color (Cocoa SKForceFieldSets.contains). */
  bool contains(const RKString& uniqueIdentifier);

  static constexpr const char* defaultDisplayName = ForceFieldSet::defaultDisplayName;
  static constexpr const char* aluminosilicateDisplayName = ForceFieldSet::aluminosilicateDisplayName;

  /// Cocoa SKForceFieldSets.suggestedDisplayName(forMaterialTypeName:).
  static RKString suggestedDisplayName(const RKString& materialTypeName);

  /// Built-in non-editable tables (Aluminosilicate) always come from code.
  ForceFieldSet resolvedSet(const RKString& displayName) const;
private:
  int64_t _versionNumber{1};
  int64_t _numberOfPredefinedSets = 2;
  std::vector<ForceFieldSet> _forceFieldSets;

  friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<ForceFieldSet>& val);
  friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<ForceFieldSet>& val);

  friend BinaryArchive &operator<<(BinaryArchive &, const ForceFieldSets &);
  friend BinaryArchive &operator>>(BinaryArchive &, ForceFieldSets &);
};
