/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "rkstring.h"
#include <filesystem>
#include <cctype>
#include <cmath>
#include <algorithm>
#include "skcifparser.h"
#include "skcifsymmetryoperationparser.h"
#include "skcifspacegroupidentification.h"
#include "symmetrykitprotocols.h"
#include "skasymmetricatom.h"
#include "skelement.h"
#include "sknucleotide.h"
#include "skspacegroup.h"
#include "skcell.h"

namespace
{
RKString stripElementSuffix(RKString s)
{
  std::string u = s.utf8();
  std::string out;
  out.reserve(u.size());
  for (size_t i = 0; i < u.size(); )
  {
    const char c = u[i];
    if ((c >= '0' && c <= '9') || c == '+' || c == '-')
    {
      i += (i + 1 < u.size()) ? 2 : 1;
      continue;
    }
    out.push_back(c);
    ++i;
  }
  return RKString(out);
}

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

bool isKnownAminoAcidResidue(const RKString &residueName)
{
  static const std::set<RKString> codes = {
    RKString("ALA"), RKString("ASX"), RKString("CYS"), RKString("ASP"), RKString("GLU"),
    RKString("PHE"), RKString("GLY"), RKString("HIS"), RKString("ILE"), RKString("LYS"),
    RKString("LEU"), RKString("MET"), RKString("ASN"), RKString("PYL"), RKString("PRO"),
    RKString("GLN"), RKString("ARG"), RKString("SER"), RKString("THR"), RKString("SEC"),
    RKString("VAL"), RKString("TRP"), RKString("TYR"), RKString("GLX"), RKString("UNK")
  };
  return codes.find(residueName) != codes.end() ||
         PredefinedElements::residueDefinitions.find(residueName) != PredefinedElements::residueDefinitions.end();
}
} // namespace

SKCIFParser::SKCIFParser(const std::filesystem::path &path, bool proteinOnlyAsymmetricUnitCell, bool asMolecule,
                         CharacterSet charactersToBeSkipped, bool asProtein, bool separatePolymerChains): SKParser(),
  _scanner(path, charactersToBeSkipped),
  _proteinOnlyAsymmetricUnitCell(proteinOnlyAsymmetricUnitCell),
  _asMolecule(asMolecule),
  _asProtein(asProtein),
  _separatePolymerChains(separatePolymerChains)
{
  // CIF angles are degrees; override SKParser's radian defaults.
  _alpha = 90.0;
  _beta = 90.0;
  _gamma = 90.0;
  _name = _scanner.displayName();
  (void)_separatePolymerChains; // API parity with PDB; CIF/mmCIF has no TER records.
}

