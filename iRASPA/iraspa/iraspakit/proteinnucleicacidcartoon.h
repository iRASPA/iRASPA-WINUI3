/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    PyMOL nucleic-acid cartoon settings (cartoon_nucleic_acid_mode, cCartoon_* cross-sections).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"

/// PyMOL cCartoon_oval / tube / dumbbell / rect backbone cross-section for DNA/RNA.
enum class NucleicAcidBackboneStyle
{
  oval = 0,
  tube,
  dumbbell,
  rect
};

/// PyMOL cartoon_nucleic_acid_mode: phosphate trace (0/2/4) vs C3' trace (1).
enum class NucleicAcidTraceMode
{
  phosphateMode4 = 4,
  c3PrimeMode1 = 1
};

/// PyMOL cartoon_ring_mode 1: flat filled ribose/base ring planes.
enum class NucleicAcidRingMode
{
  off = 0,
  filledPlanes = 1
};

/// PyMOL cartoon_ladder_mode 1: glycosidic/backbone-to-base and Watson-Crick rungs.
enum class NucleicAcidLadderMode
{
  off = 0,
  rungs = 1
};

inline RKString nucleicAcidBackboneStyleDisplayName(NucleicAcidBackboneStyle style)
{
  switch (style)
  {
  case NucleicAcidBackboneStyle::oval: return RKStringLiteral("Oval");
  case NucleicAcidBackboneStyle::tube: return RKStringLiteral("Tube");
  case NucleicAcidBackboneStyle::dumbbell: return RKStringLiteral("Dumbbell");
  case NucleicAcidBackboneStyle::rect: return RKStringLiteral("Rect");
  }
  return RKString();
}

inline RKString nucleicAcidTraceModeDisplayName(NucleicAcidTraceMode mode)
{
  switch (mode)
  {
  case NucleicAcidTraceMode::phosphateMode4: return RKStringLiteral("Phosphate (P)");
  case NucleicAcidTraceMode::c3PrimeMode1: return RKStringLiteral("C3′");
  }
  return RKString();
}
