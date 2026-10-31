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

#include "indexpath.h"
#include <iostream>
#include <algorithm>

IndexPath::IndexPath()
{
  _path.reserve(10);
}

IndexPath::IndexPath(const int64_t index)
{
  _path.reserve(10);
  _path.push_back(index);
}

int64_t& IndexPath::operator[] (const size_t index) // for non-const objects: can be used for assignment
{
  return _path[index];
}

const int64_t& IndexPath::operator[] (const size_t index) const // for const objects: can only be used for access
{
  return _path[index];
}

void IndexPath::increaseValueAtLastIndex()
{
  if (!_path.empty())
  {
    _path.back() += 1;
  }
}

void IndexPath::decreaseValueAtLastIndex()
{
  if (!_path.empty())
  {
    _path.back() -= 1;
  }
}

size_t IndexPath::size()
{
  return _path.size();
}

IndexPath IndexPath::appending(size_t index)
{
  IndexPath indexpath{};
  indexpath._path.insert(indexpath._path.end(), _path.begin(), _path.end());
  indexpath._path.push_back(index);
  return indexpath;
}

IndexPath IndexPath::removingLastIndex() const
{
  IndexPath indexPath{};
  if (_path.size() <= 1) {return indexPath;}
  indexPath._path = std::vector<int64_t>(_path.begin(),std::prev(_path.end(), 1));
  return indexPath;
}

const IndexPath IndexPath::operator+(const IndexPath& rhs)
{
  IndexPath indexpath{};
  indexpath._path.insert(indexpath._path.end(), _path.begin(), _path.end());
  indexpath._path.insert(indexpath._path.end(), rhs._path.begin(), rhs._path.end());
  return indexpath;
}

bool IndexPath::operator<( const IndexPath& otherObject ) const
{
  // Lexicographical comparison on the signed element type; a shorter path that is a
  // prefix of a longer one sorts first (e.g. [0] < [0, 0]).
  return std::lexicographical_compare(_path.begin(), _path.end(),
                                      otherObject._path.begin(), otherObject._path.end());
}

bool IndexPath::operator>( const IndexPath& otherObject ) const
{
  return otherObject < *this;
}

bool IndexPath::operator==( const IndexPath& otherObject ) const
{
  return _path == otherObject._path;
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<IndexPath>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const IndexPath& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<IndexPath>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  if (vecSize < 0)
  {
    throw BinaryArchiveError("IndexPath vector: negative element count");
  }
  val.reserve(static_cast<size_t>(vecSize));

  while(vecSize--)
  {
    IndexPath tempVal{};
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const IndexPath &indexPath)
{
  stream << static_cast<int32_t>(indexPath._path.size());
  for(const size_t singleVal : indexPath._path)
     stream << int64_t(singleVal);
  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, IndexPath &indexPath)
{
  int32_t vecSize;
  indexPath._path.clear();
  stream >> vecSize;
  if (vecSize < 0)
  {
    throw BinaryArchiveError("IndexPath: negative element count");
  }
  indexPath._path.reserve(static_cast<size_t>(vecSize));

  while(vecSize--)
  {
    int64_t tempVal{};
    stream >> tempVal;
    indexPath._path.push_back(tempVal);
  }
  return stream;
}

/*
 BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<SKAtomTreeNode>>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const std::shared_ptr<SKAtomTreeNode>& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<SKAtomTreeNode>>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);

  while(vecSize--)
  {
    std::shared_ptr<SKAtomTreeNode> tempVal = std::make_shared<SKAtomTreeNode>();
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}*/