void SKCIFParser::startParsing() noexcept(false)
{
  while(!_scanner.isAtEnd())
  {
    RKString tempString;

    _previousScanLocation = _scanner.scanLocation();
    if (_scanner.scanUpToCharacters(CharacterSet::whitespaceAndNewlineCharacterSet(), tempString))
    {
      RKString keyword = tempString.toLower();

      if (keyword.startsWith("_audit"))
      {
        parseAudit(keyword);
      }
      else if(keyword.startsWith("_iraspa"))
      {
        parseiRASPA(keyword);
      }
      else if(keyword.startsWith("_chemical"))
      {
        parseChemical(keyword);
      }
      else if(keyword.startsWith("_cell"))
      {
        parseCell(keyword);
      }
      else if(keyword.startsWith("_symmetry") || keyword.startsWith("_space_group"))
      {
        parseSymmetry(keyword);
      }
      else if(keyword.startsWith("_pdbx_struct_mod_residue"))
      {
        parseModResidue(keyword);
      }
      else if(keyword.startsWith("data_"))
      {
        parseName(keyword);
      }
      else if(keyword.startsWith("loop_"))
      {
        parseLoop(keyword);
      }
      else if(keyword.startsWith("#"))
      {
        _scanner.setScanLocation(_previousScanLocation);
        skipComment();
      }
      else if(tempString.startsWith("_"))
      {
        // Unknown CIF/mmCIF data item: consume the value so the scanner stays aligned.
        (void)parseValue();
      }
    }
  }

  resolveSpaceGroupFromCIFSymmetryOperations();
  resolvePolymerChainsFromEntityTables();

  std::vector<std::shared_ptr<SKStructure>> movieFrames{};
  std::shared_ptr<SKStructure> structure = std::make_shared<SKStructure>();

  const double cellA = (_a > 1e-6) ? _a : 20.0;
  const double cellB = (_b > 1e-6) ? _b : 20.0;
  const double cellC = (_c > 1e-6) ? _c : 20.0;
  structure->cell = std::make_shared<SKCell>(cellA, cellB, cellC,
                                             _alpha * M_PI / 180.0, _beta * M_PI / 180.0, _gamma * M_PI / 180.0);

  const SKStructure::Kind kind = kindOfCurrentPart();
  structure->kind = kind;
  structure->displayName = _name;
  structure->atoms = _atoms;
  structure->creationDate = _creationDate;
  structure->creationMethod = _creationMethod;
  structure->chemicalFormulaSum = _chemicalFormulaSum;
  structure->chemicalFormulaStructural = _chemicalFormulaStructural;
  structure->cellFormulaUnitsZ = _cellFormulaUnitsZ;
  structure->numberOfChannels = _numberOfChannels;
  structure->numberOfPockets = _numberOfPockets;
  structure->dimensionality = _dimensionality;
  structure->Di = _Di;
  structure->Df = _Df;
  structure->Dif = _Dif;

  switch (kind)
  {
  case SKStructure::Kind::protein:
  case SKStructure::Kind::dna:
    structure->drawUnitCell = false;
    structure->spaceGroupHallNumber = 1;
    structure->periodic = false;
    break;
  case SKStructure::Kind::proteinCrystal:
  case SKStructure::Kind::proteinCrystalSolvent:
  case SKStructure::Kind::dnaCrystal:
    structure->drawUnitCell = !_proteinOnlyAsymmetricUnitCell;
    structure->spaceGroupHallNumber = _proteinOnlyAsymmetricUnitCell ? 1 : _spaceGroupHallNumber;
    structure->periodic = true;
    break;
  case SKStructure::Kind::molecule:
    structure->drawUnitCell = false;
    structure->spaceGroupHallNumber = 1;
    structure->periodic = false;
    break;
  default:
    structure->drawUnitCell = true;
    structure->spaceGroupHallNumber = _spaceGroupHallNumber;
    structure->periodic = true;
    break;
  }

  {
    std::vector<RKString> extraNames;
    if (_chemicalNameCommon) extraNames.push_back(*_chemicalNameCommon);
    if (_chemicalNameSystematic) extraNames.push_back(*_chemicalNameSystematic);
    if (_chemicalNameStructureType) extraNames.push_back(*_chemicalNameStructureType);
    structure->applyInferredMaterialType(extraNames);
  }

  movieFrames.push_back(structure);
  _movies.push_back(movieFrames);
}

void SKCIFParser::parseAudit(const RKString& string)
{
  if (string == "_audit_creation_date")
  {
    if (auto value = parseValue()) { _creationDate = *value; }
  }
  else if (string == "_audit_creation_method")
  {
    if (auto value = parseValue()) { _creationMethod = *value; }
  }
  else
  {
    (void)parseValue();
  }
}

void SKCIFParser::parseiRASPA(const RKString& string)
{
  if (string == "_iraspa_number_of_channels") { _numberOfChannels = static_cast<int>(scanInt()); }
  else if (string == "_iraspa_number_of_pockets") { _numberOfPockets = static_cast<int>(scanInt()); }
  else if (string == "_iraspa_dimensionality") { _dimensionality = static_cast<int>(scanInt()); }
  else if (string == "_iraspa_di") { _Di = scanDouble(); }
  else if (string == "_iraspa_df") { _Df = scanDouble(); }
  else if (string == "_iraspa_dif") { _Dif = scanDouble(); }
  else { (void)parseValue(); }
}

void SKCIFParser::parseChemical(const RKString& string)
{
  if (string == "_chemical_formula_structural")
  {
    if (auto value = parseValue()) { _chemicalFormulaStructural = *value; }
  }
  else if (string == "_chemical_formula_sum")
  {
    if (auto value = parseValue()) { _chemicalFormulaSum = *value; }
  }
  else if (string == "_chemical_name_common")
  {
    if (auto value = parseValue()) { _chemicalNameCommon = *value; }
  }
  else if (string == "_chemical_name_systematic")
  {
    if (auto value = parseValue()) { _chemicalNameSystematic = *value; }
  }
  else if (string == "_chemical_name_structure_type")
  {
    if (auto value = parseValue()) { _chemicalNameStructureType = *value; }
  }
  else
  {
    (void)parseValue();
  }
}

