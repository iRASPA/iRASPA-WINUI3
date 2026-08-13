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

#include "skcolorset.h"
#include "rkstring.h"
#include "rkcolor.h"
#include <iostream>

SKColorSet::SKColorSet(RKString name, SKColorSet& from, bool editable): _displayName(name), _editable(editable)
{
  for(auto const &[key, value]: from._colors)
  {
    this->_colors[key] = value;
  }
}

const RKColor* SKColorSet::operator[] (RKString colorName) const
{
  if(_colors.find(colorName) != _colors.end())
  {
    return &_colors.at(colorName);
  }
  return nullptr;
}

SKColorSet::SKColorSet(ColorScheme scheme)
{
  switch(scheme)
  {
    case ColorScheme::jmol:
      _displayName = "Jmol";
      _colors = SKColorSet::jMol;
      break;
    case ColorScheme::rasmol_modern:
      _displayName = "Rasmol modern";
      _colors = SKColorSet::rasmolModern;
      break;
    case ColorScheme::rasmol:
      _displayName = "Rasmol";
      _colors = SKColorSet::rasmol;
      break;
    case ColorScheme::vesta:
      _displayName = "Vesta";
      _colors = SKColorSet::vesta;
      break;
  default:
    break;
  }
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::map<RKString, RKColor>& table)
{
  // Cocoa BinaryEncoder.encode(Dictionary<String, NSColor>): empty → 0xFFFFFFFF.
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

BinaryArchive &operator>>(BinaryArchive & stream, std::map<RKString, RKColor>& table)
{
  uint32_t count = 0;
  table.clear();
  stream >> count;
  // Cocoa empty sentinel; also accept 0 from older WinUI writes.
  if (count == 0xFFFFFFFFu || count == 0)
    return stream;

  RKString key;
  RKColor value;
  for (uint32_t i = 0; i < count; ++i)
  {
    stream >> key;
    stream >> value;
    table[key] = value;
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const SKColorSet &colorSet)
{
  stream << colorSet._versionNumber;
  stream << colorSet._displayName;
  stream << colorSet._editable;
  stream << colorSet._colors;

  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, SKColorSet &colorSet)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > colorSet._versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "SKColorSet");
  }

  stream >> colorSet._displayName;
  stream >> colorSet._editable;
  stream >> colorSet._colors;

  return stream;
}

