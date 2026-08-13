/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"

namespace SKNucleotide
{
  bool isNucleotideResidueName(const RKString &residueName);
  RKString normalizedAtomName(const RKString &atomName);
}
