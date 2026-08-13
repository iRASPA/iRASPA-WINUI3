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

#include <iostream>
#include <vector>
#include <unordered_set>
#include <mathkit.h>
#include <type_traits>
#include <foundationkit.h>

class SKAsymmetricAtom;

class SKAtomCopy
{
public:
    SKAtomCopy(): _position(), _tag(0), _type(AtomCopyType::copy), _parent() {}
    SKAtomCopy(const SKAtomCopy &atomCopy);
    SKAtomCopy(std::shared_ptr<SKAsymmetricAtom> asymmetricParentAtom, double3 position): _position(position), _tag(0), _type(AtomCopyType::copy), _parent(asymmetricParentAtom) {}
    enum class AtomCopyType: int64_t
    {
      copy = 2, duplicate = 3
    };
    const std::shared_ptr<SKAsymmetricAtom> parent() const {return this->_parent.lock();}
    double3 position() const {return _position;}
    void setPosition(double3 p) {_position = p;}
    AtomCopyType type() {return _type;}
    void setType(AtomCopyType type) {_type = type;}
    int64_t tag() {return _tag;}
    void setTag(int64_t tag) {_tag = tag;}
    int64_t asymmetricIndex() {return _asymmetricIndex;}
    void setAsymmetricIndex(int64_t value) {_asymmetricIndex = value;}
private:
    int64_t _versionNumber{1};
    struct Hash
    {
      template <typename T> std::size_t operator() (T* const &p) const
      {
        return std::hash<T>()(*p);
      }
    };
    struct Compare
    {
      template <typename T> size_t operator() (T* const &a, T* const &b) const
      {
        return *a == *b;
      }
    };
    double3 _position;
    int64_t _tag;
    AtomCopyType _type;
    std::weak_ptr<SKAsymmetricAtom> _parent;
    int64_t _asymmetricIndex;

    friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<SKAtomCopy> &);
    friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<SKAtomCopy> &);

    friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<SKAsymmetricAtom> &);
    friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<SKAsymmetricAtom> &);
};