void SKCIFParser::parseCell(const RKString& string)
{
  if (string == "_cell_length_a" || string == "_cell.length_a")
  {
    _a = scanDouble();
    _cellLengthsDefined = true;
  }
  else if (string == "_cell_length_b" || string == "_cell.length_b")
  {
    _b = scanDouble();
    _cellLengthsDefined = true;
  }
  else if (string == "_cell_length_c" || string == "_cell.length_c")
  {
    _c = scanDouble();
    _cellLengthsDefined = true;
  }
  else if (string == "_cell_angle_alpha" || string == "_cell.angle_alpha")
  {
    _alpha = scanDouble();
  }
  else if (string == "_cell_angle_beta" || string == "_cell.angle_beta")
  {
    _beta = scanDouble();
  }
  else if (string == "_cell_angle_gamma" || string == "_cell.angle_gamma")
  {
    _gamma = scanDouble();
  }
  else if (string == "_cell_formula_units_z" || string == "_cell.z_pdb")
  {
    _cellFormulaUnitsZ = static_cast<int>(scanInt());
  }
  else
  {
    // Ignore unrecognized mmCIF/coreCIF cell tags.
    (void)parseValue();
  }
}

void SKCIFParser::parseSymmetry(const RKString& string)
{
  if (string == "_symmetry_cell_setting" || string == "_symmetry_cell_settings")
  {
    (void)parseValue();
    return;
  }

  if (string == "_symmetry_space_group_name_hall" ||
      string == "_symmetry.space_group_name_hall" ||
      string == "_space_group_name_hall")
  {
    if (auto possibleString = scanString())
    {
      if (auto hall = SKSpaceGroup::HallNumber(*possibleString))
      {
        _spaceGroupHallNumber = hall;
        _spaceGroupFound = SpaceGroupStatus::HallSymbolFound;
        _spaceGroupITNumber = static_cast<int>(SKSpaceGroup(*hall).spaceGroupSetting().number());
        _declaredHMSymbol = SKSpaceGroup(*hall).spaceGroupSetting().HMString();
      }
    }
    return;
  }

  if (string == "_space_group_name_h-m_alt" ||
      string == "_symmetry_space_group_name_h-m" ||
      string == "_symmetry.pdbx_full_space_group_name_h-m")
  {
    if (_spaceGroupFound != SpaceGroupStatus::HallSymbolFound)
    {
      if (auto possibleString = scanString())
      {
        _declaredHMSymbol = *possibleString;
        if (auto hall = SKSpaceGroup::HallNumberFromHMString(*possibleString))
        {
          _spaceGroupHallNumber = hall;
          _spaceGroupFound = SpaceGroupStatus::HMSymbolFound;
          _spaceGroupITNumber = static_cast<int>(SKSpaceGroup(*hall).spaceGroupSetting().number());
        }
      }
    }
    else
    {
      (void)parseValue();
    }
    return;
  }

  if (string == "_space_group_it_number" ||
      string == "_symmetry_int_tables_number" ||
      string == "_symmetry.int_tables_number")
  {
    const int spaceGroupNumber = static_cast<int>(scanInt());
    _spaceGroupITNumber = spaceGroupNumber;
    if (_spaceGroupFound == SpaceGroupStatus::notFound)
    {
      if (auto hall = SKSpaceGroup::HallNumberFromSpaceGroupNumber(spaceGroupNumber))
      {
        _spaceGroupHallNumber = hall;
        _spaceGroupFound = SpaceGroupStatus::NumberFound;
      }
    }
    return;
  }

  if (string == "_symmetry_equiv_pos_as_xyz" || string == "_space_group_symop_operation_xyz")
  {
    if (auto value = parseValue())
    {
      parseSymmetryEquivPos(*value);
    }
    return;
  }

  (void)parseValue();
}

void SKCIFParser::parseName(const RKString& string)
{
  RKString name = string;
  if (name.size() > 5)
  {
    name = name.mid(5);
  }
  _name = name;
}

void SKCIFParser::parseModResidue(const RKString& string)
{
  auto value = parseValue();
  if (!value) { return; }
  if (string == "_pdbx_struct_mod_residue.label_comp_id" ||
      string == "_pdbx_struct_mod_residue.auth_comp_id")
  {
    RKString trimmed = value->trimmed().toUpper();
    if (!trimmed.isEmpty() && trimmed != "?" && trimmed != ".")
    {
      _modifiedResidues.insert(trimmed);
    }
  }
}

void SKCIFParser::parseSymmetryEquivPos(const RKString &xyz)
{
  RKString trimmed = xyz.trimmed();
  if (trimmed.isEmpty() || trimmed == "?" || trimmed == ".") { return; }
  _cifSymmetryOperationStrings.push_back(trimmed);
}

