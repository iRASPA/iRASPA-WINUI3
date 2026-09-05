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

#include "rkstring.h"
#include <filesystem>
#include <cctype>
#include "skxyzparser.h"
#include "skstructure.h"
#include <optional>

SKXYZParser::SKXYZParser(const std::filesystem::path &path, bool proteinOnlyAsymmetricUnitCell, bool asMolecule, CharacterSet charactersToBeSkipped): SKParser(),
    _scanner(path, charactersToBeSkipped), _proteinOnlyAsymmetricUnitCell(proteinOnlyAsymmetricUnitCell), _asMolecule(asMolecule), _frame(std::make_shared<SKStructure>())
{
  _frame->kind = SKStructure::Kind::molecule;
  _frame->displayName = _scanner.displayName();
}

void SKXYZParser::startParsing() noexcept(false)
{
  int lineNumber = 0;
  int numberOfAtoms = 0;
  RKString scannedLine;
  double3x3 unitCell{};

  // skip first line
  _scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine);
  if(scannedLine.empty()) {throw std::runtime_error("Empty file");}

  // scan second line
  _scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine);
  if(scannedLine.empty()) {throw std::runtime_error("XYZ file near empty");}

  RKString simplifiedLine = scannedLine.simplified().toLower();
  if(simplifiedLine.startsWith("lattice=\""))
  {
    simplifiedLine.remove(0,RKString("lattice=\"").size());
    simplifiedLine.replace('\"',' ');

    // read lattice vectors
    std::vector<RKString> termsScannedLined = simplifiedLine.splitWhitespace();

    if(termsScannedLined.size()>=9)
    {
      double3x3 cell;
      bool succes = false;

      unitCell.ax = termsScannedLined[0].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the ax-cell coordinate");}

      unitCell.ay = termsScannedLined[1].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the ay-cell coordinate");}

      unitCell.az = termsScannedLined[2].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the az-cell coordinate");}

      unitCell.bx = termsScannedLined[3].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the bx-cell coordinate");}

      unitCell.by = termsScannedLined[4].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the by-cell coordinate");}

      unitCell.bz = termsScannedLined[5].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the bz-cell coordinate");}

      unitCell.cx = termsScannedLined[6].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the cx-cell coordinate");}

      unitCell.cy = termsScannedLined[7].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the cy-cell coordinate");}

      unitCell.cz = termsScannedLined[8].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Count not parse the cz-cell coordinate");}

      _frame->drawUnitCell = false;
      if(!_asMolecule)
      {
        _frame->kind = SKStructure::Kind::molecularCrystal;
        _frame->drawUnitCell = true;
      }
      _frame->cell = std::make_shared<SKCell>(unitCell);
    }
  }

  while(!_scanner.isAtEnd())
  {
    // scan to first keyword
    _previousScanLocation = _scanner.scanLocation();
    if (_scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine) && !scannedLine.empty())
    {
      lineNumber += 1;

      // handle reading in all atoms
      double3 position;

      // read chemical element, and x,y,z position
      std::vector<RKString> termsScannedLined = scannedLine.splitWhitespace();
      if(termsScannedLined.size()<4) {throw std::runtime_error("Missing data");}

      numberOfAtoms += 1;
      std::shared_ptr<SKAsymmetricAtom> atom = std::make_shared<SKAsymmetricAtom>();

      RKString chemicalElement = termsScannedLined[0].simplified().toLower();
      chemicalElement.replace(0, 1, RKString(static_cast<char>(std::toupper(static_cast<unsigned char>(chemicalElement[0])))));

      bool succes = false;
      position.x = termsScannedLined[1].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Could not parse the x-coordinate");}

      position.y = termsScannedLined[2].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Could not parse the y-coordinate");}

      position.z = termsScannedLined[3].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Could not parse the z-coordinate");}

      if (std::map<RKString,int>::iterator index = PredefinedElements::atomicNumberData.find(chemicalElement); index != PredefinedElements::atomicNumberData.end())
      {
        atom->setPosition(position);
        atom->setElementIdentifier(index->second);
        atom->setUniqueForceFieldName(chemicalElement);
        atom->setDisplayName(chemicalElement);
      }
      else
      {
        atom->setPosition(position);
        atom->setElementIdentifier(0);
        atom->setDisplayName("Unknown");
      }
      _frame->atoms.push_back(atom);
    }
  }
  const bool periodic = _frame->kind == SKStructure::Kind::molecularCrystal;
  _frame->applyInferredMaterialType({}, periodic ? std::optional<SKStructure::Kind>(SKStructure::Kind::crystal) : std::nullopt);
  _movies.push_back({_frame});
}
