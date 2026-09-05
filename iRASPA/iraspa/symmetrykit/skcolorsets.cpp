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

#include "skcolorsets.h"
#include "rkstring.h"
#include "skelement.h"
#include <algorithm>
#include <iostream>

namespace {
constexpr SKColorSet::ColorScheme kPredefinedSchemes[] = {
  SKColorSet::ColorScheme::jmol,
  SKColorSet::ColorScheme::rasmol_modern,
  SKColorSet::ColorScheme::rasmol,
  SKColorSet::ColorScheme::vesta,
  SKColorSet::ColorScheme::crystalMaker,
  SKColorSet::ColorScheme::mercury,
  SKColorSet::ColorScheme::pubChem,
  SKColorSet::ColorScheme::pymol,
  SKColorSet::ColorScheme::vmdCpk
};

RKString schemeDisplayName(SKColorSet::ColorScheme scheme)
{
  return SKColorSet(scheme).displayName();
}
}

SKColorSets::SKColorSets()
{
  for (SKColorSet::ColorScheme scheme : kPredefinedSchemes)
    _colorSets.emplace_back(scheme);
}

std::vector<SKColorSet>& SKColorSets::colorSets()
{
  ensurePredefinedSets();
  return _colorSets;
}

SKColorSet& SKColorSets::operator[] (size_t index)
{
  ensurePredefinedSets();
  return _colorSets[index % _colorSets.size()];
}

size_t SKColorSets::count() const
{
  ensurePredefinedSets();
  return _colorSets.size();
}

void SKColorSets::append(SKColorSet colorSet)
{
  ensurePredefinedSets();
  _colorSets.push_back(std::move(colorSet));
}

SKColorSet* SKColorSets::operator[] (RKString name)
{
  ensurePredefinedSets();
  if (auto index = firstIndex(name))
    return &_colorSets[*index];
  return nullptr;
}

const SKColorSet* SKColorSets::operator[] (RKString name) const
{
  ensurePredefinedSets();
  if (auto index = firstIndex(name))
    return &_colorSets[*index];
  return nullptr;
}

void SKColorSets::insert(const RKString& key, int atomicNumber)
{
  ensurePredefinedSets();
  if(atomicNumber < 0 || atomicNumber >= static_cast<int>(PredefinedElements::predefinedElements.size()))
  {
    return;
  }
  const RKString element = PredefinedElements::predefinedElements[atomicNumber]._chemicalSymbol;
  for(SKColorSet& colorSet: _colorSets)
  {
    RKColor color = RKColor::fromRgb(0, 0, 0);
    if(const RKColor* elementColor = static_cast<const SKColorSet&>(colorSet)[element])
    {
      color = *elementColor;
    }
    colorSet[key] = color;
  }
}

void SKColorSets::remove(const RKString& key)
{
  ensurePredefinedSets();
  for(SKColorSet& colorSet: _colorSets)
  {
    colorSet.remove(key);
  }
}

std::optional<size_t> SKColorSets::firstIndex(const RKString& name) const
{
  const RKString lower = name.toLower();
  for (size_t i = 0; i < _colorSets.size(); ++i)
  {
    if (_colorSets[i].displayName().toLower() == lower)
      return i;
  }
  return std::nullopt;
}

void SKColorSets::ensurePredefinedSets() const
{
  for (size_t schemeIndex = 0; schemeIndex < std::size(kPredefinedSchemes); ++schemeIndex)
  {
    const SKColorSet::ColorScheme scheme = kPredefinedSchemes[schemeIndex];
    const RKString name = schemeDisplayName(scheme);
    if (firstIndex(name))
      continue;

    std::optional<size_t> afterPrevious;
    for (size_t previous = 0; previous < schemeIndex; ++previous)
    {
      if (auto index = firstIndex(schemeDisplayName(kPredefinedSchemes[previous])))
        afterPrevious = *index + 1;
    }
    size_t insertIndex = afterPrevious.value_or(_colorSets.size());
    insertIndex = std::min(insertIndex, _colorSets.size());
    _colorSets.insert(_colorSets.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                      SKColorSet(scheme));
  }
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<SKColorSet>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const SKColorSet& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<SKColorSet>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);
  SKColorSet tempVal;
  while(vecSize--)
  {
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const SKColorSets &colorSets)
{
  colorSets.ensurePredefinedSets();
  stream << colorSets._versionNumber;
  stream << colorSets._colorSets;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, SKColorSets &colorSets)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > colorSets._versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "SKColorSets");
  }

  stream >> colorSets._colorSets;
  colorSets.ensurePredefinedSets();

  return stream;
}
