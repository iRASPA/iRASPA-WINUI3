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

#pragma once

#include <cmath>
#include "rkstring.h"
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <tuple>
#include <cwctype>
#include <optional>
#include <foundationkit.h>
#include "skparser.h"
#include "skasymmetricatom.h"
#include "skatomtreenode.h"
#include "skatomtreecontroller.h"
#include "skspacegroup.h"
#include "skstructure.h"

class SKPDBParser: public SKParser
{
public:
  SKPDBParser(const std::filesystem::path &path, bool onlyAsymmetricUnitCell, bool asMolecule,
              CharacterSet charactersToBeSkipped, bool separatePolymerChains = false);
  void startParsing() noexcept(false) override final;
private:
  Scanner _scanner;
  bool _proteinOnlyAsymmetricUnitCell;
  bool _asMolecule;
  // PDB TER records end a polymer chain. When false (default), TER is ignored so
  // every chain stays in one structure; when true, each TER starts a new movie.
  bool _separatePolymerChains;
  std::string::const_iterator _previousScanLocation;

  bool _proteinDetected = false;
  bool _periodic = false;
  // An entry solved in solution or in the microscope carries a placeholder cell, so what the file
  // says it is decides the periodicity together with the cell itself.
  bool _experimentIsNonPeriodic = false;
  std::shared_ptr<SKStructure> _frame;
  std::optional<SKCell> _cell;
  int _spaceGroupHallNumber;

  // What the header asserts about the polymer: the chains SEQRES lists, and the residues MODRES
  // ties back to a standard one. Both say "this belongs to the chain" for residues the coordinates
  // alone would leave in doubt.
  std::set<char16_t> _polymerChains;
  std::set<RKString> _modifiedResidues;

  // One residue of the part being read, enough of it to tell a peptide from a water.
  struct ResidueRecord
  {
    RKString name;
    bool hasNitrogen = false;
    bool hasAlphaCarbon = false;
    bool hasCarbonyl = false;
    bool water = false;
    double3 nitrogen{};
    double3 carbonyl{};
  };
  std::map<std::pair<char16_t,int64_t>, ResidueRecord> _residues;

  void addFrameToStructure(size_t currentMovie, size_t currentFrame);
  void noteResidueAtom(const std::shared_ptr<SKAsymmetricAtom> &atom);
  void parseSeqres(const RKString &line);
  void parseModres(const RKString &line);
  SKStructure::Kind kindOfCurrentPart();
};
