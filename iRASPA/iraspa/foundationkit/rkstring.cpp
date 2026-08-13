/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "rkstring.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

RKString::RKString(const std::wstring &w)
{
#if defined(_WIN32)
  if (w.empty())
    return;
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1)
    return;
  _utf8.assign(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, _utf8.data(), n, nullptr, nullptr);
#else
  _utf8.reserve(w.size());
  for (wchar_t c : w)
    _utf8.push_back(static_cast<char>(c <= 0x7F ? c : '?'));
#endif
}

std::wstring RKString::toStdWString() const
{
#if defined(_WIN32)
  if (_utf8.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, _utf8.c_str(), -1, nullptr, 0);
  if (n <= 1)
    return {};
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, _utf8.c_str(), -1, w.data(), n);
  return w;
#else
  std::wstring w;
  w.reserve(_utf8.size());
  for (unsigned char c : _utf8)
    w.push_back(static_cast<wchar_t>(c));
  return w;
#endif
}

RKString RKString::number(int v)
{
  return RKString(std::to_string(v));
}

RKString RKString::number(int64_t v)
{
  return RKString(std::to_string(v));
}

RKString RKString::number(std::size_t v)
{
  return RKString(std::to_string(v));
}

RKString RKString::number(double v, char format, int precision)
{
  char buf[128];
  if (format == 'f' || format == 'F')
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
  else if (format == 'e' || format == 'E')
    std::snprintf(buf, sizeof(buf), "%.*e", precision, v);
  else
    std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
  return RKString(buf);
}

RKString RKString::trimmed() const
{
  std::size_t start = 0;
  while (start < _utf8.size() &&
         std::isspace(static_cast<unsigned char>(_utf8[start])))
    ++start;
  std::size_t end = _utf8.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(_utf8[end - 1])))
    --end;
  return RKString(_utf8.substr(start, end - start));
}

RKString RKString::simplified() const
{
  std::string out;
  out.reserve(_utf8.size());
  bool inSpace = false;
  bool started = false;
  for (unsigned char c : _utf8)
  {
    if (std::isspace(c))
    {
      if (started)
        inSpace = true;
      continue;
    }
    if (inSpace)
    {
      out.push_back(' ');
      inSpace = false;
    }
    out.push_back(static_cast<char>(c));
    started = true;
  }
  return RKString(std::move(out));
}

bool RKString::contains(std::string_view needle) const
{
  return _utf8.find(needle) != std::string::npos;
}

bool RKString::startsWith(std::string_view prefix) const
{
  return _utf8.size() >= prefix.size() &&
         _utf8.compare(0, prefix.size(), prefix) == 0;
}

bool RKString::endsWith(std::string_view suffix) const
{
  return _utf8.size() >= suffix.size() &&
         _utf8.compare(_utf8.size() - suffix.size(), suffix.size(), suffix) == 0;
}

RKString RKString::toLower() const
{
  std::string out = _utf8;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return RKString(std::move(out));
}

RKString RKString::toUpper() const
{
  std::string out = _utf8;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return RKString(std::move(out));
}

RKString RKString::left(int n) const
{
  if (n <= 0)
    return RKString();
  return RKString(_utf8.substr(0, static_cast<size_t>(n)));
}

RKString RKString::mid(int pos, int n) const
{
  if (pos < 0 || static_cast<size_t>(pos) >= _utf8.size())
    return RKString();
  if (n < 0)
    return RKString(_utf8.substr(static_cast<size_t>(pos)));
  return RKString(_utf8.substr(static_cast<size_t>(pos), static_cast<size_t>(n)));
}

RKString RKString::right(int n) const
{
  if (n <= 0)
    return RKString();
  if (static_cast<size_t>(n) >= _utf8.size())
    return *this;
  return RKString(_utf8.substr(_utf8.size() - static_cast<size_t>(n)));
}

int RKString::indexOf(std::string_view needle) const
{
  const auto p = _utf8.find(needle);
  return p == std::string::npos ? -1 : static_cast<int>(p);
}

RKString RKString::arg(const RKString &a1) const
{
  const auto p = _utf8.find("%1");
  if (p == std::string::npos)
    return *this;
  std::string out = _utf8;
  out.replace(p, 2, a1.utf8());
  return RKString(std::move(out));
}

RKString RKString::arg(int a1) const
{
  return arg(RKString::number(a1));
}

