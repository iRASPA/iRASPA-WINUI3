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

#include "forcefieldsets.h"
#include "rkstring.h"
#include <skmaterialtype.h>
#include <cmath>
#include <algorithm>

ForceFieldSets::ForceFieldSets(): _forceFieldSets{
  ForceFieldSet(),
  ForceFieldSet::aluminosilicate(),
  ForceFieldSet::zeoliteAtlas(),
  ForceFieldSet::aluminosilicateZeoPlusPlus()}
{
}

ForceFieldSet& ForceFieldSets::operator[] (const int index)
{
  ensurePredefinedSets();
  return _forceFieldSets[static_cast<size_t>(index) % _forceFieldSets.size()];
}

std::vector<ForceFieldSet>& ForceFieldSets::forceFieldSets()
{
  ensurePredefinedSets();
  return _forceFieldSets;
}

size_t ForceFieldSets::count() const
{
  ensurePredefinedSets();
  return _forceFieldSets.size();
}

ForceFieldSet* ForceFieldSets::operator[] (const RKString name)
{
  ensurePredefinedSets();
  if (auto index = firstIndex(name))
    return &_forceFieldSets[*index];
  return nullptr;
}

ForceFieldSet* ForceFieldSets::operator[] (const RKString name) const
{
  ensurePredefinedSets();
  if (auto index = firstIndex(name))
    return &_forceFieldSets[*index];
  return nullptr;
}

void ForceFieldSets::append(ForceFieldSet forceField)
{
  ensurePredefinedSets();
  if (firstIndex(forceField.displayName())
      && (ForceFieldSet::isAluminosilicateFamily(forceField.displayName())
          || forceField.displayName().toLower() == RKString(defaultDisplayName).toLower()))
  {
    return;
  }
  _forceFieldSets.push_back(std::move(forceField));
}

RKString ForceFieldSets::suggestedDisplayName(const RKString& materialTypeName)
{
  const auto materialType = SKMaterialTypeAPI::fromDisplayName(materialTypeName);
  if (materialType && SKMaterialTypeAPI::usesAluminosilicateForceField(*materialType))
    return RKString(aluminosilicateDisplayName);
  return RKString(defaultDisplayName);
}

ForceFieldSet ForceFieldSets::resolvedSet(const RKString& displayName) const
{
  ensurePredefinedSets();
  if (ForceFieldSet::isAluminosilicateFamily(displayName))
    return ForceFieldSet::predefined(displayName);
  if (ForceFieldSet* set = (*this)[displayName])
    return *set;
  return ForceFieldSet::predefined(displayName);
}

bool ForceFieldSets::contains(const RKString& uniqueIdentifier)
{
  ensurePredefinedSets();
  for(ForceFieldSet& forceFieldSet: _forceFieldSets)
  {
    if(forceFieldSet[uniqueIdentifier] != nullptr)
    {
      return true;
    }
  }

  return false;
}

std::optional<size_t> ForceFieldSets::firstIndex(const RKString& name) const
{
  const RKString lower = name.toLower();
  for (size_t i = 0; i < _forceFieldSets.size(); ++i)
  {
    if (_forceFieldSets[i].displayName().toLower() == lower)
      return i;
  }
  return std::nullopt;
}