std::map<RKString, RKColor> SKColorSet::jMol =
{
  {RKString("H"), RKColor(0xFFFFFF)},  {RKString("He"), RKColor(0xD9FFFF)}, {RKString("Li"), RKColor(0xCC80FF)}, {RKString("Be"), RKColor(0xC2FF00)},
  {RKString("B"), RKColor(0xFFB5B5)},  {RKString("C"), RKColor(0x909090)},  {RKString("N"),  RKColor(0x3050F8)}, {RKString("O"),  RKColor(0xFF0D0D)},
  {RKString("F"), RKColor(0x90E050)},  {RKString("Ne"),RKColor(0xB3E3F5)},  {RKString("Na"), RKColor(0xAB5CF2)}, {RKString("Mg"), RKColor(0x8AFF00)},
  {RKString("Al"), RKColor(0xBFA6A6)}, {RKString("Si"), RKColor(0xF0C8A0)}, {RKString("P"), RKColor(0xFF8000)},  {RKString("S"), RKColor(0xFFFF30)},
  {RKString("Cl"), RKColor(0x1FF01F)}, {RKString("Ar"), RKColor(0x80D1E3)}, {RKString("K"), RKColor(0x8F40D4)},  {RKString("Ca"), RKColor(0x3DFF00)},
  {RKString("Sc"), RKColor(0xE6E6E6)}, {RKString("Ti"), RKColor(0xBFC2C7)}, {RKString("V"), RKColor(0xA6A6AB)},  {RKString("Cr"), RKColor(0x8A99C7)},
  {RKString("Mn"), RKColor(0x9C7AC7)}, {RKString("Fe"), RKColor(0xE06633)}, {RKString("Co"), RKColor(0xF090A0)}, {RKString("Ni"), RKColor(0x50D050)},
  {RKString("Cu"), RKColor(0xC88033)}, {RKString("Zn"), RKColor(0x7D80B0)}, {RKString("Ga"), RKColor(0xC28F8F)}, {RKString("Ge"), RKColor(0x668F8F)},
  {RKString("As"), RKColor(0xBD80E3)}, {RKString("Se"), RKColor(0xFFA100)}, {RKString("Br"), RKColor(0xA62929)}, {RKString("Kr"), RKColor(0x5CB8D1)},
  {RKString("Rb"), RKColor(0x702EB0)}, {RKString("Sr"), RKColor(0x00FF00)}, {RKString("Y"), RKColor(0x94FFFF)},  {RKString("Zr"), RKColor(0x94E0E0)},
  {RKString("Nb"), RKColor(0x73C2C9)}, {RKString("Mo"), RKColor(0x54B5B5)}, {RKString("Tc"), RKColor(0x3B9E9E)}, {RKString("Ru"), RKColor(0x248F8F)},
  {RKString("Rh"), RKColor(0x0A7D8C)}, {RKString("Pd"), RKColor(0x006985)}, {RKString("Ag"), RKColor(0xC0C0C0)}, {RKString("Cd"), RKColor(0xFFD98F)},
  {RKString("In"), RKColor(0xA67573)}, {RKString("Sn"), RKColor(0x668080)}, {RKString("Sb"), RKColor(0x9E63B5)}, {RKString("Te"), RKColor(0xD47A00)},
  {RKString("I"), RKColor(0x940094)},  {RKString("Xe"), RKColor(0x429EB0)}, {RKString("Cs"), RKColor(0x57178F)}, {RKString("Ba"), RKColor(0x00C900)},
  {RKString("La"), RKColor(0x70D4FF)}, {RKString("Ce"), RKColor(0xFFFFC7)}, {RKString("Pr"), RKColor(0xD9FFC7)}, {RKString("Nd"), RKColor(0xC7FFC7)},
  {RKString("Pm"), RKColor(0xA3FFC7)}, {RKString("Sm"), RKColor(0x8FFFC7)}, {RKString("Eu"), RKColor(0x61FFC7)}, {RKString("Gd"), RKColor(0x45FFC7)},
  {RKString("Tb"), RKColor(0x30FFC7)}, {RKString("Dy"), RKColor(0x1FFFC7)}, {RKString("Ho"), RKColor(0x00FF9C)}, {RKString("Er"), RKColor(0x00E675)},
  {RKString("Tm"), RKColor(0x00D452)}, {RKString("Yb"), RKColor(0x00BF38)}, {RKString("Lu"), RKColor(0x00AB24)}, {RKString("Hf"), RKColor(0x4DC2FF)},
  {RKString("Ta"), RKColor(0x4DA6FF)}, {RKString("W"), RKColor(0x2194D6)}, {RKString("Re"), RKColor(0x267DAB)},  {RKString("Os"), RKColor(0x266696)},
  {RKString("Ir"), RKColor(0x175487)}, {RKString("Pt"), RKColor(0xD0D0E0)}, {RKString("Au"), RKColor(0xFFD123)}, {RKString("Hg"), RKColor(0xB8B8D0)},
  {RKString("Tl"), RKColor(0xA6544D)}, {RKString("Pb"), RKColor(0x575961)}, {RKString("Bi"), RKColor(0x9E4FB5)}, {RKString("Po"), RKColor(0xAB5C00)},
  {RKString("At"), RKColor(0x754F45)}, {RKString("Rn"), RKColor(0x428296)}, {RKString("Fr"), RKColor(0x420066)}, {RKString("Ra"), RKColor(0x007D00)},
  {RKString("Ac"), RKColor(0x70ABFA)}, {RKString("Th"), RKColor(0x00BAFF)}, {RKString("Pa"), RKColor(0x00A1FF)}, {RKString("U"), RKColor(0x008FFF)},
  {RKString("Np"), RKColor(0x0080FF)}, {RKString("Pu"), RKColor(0x006BFF)}, {RKString("Am"), RKColor(0x545CF2)}, {RKString("Cm"), RKColor(0x785CE3)},
  {RKString("Bk"), RKColor(0x8A4FE3)}, {RKString("Cf"), RKColor(0xA136D4)}, {RKString("Es"), RKColor(0xB31FD4)}, {RKString("Fm"), RKColor(0xB31FBA)},
  {RKString("Md"), RKColor(0xB30DA6)}, {RKString("No"), RKColor(0xBD0D87)}, {RKString("Lr"), RKColor(0xC70066)}, {RKString("Rf"), RKColor(0xCC0059)},
  {RKString("Db"), RKColor(0xD1004F)}, {RKString("Sg"), RKColor(0xD90045)}, {RKString("Bh"), RKColor(0xE00038)}, {RKString("Hs"), RKColor(0xE6002E)},
  {RKString("Mt"), RKColor(0xEB0026)}, {RKString("Ds"), RKColor(0xEB0026)}, {RKString("Rg"), RKColor(0xEB0026)}, {RKString("Cn"), RKColor(0xEB0026)},
  {RKString("Uut"), RKColor(0xEB0026)}, {RKString("Uuq"), RKColor(0xEB0026)}, {RKString("Uup"), RKColor(0xEB0026)}, {RKString("Uuh"), RKColor(0xEB0026)},
  {RKString("Uus"), RKColor(0xEB0026)}, {RKString("Uuo"), RKColor(0xEB0026)}
};

