/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstddef>
#include <ostream>

// UTF-8 in memory; archive wire format matches Qt QString / Cocoa BinaryCodable.
class RKString
{
public:
  enum SplitBehavior { KeepEmptyParts = 0, SkipEmptyParts = 1 };

  RKString() = default;
  RKString(std::string s) : _utf8(std::move(s)) {}
  RKString(std::string_view s) : _utf8(s) {}
  RKString(const char *s) : _utf8(s ? s : "") {}
  explicit RKString(const std::wstring &w);
  RKString(char c) : _utf8(1, c) {}

  const std::string &utf8() const { return _utf8; }
  std::string &utf8() { return _utf8; }

  std::string toStdString() const { return _utf8; }
  std::wstring toStdWString() const;
  static RKString fromStdString(const std::string &s) { return RKString(s); }
  static RKString fromStdWString(const std::wstring &w) { return RKString(w); }
  static RKString fromUtf8(const char *s) { return RKString(s ? s : ""); }
  static RKString fromUtf8(const std::string &s) { return RKString(s); }
  static RKString number(int v);
  static RKString number(int64_t v);
  static RKString number(std::size_t v);
  static RKString number(double v, char format = 'g', int precision = 6);

  bool isEmpty() const { return _utf8.empty(); }
  bool empty() const { return _utf8.empty(); }
  void clear() { _utf8.clear(); }
  int size() const { return static_cast<int>(_utf8.size()); }
  int length() const { return size(); }
  const char *c_str() const { return _utf8.c_str(); }

  char operator[](int i) const { return _utf8[static_cast<size_t>(i)]; }
  char &operator[](int i) { return _utf8[static_cast<size_t>(i)]; }

  RKString trimmed() const;
  RKString simplified() const;
  bool contains(std::string_view needle) const;
  bool startsWith(std::string_view prefix) const;
  bool endsWith(std::string_view suffix) const;
  RKString toLower() const;
  RKString toUpper() const;
  RKString left(int n) const;
  RKString mid(int pos, int n = -1) const;
  RKString right(int n) const;
  /** Portion before the first occurrence of `c` (whole string if absent). */
  RKString beforeFirst(char c) const;
  int indexOf(std::string_view needle) const;
  RKString arg(const RKString &a1) const;
  RKString arg(int a1) const;
  RKString arg(double a1, int fieldWidth = 0, char format = 'g', int precision = -1, char fillChar = ' ') const;
  RKString rightJustified(int width, char fill = ' ', bool truncate = false) const;
  RKString leftJustified(int width, char fill = ' ', bool truncate = false) const;

  void remove(int pos, int n);
  RKString &remove(const RKString &str);
  RKString &remove(char c);
  RKString &replace(char before, char after);
  RKString &replace(int pos, int n, const RKString &after);
  RKString &replace(std::string_view before, std::string_view after);

  int toInt(bool *ok = nullptr) const;
  double toDouble(bool *ok = nullptr) const;
  float toFloat(bool *ok = nullptr) const;

  std::vector<RKString> splitWhitespace(SplitBehavior behavior = SkipEmptyParts) const;

  std::vector<RKString> split(char sep, SplitBehavior behavior = KeepEmptyParts) const;

  static int compare(const RKString &a, const RKString &b);

  bool operator==(const RKString &o) const { return _utf8 == o._utf8; }
  bool operator!=(const RKString &o) const { return _utf8 != o._utf8; }
  bool operator<(const RKString &o) const { return _utf8 < o._utf8; }
  bool operator==(const char *o) const { return _utf8 == (o ? o : ""); }
  bool operator!=(const char *o) const { return !(*this == o); }

  RKString &operator+=(const RKString &o) { _utf8 += o._utf8; return *this; }
  RKString &operator+=(const char *o) { if (o) _utf8 += o; return *this; }
  RKString &operator+=(char c) { _utf8.push_back(c); return *this; }

private:
  std::string _utf8;
};

inline RKString operator+(const RKString &a, const RKString &b)
{
  return RKString(a.utf8() + b.utf8());
}
inline RKString operator+(const RKString &a, const char *b)
{
  return RKString(a.utf8() + (b ? b : ""));
}
inline RKString operator+(const char *a, const RKString &b)
{
  return RKString(std::string(a ? a : "") + b.utf8());
}

inline std::ostream &operator<<(std::ostream &os, const RKString &s)
{
  return os << s.utf8();
}

#define RKStringLiteral(str) RKString(str)