void SKCIFParser::resolveSpaceGroupFromCIFSymmetryOperations()
{
  if (_cifSymmetryOperationStrings.empty()) { return; }

  try
  {
    _cifSymmetryOperations = SKCIFSymmetryOperationParser::parseOperations(_cifSymmetryOperationStrings);
  }
  catch (...)
  {
    _cifSymmetryOperations.clear();
    return;
  }

  const std::optional<int> declaredHall =
      (_spaceGroupFound == SpaceGroupStatus::HallSymbolFound) ? _spaceGroupHallNumber : std::nullopt;
  const std::optional<RKString> declaredHM =
      (_spaceGroupFound == SpaceGroupStatus::HMSymbolFound ||
       _spaceGroupFound == SpaceGroupStatus::HallSymbolFound)
          ? _declaredHMSymbol
          : std::nullopt;

  const std::vector<int> candidates = SKCIFSpaceGroupIdentification::candidateHallNumbers(
      _spaceGroupITNumber, declaredHall, declaredHM);

  const auto identified = SKCIFSpaceGroupIdentification::identifyHallNumber(_cifSymmetryOperations, candidates);
  if (!identified)
  {
    // WinUI has no custom-ops space-group path; keep the best declared Hall number.
    _spaceGroupFound = SpaceGroupStatus::CIFSymmetryOperationsFound;
    return;
  }

  _spaceGroupHallNumber = *identified;
  _spaceGroupFound = SpaceGroupStatus::CIFSymmetryOperationsFound;
}

void SKCIFParser::resolvePolymerChainsFromEntityTables()
{
  for (const auto &[asym, entity] : _asymToEntity)
  {
    if (_polymerEntityIds.find(entity) != _polymerEntityIds.end() && !asym.isEmpty())
    {
      _polymerChains.insert(static_cast<char16_t>(asym[0]));
    }
  }
}

std::optional<RKString> SKCIFParser::dictionaryValue(const std::map<RKString, RKString> &dictionary,
                                                     std::initializer_list<const char *> keys) const
{
  for (const char *key : keys)
  {
    auto it = dictionary.find(RKString(key).toLower());
    if (it == dictionary.end())
    {
      it = dictionary.find(RKString(key));
    }
    if (it != dictionary.end())
    {
      RKString trimmed = it->second.trimmed();
      if (!trimmed.isEmpty() && trimmed != "?" && trimmed != ".")
      {
        return trimmed;
      }
    }
  }
  return std::nullopt;
}

std::optional<RKString> SKCIFParser::normalizedChemicalElement(const std::optional<RKString> &symbol) const
{
  if (!symbol) { return std::nullopt; }
  RKString chemicalElement = symbol->trimmed();
  if (chemicalElement.isEmpty()) { return std::nullopt; }
  chemicalElement = stripElementSuffix(chemicalElement);
  if (chemicalElement.isEmpty()) { return std::nullopt; }
  chemicalElement = chemicalElement.toLower();
  chemicalElement.replace(0, 1, RKString(static_cast<char>(std::toupper(static_cast<unsigned char>(chemicalElement[0])))));
  return chemicalElement;
}

std::optional<double> SKCIFParser::parseCIFDouble(const std::optional<RKString> &string) const
{
  if (!string || string->isEmpty()) { return std::nullopt; }
  bool success = false;
  const double value = string->split('(')[0].toDouble(&success);
  if (!success) { return std::nullopt; }
  return value;
}

