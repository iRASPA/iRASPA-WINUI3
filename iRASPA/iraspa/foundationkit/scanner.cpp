/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "scanner.h"

#include <fstream>
#include <sstream>

namespace {

std::string readFileUtf8(const std::filesystem::path &path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

} // namespace

void Scanner::resetScanLocation()
{
  _scanLocation = _string.utf8().cbegin();
}

Scanner::Scanner(const std::filesystem::path &path, CharacterSet charactersToBeSkipped)
  : _charactersToBeSkipped(charactersToBeSkipped)
{
  _displayName = RKString(path.stem().string());
  _string = RKString(readFileUtf8(path));
  resetScanLocation();
}

Scanner::Scanner(const RKString &content, CharacterSet charactersToBeSkipped)
  : _charactersToBeSkipped(charactersToBeSkipped)
  , _string(content)
{
  resetScanLocation();
}

Scanner::Scanner(std::string content, CharacterSet charactersToBeSkipped)
  : _charactersToBeSkipped(charactersToBeSkipped)
  , _string(std::move(content))
{
  resetScanLocation();
}

std::string::const_iterator Scanner::find_first_not_of(const std::string &chars, const std::string &text,
                                                      std::string::const_iterator location) const
{
  auto it = location;
  const auto end = text.cend();
  while (it != end)
  {
    if (chars.find(*it) == std::string::npos)
      return it;
    ++it;
  }
  return end;
}

std::string::const_iterator Scanner::find_first_of(const std::string &chars, const std::string &text,
                                                   std::string::const_iterator location) const
{
  auto it = location;
  const auto end = text.cend();
  while (it != end)
  {
    if (chars.find(*it) != std::string::npos)
      return it;
    ++it;
  }
  return end;
}

bool Scanner::scanCharacters(CharacterSet set, RKString &into)
{
  const std::string &text = _string.utf8();
  auto found = find_first_not_of(set.string(), text, _scanLocation);

  if (found >= text.cend())
  {
    _scanLocation = text.cend();
    into = RKString();
    return false;
  }

  into = RKString(std::string(_scanLocation, found));
  _scanLocation = found;
  return true;
}

bool Scanner::scanLine(RKString &into)
{
  const std::string &text = _string.utf8();
  auto newlineLocation = find_first_of(CharacterSet::newlineCharacter().string(), text, _scanLocation);

  if (newlineLocation >= text.cend())
  {
    _scanLocation = text.cend();
    into = RKString();
    return false;
  }

  into = RKString(std::string(_scanLocation, newlineLocation));
  _scanLocation = newlineLocation + 1;
  return true;
}

bool Scanner::scanUpToCharacters(CharacterSet set, RKString &into)
{
  const std::string &text = _string.utf8();
  auto found = find_first_not_of(_charactersToBeSkipped.string(), text, _scanLocation);

  if (found >= text.cend())
  {
    _scanLocation = text.cend();
    into = RKString();
    return false;
  }

  _scanLocation = found;
  found = find_first_of(set.string(), text, _scanLocation);

  if (found < text.cend())
  {
    into = RKString(std::string(_scanLocation, found));
    _scanLocation = found;
    return true;
  }

  _scanLocation = text.cend();
  into = RKString();
  return false;
}

bool Scanner::isAtEnd() const
{
  return _scanLocation >= _string.utf8().cend();
}

bool Scanner::scanDouble(double &value)
{
  const std::string &text = _string.utf8();
  auto found = find_first_not_of(_charactersToBeSkipped.string(), text, _scanLocation);

  if (found >= text.cend())
  {
    _scanLocation = text.cend();
    return false;
  }

  _scanLocation = found;
  found = find_first_of(_charactersToBeSkipped.string(), text, _scanLocation);

  if (found < text.cend())
  {
    RKString into(std::string(_scanLocation, found));
    _scanLocation = found;
    bool success = false;
    value = into.toDouble(&success);
    return success;
  }

  _scanLocation = text.cend();
  return false;
}

bool Scanner::scanInt(int &value)
{
  const std::string &text = _string.utf8();
  auto found = find_first_not_of(_charactersToBeSkipped.string(), text, _scanLocation);

  if (found >= text.cend())
  {
    _scanLocation = text.cend();
    return false;
  }

  _scanLocation = found;
  found = find_first_of(_charactersToBeSkipped.string(), text, _scanLocation);

  if (found < text.cend())
  {
    RKString into(std::string(_scanLocation, found));
    _scanLocation = found;
    bool success = false;
    value = into.toInt(&success);
    return success;
  }

  _scanLocation = text.cend();
  return false;
}
