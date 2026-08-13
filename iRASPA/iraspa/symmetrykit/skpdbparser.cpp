#include <filesystem>
#include <cctype>
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

#include "skpdbparser.h"
#include <cctype>
#include <cmath>
#include "symmetrykitprotocols.h"
#include "skasymmetricatom.h"
#include "skelement.h"

SKPDBParser::SKPDBParser(const std::filesystem::path &path, bool proteinOnlyAsymmetricUnitCell, bool asMolecule,
                         CharacterSet charactersToBeSkipped, bool separatePolymerChains): SKParser(),
  _scanner(path, charactersToBeSkipped), _proteinOnlyAsymmetricUnitCell(proteinOnlyAsymmetricUnitCell),
  _asMolecule(asMolecule), _separatePolymerChains(separatePolymerChains),
  _frame(std::make_shared<SKStructure>()), _spaceGroupHallNumber(1)
{
  _frame->kind = SKStructure::Kind::molecule;
  _frame->displayName = _scanner.displayName();
}

namespace
{
// What a crystallographer soaks a crystal in: the water itself, and the salts and cryoprotectants
// that come with it. A part made of these and nothing else is solvent however it was written down.
bool isWaterResidue(const RKString &residueName)
{
  return residueName == "HOH" || residueName == "DOD" || residueName == "WAT" || residueName == "H2O";
}

bool isSolventAgentResidue(const RKString &residueName)
{
  static const std::set<RKString> agents = {
    RKString("SO4"), RKString("PO4"), RKString("GOL"), RKString("EDO"), RKString("MPD"),
    RKString("PEG"), RKString("PG4"), RKString("ACT"), RKString("ACY"), RKString("DMS"),
    RKString("TRS"), RKString("MES"), RKString("EPE"), RKString("IMD"), RKString("FMT"),
    RKString("NA"), RKString("K"), RKString("MG"), RKString("CA"), RKString("ZN"),
    RKString("MN"), RKString("FE"), RKString("NI"), RKString("CU"), RKString("CD"),
    RKString("CL"), RKString("BR"), RKString("IOD"), RKString("F"), RKString("CO")
  };
  return agents.find(residueName) != agents.end();
}

// A cell of one Angstrom on a side with right angles is what the format writes when there is no
// crystal at all; every real cell is larger than that.
bool isPlaceholderCell(double a, double b, double c, double alpha, double beta, double gamma)
{
  const auto isOne = [](double value) { return std::fabs(value - 1.0) < 1.0e-3; };
  const auto isRight = [](double value) { return std::fabs(value - 90.0) < 1.0e-3; };
  return isOne(a) && isOne(b) && isOne(c) && isRight(alpha) && isRight(beta) && isRight(gamma);
}
} // namespace

// The residues of the part being read, gathered as the atoms arrive: which of the three backbone
// atoms each residue has, and where its nitrogen and its carbonyl carbon sit, which is what says
// whether two residues are bonded to one another.
void SKPDBParser::noteResidueAtom(const std::shared_ptr<SKAsymmetricAtom> &atom)
{
  const RKString residueName = atom->residueName();
  if(residueName.isEmpty()) { return; }

  ResidueRecord &residue = _residues[{atom->chainIdentifier(), atom->residueSequenceNumber()}];
  residue.name = residueName;
  residue.water = isWaterResidue(residueName);

  const RKString atomName = atom->displayName().toUpper();
  if(atomName == "N")
  {
    residue.hasNitrogen = true;
    residue.nitrogen = atom->position();
  }
  else if(atomName == "CA")
  {
    residue.hasAlphaCarbon = true;
  }
  else if(atomName == "C")
  {
    residue.hasCarbonyl = true;
    residue.carbonyl = atom->position();
  }
}

// SEQRES gives the sequence of each polymer chain, so a chain that has one is a polymer whatever
// its coordinates turned out like.
void SKPDBParser::parseSeqres(const RKString &line)
{
  if(line.size() < 19) { return; }

  const char16_t chainIdentifier = static_cast<char16_t>(line[11]);
  for(int start = 19; start + 3 <= line.size(); start += 4)
  {
    const RKString residueName = line.mid(start, 3).simplified().toUpper();
    if(residueName.isEmpty() || isWaterResidue(residueName)) { continue; }
    if(PredefinedElements::residueDefinitions.find(residueName) != PredefinedElements::residueDefinitions.end())
    {
      _polymerChains.insert(chainIdentifier);
      return;
    }
  }
}

