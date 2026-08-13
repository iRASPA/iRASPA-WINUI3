/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "characterset.h"

CharacterSet::CharacterSet(std::string_view chars)
  : _string(chars)
{
}

CharacterSet::CharacterSet(const char *chars, std::size_t n)
  : _string(chars ? chars : "", n)
{
}

bool CharacterSet::contains(char c) const
{
  return _string.find(c) != std::string::npos;
}

// \x0A   \n    Linefeed (LF)
// \x0B   \v    Vertical tab
// \x0C   \f    Formfeed (FF)
// \x0D   \r    Carriage return (CR)
// \x85   NEL

CharacterSet CharacterSet::newlineCharacter()
{
  return CharacterSet("\x0A", 1);
}

CharacterSet CharacterSet::newlineCharacterSet()
{
  return CharacterSet("\x0A\x0B\x0C\x0D\x85", 5);
}

CharacterSet CharacterSet::whitespaceAndNewlineCharacterSet()
{
  return CharacterSet("\x0A\x0B\x0C\x0D\x85\x20\x09\xA0", 8);
}

CharacterSet CharacterSet::whitespaceCharacterSet()
{
  return CharacterSet("\x20\x09\xA0", 3);
}