void SKCIFParser::appendAtomSite(const std::map<RKString, RKString> &dictionary, const RKString &chemicalSymbol)
{
  _numberOfAtoms += 1;
  auto atom = std::make_shared<SKAsymmetricAtom>();
  // A site starts out with no element so that the residue definition below, which is the better
  // source when there is one, can be told apart from a site whose element is still to be read from
  // its type symbol. The default constructor's carbon would read as "already resolved".
  atom->setElementIdentifier(0);

  if (auto groupPDB = dictionaryValue(dictionary, {"_atom_site.group_PDB", "_atom_site.group_pdb"}))
  {
    atom->setSolvent(groupPDB->toUpper() == "HETATM");
  }

  if (auto serial = dictionaryValue(dictionary, {"_atom_site.id"}))
  {
    bool ok = false;
    atom->setSerialNumber(serial->toInt(&ok));
  }

  RKString atomName = dictionaryValue(dictionary, {"_atom_site.label_atom_id", "_atom_site.auth_atom_id",
                                                   "_atom_site_label", "_atom_site.id", "_atom_site.label"})
                          .value_or(chemicalSymbol);
  atom->setDisplayName(atomName);
  if (atomName.size() >= 3)
  {
    atom->setRemotenessIndicator(atomName[2]);
  }
  if (atomName.size() >= 4)
  {
    atom->setBranchDesignator(atomName[3]);
  }

  RKString residueName = dictionaryValue(dictionary, {"_atom_site.label_comp_id", "_atom_site.auth_comp_id"})
                             .value_or(RKString("")).toUpper();
  atom->setResidueName(residueName);

  const RKString residueLookup = residueName + "+" + atomName.toUpper();
  auto elementIt = PredefinedElements::residueDefinitionsElement.find(residueLookup);
  if (elementIt != PredefinedElements::residueDefinitionsElement.end())
  {
    _numberOfAminoAcidAtoms += 1;
    auto typeIt = PredefinedElements::residueDefinitionsType.find(residueLookup);
    if (typeIt != PredefinedElements::residueDefinitionsType.end())
    {
      atom->backBoneAtom(PredefinedElements::isBackboneAtomType(typeIt->second));
    }
    auto atomicIt = PredefinedElements::atomicNumberData.find(elementIt->second);
    if (atomicIt != PredefinedElements::atomicNumberData.end())
    {
      atom->setElementIdentifier(atomicIt->second);
      atom->setUniqueForceFieldName(PredefinedElements::predefinedElements[static_cast<size_t>(atomicIt->second)]._chemicalSymbol);
    }
  }
  else if (SKNucleotide::isNucleotideResidueName(residueName))
  {
    _numberOfNucleicAcidAtoms += 1;
  }
  else if (isKnownAminoAcidResidue(residueName))
  {
    _numberOfAminoAcidAtoms += 1;
  }

  if (auto chainString = dictionaryValue(dictionary, {"_atom_site.label_asym_id", "_atom_site.auth_asym_id",
                                                      "_atom_site.label_entity_id"}))
  {
    if (!chainString->isEmpty())
    {
      atom->setChainIdentifier((*chainString)[0]);
    }
  }

  if (auto sequenceID = dictionaryValue(dictionary, {"_atom_site.label_seq_id", "_atom_site.auth_seq_id"}))
  {
    bool ok = false;
    atom->setResidueSequenceNumber(sequenceID->toInt(&ok));
  }

  if (auto insertionCode = dictionaryValue(dictionary, {"_atom_site.pdbx_PDB_ins_code", "_atom_site.pdbx_pdb_ins_code"}))
  {
    if (!insertionCode->isEmpty())
    {
      atom->setCodeForInsertionOfResidues((*insertionCode)[0]);
    }
  }

  const bool looksLikeProteinSite =
      dictionaryValue(dictionary, {"_atom_site.group_PDB", "_atom_site.group_pdb",
                                   "_atom_site.label_comp_id", "_atom_site.auth_comp_id"}).has_value();

  const auto cartnX = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.Cartn_x", "_atom_site.cartn_x", "_atom_site_Cartn_x", "_atom_site_cartn_x"}));
  const auto cartnY = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.Cartn_y", "_atom_site.cartn_y", "_atom_site_Cartn_y", "_atom_site_cartn_y"}));
  const auto cartnZ = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.Cartn_z", "_atom_site.cartn_z", "_atom_site_Cartn_z", "_atom_site_cartn_z"}));
  const auto fractX = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.fract_x", "_atom_site_fract_x"}));
  const auto fractY = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.fract_y", "_atom_site_fract_y"}));
  const auto fractZ = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.fract_z", "_atom_site_fract_z"}));

  if (looksLikeProteinSite)
  {
    if (cartnX && cartnY && cartnZ)
    {
      atom->setPosition(double3(*cartnX, *cartnY, *cartnZ));
      atom->fractional(false);
    }
    else if (fractX && fractY && fractZ)
    {
      atom->setPosition(double3(*fractX, *fractY, *fractZ));
      atom->fractional(true);
    }
  }
  else
  {
    if (fractX && fractY && fractZ)
    {
      atom->setPosition(double3(*fractX, *fractY, *fractZ));
      atom->fractional(true);
    }
    else if (cartnX && cartnY && cartnZ)
    {
      atom->setPosition(double3(*cartnX, *cartnY, *cartnZ));
      atom->fractional(false);
    }
  }

  if (auto charge = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.charge", "_atom_site_charge", "_atom_site.pdbx_formal_charge"})))
  {
    atom->setCharge(*charge);
  }

  if (auto occupancy = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.occupancy", "_atom_site_occupancy"})))
  {
    atom->setOccupancy(*occupancy);
  }

  if (auto temperature = parseCIFDouble(dictionaryValue(dictionary, {"_atom_site.B_iso_or_equiv", "_atom_site_B_iso_or_equiv",
                                                                     "_atom_site.U_iso_or_equiv", "_atom_site_U_iso_or_equiv",
                                                                     "_atom_site.b_iso_or_equiv", "_atom_site.u_iso_or_equiv"})))
  {
    atom->setTemperaturefactor(*temperature);
  }

  if (atom->elementIdentifier() == 0)
  {
    auto atomicIt = PredefinedElements::atomicNumberData.find(chemicalSymbol);
    if (atomicIt != PredefinedElements::atomicNumberData.end())
    {
      atom->setElementIdentifier(atomicIt->second);
    }
    else
    {
      RKString stripped = stripElementSuffix(chemicalSymbol);
      atomicIt = PredefinedElements::atomicNumberData.find(stripped);
      if (atomicIt != PredefinedElements::atomicNumberData.end())
      {
        atom->setElementIdentifier(atomicIt->second);
      }
    }
  }

  if (!looksLikeProteinSite)
  {
    if (auto label = dictionaryValue(dictionary, {"_atom_site_label", "_atom_site.label"}))
    {
      atom->setDisplayName(*label);
    }
    if (auto ff = dictionaryValue(dictionary, {"_atom_site_forcefield_label", "_atom_site.forcefield_label"}))
    {
      atom->setUniqueForceFieldName(*ff);
    }
    else
    {
      atom->setUniqueForceFieldName(atom->displayName());
    }
  }
  else
  {
    if (auto ff = dictionaryValue(dictionary, {"_atom_site.forcefield_label", "_atom_site_forcefield_label"}))
    {
      atom->setUniqueForceFieldName(*ff);
    }
    else if (atom->elementIdentifier() > 0)
    {
      atom->setUniqueForceFieldName(PredefinedElements::predefinedElements[static_cast<size_t>(atom->elementIdentifier())]._chemicalSymbol);
    }
    else
    {
      atom->setUniqueForceFieldName(chemicalSymbol);
    }
  }

  if (atom->elementIdentifier() <= 0) { return; }

  if (isKnownAminoAcidResidue(residueName) ||
      SKNucleotide::isNucleotideResidueName(residueName) ||
      _modifiedResidues.find(residueName) != _modifiedResidues.end())
  {
    _polymerChains.insert(atom->chainIdentifier());
  }

  noteResidueAtom(atom);
  _atoms.push_back(atom);
}

void SKCIFParser::recordEntityPolySeq(const std::map<RKString, RKString> &dictionary, const RKString &monId)
{
  RKString residueName = monId.trimmed().toUpper();
  if (residueName.isEmpty() || residueName == "?" || residueName == ".") { return; }
  if (!isKnownAminoAcidResidue(residueName) &&
      !SKNucleotide::isNucleotideResidueName(residueName) &&
      _modifiedResidues.find(residueName) == _modifiedResidues.end())
  {
    return;
  }

  if (auto entityId = dictionaryValue(dictionary, {"_entity_poly_seq.entity_id"}))
  {
    _polymerEntityIds.insert(*entityId);
    for (const auto &[asym, entity] : _asymToEntity)
    {
      if (entity == *entityId && !asym.isEmpty())
      {
        _polymerChains.insert(static_cast<char16_t>(asym[0]));
      }
    }
  }
}

void SKCIFParser::recordEntityPolyType(const RKString &entityId, const RKString &polyType)
{
  RKString entity = entityId.trimmed();
  RKString type = polyType.trimmed().toLower();
  if (entity.isEmpty()) { return; }
  const std::string t = type.utf8();
  if (t.find("polypeptide") != std::string::npos ||
      t.find("polydeoxyribonucleotide") != std::string::npos ||
      t.find("polyribonucleotide") != std::string::npos ||
      t.find("nucleotide") != std::string::npos)
  {
    _polymerEntityIds.insert(entity);
    for (const auto &[asym, mappedEntity] : _asymToEntity)
    {
      if (mappedEntity == entity && !asym.isEmpty())
      {
        _polymerChains.insert(static_cast<char16_t>(asym[0]));
      }
    }
  }
}

void SKCIFParser::noteResidueAtom(const std::shared_ptr<SKAsymmetricAtom> &atom)
{
  RKString residueName = atom->residueName().trimmed().toUpper();
  if (residueName.isEmpty()) { return; }

  ResidueRecord &record = _residues[{atom->chainIdentifier(), atom->residueSequenceNumber()}];
  record.name = residueName;
  if (isWaterResidue(residueName)) { record.water = true; }
  if (SKNucleotide::isNucleotideResidueName(residueName)) { record.nucleotide = true; }

  RKString atomName = atom->displayName().trimmed().toUpper();
  if (atomName == "N")
  {
    record.hasNitrogen = true;
    record.nitrogen = atom->position();
  }
  else if (atomName == "CA")
  {
    record.hasAlphaCarbon = true;
  }
  else if (atomName == "C")
  {
    record.hasCarbonyl = true;
    record.carbonyl = atom->position();
  }
}

SKStructure::Kind SKCIFParser::kindOfCurrentPart()
{
  int peptideResidues = 0;
  int nucleicResidues = 0;
  int waterResidues = 0;
  int otherResidues = 0;

  for (const auto &[key, residue] : _residues)
  {
    const bool declaredPolymer = _polymerChains.find(key.chain) != _polymerChains.end() &&
                                 (_modifiedResidues.find(residue.name) != _modifiedResidues.end() ||
                                  isKnownAminoAcidResidue(residue.name) ||
                                  SKNucleotide::isNucleotideResidueName(residue.name));

    if (residue.water)
    {
      waterResidues += 1;
    }
    else if (residue.nucleotide || (declaredPolymer && SKNucleotide::isNucleotideResidueName(residue.name)))
    {
      nucleicResidues += 1;
    }
    else if ((residue.hasNitrogen && residue.hasAlphaCarbon && residue.hasCarbonyl) ||
             (declaredPolymer && !isWaterResidue(residue.name) && !SKNucleotide::isNucleotideResidueName(residue.name)))
    {
      peptideResidues += 1;
    }
    else
    {
      otherResidues += 1;
    }
  }

  int peptideBonds = 0;
  const ResidueRecord *previous = nullptr;
  char16_t previousChain = u'\0';
  for (const auto &[key, residue] : _residues)
  {
    if (previous && key.chain == previousChain && previous->hasCarbonyl && residue.hasNitrogen)
    {
      if ((previous->carbonyl - residue.nitrogen).length() < 2.0)
      {
        peptideBonds += 1;
      }
    }
    previous = &residue;
    previousChain = key.chain;
  }

  const bool periodic = _cellLengthsDefined && _a > 1e-6 && _b > 1e-6 && _c > 1e-6 && !_asMolecule;

  const bool isProtein = peptideResidues >= 2 && peptideBonds >= 1 && peptideResidues > otherResidues;
  if (isProtein)
  {
    _proteinDetected = true;
    return (periodic && !_asProtein) ? SKStructure::Kind::proteinCrystal : SKStructure::Kind::protein;
  }

  const bool isDNA = nucleicResidues >= 2 && nucleicResidues > otherResidues && nucleicResidues >= peptideResidues;
  if (isDNA)
  {
    _dnaDetected = true;
    return (periodic && !_asMolecule) ? SKStructure::Kind::dnaCrystal : SKStructure::Kind::dna;
  }

  if (_numberOfAtoms > 0)
  {
    if (static_cast<double>(_numberOfAminoAcidAtoms) / static_cast<double>(_numberOfAtoms) > 0.5)
    {
      _proteinDetected = true;
      return (periodic && !_asProtein) ? SKStructure::Kind::proteinCrystal : SKStructure::Kind::protein;
    }
    if (static_cast<double>(_numberOfNucleicAcidAtoms) / static_cast<double>(_numberOfAtoms) > 0.5)
    {
      _dnaDetected = true;
      return (periodic && !_asMolecule) ? SKStructure::Kind::dnaCrystal : SKStructure::Kind::dna;
    }
  }

  bool onlySolvent = waterResidues > 0 && peptideResidues == 0 && nucleicResidues == 0;
  if (onlySolvent)
  {
    for (const auto &[key, residue] : _residues)
    {
      (void)key;
      if (!residue.water && !isSolventAgentResidue(residue.name))
      {
        onlySolvent = false;
        break;
      }
    }
  }
  if (onlySolvent)
  {
    return SKStructure::Kind::proteinCrystalSolvent;
  }

  if (_asMolecule)
  {
    return SKStructure::Kind::molecule;
  }
  return SKStructure::Kind::crystal;
}

std::optional<RKString> SKCIFParser::parseValue()
{
  if (_scanner.isAtEnd())
  {
    return std::nullopt;
  }

  skipWhitespaceAndComments();
  if (_scanner.isAtEnd())
  {
    return std::nullopt;
  }

  const std::string &source = _scanner.string().utf8();
  auto location = _scanner.scanLocation();
  if (location >= source.cend())
  {
    return std::nullopt;
  }

  // Quoted char strings
  if (*location == '\'' || *location == '"')
  {
    const char quote = *location;
    ++location;
    std::string content;
    while (location < source.cend())
    {
      const char character = *location;
      ++location;
      if (character == quote)
      {
        if (location < source.cend() && *location == quote)
        {
          content.push_back(quote);
          ++location;
          continue;
        }
        _scanner.setScanLocation(location);
        return RKString(content);
      }
      content.push_back(character);
    }
    _scanner.setScanLocation(location);
    return RKString(content);
  }

  // Semicolon text fields (line-start)
  if (*location == ';')
  {
    ++location;
    std::string content;
    while (location < source.cend() && *location != '\n' && *location != '\r')
    {
      content.push_back(*location);
      ++location;
    }
    while (location < source.cend() && (*location == '\n' || *location == '\r'))
    {
      ++location;
    }
    while (location < source.cend())
    {
      if (*location == ';')
      {
        ++location;
        _scanner.setScanLocation(location);
        return RKString(content);
      }
      if (!content.empty()) { content.push_back('\n'); }
      while (location < source.cend() && *location != '\n' && *location != '\r')
      {
        content.push_back(*location);
        ++location;
      }
      while (location < source.cend() && (*location == '\n' || *location == '\r'))
      {
        ++location;
      }
    }
    _scanner.setScanLocation(location);
    return RKString(content);
  }

  std::string::const_iterator previousScanLocation = _scanner.scanLocation();
  RKString tempString;
  if (!_scanner.scanUpToCharacters(CharacterSet::whitespaceAndNewlineCharacterSet(), tempString))
  {
    return std::nullopt;
  }

  RKString keyword = tempString.toLower();
  if (keyword.startsWith("_") || keyword.startsWith("loop_") || keyword.startsWith("data_") || keyword.startsWith("save_"))
  {
    _scanner.setScanLocation(previousScanLocation);
    return std::nullopt;
  }

  return tempString;
}

void SKCIFParser::parseLoop(const RKString& string)
{
  (void)string;
  RKString tempString;
  std::string::const_iterator previousScanLocation;
  std::vector<RKString> tags;

  previousScanLocation = _scanner.scanLocation();
  while (_scanner.scanUpToCharacters(CharacterSet::whitespaceAndNewlineCharacterSet(), tempString) &&
         (tempString.size() > 0) && (tempString.startsWith("_") || tempString.startsWith("#")))
  {
    if (tempString.startsWith("#"))
    {
      skipComment();
    }
    else
    {
      tags.push_back(tempString.toLower());
    }
    previousScanLocation = _scanner.scanLocation();
  }

  _scanner.setScanLocation(previousScanLocation);

  std::optional<RKString> value = std::nullopt;
  do
  {
    std::map<RKString, RKString> dictionary{};

    for (const RKString &tag : tags)
    {
      if ((value = parseValue()))
      {
        dictionary[tag] = *value;
      }
    }

    if (value)
    {
      if (auto symmetryXYZ = dictionaryValue(dictionary, {"_symmetry_equiv_pos_as_xyz", "_space_group_symop_operation_xyz"}))
      {
        parseSymmetryEquivPos(*symmetryXYZ);
      }
      else if (auto chemicalSymbol = normalizedChemicalElement(
                   dictionaryValue(dictionary, {"_atom_site_type_symbol", "_atom_site.type_symbol"})))
      {
        appendAtomSite(dictionary, *chemicalSymbol);
      }
      else if (auto monId = dictionaryValue(dictionary, {"_entity_poly_seq.mon_id"}))
      {
        recordEntityPolySeq(dictionary, *monId);
      }
      else if (auto modRes = dictionaryValue(dictionary, {"_pdbx_struct_mod_residue.label_comp_id",
                                                          "_pdbx_struct_mod_residue.auth_comp_id"}))
      {
        RKString trimmed = modRes->trimmed().toUpper();
        if (!trimmed.isEmpty())
        {
          _modifiedResidues.insert(trimmed);
        }
      }
      else if (auto asymId = dictionaryValue(dictionary, {"_struct_asym.id"}))
      {
        if (auto entityId = dictionaryValue(dictionary, {"_struct_asym.entity_id"}))
        {
          _asymToEntity[*asymId] = *entityId;
        }
      }
      else if (auto entityId = dictionaryValue(dictionary, {"_entity_poly.entity_id"}))
      {
        if (auto polyType = dictionaryValue(dictionary, {"_entity_poly.type"}))
        {
          recordEntityPolyType(*entityId, *polyType);
        }
      }
    }
  }
  while (value);
}

void SKCIFParser::skipComment()
{
  RKString tempString;
  _scanner.scanUpToCharacters(CharacterSet::newlineCharacterSet(), tempString);
}

void SKCIFParser::skipWhitespaceAndComments()
{
  const std::string &source = _scanner.string().utf8();
  auto location = _scanner.scanLocation();
  while (location < source.cend())
  {
    while (location < source.cend() &&
           (std::isspace(static_cast<unsigned char>(*location)) != 0))
    {
      ++location;
    }
    if (location < source.cend() && *location == '#')
    {
      _scanner.setScanLocation(location);
      skipComment();
      location = _scanner.scanLocation();
      continue;
    }
    break;
  }
  _scanner.setScanLocation(location);
}

int64_t SKCIFParser::scanInt()
{
  auto value = parseValue();
  if (!value) { return 0; }
  bool success = false;
  return value->split('(')[0].toInt(&success);
}

double SKCIFParser::scanDouble()
{
  auto value = parseValue();
  if (!value) { return 0.0; }
  bool success = false;
  return value->split('(')[0].toDouble(&success);
}

std::optional<RKString> SKCIFParser::scanString()
{
  return parseValue();
}
