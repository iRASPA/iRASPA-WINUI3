/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonSecondaryStructureMethod.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include "sksecondarystructure.h"

enum class ProteinRibbonSecondaryStructureMethod
{
  stride,
  dss,
  dssp,
  psea,
  sequoia,
  segno
};

inline RKString proteinRibbonSecondaryStructureMethodDisplayName(ProteinRibbonSecondaryStructureMethod method)
{
  switch (method)
  {
  case ProteinRibbonSecondaryStructureMethod::stride: return RKString("STRIDE");
  case ProteinRibbonSecondaryStructureMethod::dss: return RKString("DSS");
  case ProteinRibbonSecondaryStructureMethod::dssp: return RKString("DSSP");
  case ProteinRibbonSecondaryStructureMethod::psea: return RKString("P-SEA");
  case ProteinRibbonSecondaryStructureMethod::sequoia: return RKString("Sequoia");
  case ProteinRibbonSecondaryStructureMethod::segno: return RKString("SEGNO");
  }
  return RKString();
}

// The archive stores the display name, because that is the raw value the Swift enum was written with.
// An unknown name falls back the way the Swift decoder does rather than failing the read.
inline ProteinRibbonSecondaryStructureMethod proteinRibbonSecondaryStructureMethodFromName(const RKString &name)
{
  for (const ProteinRibbonSecondaryStructureMethod method : {ProteinRibbonSecondaryStructureMethod::stride,
                                                            ProteinRibbonSecondaryStructureMethod::dss,
                                                            ProteinRibbonSecondaryStructureMethod::dssp,
                                                            ProteinRibbonSecondaryStructureMethod::psea,
                                                            ProteinRibbonSecondaryStructureMethod::sequoia,
                                                            ProteinRibbonSecondaryStructureMethod::segno})
  {
    if (proteinRibbonSecondaryStructureMethodDisplayName(method) == name) { return method; }
  }
  return ProteinRibbonSecondaryStructureMethod::stride;
}

inline SKSecondaryStructureAssignmentMethod proteinRibbonSecondaryStructureAssignmentMethod(ProteinRibbonSecondaryStructureMethod method)
{
  switch (method)
  {
  case ProteinRibbonSecondaryStructureMethod::stride: return SKSecondaryStructureAssignmentMethod::stride;
  case ProteinRibbonSecondaryStructureMethod::dss: return SKSecondaryStructureAssignmentMethod::dss;
  case ProteinRibbonSecondaryStructureMethod::dssp: return SKSecondaryStructureAssignmentMethod::dssp;
  case ProteinRibbonSecondaryStructureMethod::psea: return SKSecondaryStructureAssignmentMethod::psea;
  case ProteinRibbonSecondaryStructureMethod::sequoia: return SKSecondaryStructureAssignmentMethod::sequoia;
  case ProteinRibbonSecondaryStructureMethod::segno: return SKSecondaryStructureAssignmentMethod::segno;
  }
  return SKSecondaryStructureAssignmentMethod::stride;
}