std::map<RKString, RKColor> SKColorSet::rasmol =
{
  {RKString("H"), RKColor(0xFFFFFF)},  {RKString("He"), RKColor(0xFFC0CB)}, {RKString("Li"), RKColor(0xB22222)}, {RKString("Be"), RKColor(0xFF1493)},
  {RKString("B"), RKColor(0x00FF00)},  {RKString("C"), RKColor(0xC8C8C8)},  {RKString("N"), RKColor(0x8F8FFF)},  {RKString("O"), RKColor(0xF00000)},
  {RKString("F"), RKColor(0xDAA520)},  {RKString("Ne"), RKColor(0xFF1493)}, {RKString("Na"), RKColor(0x0000FF)}, {RKString("Mg"), RKColor(0x228B22)},
  {RKString("Al"), RKColor(0x808090)}, {RKString("Si"), RKColor(0xDAA520)}, {RKString("P"), RKColor(0xFFA500)},  {RKString("S"), RKColor(0xFFC832)},
  {RKString("Cl"), RKColor(0x00FF00)}, {RKString("Ar"), RKColor(0xFF1493)}, {RKString("K"), RKColor(0xFF1493)},  {RKString("Ca"), RKColor(0x808090)},
  {RKString("Sc"), RKColor(0xFF1493)}, {RKString("Ti"), RKColor(0x808090)}, {RKString("V"), RKColor(0xFF1493)},  {RKString("Cr"), RKColor(0x808090)},
  {RKString("Mn"), RKColor(0x808090)}, {RKString("Fe"), RKColor(0xFFA500)}, {RKString("Co"), RKColor(0xFF1493)}, {RKString("Ni"), RKColor(0xA52A2A)},
  {RKString("Cu"), RKColor(0xA52A2A)}, {RKString("Zn"), RKColor(0xA52A2A)}, {RKString("Ga"), RKColor(0xFF1493)}, {RKString("Ge"), RKColor(0xFF1493)},
  {RKString("As"), RKColor(0xFF1493)}, {RKString("Se"), RKColor(0xFF1493)}, {RKString("Br"), RKColor(0xA52A2A)}, {RKString("Kr"), RKColor(0xFF1493)},
  {RKString("Rb"), RKColor(0xFF1493)}, {RKString("Sr"), RKColor(0xFF1493)}, {RKString("Y"), RKColor(0xFF1493)},  {RKString("Zr"), RKColor(0xFF1493)},
  {RKString("Nb"), RKColor(0xFF1493)}, {RKString("Mo"), RKColor(0xFF1493)}, {RKString("Tc"), RKColor(0xFF1493)}, {RKString("Ru"), RKColor(0xFF1493)},
  {RKString("Rh"), RKColor(0xFF1493)}, {RKString("Pd"), RKColor(0xFF1493)}, {RKString("Ag"), RKColor(0x808090)}, {RKString("Cd"), RKColor(0xFF1493)},
  {RKString("In"), RKColor(0xFF1493)}, {RKString("Sn"), RKColor(0xFF1493)}, {RKString("Sb"), RKColor(0xFF1493)}, {RKString("Te"), RKColor(0xFF1493)},
  {RKString("I"), RKColor(0xA020F0)},  {RKString("Xe"), RKColor(0xFF1493)}, {RKString("Cs"), RKColor(0xFF1493)}, {RKString("Ba"), RKColor(0xFFA500)},
  {RKString("La"), RKColor(0xFF1493)}, {RKString("Ce"), RKColor(0xFF1493)}, {RKString("Pr"), RKColor(0xFF1493)}, {RKString("Nd"), RKColor(0xFF1493)},
  {RKString("Pm"), RKColor(0xFF1493)}, {RKString("Sm"), RKColor(0xFF1493)}, {RKString("Eu"), RKColor(0xFF1493)}, {RKString("Gd"), RKColor(0xFF1493)},
  {RKString("Tb"), RKColor(0xFF1493)}, {RKString("Dy"), RKColor(0xFF1493)}, {RKString("Ho"), RKColor(0xFF1493)}, {RKString("Er"), RKColor(0xFF1493)},
  {RKString("Tm"), RKColor(0xFF1493)}, {RKString("Yb"), RKColor(0xFF1493)}, {RKString("Lu"), RKColor(0xFF1493)}, {RKString("Hf"), RKColor(0xFF1493)},
  {RKString("Ta"), RKColor(0xFF1493)}, {RKString("W"),  RKColor(0xFF1493)}, {RKString("Re"), RKColor(0xFF1493)}, {RKString("Os"), RKColor(0xFF1493)},
  {RKString("Ir"), RKColor(0xFF1493)}, {RKString("Pt"), RKColor(0xFF1493)}, {RKString("Au"), RKColor(0xDAA520)}, {RKString("Hg"), RKColor(0xFF1493)},
  {RKString("Tl"), RKColor(0xFF1493)}, {RKString("Pb"), RKColor(0xFF1493)}, {RKString("Bi"), RKColor(0xFF1493)}, {RKString("Po"), RKColor(0xFF1493)},
  {RKString("At"), RKColor(0xFF1493)}, {RKString("Rn"), RKColor(0xFF1493)}, {RKString("Fr"), RKColor(0xFF1493)}, {RKString("Ra"), RKColor(0xFF1493)},
  {RKString("Ac"), RKColor(0xFF1493)}, {RKString("Th"), RKColor(0xFF1493)}, {RKString("Pa"), RKColor(0xFF1493)}, {RKString("U"),  RKColor(0xFF1493)},
  {RKString("Np"), RKColor(0xFF1493)}, {RKString("Pu"), RKColor(0xFF1493)}, {RKString("Am"), RKColor(0xFF1493)}, {RKString("Cm"), RKColor(0xFF1493)},
  {RKString("Bk"), RKColor(0xFF1493)}, {RKString("Cf"), RKColor(0xFF1493)}, {RKString("Es"), RKColor(0xFF1493)}, {RKString("Fm"), RKColor(0xFF1493)},
  {RKString("Md"), RKColor(0xFF1493)}, {RKString("No"), RKColor(0xFF1493)}, {RKString("Lr"), RKColor(0xFF1493)}, {RKString("Rf"), RKColor(0xFF1493)},
  {RKString("Db"), RKColor(0xFF1493)}, {RKString("Sg"), RKColor(0xFF1493)}, {RKString("Bh"), RKColor(0xFF1493)}, {RKString("Hs"), RKColor(0xFF1493)},
  {RKString("Mt"), RKColor(0xFF1493)}, {RKString("Ds"), RKColor(0xFF1493)}, {RKString("Rg"), RKColor(0xFF1493)}, {RKString("Cn"), RKColor(0xFF1493)},
  {RKString("Uut"), RKColor(0xFF1493)}, {RKString("Uuq"), RKColor(0xFF1493)}, {RKString("Uup"), RKColor(0xFF1493)}, {RKString("Uuh"), RKColor(0xFF1493)},
  {RKString("Uus"), RKColor(0xFF1493)}, {RKString("Uuo"), RKColor(0xFF1493)}
};

