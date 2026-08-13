/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include <cmath>
#include <cctype>
#include "rkstring.h"
#include "skelement.h"
#include "skmmcifwriter.h"

SKmmCIFWriter::SKmmCIFWriter(RKString displayName, SKSpaceGroup &spaceGroup, std::shared_ptr<SKCell> cell, double3 origin,
                             std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms,
                             bool atomsAreFractional, bool exportFractional, bool withProteinInfo):
   _displayName(displayName), _spaceGroup(spaceGroup), _cell(cell), _origin(origin), _atoms(atoms),
   _atomsAreFractional(atomsAreFractional), _exportFractional(exportFractional), _withProteinInfo(withProteinInfo)
{
}

RKString SKmmCIFWriter::cifDataBlockName(const RKString &displayName)
{
  RKString trimmed = displayName.trimmed();
  std::string mapped;
  mapped.reserve(static_cast<size_t>(trimmed.size()));
  for (int i = 0; i < trimmed.size(); ++i)
  {
    const unsigned char c = static_cast<unsigned char>(trimmed[i]);
    if (std::isalnum(c) || c == '_' || c == '-')
    {
      mapped.push_back(static_cast<char>(c));
    }
    else
    {
      mapped.push_back('_');
    }
  }
  return mapped.empty() ? RKString("structure") : RKString(mapped);
}

RKString SKmmCIFWriter::atomName(const std::shared_ptr<SKAsymmetricAtom> &atom, const RKString &chemicalElement)
{
  RKString display = atom->displayName().trimmed();
  if (!display.isEmpty())
  {
    if (display.utf8().find(' ') != std::string::npos)
    {
      return RKString("'") + display + "'";
    }
    return display;
  }

  RKString name = chemicalElement;
  if (atom->remotenessIndicator() != u' ' && atom->remotenessIndicator() != u'\0')
  {
    name += static_cast<char>(atom->remotenessIndicator());
  }
  if (atom->branchDesignator() != u' ' && atom->branchDesignator() != u'\0')
  {
    name += static_cast<char>(atom->branchDesignator());
  }
  return name;
}

