/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinBackbone.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <vector>
#include <memory>
#include <optional>
#include <skasymmetricatom.h>
#include "skelement.h"

std::optional<SKBackboneAtomRole> backboneAtomRoleForType(const RKString &type);
std::optional<SKBackboneAtomRole> backboneAtomRole(const std::shared_ptr<SKAsymmetricAtom> &atom);
bool isBackboneAtomType(const RKString &type);

struct ProteinBackboneResidue
{
  RKString residueName;
  int64_t residueSequenceNumber = 0;
  char codeForInsertionOfResidues = ' ';
  std::shared_ptr<SKAsymmetricAtom> nitrogen;
  std::shared_ptr<SKAsymmetricAtom> alphaCarbon;
  std::shared_ptr<SKAsymmetricAtom> carbonylCarbon;
  std::shared_ptr<SKAsymmetricAtom> carbonylOxygen;

  ProteinBackboneResidue() = default;
  ProteinBackboneResidue(RKString residueName,
                         int64_t residueSequenceNumber,
                         char codeForInsertionOfResidues,
                         std::shared_ptr<SKAsymmetricAtom> nitrogen,
                         std::shared_ptr<SKAsymmetricAtom> alphaCarbon,
                         std::shared_ptr<SKAsymmetricAtom> carbonylCarbon,
                         std::shared_ptr<SKAsymmetricAtom> carbonylOxygen);

  std::vector<std::shared_ptr<SKAsymmetricAtom>> backboneAtoms() const;
};

struct ProteinBackboneChain
{
  char chainIdentifier = ' ';
  std::vector<ProteinBackboneResidue> residues;

  ProteinBackboneChain() = default;
  ProteinBackboneChain(char chainIdentifier, std::vector<ProteinBackboneResidue> residues);
};

struct ProteinBackbone
{
  std::vector<ProteinBackboneChain> chains;

  ProteinBackbone() = default;
  explicit ProteinBackbone(std::vector<ProteinBackboneChain> chains);

  int alphaCarbonResidueCount() const;
  static ProteinBackbone build(const std::vector<std::shared_ptr<SKAsymmetricAtom>> &atoms);
};