std::map<RKString, RKColor> SKColorSet::rasmolModern =
{
  {RKString("H"),  RKColor(0xFFFFFF)}, {RKString("He"), RKColor(0xFFC0CB)}, {RKString("Li"), RKColor(0xB22121)}, {RKString("Be"), RKColor(0xFA1691)},
  {RKString("B"),  RKColor(0x00FF00)}, {RKString("C"),  RKColor(0xD3D3D3)}, {RKString("N"), RKColor(0x87CEE6)},  {RKString("O"),  RKColor(0xFF0000)},
  {RKString("F"),  RKColor(0xDAA520)}, {RKString("Ne"), RKColor(0xFA1691)}, {RKString("Na"), RKColor(0x0000FF)}, {RKString("Mg"), RKColor(0x228B22)},
  {RKString("Al"), RKColor(0x696969)}, {RKString("Si"), RKColor(0xDAA520)}, {RKString("P"), RKColor(0xFFAA00)},  {RKString("S"),  RKColor(0xFFFF00)},
  {RKString("Cl"), RKColor(0x00FF00)}, {RKString("Ar"), RKColor(0xFA1691)}, {RKString("K"), RKColor(0xFA1691)},  {RKString("Ca"), RKColor(0x696969)},
  {RKString("Sc"), RKColor(0xFA1691)}, {RKString("Ti"), RKColor(0x696969)}, {RKString("V"), RKColor(0xFA1691)},  {RKString("Cr"), RKColor(0x696969)},
  {RKString("Mn"), RKColor(0x696969)}, {RKString("Fe"), RKColor(0xFFAA00)}, {RKString("Co"), RKColor(0xFA1691)}, {RKString("Ni"), RKColor(0x802828)},
  {RKString("Cu"), RKColor(0x802828)}, {RKString("Zn"), RKColor(0x802828)}, {RKString("Ga"), RKColor(0xFA1691)}, {RKString("Ge"), RKColor(0xFA1691)},
  {RKString("As"), RKColor(0xFA1691)}, {RKString("Se"), RKColor(0xFA1691)}, {RKString("Br"), RKColor(0x802828)}, {RKString("Kr"), RKColor(0xFA1691)},
  {RKString("Rb"), RKColor(0xFA1691)}, {RKString("Sr"), RKColor(0xFA1691)}, {RKString("Y"), RKColor(0xFA1691)},  {RKString("Zr"), RKColor(0xFA1691)},
  {RKString("Nb"), RKColor(0xFA1691)}, {RKString("Mo"), RKColor(0xFA1691)}, {RKString("Tc"), RKColor(0xFA1691)}, {RKString("Ru"), RKColor(0xFA1691)},
  {RKString("Rh"), RKColor(0xFA1691)}, {RKString("Pd"), RKColor(0xFA1691)}, {RKString("Ag"), RKColor(0x696969)}, {RKString("Cd"), RKColor(0xFA1691)},
  {RKString("In"), RKColor(0xFA1691)}, {RKString("Sn"), RKColor(0xFA1691)}, {RKString("Sb"), RKColor(0xFA1691)}, {RKString("Te"), RKColor(0xFA1691)},
  {RKString("I"),  RKColor(0xFA1691)}, {RKString("Xe"), RKColor(0xFA1691)}, {RKString("Cs"), RKColor(0xFA1691)}, {RKString("Ba"), RKColor(0xFFAA00)},
  {RKString("La"), RKColor(0xFA1691)}, {RKString("Ce"), RKColor(0xFA1691)}, {RKString("Pr"), RKColor(0xFA1691)}, {RKString("Nd"), RKColor(0xFA1691)},
  {RKString("Pm"), RKColor(0xFA1691)}, {RKString("Sm"), RKColor(0xFA1691)}, {RKString("Eu"), RKColor(0xFA1691)}, {RKString("Gd"), RKColor(0xFA1691)},
  {RKString("Tb"), RKColor(0xFA1691)}, {RKString("Dy"), RKColor(0xFA1691)}, {RKString("Ho"), RKColor(0xFA1691)}, {RKString("Er"), RKColor(0xFA1691)},
  {RKString("Tm"), RKColor(0xFA1691)}, {RKString("Yb"), RKColor(0xFA1691)}, {RKString("Lu"), RKColor(0xFA1691)}, {RKString("Hf"), RKColor(0xFA1691)},
  {RKString("Ta"), RKColor(0xFA1691)}, {RKString("W"),  RKColor(0xFA1691)}, {RKString("Re"), RKColor(0xFA1691)}, {RKString("Os"), RKColor(0xFA1691)},
  {RKString("Ir"), RKColor(0xFA1691)}, {RKString("Pt"), RKColor(0xFA1691)}, {RKString("Au"), RKColor(0xDAA520)}, {RKString("Hg"), RKColor(0xFA1691)},
  {RKString("Tl"), RKColor(0xFA1691)}, {RKString("Pb"), RKColor(0xFA1691)}, {RKString("Bi"), RKColor(0xFA1691)}, {RKString("Po"), RKColor(0xFA1691)},
  {RKString("At"), RKColor(0xFA1691)}, {RKString("Rn"), RKColor(0xFA1691)}, {RKString("Fr"), RKColor(0xFA1691)}, {RKString("Ra"), RKColor(0xFA1691)},
  {RKString("Ac"), RKColor(0xFA1691)}, {RKString("Th"), RKColor(0xFA1691)}, {RKString("Pa"), RKColor(0xFA1691)}, {RKString("U"),  RKColor(0xFA1691)},
  {RKString("Np"), RKColor(0xFA1691)}, {RKString("Pu"), RKColor(0xFA1691)}, {RKString("Am"), RKColor(0xFA1691)}, {RKString("Cm"), RKColor(0xFA1691)},
  {RKString("Bk"), RKColor(0xFA1691)}, {RKString("Cf"), RKColor(0xFA1691)}, {RKString("Es"), RKColor(0xFA1691)}, {RKString("Fm"), RKColor(0xFA1691)},
  {RKString("Md"), RKColor(0xFA1691)}, {RKString("No"), RKColor(0xFA1691)}, {RKString("Lr"), RKColor(0xFA1691)}, {RKString("Rf"), RKColor(0xFA1691)},
  {RKString("Db"), RKColor(0xFA1691)}, {RKString("Sg"), RKColor(0xFA1691)}, {RKString("Bh"), RKColor(0xFA1691)}, {RKString("Hs"), RKColor(0xFA1691)},
  {RKString("Mt"), RKColor(0xFA1691)}, {RKString("Ds"), RKColor(0xFA1691)}, {RKString("Rg"), RKColor(0xFA1691)}, {RKString("Cn"), RKColor(0xFA1691)},
  {RKString("Uut"), RKColor(0xFA1691)}, {RKString("Uuq"), RKColor(0xFA1691)}, {RKString("Uup"), RKColor(0xFA1691)}, {RKString("Uuh"), RKColor(0xFA1691)},
  {RKString("Uus"), RKColor(0xFA1691)},{RKString("Uuo"), RKColor(0xFA1691)}
};

