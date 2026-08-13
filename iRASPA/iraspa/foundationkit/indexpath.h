/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include "binaryarchive.h"
#include <vector>

class IndexPath
{
public:
    IndexPath();
    IndexPath(const int64_t index);
    int64_t& operator[] (const size_t index);
    const int64_t& operator[] (const size_t index) const;
    inline int64_t lastIndex() const {if (!_path.empty()) return _path.back(); return 0;}
    const IndexPath operator+(const IndexPath& rhs);
    void increaseValueAtLastIndex();
    void decreaseValueAtLastIndex();
    size_t size();
    bool empty() const {return _path.empty();}
    IndexPath appending(size_t index);
    IndexPath removingLastIndex() const;

    bool operator<( const IndexPath& otherObject ) const;
    bool operator>( const IndexPath& otherObject ) const;
    bool operator==( const IndexPath& otherObject ) const;
private:
    std::vector<int64_t> _path;

    friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<IndexPath>& val);
    friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<IndexPath>& val);

    friend BinaryArchive &operator<<(BinaryArchive &, const IndexPath &);
    friend BinaryArchive &operator>>(BinaryArchive &, IndexPath &);
};
