/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    DNA backbone trace follows PyMOL cartoon_nucleic_acid_mode 4 (O5' / phosphate / O3' centerline, C2'/C3' orientation).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <vector>
#include <memory>
#include <optional>
#include <skasymmetricatom.h>
#include "proteinbackbone.h"

enum class DNABackboneAtomRole
{
  phosphate,
  o5Prime,
  c5Prime,
  c4Prime,
  o4Prime,
  c1Prime,
  c2Prime,
  c3Prime,
  o3Prime
};

std::optional<DNABackboneAtomRole> dnaBackboneAtomRole(const std::shared_ptr<SKAsymmetricAtom> &atom);

struct DNABackboneResidue
{
  RKString residueName;
  int64_t residueSequenceNumber = 0;
  char codeForInsertionOfResidues = ' ';
  std::shared_ptr<SKAsymmetricAtom> phosphate;
  std::shared_ptr<SKAsymmetricAtom> o5Prime;
  std::shared_ptr<SKAsymmetricAtom> c5Prime;
  std::shared_ptr<SKAsymmetricAtom> c4Prime;
  std::shared_ptr<SKAsymmetricAtom> o4Prime;
  std::shared_ptr<SKAsymmetricAtom> c1Prime;
  std::shared_ptr<SKAsymmetricAtom> c2Prime;
  std::shared_ptr<SKAsymmetricAtom> c3Prime;
  std::shared_ptr<SKAsymmetricAtom> o3Prime;

  DNABackboneResidue() = default;
  DNABackboneResidue(RKString residueName,
                     int64_t residueSequenceNumber,
                     char codeForInsertionOfResidues,
                     std::shared_ptr<SKAsymmetricAtom> phosphate,
                     std::shared_ptr<SKAsymmetricAtom> o5Prime,
                     std::shared_ptr<SKAsymmetricAtom> c5Prime,
                     std::shared_ptr<SKAsymmetricAtom> c4Prime,
                     std::shared_ptr<SKAsymmetricAtom> o4Prime,
                     std::shared_ptr<SKAsymmetricAtom> c1Prime,
                     std::shared_ptr<SKAsymmetricAtom> c2Prime,
                     std::shared_ptr<SKAsymmetricAtom> c3Prime,
                     std::shared_ptr<SKAsymmetricAtom> o3Prime);

  std::shared_ptr<SKAsymmetricAtom> ribbonCenterAtom() const;
  std::vector<std::shared_ptr<SKAsymmetricAtom>> backboneAtoms() const;
};

struct DNABackboneChain
{
  char chainIdentifier = ' ';
  std::vector<DNABackboneResidue> residues;

  DNABackboneChain() = default;
  DNABackboneChain(char chainIdentifier, std::vector<DNABackboneResidue> residues);
};

struct DNABackbone
{
  std::vector<DNABackboneChain> chains;

  DNABackbone() = default;
  explicit DNABackbone(std::vector<DNABackboneChain> chains);

  int nucleotideResidueCount() const;
  static DNABackbone build(const std::vector<std::shared_ptr<SKAsymmetricAtom>> &atoms);
  ProteinBackbone toProteinBackbone() const;
};