std::map<RKString, RKColor> SKColorSet::vesta =
{
  {RKString("H"), RKColor(0xFFCCCC)},  {RKString("He"), RKColor(0xFCE9CF)}, {RKString("Li"), RKColor(0x86E074)}, {RKString("Be"), RKColor(0x5FD87B)},
  {RKString("B"), RKColor(0x20A20F)},  {RKString("C"),  RKColor(0x814929)}, {RKString("N"),  RKColor(0xB0BAE6)}, {RKString("O"), RKColor(0xFF0300)},
  {RKString("F"), RKColor(0xB0BAE6)},  {RKString("Ne"), RKColor(0xFF38B5)}, {RKString("Na"), RKColor(0xFADD3D)}, {RKString("Mg"), RKColor(0xFC7C16)},
  {RKString("Al"), RKColor(0x81B3D6)}, {RKString("Si"), RKColor(0x1B3BFA)}, {RKString("P"),  RKColor(0xC19CC3)}, {RKString("S"), RKColor(0xFFFA00)},
  {RKString("Cl"), RKColor(0x32FC03)}, {RKString("Ar"), RKColor(0xCFFEC5)}, {RKString("K"),  RKColor(0xA122F7)}, {RKString("Ca"), RKColor(0x5B96BE)},
  {RKString("Sc"), RKColor(0xB663AC)}, {RKString("Ti"), RKColor(0x78CAFF)}, {RKString("V"),  RKColor(0xE51A00)}, {RKString("Cr"), RKColor(0x00009E)},
  {RKString("Mn"), RKColor(0xA9099E)}, {RKString("Fe"), RKColor(0xB57200)}, {RKString("Co"), RKColor(0x0000AF)}, {RKString("Ni"), RKColor(0xB8BCBE)},
  {RKString("Cu"), RKColor(0x2247DD)}, {RKString("Zn"), RKColor(0x8F9082)}, {RKString("Ga"), RKColor(0x9FE474)}, {RKString("Ge"), RKColor(0x7E6FA6)},
  {RKString("As"), RKColor(0x75D057)}, {RKString("Se"), RKColor(0x9AEF10)}, {RKString("Br"), RKColor(0x7F3103)}, {RKString("Kr"), RKColor(0xFAC1F3)},
  {RKString("Rb"), RKColor(0xFF0099)}, {RKString("Sr"), RKColor(0x00FF27)}, {RKString("Y"),  RKColor(0x67988E)}, {RKString("Zr"), RKColor(0x00FF00)},
  {RKString("Nb"), RKColor(0x4CB376)}, {RKString("Mo"), RKColor(0xB486B0)}, {RKString("Tc"), RKColor(0xCDAFCB)}, {RKString("Ru"), RKColor(0xCFB8AE)},
  {RKString("Rh"), RKColor(0xCED2AB)}, {RKString("Pd"), RKColor(0xC2C4b9)}, {RKString("Ag"), RKColor(0xB8BCBE)}, {RKString("Cd"), RKColor(0xF31FDC)},
  {RKString("In"), RKColor(0xD781BB)}, {RKString("Sn"), RKColor(0x9B8FBA)}, {RKString("Sb"), RKColor(0xD88350)}, {RKString("Te"), RKColor(0xADA252)},
  {RKString("I"),  RKColor(0x8F1F8B)}, {RKString("Xe"), RKColor(0x9BA1F8)}, {RKString("Cs"), RKColor(0x0FFFB9)}, {RKString("Ba"), RKColor(0x1AF02D)},
  {RKString("La"), RKColor(0x5AC449)}, {RKString("Ce"), RKColor(0xD1FD06)}, {RKString("Pr"), RKColor(0xFDE206)}, {RKString("Nd"), RKColor(0xFC8E07)},
  {RKString("Pm"), RKColor(0x0000F5)}, {RKString("Sm"), RKColor(0xFD067D)}, {RKString("Eu"), RKColor(0xFB08D5)}, {RKString("Gd"), RKColor(0xC004FF)},
  {RKString("Tb"), RKColor(0x7104FE)}, {RKString("Dy"), RKColor(0x3106FD)}, {RKString("Ho"), RKColor(0x0742FB)}, {RKString("Er"), RKColor(0x49733B)},
  {RKString("Tm"), RKColor(0x0000E0)}, {RKString("Yb"), RKColor(0x27FDF4)}, {RKString("Lu"), RKColor(0x26FDB5)}, {RKString("Hf"), RKColor(0xB4B459)},
  {RKString("Ta"), RKColor(0xB79B56)}, {RKString("W"),  RKColor(0x8E8A80)}, {RKString("Re"), RKColor(0xB3b18E)}, {RKString("Os"), RKColor(0xC9B179)},
  {RKString("Ir"), RKColor(0xC9CF73)}, {RKString("Pt"), RKColor(0xCCC6BF)}, {RKString("Au"), RKColor(0xFEB338)}, {RKString("Hg"), RKColor(0xD3B8CC)},
  {RKString("Tl"), RKColor(0x96896D)}, {RKString("Pb"), RKColor(0x53535B)}, {RKString("Bi"), RKColor(0xD230F8)}, {RKString("Po"), RKColor(0x0000FF)},
  {RKString("At"), RKColor(0x0000FF)}, {RKString("Rn"), RKColor(0xFFFF00)}, {RKString("Fr"), RKColor(0x000000)}, {RKString("Ra"), RKColor(0x6eAA59)},
  {RKString("Ac"), RKColor(0x649E73)}, {RKString("Th"), RKColor(0x26FE78)}, {RKString("Pa"), RKColor(0x29FB35)}, {RKString("U"),  RKColor(0x7aA2AA)},
  {RKString("Np"), RKColor(0x4C4C4C)}, {RKString("Pu"), RKColor(0x4C4C4C)}, {RKString("Am"), RKColor(0x4C4C4C)}, {RKString("Cm"), RKColor(0x4C4C4C)},
  {RKString("Bk"), RKColor(0x4C4C4C)}, {RKString("Cf"), RKColor(0x4C4C4C)}, {RKString("Es"), RKColor(0x4C4C4C)}, {RKString("Fm"), RKColor(0x4C4C4C)},
  {RKString("Md"), RKColor(0x4C4C4C)}, {RKString("No"), RKColor(0x4C4C4C)}, {RKString("Lr"), RKColor(0x4C4C4C)}, {RKString("Rf"), RKColor(0x4C4C4C)},
  {RKString("Db"), RKColor(0x4C4C4C)}, {RKString("Sg"), RKColor(0x4C4C4C)}, {RKString("Bh"), RKColor(0x4C4C4C)}, {RKString("Hs"), RKColor(0x4C4C4C)},
  {RKString("Mt"), RKColor(0x4C4C4C)}, {RKString("Ds"), RKColor(0x4C4C4C)}, {RKString("Rg"), RKColor(0x4C4C4C)}, {RKString("Cn"), RKColor(0x4C4C4C)},
  {RKString("Uut"), RKColor(0x4C4C4C)}, {RKString("Uuq"), RKColor(0x4C4C4C)}, {RKString("Uup"), RKColor(0x4C4C4C)}, {RKString("Uuh"), RKColor(0x4C4C4C)},
  {RKString("Uus"), RKColor(0x4C4C4C)},{RKString("Uuo"), RKColor(0x4C4C4C)}
};

