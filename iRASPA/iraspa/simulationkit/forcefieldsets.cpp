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

ForceFieldSets::ForceFieldSets(): _forceFieldSets{ForceFieldSet(), ForceFieldSet::aluminosilicate()}
{

}

ForceFieldSet* ForceFieldSets::operator[] (const RKString name)
{
  for(ForceFieldSet& forceFieldSet: _forceFieldSets)
  {
    if(forceFieldSet.displayName().toLower() == name.toLower())
    {
      return &forceFieldSet;
    }
  }

  return nullptr;
}

ForceFieldSet* ForceFieldSets::operator[] (const RKString name) const
{
  for(ForceFieldSet& forceFieldSet: const_cast<std::vector<ForceFieldSet>&>(_forceFieldSets))
  {
    if(forceFieldSet.displayName().toLower() == name.toLower())
    {
      return &forceFieldSet;
    }
  }
  return nullptr;
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
  if (displayName.toLower() == RKString(aluminosilicateDisplayName).toLower())
    return ForceFieldSet::aluminosilicate();
  if (ForceFieldSet* set = (*this)[displayName])
    return *set;
  return ForceFieldSet();
}

bool ForceFieldSets::contains(const RKString& uniqueIdentifier)
{
  for(ForceFieldSet& forceFieldSet: _forceFieldSets)
  {
    if(forceFieldSet[uniqueIdentifier] != nullptr)
    {
      return true;
    }
  }

  return false;
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

  return stream;
}

