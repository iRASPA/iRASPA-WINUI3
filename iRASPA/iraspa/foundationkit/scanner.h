/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "characterset.h"
#include "rkstring.h"

#include <filesystem>
#include <string>

class Scanner
{
public:
  Scanner(const std::filesystem::path &path, CharacterSet charactersToBeSkipped);
  Scanner(const RKString &content, CharacterSet charactersToBeSkipped);
  Scanner(std::string content, CharacterSet charactersToBeSkipped);

  const RKString &string() const { return _string; }
  std::string::const_iterator scanLocation() const { return _scanLocation; }
  void setScanLocation(std::string::const_iterator location) { _scanLocation = location; }

  bool scanCharacters(CharacterSet set, RKString &into);
  bool scanLine(RKString &into);
  bool scanUpToCharacters(CharacterSet set, RKString &into);
  bool isAtEnd() const;
  bool scanDouble(double &value);
  bool scanInt(int &value);
  const RKString &displayName() const { return _displayName; }

private:
  CharacterSet _charactersToBeSkipped;
  RKString _displayName;
  RKString _string;
  std::string::const_iterator _scanLocation;

  std::string::const_iterator find_first_not_of(const std::string &chars, const std::string &text,
                                               std::string::const_iterator location) const;
  std::string::const_iterator find_first_of(const std::string &chars, const std::string &text,
                                            std::string::const_iterator location) const;
  void resetScanLocation();
};
