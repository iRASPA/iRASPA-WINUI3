/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <string>
#include <string_view>

class CharacterSet
{
public:
  CharacterSet() = default;
  explicit CharacterSet(std::string_view chars);
  explicit CharacterSet(const char *chars, std::size_t n);

  const std::string &string() const { return _string; }
  bool contains(char c) const;

  static CharacterSet newlineCharacter();
  static CharacterSet newlineCharacterSet();
  static CharacterSet whitespaceAndNewlineCharacterSet();
  static CharacterSet whitespaceCharacterSet();

private:
  std::string _string;
};