// MODRES names a residue written as a HETATM that is part of the chain all the same, selenomethionine
// being the everyday case. Without it such a protein reads as half solvent.
void SKPDBParser::parseModres(const RKString &line)
{
  if(line.size() < 15) { return; }

  const RKString residueName = line.mid(12, 3).simplified().toUpper();
  if(!residueName.isEmpty())
  {
    _modifiedResidues.insert(residueName);
  }
}

// Protein, solvent or neither, decided on the residues of the part rather than on its atoms: a
// count of atoms is swayed by the hydrogens a file may or may not carry, and by the three atoms
// every water brings with it.
SKStructure::Kind SKPDBParser::kindOfCurrentPart()
{
  int peptideResidues = 0;
  int waterResidues = 0;
  int otherResidues = 0;

  for(const auto &[key, residue] : _residues)
  {
    const bool declaredPolymer = _polymerChains.find(key.first) != _polymerChains.end() &&
                                 (_modifiedResidues.find(residue.name) != _modifiedResidues.end() ||
                                  PredefinedElements::residueDefinitions.find(residue.name) !=
                                      PredefinedElements::residueDefinitions.end());

    if(residue.water)
    {
      waterResidues += 1;
    }
    else if((residue.hasNitrogen && residue.hasAlphaCarbon && residue.hasCarbonyl) ||
            (declaredPolymer && !isWaterResidue(residue.name)))
    {
      peptideResidues += 1;
    }
    else
    {
      otherResidues += 1;
    }
  }

  // Two residues joined by a peptide bond are a chain; one amino acid on its own is a ligand. The
  // carbonyl carbon of a residue lies about 1.33 Angstrom from the nitrogen of the next one.
  int peptideBonds = 0;
  const ResidueRecord *previous = nullptr;
  char16_t previousChain = u'\0';
  for(const auto &[key, residue] : _residues)
  {
    if(previous && key.first == previousChain && previous->hasCarbonyl && residue.hasNitrogen)
    {
      if((previous->carbonyl - residue.nitrogen).length() < 2.0)
      {
        peptideBonds += 1;
      }
    }
    previous = &residue;
    previousChain = key.first;
  }

  const bool isProtein = peptideResidues >= 2 && peptideBonds >= 1 && peptideResidues > otherResidues;
  if(isProtein)
  {
    _proteinDetected = true;
    return _periodic && !_asMolecule ? SKStructure::Kind::proteinCrystal : SKStructure::Kind::protein;
  }

  bool onlySolvent = waterResidues > 0 && peptideResidues == 0;
  if(onlySolvent)
  {
    for(const auto &[key, residue] : _residues)
    {
      if(!residue.water && !isSolventAgentResidue(residue.name)) { onlySolvent = false; break; }
    }
  }
  if(_proteinDetected && onlySolvent)
  {
    // There is no kind for the solvent of a protein that is not a crystal, and a loose bag of water
    // is a molecule as much as anything else.
    return _periodic && !_asMolecule ? SKStructure::Kind::proteinCrystalSolvent
                                     : SKStructure::Kind::molecule;
  }

  return _periodic && !_asMolecule ? SKStructure::Kind::molecularCrystal : SKStructure::Kind::molecule;
}

void SKPDBParser::addFrameToStructure(size_t currentMovie, size_t currentFrame)
{
  if (currentMovie >= _movies.size())
  {
    std::vector<std::shared_ptr<SKStructure>> movie = std::vector<std::shared_ptr<SKStructure>>();
    _movies.push_back(movie);
  }

  if (currentFrame >= _movies[currentMovie].size())
  {
    if(_cell)
    {
      _frame->cell = std::make_shared<SKCell>(*_cell);
    }

    const SKStructure::Kind kind = kindOfCurrentPart();
    _frame->kind = kind;

    switch(kind)
    {
    case SKStructure::Kind::proteinCrystal:
    case SKStructure::Kind::proteinCrystalSolvent:
      _frame->drawUnitCell = !_proteinOnlyAsymmetricUnitCell;
      _frame->spaceGroupHallNumber = _proteinOnlyAsymmetricUnitCell ? 1 : _spaceGroupHallNumber;
      break;
    case SKStructure::Kind::molecularCrystal:
      _frame->drawUnitCell = true;
      _frame->spaceGroupHallNumber = _spaceGroupHallNumber;
      break;
    default:
      _frame->drawUnitCell = false;
      _frame->spaceGroupHallNumber = 1;
      break;
    }

    _movies[currentMovie].push_back(_frame);

    _frame = std::make_shared<SKStructure>();
    _frame->atoms.clear();
    _frame->displayName = _scanner.displayName();
    _frame->kind = SKStructure::Kind::molecule;
    _residues.clear();
  }
}

