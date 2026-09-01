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

#include "skasymmetricbond.h"

SKAsymmetricBond::SKAsymmetricBond(std::shared_ptr<SKAsymmetricAtom> a, std::shared_ptr<SKAsymmetricAtom> b): _isVisible(true), _bondType(SKBondType::singleBond)
{
  if(a->tag() < b->tag())
  {
     _atom1 = a;
     _atom2 = b;
  }
  else
  {
     _atom1 = b;
     _atom2 = a;
  }
}

bool SKAsymmetricBond::operator==(SKAsymmetricBond const& rhs) const
{
  return (this->atom1().get() == rhs.atom1().get() && this->atom2().get() == rhs.atom2().get()) ||
         (this->atom1().get() == rhs.atom2().get() && this->atom2().get() == rhs.atom1().get());
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<SKAsymmetricBond>>& val)
{
  // A bond is written as the tags of the two atoms it joins, so a bond that has lost an atom cannot
  // be described: its stored tag now names whichever atom took that number, which would come back as
  // a bond between the wrong pair. Left out instead, which is what reading does with a bond whose
  // tags name nothing.
  std::vector<std::shared_ptr<SKAsymmetricBond>> writable;
  writable.reserve(val.size());
  for(const std::shared_ptr<SKAsymmetricBond>& singleVal : val)
  {
    if(singleVal && singleVal->atom1() && singleVal->atom2())
      writable.push_back(singleVal);
  }

  stream << static_cast<int32_t>(writable.size());
  for(const std::shared_ptr<SKAsymmetricBond>& singleVal : writable)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<SKAsymmetricBond>>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);

  while(vecSize--)
  {
    std::shared_ptr<SKAsymmetricBond> tempVal = std::make_shared<SKAsymmetricBond>();
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<SKBond>>& val)
{
  // As above: a copy names its atoms by tag, so one that has lost an atom is left out.
  std::vector<std::shared_ptr<SKBond>> writable;
  writable.reserve(val.size());
  for(const std::shared_ptr<SKBond>& singleVal : val)
  {
    if(singleVal && singleVal->atom1() && singleVal->atom2())
      writable.push_back(singleVal);
  }

  stream << static_cast<int32_t>(writable.size());
  for(const std::shared_ptr<SKBond>& singleVal : writable)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<SKBond>>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);

  while(vecSize--)
  {
    std::shared_ptr<SKBond> tempVal = std::make_shared<SKBond>();
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const std::shared_ptr<SKAsymmetricBond> &asymmetricBond)
{
  // A bond holds its atoms weakly, so one whose atom has gone has nothing to ask for a tag. The
  // vector above leaves such bonds out, and this is the last resort for a bond written on its own:
  // the tag it was read with, since writing a document is not the place to fall over.
  const std::shared_ptr<SKAsymmetricAtom> atom1 = asymmetricBond->atom1();
  const std::shared_ptr<SKAsymmetricAtom> atom2 = asymmetricBond->atom2();
  stream << (atom1 ? atom1->tag() : asymmetricBond->_tag1);
  stream << (atom2 ? atom2->tag() : asymmetricBond->_tag2);
  stream << asymmetricBond->_copies;
  stream << static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(asymmetricBond->_bondType);
  stream << asymmetricBond->_isVisible;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, std::shared_ptr<SKAsymmetricBond> &asymmetricBond)
{
  stream >> asymmetricBond->_tag1;
  stream >> asymmetricBond->_tag2;
  stream >> asymmetricBond->_copies;
  int64_t bondType;
  stream >> bondType;
  asymmetricBond->_bondType = SKAsymmetricBond::SKBondType(bondType);
  stream >> asymmetricBond->_isVisible;

  return stream;
}
