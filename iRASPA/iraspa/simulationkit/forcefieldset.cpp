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

#include "forcefieldset.h"
#include "rkstring.h"
#include <symmetrykit.h>

ForceFieldSet::ForceFieldSet()
{
  this->_displayName = "Default";
  this->_editable = false;

  this->_atomTypeList.clear();   // = forcefieldset._atomTypes;
  for(const auto &forceFieldType : _defaultForceField)
  {
    _atomTypeList.push_back(forceFieldType);
  }
}

ForceFieldSet::ForceFieldSet(RKString name, ForceFieldSet& forcefieldset, bool editable)
{
  this->_displayName = name;
  this->_editable = editable;

  this->_atomTypeList.clear();
  for(const ForceFieldType &forceFieldType : forcefieldset._atomTypeList)
  {
   // std::shared_ptr<ForceFieldType> newForceFieldType = std::make_shared<ForceFieldType>(forceFieldType->forceFieldStringIdentifier(), forceFieldType->atomicNumber(), forceFieldType->potentialParameters(),
   //                                                                                      forceFieldType->mass(), forceFieldType->userDefinedRadius(), forceFieldType->editable());
    _atomTypeList.push_back(forceFieldType);
  }
}

ForceFieldSet ForceFieldSet::aluminosilicate()
{
  // Framework + extra-framework types for the Calero/Auerbach charge scheme.
  // WINUI3 ForceFieldType has no charge field yet; LJ parameters and identifiers match Cocoa.
  auto tAtom = [](const char *symbol, int atomicNumber, double radius, double mass) {
    return ForceFieldType(RKString(symbol), atomicNumber, double2(22.0, 2.30), mass, radius, false);
  };
  auto cation = [](const char *symbol) -> ForceFieldType {
    ForceFieldSet defaults;
    if (ForceFieldType *base = defaults[RKString(symbol)])
      return ForceFieldType(*base);
    return ForceFieldType(RKString(symbol), 0, double2(0.0, 0.0), 0.0, 0.0, false);
  };

  ForceFieldSet defaults;
  ForceFieldSet set(RKString(aluminosilicateDisplayName), defaults, false);
  set._atomTypeList = {
    ForceFieldType(RKString("O"), 8, double2(53.0, 3.30), 15.9994, 0.66, false),
    ForceFieldType(RKString("Oa"), 8, double2(53.0, 3.30), 15.9994, 0.66, false),
    tAtom("Si", 14, 1.11, 28.0855),
    tAtom("Al", 13, 1.21, 26.9815386),
    tAtom("P", 15, 1.07, 30.973762),
    tAtom("B", 5, 0.84, 10.881),
    tAtom("Ga", 31, 1.22, 69.723),
    tAtom("Ge", 32, 1.20, 72.64),
    cation("Li"), cation("Na"), cation("K"), cation("Rb"), cation("Cs"), cation("Ag"),
    cation("Mg"), cation("Ca"), cation("Sr"), cation("Ba"),
    cation("Mn"), cation("Fe"), cation("Co"), cation("Ni"), cation("Cu"), cation("Zn"), cation("Cd"),
    cation("Y"), cation("La"), cation("Ce")
  };
  return set;
}

void ForceFieldSet::duplicate(size_t index)
{
  _atomTypeList.insert(_atomTypeList.begin() + index + 1, _atomTypeList[index]);
}

ForceFieldType* ForceFieldSet::operator[] (const RKString name)
{
  for(ForceFieldType& atomType: _atomTypeList)
  {
    if(atomType.forceFieldStringIdentifier().toLower() == name.toLower())
    {
      return &atomType;
    }
  }

  return nullptr;
}

bool ForceFieldSet::isDefaultForceFieldType(const RKString& uniqueForceFieldName)
{
  const RKString name = uniqueForceFieldName.toLower();
  return std::find_if(_defaultForceField.begin(), _defaultForceField.end(),
      [&name](const ForceFieldType& x) { return x.forceFieldStringIdentifier().toLower() == name;})
      != _defaultForceField.end();
}

