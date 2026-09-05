/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cwctype>
#include <cmath>
#include <optional>
#include <foundationkit.h>
#include "skparser.h"
#include "skasymmetricatom.h"
#include "skatomtreenode.h"
#include "skatomtreecontroller.h"
#include "skstructure.h"
#include "skseitzintegermatrix.h"

class SKCIFParser: public SKParser
{
public:
  SKCIFParser(const std::filesystem::path &path, bool onlyAsymmetricUnitCell, bool asMolecule,
              CharacterSet charactersToBeSkipped, bool asProtein = true, bool separatePolymerChains = false);
  void startParsing() noexcept(false) override final;
  std::optional<int> spaceGroupHallNumber() {return _spaceGroupHallNumber;}
private:
  enum class SpaceGroupStatus
  {
    notFound = 0,
    HallSymbolFound = 1,
    HMSymbolFound = 2,
    NumberFound = 3,
    CIFSymmetryOperationsFound = 4
  };

  struct ResidueKey
  {
    char16_t chain = u' ';
    int64_t sequence = 0;
    bool operator<(const ResidueKey &other) const
    {
      if (chain != other.chain) { return chain < other.chain; }
      return sequence < other.sequence;
    }
  };

  struct ResidueRecord
  {
    RKString name;
    bool hasNitrogen = false;
    bool hasAlphaCarbon = false;
    bool hasCarbonyl = false;
    bool water = false;
    bool nucleotide = false;
    double3 nitrogen{};
    double3 carbonyl{};
  };

  Scanner _scanner;
  bool _proteinOnlyAsymmetricUnitCell;
  bool _asMolecule;
  bool _asProtein;
  bool _separatePolymerChains;
  std::string::const_iterator _previousScanLocation;
  std::optional<int> _spaceGroupHallNumber;
  std::vector<std::shared_ptr<SKAsymmetricAtom>> _atoms{};

  SpaceGroupStatus _spaceGroupFound = SpaceGroupStatus::notFound;
  int _spaceGroupITNumber = 0;
  std::optional<RKString> _declaredHMSymbol;
  std::vector<RKString> _cifSymmetryOperationStrings;
  std::vector<SKSeitzIntegerMatrix> _cifSymmetryOperations;

  bool _cellLengthsDefined = false;
  int _cellFormulaUnitsZ = 0;
  RKString _name;

  std::optional<RKString> _creationDate;
  std::optional<RKString> _creationMethod;
  std::optional<RKString> _chemicalNameCommon;
  std::optional<RKString> _chemicalNameSystematic;
  std::optional<RKString> _chemicalNameStructureType;
  std::optional<RKString> _chemicalFormulaStructural;
  std::optional<RKString> _chemicalFormulaSum;
  std::optional<int> _numberOfChannels;
  std::optional<int> _numberOfPockets;
  std::optional<int> _dimensionality;
  std::optional<double> _Di;
  std::optional<double> _Df;
  std::optional<double> _Dif;

  int _numberOfAminoAcidAtoms = 0;
  int _numberOfNucleicAcidAtoms = 0;
  int _numberOfAtoms = 0;
  bool _proteinDetected = false;
  bool _dnaDetected = false;

  std::set<char16_t> _polymerChains;
  std::set<RKString> _modifiedResidues;
  std::map<ResidueKey, ResidueRecord> _residues;
  std::map<RKString, RKString> _asymToEntity;
  std::set<RKString> _polymerEntityIds;

  void parseAudit(const RKString& string);
  void parseiRASPA(const RKString& string);
  void parseChemical(const RKString& string);
  void parseCell(const RKString& string);
  void parseSymmetry(const RKString& string);
  void parseName(const RKString& string);
  void parseModResidue(const RKString& string);
  void parseLoop(const RKString& string);
  void parseSymmetryEquivPos(const RKString &xyz);
  void resolveSpaceGroupFromCIFSymmetryOperations();
  void resolvePolymerChainsFromEntityTables();
  void appendAtomSite(const std::map<RKString, RKString> &dictionary, const RKString &chemicalSymbol);
  void recordEntityPolySeq(const std::map<RKString, RKString> &dictionary, const RKString &monId);
  void recordEntityPolyType(const RKString &entityId, const RKString &polyType);
  void noteResidueAtom(const std::shared_ptr<SKAsymmetricAtom> &atom);
  SKStructure::Kind kindOfCurrentPart();

  std::optional<RKString> dictionaryValue(const std::map<RKString, RKString> &dictionary,
                                          std::initializer_list<const char *> keys) const;
  std::optional<RKString> normalizedChemicalElement(const std::optional<RKString> &symbol) const;
  std::optional<double> parseCIFDouble(const std::optional<RKString> &string) const;
  std::optional<RKString> parseValue();
  void skipComment();
  void skipWhitespaceAndComments();
  int64_t scanInt();
  double scanDouble();
  std::optional<RKString> scanString();
};
