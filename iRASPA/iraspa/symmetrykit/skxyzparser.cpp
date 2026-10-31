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

namespace
{
  // Extended-XYZ comment line: Lattice="ax ay az bx by bz cx cy cz" may appear anywhere on the line.
  std::optional<double3x3> parseLatticeFromCommentLine(const RKString &commentLine)
  {
    const std::string lower = commentLine.toLower().utf8();
    const std::string key = "lattice=";
    const size_t keyPos = lower.find(key);
    if(keyPos == std::string::npos) return std::nullopt;

    size_t start = keyPos + key.size();
    size_t end = std::string::npos;
    if(start < lower.size() && (lower[start] == '"' || lower[start] == '\''))
    {
      const char quote = lower[start];
      start += 1;
      end = lower.find(quote, start);
    }
    if(end == std::string::npos) end = lower.size();

    const std::vector<RKString> terms = RKString(commentLine.utf8().substr(start, end - start)).splitWhitespace();
    if(terms.size() < 9) return std::nullopt;

    double values[9];
    for(size_t i=0;i<9;i++)
    {
      bool succes = false;
      values[i] = terms[i].toDouble(&succes);
      if(!succes) {throw std::runtime_error("Could not parse the lattice in the XYZ comment line");}
    }

    double3x3 unitCell{};
    unitCell.ax = values[0]; unitCell.ay = values[1]; unitCell.az = values[2];
    unitCell.bx = values[3]; unitCell.by = values[4]; unitCell.bz = values[5];
    unitCell.cx = values[6]; unitCell.cy = values[7]; unitCell.cz = values[8];
    return unitCell;
  }

  bool isAtomCountLine(const std::vector<RKString> &terms)
  {
    if(terms.size() != 1) return false;
    bool succes = false;
    const int count = terms[0].toInt(&succes);
    return succes && count >= 0;
  }
}

void SKXYZParser::startParsing() noexcept(false)
{
  int lineNumber = 0;
  int numberOfAtoms = 0;
  RKString scannedLine;
  std::vector<std::shared_ptr<SKStructure>> frames{};

  // A frame starts with an atom-count line followed by a comment line (optionally carrying a lattice).
  auto beginFrame = [this](std::shared_ptr<SKStructure> frame, const RKString &commentLine)
  {
    frame->kind = SKStructure::Kind::molecule;
    frame->displayName = _scanner.displayName();
    frame->drawUnitCell = false;
    if(std::optional<double3x3> unitCell = parseLatticeFromCommentLine(commentLine))
    {
      if(!_asMolecule)
      {
        frame->kind = SKStructure::Kind::molecularCrystal;
        frame->drawUnitCell = true;
      }
      frame->cell = std::make_shared<SKCell>(*unitCell);
    }
  };

  auto finishFrame = [this, &frames](const std::shared_ptr<SKStructure> &frame)
  {
    const bool periodic = frame->kind == SKStructure::Kind::molecularCrystal;
    frame->applyInferredMaterialType({}, periodic ? std::optional<SKStructure::Kind>(SKStructure::Kind::crystal) : std::nullopt);
    frames.push_back(frame);
  };

  // skip first line (atom count)
  _scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine);
  if(scannedLine.empty()) {throw std::runtime_error("Empty file");}

  // scan second line (comment, possibly with a lattice)
  _scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine);
  if(scannedLine.empty()) {throw std::runtime_error("XYZ file near empty");}

  beginFrame(_frame, scannedLine);

  while(!_scanner.isAtEnd())
  {
    // scan to first keyword
    _previousScanLocation = _scanner.scanLocation();
    if (_scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine) && !scannedLine.simplified().empty())
    {
      lineNumber += 1;

      // handle reading in all atoms
      double3 position;

      // read chemical element, and x,y,z position
      std::vector<RKString> termsScannedLined = scannedLine.splitWhitespace();

      // A lone integer starts the next frame of a multi-frame (trajectory) XYZ file.
      if(isAtomCountLine(termsScannedLined))
      {
        finishFrame(_frame);
        _frame = std::make_shared<SKStructure>();

        // The scanner stopped at the newline ending the count line; step over exactly that one
        // line ending so an empty comment line is not skipped.
        const std::string &text = _scanner.string().utf8();
        auto location = _scanner.scanLocation();
        if(location < text.cend() && *location == '\r') ++location;
        if(location < text.cend() && *location == '\n') ++location;
        _scanner.setScanLocation(location);

        RKString commentLine;
        _scanner.scanLine(commentLine);
        beginFrame(_frame, commentLine);
        continue;
      }

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
  finishFrame(_frame);
  _movies.push_back(frames);
}