RKString ForceFieldSet::uniqueName(int atomicNumber)
{
  for(int i=1;i<65536;i++)
  {
    SKElement& element = PredefinedElements::predefinedElements[atomicNumber];
    RKString newName = element._chemicalSymbol + RKString::number(i);
    std::vector<ForceFieldType>::iterator found = std::find_if(_atomTypeList.begin(), _atomTypeList.end(),
        [&newName](const ForceFieldType& x) { return x.forceFieldStringIdentifier().toLower() == newName.toLower();});
    if(found == _atomTypeList.end())
    {
      return newName;
    }
  }
  return "new";
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<ForceFieldType>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const ForceFieldType& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<ForceFieldType>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);
  ForceFieldType tempVal;
  while(vecSize--)
  {
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const ForceFieldSet &forcefieldType)
{
  stream << forcefieldType._versionNumber;

  stream << forcefieldType._displayName;
  stream << forcefieldType._editable;
  stream << forcefieldType._atomTypeList;

  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, ForceFieldSet &forcefieldType)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > forcefieldType._versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "ForceFieldSet");
  }

  stream >> forcefieldType._displayName;
  stream >> forcefieldType._editable;
  stream >> forcefieldType._atomTypeList;

  return stream;
}

std::vector<ForceFieldType> ForceFieldSet::_defaultForceField  =
{
  {ForceFieldType(RKString("H")   ,  1,  double2(7.64893,2.84642),  1.00794,  0.31, false)}, // DREIDING
  {ForceFieldType(RKString("He")  ,  2,  double2(10.9,2.64),  4.002602,  0.28, false)}, // Talu and Myers
  {ForceFieldType(RKString("Li")  ,  3,  double2(12.580415,2.183592758),  6.9421,  1.28, false)}, // UFF
  {ForceFieldType(RKString("Be")  ,  4,  double2(42.773411,2.445516981),  9.012182,  0.96, false)}, // UFF
  {ForceFieldType(RKString("B")   ,  5,  double2(47.8058,3.58141),  10.881,  0.84, false)}, // DREIDING
  {ForceFieldType(RKString("C")   ,  6,  double2(47.8562, 3.47299),  12.0107,  0.76, false)}, // DREIDING
  {ForceFieldType(RKString("N")   ,  7,  double2(38.9492,3.26256),  14.0067,  0.71, false)}, // DREIDING
  {ForceFieldType(RKString("O")   ,  8,  double2(53.0, 3.30),  15.9994,  0.66, false)}, // TraPPE-ZEO
  {ForceFieldType(RKString("F")   ,  9,  double2(36.4834,3.0932),  18.9984032,  0.57, false)}, // DREIDING
  {ForceFieldType(RKString("Ne")  ,  10, double2(21.1350972,2.889184543),  20.1797,  0.58, false)}, // UFF
  {ForceFieldType(RKString("Na")  ,  11, double2(15.096498,2.657550876),  22.98976928,  1.66, false)}, // UFF
  {ForceFieldType(RKString("Mg")  ,  12, double2(55.8570426,2.691405028),  24.305,  1.41, false)}, // UFF
  {ForceFieldType(RKString("Al")  ,  13, double2(22.6183,2.30),  26.9815386,  1.21, false)}, // chosen same as Si from TraPPE-ZEO but with epsilon ratio from DREIDING (4.39/4.27)*22.0 = 22.6183
  {ForceFieldType(RKString("Si")  ,  14, double2(22.0,2.30),  28.0855,  1.11, false)}, // TraPPE-ZEO
  {ForceFieldType(RKString("P")   ,  15, double2(161.03, 3.69723),  30.973762,  1.07, false)}, // DREIDING
  {ForceFieldType(RKString("S")   ,  16, double2(173.107,3.59032),  32.065,  1.05, false)}, // DREIDING
  {ForceFieldType(RKString("Cl")  ,  17, double2(142.562, 3.51932),  35.453,  1.02, false)}, // DREIDING
  {ForceFieldType(RKString("Ar")  ,  18, double2(93.095071, 3.445996242),  39.948,  1.06, false)}, // UFF
  {ForceFieldType(RKString("K")   ,  19, double2(17.612581, 3.396105914),  39.0983,  2.03, false)}, // UFF
  {ForceFieldType(RKString("Ca")  ,  20, double2(119.7655508,3.028164743),  40.078,  1.76, false)}, // UFF
  {ForceFieldType(RKString("Sc")  ,  21, double2(9.5611154,2.935511276),  44.955912,  1.7, false)}, // UFF
  {ForceFieldType(RKString("Ti")  ,  22, double2(8.5546822,2.82860343),  47.867,  1.6, false)}, // UFF
  {ForceFieldType(RKString("V")   ,  23, double2(8.0514656,2.80098557),  50.9415,  1.53, false)}, // UFF
  {ForceFieldType(RKString("Cr")  ,  24, double2(7.548249,2.693186825),  51.9961,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Mn")  ,  25, double2(6.5418158,2.637951104),  54.939045,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Fe")  ,  26, double2(6.5418158,2.594297067),  55.845,  1.32, false)}, // UFF
  {ForceFieldType(RKString("Co")  ,  27, double2(7.0450324,2.558661118),  58.933195,  1.26, false)}, // UFF
  {ForceFieldType(RKString("Ni")  ,  28, double2(7.548249,2.524806967),  58.6934,  1.24, false)}, // UFF
  {ForceFieldType(RKString("Cu")  ,  29, double2(2.516083,3.11369102),  63.546,  1.32, false)}, // UFF
  {ForceFieldType(RKString("Zn")  ,  30, double2(62.3988584, 2.461553158),  65.38,  1.22, false)}, // UFF
  {ForceFieldType(RKString("Ga")  ,  31, double2(208.834889,3.904809082),  69.723,  1.22, false)}, // UFF
  {ForceFieldType(RKString("Ge")  ,  32, double2(190.7190914, 3.813046514),  72.64,  1.2, false)}, // UFF
  {ForceFieldType(RKString("As")  ,  33, double2(155.4939294, 3.768501578),  74.9216,  1.19, false)}, // UFF
  {ForceFieldType(RKString("Se")  ,  34, double2(146.4360306, 3.746229110),  78.96,  1.2, false)}, // UFF
  {ForceFieldType(RKString("Br")  ,  35, double2(186.191,3.51905),  79.904,  1.2, false)}, // DREIDING
  {ForceFieldType(RKString("Kr")  ,  36, double2(110.707652, 3.689211592),  83.798,  1.16, false)}, // UFF
  {ForceFieldType(RKString("Rb")  ,  37, double2(20.128664, 3.665157326),  85.4678,  2.2, false)}, // UFF
  {ForceFieldType(RKString("Sr")  ,  38, double2(118.255901, 3.243762233),  87.62,  1.95, false)}, // UFF
  {ForceFieldType(RKString("Y")   ,  39, double2(36.2315952, 2.980056212),  88.90585,  1.9, false)}, // UFF
  {ForceFieldType(RKString("Zr")  ,  40, double2(34.7219454,2.783167595),  91.224,  1.75, false)}, // UFF
  {ForceFieldType(RKString("Nb")  ,  41, double2(29.6897794, 2.819694443),  92.90638,  1.64, false)}, // UFF
  {ForceFieldType(RKString("Mo")  ,  42, double2(28.1801296, 2.719022888),  95.96,  1.54, false)}, // UFF
  {ForceFieldType(RKString("Tc")  ,  43, double2(24.1543968, 2.670914357),  98,  1.47, false)}, // UFF
  {ForceFieldType(RKString("Ru")  ,  44, double2(28.1801296, 2.639732902),  101.07,  1.46, false)}, // UFF
  {ForceFieldType(RKString("Rh")  ,  45, double2(26.6704798, 2.609442345),  102.59055,  1.42, false)}, // UFF
  {ForceFieldType(RKString("Pd")  ,  46, double2(24.1543968, 2.582715384),  106.42,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Ag")  ,  47, double2(18.1157976,2.804549165),  107.8682,  1.45, false)}, // UFF
  {ForceFieldType(RKString("Cd")  ,  48, double2(114.7333848,2.537279549),  112.411,  1.44, false)}, // UFF
  {ForceFieldType(RKString("In")  ,  49, double2(301.4267434,3.976080979),  114.818,  1.42, false)}, // UFF
  {ForceFieldType(RKString("Sn")  ,  50, double2(285.3238122, 3.91282717),  118.71,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Sb")  ,  51, double2(225.9442534, 3.937772334),  121.76,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Te")  ,  52, double2(200.2802068, 3.98231727),  127.6,  1.38, false)}, // UFF
  {ForceFieldType(RKString("I")   ,  53, double2(170.5904274, 4.009044232),  126.90447,  1.39, false)}, // UFF
  {ForceFieldType(RKString("Xe")  ,  54, double2(167.0679112, 3.923517955),  131.293,  1.4, false)}, // UFF
  {ForceFieldType(RKString("Cs")  ,  55, double2(22.644747, 4.02418951),  132.9054519,  2.44, false)}, // UFF
  {ForceFieldType(RKString("Ba")  ,  56, double2(183.1708424, 3.298997953),  137.327,  2.15, false)}, // UFF
  {ForceFieldType(RKString("La")  ,  57, double2(8.5546822, 3.137745285),  138.90547,  2.07, false)}, // UFF
  {ForceFieldType(RKString("Ce")  ,  58, double2(6.5418158, 3.168035842),  140.116,  2.04, false)}, // UFF
  {ForceFieldType(RKString("Pr")  ,  59, double2(5.032166, 3.212580778),  140.90765,  2.03, false)}, // UFF
  {ForceFieldType(RKString("Nd")  ,  60, double2(5.032166, 3.184962917),  144.242,  2.01, false)}, // UFF
  {ForceFieldType(RKString("Pm")  ,  61, double2(4.5289494, 3.160017753),  145,  1.99, false)}, // UFF
  {ForceFieldType(RKString("Sm")  ,  62, double2(4.0257328, 3.135963488),  150.36,  1.98, false)}, // UFF
  {ForceFieldType(RKString("Eu")  ,  63, double2(4.0257328, 3.111909222),  151.964,  1.98, false)}, // UFF
  {ForceFieldType(RKString("Gd")  ,  64, double2(4.5289494, 3.000546883),  157.25,  1.96, false)}, // UFF
  {ForceFieldType(RKString("Tb")  ,  65, double2(3.5225162, 3.074491476),  158.92535,  1.94, false)}, // UFF
  {ForceFieldType(RKString("Dy")  ,  66, double2(3.5225162, 3.054000806),  162.5,  1.92, false)}, // UFF
  {ForceFieldType(RKString("Ho")  ,  67, double2(3.5225162, 3.03707373),  164.93032,  1.92, false)}, // UFF
  {ForceFieldType(RKString("Er")  ,  68, double2(3.5225162, 3.021037553),  167.259,  1.89, false)}, // UFF
  {ForceFieldType(RKString("Tm")  ,  69, double2(3.0192996, 3.005892275),  168.93421,  1.9, false)}, // UFF
  {ForceFieldType(RKString("Yb")  ,  70, double2(114.7333848, 2.988965199),  173.054,  1.87, false)}, // UFF
  {ForceFieldType(RKString("Lu")  ,  71, double2(20.6318806, 3.242871334),  174.9668,  1.87, false)}, // UFF
  {ForceFieldType(RKString("Hf")  ,  72, double2(36.2315952, 2.798312874),  178.49,  1.75, false)}, // UFF
  {ForceFieldType(RKString("Ta")  ,  73, double2(40.7605446, 2.824148937),  180.94788,  1.7, false)}, // UFF
  {ForceFieldType(RKString("W")   ,  74, double2(33.7155122, 2.734168166),  183.84,  1.62, false)}, // UFF
  {ForceFieldType(RKString("Re")  ,  75, double2(33.2122956, 2.631714813),  186.207,  1.51, false)}, // UFF
  {ForceFieldType(RKString("Os")  ,  76, double2(18.6190142, 2.779604001),  190.23,  1.44, false)}, // UFF
  {ForceFieldType(RKString("Ir")  ,  77, double2(36.7348118, 2.53015236),  192.217,  1.41, false)}, // UFF
  {ForceFieldType(RKString("Pt")  ,  78, double2(40.257328, 2.45353507),  195.084,  1.36, false)}, // UFF
  {ForceFieldType(RKString("Au")  ,  79, double2(19.6254474, 2.933729479),  196.966569,  1.36, false)}, // UFF
  {ForceFieldType(RKString("Hg")  ,  80, double2(193.738391, 2.409881033),  200.59,  1.32, false)}, // UFF
  {ForceFieldType(RKString("Tl")  ,  81, double2(342.187288, 3.872736728),  204.3833,  1.45, false)}, // UFF
  {ForceFieldType(RKString("Pb")  ,  82, double2(333.6326058, 3.828191792),  207.2,  1.46, false)}, // UFF
  {ForceFieldType(RKString("Bi")  ,  83, double2(260.6661988, 3.893227398),  208.9804,  1.48, false)}, // UFF
  {ForceFieldType(RKString("Po")  ,  84, double2(163.545395, 4.195242064),  210,  1.4, false)}, // UFF
  {ForceFieldType(RKString("At")  ,  85, double2(142.9135144, 4.231768911),  210.8,  1.5, false)}, // UFF
  {ForceFieldType(RKString("Rn")  ,  86, double2(124.7977168, 4.245132392),  222,  1.5, false)}, // UFF
  {ForceFieldType(RKString("Fr")  ,  87, double2(25.16083, 4.365403719),  223,  2.6, false)}, // UFF
  {ForceFieldType(RKString("Ra")  ,  88, double2(203.2995064, 3.275834587),  226,  2.21, false)}, // UFF
  {ForceFieldType(RKString("Ac")  ,  89, double2(16.6061478, 3.098545742),  227,  2.15, false)}, // UFF
  {ForceFieldType(RKString("Th")  ,  90, double2(13.0836316, 3.025492047),  232.03806,  2.06, false)}, // UFF
  {ForceFieldType(RKString("Pa")  ,  91, double2(11.0707652, 3.050437211),  231.03588,  2.0, false)}, // UFF
  {ForceFieldType(RKString("U")   ,  92, double2(11.0707652, 3.024601148),  238.02891,  1.96, false)}, // UFF
  {ForceFieldType(RKString("Np")  ,  93, double2(9.5611154, 3.050437211),  237,  1.9, false)}, // UFF
  {ForceFieldType(RKString("Pu")  ,  94, double2(8.0514656, 3.050437211),  244,  1.87, false)}, // UFF
  {ForceFieldType(RKString("Am")  ,  95, double2(7.0450324, 3.012128566),  243,  1.8, false)}, // UFF
  {ForceFieldType(RKString("Cm")  ,  96, double2(6.5418158, 2.963129137),  247,  1.69, false)}, // UFF
  {ForceFieldType(RKString("Bk")  ,  97, double2(6.5418158, 2.97471082),  247,  1.71, false)}, // UFF
  {ForceFieldType(RKString("Cf")  ,  98, double2(6.5418158, 2.951547453),  251,  1.71, false)}, // UFF
  {ForceFieldType(RKString("Es")  ,  99, double2(6.0385992, 2.939074871),  252,  1.68, false)}, // UFF
  {ForceFieldType(RKString("Fm")  , 100, double2(6.0385992, 2.927493188),  257,  1.70, false)}, // UFF
  {ForceFieldType(RKString("Md")  , 101, double2(5.53538260, 2.916802403),  258,  1.76, false)}, // UFF
  {ForceFieldType(RKString("No")  , 102, double2(5.5353826, 2.893639037),  259,  1.79, false)}, // UFF
  {ForceFieldType(RKString("Lr")  , 103, double2(5.5353826, 2.882948252),  262,  1.64, false)}, // same as "No"
  {ForceFieldType(RKString("Rf")  , 104, double2(5.5353826, 2.882948252),  261,  1.57, false)}, // same as "No"
  {ForceFieldType(RKString("Db")  , 105, double2(5.5353826, 2.882948252),  268,  1.49, false)}, // same as "No"
  {ForceFieldType(RKString("Sg")  , 106, double2(5.5353826, 2.882948252),  269,  1.43, false)}, // same as "No"
  {ForceFieldType(RKString("Bh")  , 107, double2(5.5353826, 2.882948252),  270,  1.41, false)}, // same as "No"
  {ForceFieldType(RKString("Hs")  , 108, double2(5.5353826, 2.882948252),  269,  1.34, false)}, // same as "No"
  {ForceFieldType(RKString("Mt")  , 109, double2(5.5353826, 2.882948252),  278,  1.29, false)}, // same as "No"
  {ForceFieldType(RKString("Ds")  , 110, double2(5.5353826, 2.882948252),  281,  1.28, false)}, // same as "No"
  {ForceFieldType(RKString("Rg")  , 111, double2(5.5353826, 2.882948252),  281,  1.21, false)}, // same as "No"
  {ForceFieldType(RKString("Cn")  , 112, double2(5.5353826, 2.882948252),  285,  1.22, false)}, // same as "No"
  {ForceFieldType(RKString("Uut") , 113, double2(5.5353826, 2.882948252),  286,  1.36, false)}, // same as "No"
  {ForceFieldType(RKString("Uuq") , 114, double2(5.5353826, 2.882948252),  289,  1.43, false)}, // same as "No"
  {ForceFieldType(RKString("Uup") , 115, double2(5.5353826, 2.882948252),  288,  1.62, false)}, // same as "No"
  {ForceFieldType(RKString("Uuh") , 116, double2(5.5353826, 2.882948252),  293,  1.75, false)}, // same as "No"
  {ForceFieldType(RKString("Uus") , 117, double2(5.5353826, 2.882948252),  294,  1.65, false)}, // same as "No"
  {ForceFieldType(RKString("Uuo") , 118, double2(5.5353826, 2.882948252),  294,  1.57, false)}  // same as "No"
};
