/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinAtomTreeBuilder.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <vector>

#include "proteinribbonsecondarystructuremethod.h"
#include "skasymmetricatom.h"
#include "skatomtreecontroller.h"
#include "skatomtreenode.h"

// A loaded protein arrives as a flat list of atoms. Cocoa presents it as a hierarchy instead:
// chain, then secondary-structure segment, then residue, then the atoms of that residue. PDB
// HETATM records (waters, ions, ligands — not polymer MODRES such as SET) collect under a
// "HETATM" group per chain. The chain level is left out when the protein has only one chain,
// and atoms without a residue identity collect under a single "Other" group.
struct ProteinAtomTreeBuilder
{
  static bool applyHierarchyIfNeeded(SKAtomTreeController &controller,
                                     ProteinRibbonSecondaryStructureMethod secondaryStructureMethod =
                                       ProteinRibbonSecondaryStructureMethod::stride);

  static std::vector<std::shared_ptr<SKAtomTreeNode>> build(const std::vector<std::shared_ptr<SKAsymmetricAtom>> &atoms,
                                                            ProteinRibbonSecondaryStructureMethod secondaryStructureMethod =
                                                              ProteinRibbonSecondaryStructureMethod::stride);
};