RKString RKString::arg(double a1, int fieldWidth, char format, int precision, char fillChar) const
{
  const int prec = precision < 0 ? 6 : precision;
  RKString num = RKString::number(a1, format, prec);
  if (fieldWidth > 0)
    num = num.rightJustified(fieldWidth, fillChar, false);
  else if (fieldWidth < 0)
    num = num.leftJustified(-fieldWidth, fillChar, false);
  return arg(num);
}

RKString RKString::rightJustified(int width, char fill, bool truncate) const
{
  if (width < 0)
    width = 0;
  if (truncate && size() > width)
    return right(width);
  if (size() >= width)
    return *this;
  return RKString(std::string(static_cast<size_t>(width - size()), fill) + _utf8);
}

RKString RKString::leftJustified(int width, char fill, bool truncate) const
{
  if (width < 0)
    width = 0;
  if (truncate && size() > width)
    return left(width);
  if (size() >= width)
    return *this;
  return RKString(_utf8 + std::string(static_cast<size_t>(width - size()), fill));
}

RKString RKString::beforeFirst(char c) const
{
  const auto p = _utf8.find(c);
  if (p == std::string::npos)
    return *this;
  return RKString(_utf8.substr(0, p));
}

void RKString::remove(int pos, int n)
{
  if (pos < 0 || static_cast<size_t>(pos) >= _utf8.size() || n <= 0)
    return;
  _utf8.erase(static_cast<size_t>(pos), static_cast<size_t>(n));
}

RKString &RKString::remove(const RKString &str)
{
  if (str.isEmpty())
    return *this;
  std::size_t p = 0;
  while ((p = _utf8.find(str.utf8(), p)) != std::string::npos)
    _utf8.erase(p, str.utf8().size());
  return *this;
}

RKString &RKString::remove(char c)
{
  _utf8.erase(std::remove(_utf8.begin(), _utf8.end(), c), _utf8.end());
  return *this;
}

int RKString::compare(const RKString &a, const RKString &b)
{
  if (a._utf8 < b._utf8)
    return -1;
  if (a._utf8 > b._utf8)
    return 1;
  return 0;
}

RKString &RKString::replace(char before, char after)
{
  for (char &c : _utf8)
    if (c == before)
      c = after;
  return *this;
}

RKString &RKString::replace(int pos, int n, const RKString &after)
{
  if (pos < 0 || static_cast<size_t>(pos) > _utf8.size())
    return *this;
  _utf8.replace(static_cast<size_t>(pos), static_cast<size_t>((std::max)(0, n)), after.utf8());
  return *this;
}

RKString &RKString::replace(std::string_view before, std::string_view after)
{
  if (before.empty())
    return *this;
  std::size_t p = 0;
  while ((p = _utf8.find(before, p)) != std::string::npos)
  {
    _utf8.replace(p, before.size(), after);
    p += after.size();
  }
  return *this;
}

int RKString::toInt(bool *ok) const
{
  char *end = nullptr;
  const long v = std::strtol(_utf8.c_str(), &end, 10);
  const bool good = end != _utf8.c_str() && end && *end == '\0';
  if (ok)
    *ok = good;
  return good ? static_cast<int>(v) : 0;
}

double RKString::toDouble(bool *ok) const
{
  char *end = nullptr;
  const double v = std::strtod(_utf8.c_str(), &end);
  const bool good = end != _utf8.c_str() && end && *end == '\0';
  if (ok)
    *ok = good;
  return good ? v : 0.0;
}

float RKString::toFloat(bool *ok) const
{
  return static_cast<float>(toDouble(ok));
}

std::vector<RKString> RKString::split(char sep, SplitBehavior behavior) const
{
  std::vector<RKString> out;
  std::string cur;
  for (char c : _utf8)
  {
    if (c == sep)
    {
      if (behavior == KeepEmptyParts || !cur.empty())
        out.emplace_back(cur);
      cur.clear();
    }
    else
      cur.push_back(c);
  }
  if (behavior == KeepEmptyParts || !cur.empty())
    out.emplace_back(cur);
  return out;
}

std::vector<RKString> RKString::splitWhitespace(SplitBehavior behavior) const
{
  std::vector<RKString> out;
  std::string cur;
  for (unsigned char c : _utf8)
  {
    if (std::isspace(c))
    {
      if (!cur.empty() || behavior == KeepEmptyParts)
      {
        if (!cur.empty() || behavior == KeepEmptyParts)
        {
          if (!cur.empty())
          {
            out.emplace_back(cur);
            cur.clear();
          }
        }
      }
    }
    else
      cur.push_back(static_cast<char>(c));
  }
  if (!cur.empty())
    out.emplace_back(cur);
  return out;
}