void SKPDBParser::startParsing() noexcept(false)
{
  int lineNumber = 0;
  int modelNumber = 0;
  size_t currentMovie = 0;
  size_t currentFrame = 0;

  while(!_scanner.isAtEnd())
  {
    RKString scannedLine;

    // scan to first keyword
    _previousScanLocation = _scanner.scanLocation();
    if (_scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), scannedLine) && !scannedLine.isEmpty())
    {
      lineNumber += 1;

      int length = scannedLine.size();

      if(length < 3) continue;

              RKString shortKeyword = scannedLine.mid(0, 3);
      
      if(shortKeyword == "TER")
      {
        // TER marks the end of a polymer chain in the PDB. Splitting on it puts
        // each chain in its own scene-view movie; with Separate polymer chains
        // off, keep reading into the current structure.
        if (_separatePolymerChains)
        {
          addFrameToStructure(currentMovie,currentFrame);
          currentMovie += 1;
        }
        continue;
      }

      if(length < 6) continue;

              RKString keyword = scannedLine.mid(0, 6);
      
      if(keyword == "HEADER")
      {
        continue;
      }

      if(keyword == "AUTHOR")
      {
        continue;
      }

      if(keyword == "REVDAT")
      {
        continue;
      }

      if(keyword == "JRNL  ")
      {
        continue;
      }

      if(keyword == "REMARK")
      {
        continue;
      }

      // A structure solved in solution, in the microscope or on paper has no crystal, however the
      // cell record below may read.
      if(keyword == "EXPDTA")
      {
        const RKString experiment = scannedLine.mid(6, length - 6).simplified().toUpper();
        if(experiment.contains("NMR") || experiment.contains("ELECTRON MICROSCOPY") ||
           experiment.contains("SOLUTION SCATTERING") || experiment.contains("THEORETICAL MODEL"))
        {
          _experimentIsNonPeriodic = true;
          _periodic = false;
        }
        continue;
      }

      if(keyword == "SEQRES")
      {
        parseSeqres(scannedLine);
        continue;
      }

      if(keyword == "MODRES")
      {
        parseModres(scannedLine);
        continue;
      }

      if(keyword == "MODEL")
      {
        currentMovie = 0;

        if(length <= 10) continue;

                  RKString modelString = scannedLine.mid(6, length - 6);
        
        bool success = false;
        int integerValue = modelString.toInt(&success);
        if(success)
        {
          _frame = std::make_shared<SKStructure>();
          currentFrame = std::max(0, integerValue-1);
          currentFrame = modelNumber;
          modelNumber += 1;
        }
        continue;
      }
      if(keyword == "ENDMDL")
      {
        // also frames with zero atoms are allowed in PDB movies from RASPA. This happens in grand-canonical ensembles at low fugacities.
        addFrameToStructure(currentMovie,currentFrame);
        currentFrame += 1;
        continue;
      }
      if(keyword == "SCALE1")
      {
        continue;
      }

      if(keyword == "SCALE2")
      {
        continue;
      }

      if(keyword == "SCALE3")
      {
        continue;
      }

      if(keyword == "CRYST1")
      {
                  RKString lengthAString;
          RKString lengthBString;
          RKString lengthCString;
        
        if(scannedLine.size()>=17)
        {
                      lengthAString = scannedLine.mid(6, 9);
                  }
        if(scannedLine.size()>=24)
        {
                      lengthBString = scannedLine.mid(15, 9);
                  }
        if(scannedLine.size()>=33)
        {
                      lengthCString = scannedLine.mid(24, 9);
                  }

        bool succes = false;
        _a = lengthAString.toDouble(&succes);
        _b = lengthBString.toDouble(&succes);
        _c = lengthCString.toDouble(&succes);

        _alpha = 90.0;
        _beta = 90.0;
        _gamma = 90.0;
        if(scannedLine.size()>=54)
        {
                      RKString alphaAngleString;
            RKString betaAngleString;
            RKString gammaAngleString;
                    if(scannedLine.size()>=40)
          {
                          alphaAngleString = scannedLine.mid(33, 7);
                      }
          if(scannedLine.size()>=47)
          {
                          betaAngleString = scannedLine.mid(40, 7);
                      }
          if(scannedLine.size()>=54)
          {
                          gammaAngleString = scannedLine.mid(47, 7);
                      }

          _alpha = alphaAngleString.toDouble(&succes);
          _beta = betaAngleString.toDouble(&succes);
          _gamma = gammaAngleString.toDouble(&succes);
        }
        _cell = SKCell(_a, _b, _c, _alpha * M_PI/180.0, _beta*M_PI/180.0, _gamma*M_PI/180.0);

        // What makes the entry periodic is a cell of a real crystal, not the length of the line the
        // cell was written on: an entry without a crystal carries a placeholder cell in the very
        // same columns.
        const bool cellIsReal = _a > 0.0 && _b > 0.0 && _c > 0.0 &&
                                !isPlaceholderCell(_a, _b, _c, _alpha, _beta, _gamma);
        _periodic = cellIsReal && !_experimentIsNonPeriodic;

        if(scannedLine.size()>=66)
        {
                      RKString spaceGroupString = scannedLine.mid(55, 11);
          
          if(std::optional<int> spaceGroupHallNumber = SKSpaceGroup::HallNumberFromHMString(spaceGroupString.simplified()))
          {
            _spaceGroupHallNumber = *spaceGroupHallNumber;
          }
        }
        continue;
      }

      if(keyword == "ORIGX1")
      {
        continue;
      }

      if(keyword == "ORIGX2")
      {
        continue;
      }

      if(keyword == "ORIGX3")
      {
        continue;
      }


      if(keyword == "ATOM  " || keyword == "HETATM")
      {
         //  COLUMNS   LENGHT  DATA TYPE       CONTENTS
         //  --------------------------------------------------------------------------------
         //   0 -  5   6       Record name     "ATOM  "
         //   6 - 10   5       Integer         Atom serial number.
         //  11        1
         //  12 - 15   4       Atom            Atom name.
         //  16        1       Character       Alternate location indicator.
         //  17 - 19   3       Residue name    Residue name.
         //  20        1
         //  21        1       Character       Chain identifier.
         //  22 - 25   4       Integer         Residue sequence number.
         //  26        1       AChar           Code for insertion of residues.
         //  27 - 29   3
         //  30 - 37   8       Real(8.3)       Orthogonal coordinates for X in Angstroms.
         //  38 - 45   8       Real(8.3)       Orthogonal coordinates for Y in Angstroms.
         //  46 - 53   8       Real(8.3)       Orthogonal coordinates for Z in Angstroms.
         //  54 - 59   6       Real(6.2)       Occupancy.
         //  60 - 65   6       Real(6.2)       Temperature factor (Default = 0.0).
         //  66 - 71   6
         //  72 - 75   4       LString(4)      Segment identifier, left-justified.
         //  76 - 77   2       LString(2)      Element symbol, right-justified.
         //  78 - 79   2       LString(2)      Charge on the atom.

        const bool isHetatm = keyword == "HETATM";
        std::shared_ptr<SKAsymmetricAtom> atom = std::make_shared<SKAsymmetricAtom>();
        atom->setSolvent(isHetatm);
        atom->fractional(false);

                  RKString positionsX;
          RKString positionsY;
          RKString positionsZ;
        
        if(scannedLine.size()>=11)
        {
          bool succes = false;
                      RKString atomSerialNumberString = scannedLine.mid(6, 5).simplified();
                    int atomSerialNumber = atomSerialNumberString.toInt(&succes);
          if(succes)
          {
            atom->setSerialNumber(atomSerialNumber);
          }
        }

        if(scannedLine.size()>=16)
        {
                      RKString atomName = scannedLine.mid(12, 4).simplified();
            atom->setDisplayName(atomName);
            atom->setRemotenessIndicator(scannedLine.mid(14, 1).utf8().empty() ? '\0' : scannedLine.mid(14, 1).utf8()[0]);
            atom->setBranchDesignator(scannedLine.mid(15, 1).utf8().empty() ? '\0' : scannedLine.mid(15, 1).utf8()[0]);
          
                      RKString elementString = scannedLine.mid(12, 2).simplified().toLower();
                    if(!elementString.isEmpty())
          {
            elementString.replace(0, 1, RKString(static_cast<char>(std::toupper(static_cast<unsigned char>(elementString[0])))));
          }
          if (std::map<RKString,int>::iterator index = PredefinedElements::atomicNumberData.find(elementString); index != PredefinedElements::atomicNumberData.end())
          {
             atom->setElementIdentifier(index->second);
          }

          if(scannedLine.size()>=20)
          {
                          RKString residueName = scannedLine.mid(17, 3).simplified().toUpper();

            atom->setResidueName(residueName);

            const RKString backboneLookupName = atomName.toUpper();
            auto it = PredefinedElements::residueDefinitionsElement.find(residueName + "+" + backboneLookupName);
            if(it !=  PredefinedElements::residueDefinitionsElement.end())
            {
              if (std::map<RKString,int>::iterator index = PredefinedElements::atomicNumberData.find(it->second); index != PredefinedElements::atomicNumberData.end())
              {
                 atom->setElementIdentifier(index->second);
              }
            }
            auto typeIt = PredefinedElements::residueDefinitionsType.find(residueName + "+" + backboneLookupName);
            if (typeIt != PredefinedElements::residueDefinitionsType.end())
            {
              atom->backBoneAtom(PredefinedElements::isBackboneAtomType(typeIt->second));
            }
          }
        }

        if(scannedLine.size() >= 22)
        {
          const char chainIdentifier = scannedLine[21];
          if (!std::isspace(static_cast<unsigned char>(chainIdentifier)))
          {
            atom->setChainIdentifier(chainIdentifier);
          }
        }

        if(scannedLine.size() >= 26)
        {
          bool success = false;
          const int residueSequenceNumber = scannedLine.mid(22, 4).trimmed().toInt(&success);
          if (success)
          {
            atom->setResidueSequenceNumber(residueSequenceNumber);
          }
        }

        if(scannedLine.size() >= 27)
        {
          const char insertionCode = scannedLine[26];
          if (!std::isspace(static_cast<unsigned char>(insertionCode)))
          {
            atom->setCodeForInsertionOfResidues(insertionCode);
          }
        }

        if(scannedLine.size()>=38)
        {
                      positionsX = scannedLine.mid(30, 8);
                  }
        if(scannedLine.size()>=46)
        {
                      positionsY = scannedLine.mid(38, 8);
                  }
        if(scannedLine.size()>=54)
        {
                      positionsZ = scannedLine.mid(46, 8);
                  }
        if(scannedLine.size()>=60)
        {
          bool succes = false;
                       RKString occupancyString = scannedLine.mid(54, 6);
                    double occupancy = occupancyString.toDouble(&succes);
          if(succes)
          {
            atom->setOccupancy(occupancy);
          }
        }
        if(scannedLine.size()>=78)
        {
                      RKString chemicalElement = scannedLine.mid(76, 2).simplified().toLower();
          
          if(!chemicalElement.isEmpty())
          {
            chemicalElement.replace(0, 1, RKString(static_cast<char>(std::toupper(static_cast<unsigned char>(chemicalElement[0])))));
            // Only the element is taken from this column. The display name has to stay the atom
            // name from columns 13-16, because the residue and backbone tables are keyed on it and
            // an alpha carbon written as "C" cannot be told from a carbonyl carbon.
            if (std::map<RKString,int>::iterator index = PredefinedElements::atomicNumberData.find(chemicalElement); index != PredefinedElements::atomicNumberData.end())
            {
               atom->setElementIdentifier(index->second);
            }
          }
        }

        double3 position;
        bool succes = false;
        position.x = positionsX.toDouble(&succes);
        position.y = positionsY.toDouble(&succes);
        position.z = positionsZ.toDouble(&succes);
        atom->setPosition(position);

        noteResidueAtom(atom);
        _frame->atoms.push_back(atom);
        continue;
      }
    }
  }

  // add current frame in case last TER, ENDMDL, or END is missing
  if(_frame->atoms.size()>0)
  {
    addFrameToStructure(currentMovie,currentFrame);
  }
}