void ForceFieldSets::restoreDefaultFrameworkTypesIfTraPPE() const
{
  ForceFieldSet* defaultSet = nullptr;
  for (ForceFieldSet& set : _forceFieldSets)
  {
    if (set.displayName().toLower() == RKString(defaultDisplayName).toLower())
    {
      defaultSet = &set;
      break;
    }
  }
  if (!defaultSet)
    return;

  ForceFieldType* oxygen = (*defaultSet)[RKString("O")];
  if (!oxygen)
    return;

  const double2 params = oxygen->potentialParameters();
  const bool looksLikeTraPPEOxygen =
      std::abs(params.x - 53.0) < 1.0e-6 && std::abs(params.y - 3.30) < 1.0e-6;
  if (!looksLikeTraPPEOxygen)
    return;

  for (const char* symbol : {"O", "Si", "Al"})
  {
    ForceFieldType* canonical = ForceFieldSet::defaultType(RKString(symbol));
    if (!canonical)
      continue;
    auto& list = defaultSet->atomTypeList();
    auto it = std::find_if(list.begin(), list.end(),
        [symbol](const ForceFieldType& type) {
          return type.forceFieldStringIdentifier().toLower() == RKString(symbol).toLower();
        });
    if (it != list.end())
      *it = *canonical;
  }
}

void ForceFieldSets::ensurePredefinedSets() const
{
  struct Predefined
  {
    const char* name;
    ForceFieldSet (*factory)();
  };
  static const Predefined predefined[] = {
    {ForceFieldSet::aluminosilicateDisplayName, ForceFieldSet::aluminosilicate},
    {ForceFieldSet::zeoliteAtlasDisplayName, ForceFieldSet::zeoliteAtlas},
    {ForceFieldSet::aluminosilicateZeoPlusPlusDisplayName, ForceFieldSet::aluminosilicateZeoPlusPlus}
  };

  _forceFieldSets.erase(
      std::remove_if(_forceFieldSets.begin(), _forceFieldSets.end(),
          [](const ForceFieldSet& set) {
            const RKString lower = set.displayName().toLower();
            return lower == RKString("Aluminosilicate").toLower()
                || lower == RKString("Zeolite Atlas").toLower()
                || lower == RKString("Aluminosilicate Zeolite Atlas").toLower();
          }),
      _forceFieldSets.end());

  for (size_t predefinedIndex = 0; predefinedIndex < std::size(predefined); ++predefinedIndex)
  {
    const Predefined& entry = predefined[predefinedIndex];
    std::vector<size_t> matches;
    for (size_t i = 0; i < _forceFieldSets.size(); ++i)
    {
      if (_forceFieldSets[i].displayName().toLower() == RKString(entry.name).toLower())
        matches.push_back(i);
    }

    if (!matches.empty())
    {
      _forceFieldSets[matches.front()] = entry.factory();
      for (size_t j = matches.size(); j-- > 1;)
        _forceFieldSets.erase(_forceFieldSets.begin() + static_cast<std::ptrdiff_t>(matches[j]));
    }
    else
    {
      std::optional<size_t> afterPrevious;
      for (size_t previous = 0; previous < predefinedIndex; ++previous)
      {
        if (auto index = firstIndex(RKString(predefined[previous].name)))
          afterPrevious = *index + 1;
      }
      size_t insertIndex;
      if (afterPrevious)
        insertIndex = *afterPrevious;
      else if (auto defaultIndex = firstIndex(RKString(defaultDisplayName)))
        insertIndex = *defaultIndex + 1;
      else
        insertIndex = _forceFieldSets.size();
      insertIndex = std::min(insertIndex, _forceFieldSets.size());
      _forceFieldSets.insert(_forceFieldSets.begin() + static_cast<std::ptrdiff_t>(insertIndex),
                             entry.factory());
    }
  }

  restoreDefaultFrameworkTypesIfTraPPE();
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<ForceFieldSet>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const ForceFieldSet& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<ForceFieldSet>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);
  ForceFieldSet tempVal;
  while(vecSize--)
  {
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const ForceFieldSets &forcefieldTypes)
{
  forcefieldTypes.ensurePredefinedSets();
  stream << forcefieldTypes._versionNumber;

  stream << forcefieldTypes._forceFieldSets;

  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, ForceFieldSets &forcefieldTypes)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > forcefieldTypes._versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "ForceFieldSets");
  }

  stream >> forcefieldTypes._forceFieldSets;
  forcefieldTypes.ensurePredefinedSets();

  return stream;
}
