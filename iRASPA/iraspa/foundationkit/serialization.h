/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "binaryarchive.h"

#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

class InvalidArchiveVersionException : public std::exception
{
public:
  InvalidArchiveVersionException(const char* file_, int line_, const char* func_, const char* info_ = "") :
      _file (file_),
      _line (line_),
      _func (func_),
      _info (info_)
  {
  }
  std::string message() const { return "Invalid archive version (upgrade to latest iRASPA version)"; }
  const char* what() const noexcept override { return "Invalid archive version (upgrade to latest iRASPA version)"; }
  const char* get_file() const { return _file; }
  int get_line() const { return _line; }
  const char* get_func() const { return _func; }
  const char* get_info() const { return _info; }
private:
  const char* _file;
  int _line;
  const char* _func;
  const char* _info;
};

class InconsistentArchiveException : public std::exception
{
public:
  InconsistentArchiveException(const char* file_, int line_, const char* func_, const char* info_ = "") :
      _file (file_),
      _line (line_),
      _func (func_),
      _info (info_)
  {
  }
  std::string message() const { return "Archive is inconsistent (internal bug)"; }
  const char* what() const noexcept override { return "Archive is inconsistent (internal bug)"; }
  const char* get_file() const { return _file; }
  int get_line() const { return _line; }
  const char* get_func() const { return _func; }
  const char* get_info() const { return _info; }
private:
  const char* _file;
  int _line;
  const char* _func;
  const char* _info;
};

template<typename Enum,
         typename = typename std::enable_if<std::is_enum<Enum>::value>::type>
BinaryArchive& operator<<(BinaryArchive& stream, const Enum& e) {
    stream << static_cast<int64_t>(e);
    return stream;
}

template<typename Enum,
         typename = typename std::enable_if<std::is_enum<Enum>::value>::type>
BinaryArchive& operator>>(BinaryArchive& stream, Enum& e) {
    int64_t v;
    stream >> v;
    e = static_cast<Enum>(v);
    return stream;
}

template<class T> BinaryArchive &operator<<(BinaryArchive& stream, const std::unordered_set<T>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const T& singleVal : val)
    stream << singleVal;
  return stream;
}

template<class T> BinaryArchive &operator>>(BinaryArchive& stream, std::unordered_set<T>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);
  T tempVal;
  while(vecSize--)
  {
    stream >> tempVal;
    val.insert(tempVal);
  }
  return stream;
}

template<class T> BinaryArchive &operator<<(BinaryArchive& stream, const std::set<T>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const T& singleVal : val)
    stream << singleVal;
  return stream;
}

template<class T> BinaryArchive &operator>>(BinaryArchive& stream, std::set<T>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  T tempVal;
  while(vecSize--)
  {
    stream >> tempVal;
    val.insert(tempVal);
  }
  return stream;
}

template<class Key, class T> BinaryArchive &operator<<(BinaryArchive& stream, const std::map<Key, T>& table)
{
  // Match Cocoa dictionary empty encoding (UInt32 0xFFFFFFFF).
  if (table.empty())
  {
    stream << static_cast<uint32_t>(0xFFFFFFFFu);
    return stream;
  }
  stream << static_cast<uint32_t>(table.size());
  for(auto const& [key, val] : table)
  {
    stream << key;
    stream << val;
  }
  return stream;
}

template<class Key, class T> BinaryArchive &operator>>(BinaryArchive& stream, std::map<Key, T>& table)
{
  uint32_t count = 0;
  table.clear();
  stream >> count;
  if (count == 0xFFFFFFFFu || count == 0)
    return stream;

  Key key;
  T value;
  for (uint32_t i = 0; i < count; ++i)
  {
    stream >> key;
    stream >> value;
    table[key] = value;
  }
  return stream;
}