RKString SKmmCIFWriter::string()
{
  const RKString safeName = cifDataBlockName(_displayName);
  RKString outputString = RKString("data_") + safeName + "\n\n";

  if (_cell)
  {
    outputString += RKString("_cell.length_a     %1\n").arg(_cell->a(), 12, 'f', 6, ' ');
    outputString += RKString("_cell.length_b     %1\n").arg(_cell->b(), 12, 'f', 6, ' ');
    outputString += RKString("_cell.length_c     %1\n").arg(_cell->c(), 12, 'f', 6, ' ');
    outputString += RKString("_cell.angle_alpha  %1\n").arg(_cell->alpha() * 180.0 / M_PI, 12, 'f', 6, ' ');
    outputString += RKString("_cell.angle_beta   %1\n").arg(_cell->beta() * 180.0 / M_PI, 12, 'f', 6, ' ');
    outputString += RKString("_cell.angle_gamma  %1\n").arg(_cell->gamma() * 180.0 / M_PI, 12, 'f', 6, ' ');
    if (_cell->zValue() > 0)
    {
      outputString += RKString("_cell.Z_PDB        %1\n").arg(_cell->zValue());
    }
    outputString += "\n";

    outputString += RKString("_symmetry.space_group_name_Hall '%1'\n").arg(_spaceGroup.spaceGroupSetting().HallString());
    outputString += RKString("_symmetry.pdbx_full_space_group_name_H-M '%1'\n").arg(_spaceGroup.spaceGroupSetting().HMString());
    outputString += RKString("_symmetry.Int_Tables_number %1\n\n").arg(RKString::number(_spaceGroup.spaceGroupSetting().number()));
  }

  outputString += RKString("loop_\n");
  outputString += RKString("_atom_site.group_PDB\n");
  outputString += RKString("_atom_site.id\n");
  outputString += RKString("_atom_site.type_symbol\n");
  if (_withProteinInfo)
  {
    outputString += RKString("_atom_site.label_atom_id\n");
    outputString += RKString("_atom_site.label_alt_id\n");
    outputString += RKString("_atom_site.label_comp_id\n");
    outputString += RKString("_atom_site.label_asym_id\n");
    outputString += RKString("_atom_site.label_entity_id\n");
    outputString += RKString("_atom_site.label_seq_id\n");
    outputString += RKString("_atom_site.pdbx_PDB_ins_code\n");
  }
  else
  {
    outputString += RKString("_atom_site.label_atom_id\n");
  }
  outputString += _exportFractional ? RKString("_atom_site.fract_x\n") : RKString("_atom_site.Cartn_x\n");
  outputString += _exportFractional ? RKString("_atom_site.fract_y\n") : RKString("_atom_site.Cartn_y\n");
  outputString += _exportFractional ? RKString("_atom_site.fract_z\n") : RKString("_atom_site.Cartn_z\n");
  outputString += RKString("_atom_site.occupancy\n");
  if (_withProteinInfo)
  {
    outputString += RKString("_atom_site.auth_seq_id\n");
    outputString += RKString("_atom_site.auth_comp_id\n");
    outputString += RKString("_atom_site.auth_asym_id\n");
    outputString += RKString("_atom_site.auth_atom_id\n");
  }
  outputString += RKString("_atom_site.charge\n");

  const double3x3 unitCell = _cell ? _cell->unitCell() : double3x3();
  const double3x3 inverseUnitCell = _cell ? _cell->inverseUnitCell() : double3x3();
  int serial = 1;
  for (const std::shared_ptr<SKAsymmetricAtom> &atom : _atoms)
  {
    double3 position;
    if (_atomsAreFractional && !_exportFractional)
    {
      position = unitCell * atom->position() - _origin;
    }
    else if (!_atomsAreFractional && _exportFractional)
    {
      position = inverseUnitCell * (atom->position() - _origin);
    }
    else
    {
      position = atom->position() - _origin;
    }

    const int atomicNumber = atom->elementIdentifier();
    if (atomicNumber < 0 || static_cast<size_t>(atomicNumber) >= PredefinedElements::predefinedElements.size())
    {
      serial += 1;
      continue;
    }
    const SKElement &element = PredefinedElements::predefinedElements[static_cast<size_t>(atomicNumber)];
    const RKString chemicalElement = element._chemicalSymbol;
    const RKString groupPDB = (_withProteinInfo && atom->solvent()) ? RKString("HETATM") : RKString("ATOM");
    const int atomId = atom->serialNumber() > 0 ? static_cast<int>(atom->serialNumber()) : serial;
    const RKString name = atomName(atom, chemicalElement);

    const RKString positionX = RKString("%1").arg(position.x, 12, 'f', 6, ' ');
    const RKString positionY = RKString("%1").arg(position.y, 12, 'f', 6, ' ');
    const RKString positionZ = RKString("%1").arg(position.z, 12, 'f', 6, ' ');
    const RKString occupancy = RKString("%1").arg(atom->occupancy(), 0, 'f', 2);
    const RKString charge = RKString("%1").arg(atom->charge(), 12, 'f', 6, ' ');

    if (_withProteinInfo)
    {
      RKString residueName = atom->residueName().trimmed();
      if (residueName.isEmpty()) { residueName = "UNK"; }
      const char16_t chainId = atom->chainIdentifier();
      const RKString chain = (chainId == u' ' || chainId == u'\0') ? RKString("A") : RKString(static_cast<char>(chainId));
      const RKString sequenceId = atom->residueSequenceNumber() == 0 ? RKString("?") : RKString::number(static_cast<int>(atom->residueSequenceNumber()));
      const char16_t insertion = atom->codeForInsertionOfResidues();
      const RKString insertionCode = (insertion == u' ' || insertion == u'\0') ? RKString("?") : RKString(static_cast<char>(insertion));
      const char16_t alt = atom->alternateLocationIndicator();
      const RKString altId = (alt == u' ' || alt == u'\0') ? RKString(".") : RKString(static_cast<char>(alt));

      outputString += groupPDB + " " + RKString::number(atomId) + " " + chemicalElement + " " + name + " " +
                      altId + " " + residueName + " " + chain + " ? " + sequenceId + " " + insertionCode + " " +
                      positionX + " " + positionY + " " + positionZ + " " + occupancy + " " +
                      sequenceId + " " + residueName + " " + chain + " " + name + " " + charge + "\n";
    }
    else
    {
      outputString += groupPDB + " " + RKString::number(atomId) + " " + chemicalElement + " " + name + " " +
                      positionX + " " + positionY + " " + positionZ + " " + occupancy + " " + charge + "\n";
    }
    serial += 1;
  }

  return outputString;
}
