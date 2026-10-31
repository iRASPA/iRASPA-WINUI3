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

Scanner::Scanner(const Scanner &other)
  : _charactersToBeSkipped(other._charactersToBeSkipped)
  , _displayName(other._displayName)
  , _string(other._string)
{
  const auto offset = other._scanLocation - other._string.utf8().cbegin();
  _scanLocation = _string.utf8().cbegin() + offset;
}

Scanner::Scanner(Scanner &&other) noexcept
  : _charactersToBeSkipped(std::move(other._charactersToBeSkipped))
  , _displayName(std::move(other._displayName))
{
  const auto offset = other._scanLocation - other._string.utf8().cbegin();
  _string = std::move(other._string);
  _scanLocation = _string.utf8().cbegin() + offset;
  other.resetScanLocation();
}

Scanner &Scanner::operator=(const Scanner &other)
{
  if (this != &other)
  {
    const auto offset = other._scanLocation - other._string.utf8().cbegin();
    _charactersToBeSkipped = other._charactersToBeSkipped;
    _displayName = other._displayName;
    _string = other._string;
    _scanLocation = _string.utf8().cbegin() + offset;
  }
  return *this;
}

Scanner &Scanner::operator=(Scanner &&other) noexcept
{
  if (this != &other)
  {
    const auto offset = other._scanLocation - other._string.utf8().cbegin();
    _charactersToBeSkipped = std::move(other._charactersToBeSkipped);
    _displayName = std::move(other._displayName);
    _string = std::move(other._string);
    _scanLocation = _string.utf8().cbegin() + offset;
    other.resetScanLocation();
  }
  return *this;
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

  if (_scanLocation >= text.cend())
  {
    _scanLocation = text.cend();
    into = RKString();
    return false;
  }

  auto newlineLocation = find_first_of(CharacterSet::newlineCharacter().string(), text, _scanLocation);

  if (newlineLocation >= text.cend())
  {
    // Last line without a trailing newline: still return it.
    into = RKString(std::string(_scanLocation, text.cend()));
    _scanLocation = text.cend();
    return true;
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

  // A token that runs up to end-of-input (no trailing delimiter) is still a token.
  into = RKString(std::string(_scanLocation, found));
  _scanLocation = found;
  return true;
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

  // A number that runs up to end-of-input (no trailing whitespace) is still a number.
  RKString into(std::string(_scanLocation, found));
  _scanLocation = found;
  bool success = false;
  value = into.toDouble(&success);
  return success;
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

  // A number that runs up to end-of-input (no trailing whitespace) is still a number.
  RKString into(std::string(_scanLocation, found));
  _scanLocation = found;
  bool success = false;
  value = into.toInt(&success);
  return success;
}
