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

#include <optional>
#include <vector>
#include <foundationkit.h>
#include "rkstring.h"
#include "binaryarchive.h"
#include "skcolorset.h"

class SKColorSets
{
public:
  SKColorSets();
public:
  std::vector<SKColorSet>& colorSets();
  SKColorSet& operator[] (size_t index);
  SKColorSet* operator[] (RKString);
  const SKColorSet* operator[] (RKString) const;
  void append(SKColorSet colorSet);
  size_t count() const;
  /** Give a force-field type a color in every set, starting from the color of
      its element (Cocoa SKColorSets.insert(key:element:)). */
  void insert(const RKString& key, int atomicNumber);
  void remove(const RKString& key);
private:
  int64_t _versionNumber{1};
  mutable std::vector<SKColorSet> _colorSets;

  std::optional<size_t> firstIndex(const RKString& name) const;
  void ensurePredefinedSets() const;

  friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<SKColorSet>& val);
  friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<SKColorSet>& val);

  friend BinaryArchive &operator<<(BinaryArchive &, const SKColorSets &);
  friend BinaryArchive &operator>>(BinaryArchive &, SKColorSets &);

};
