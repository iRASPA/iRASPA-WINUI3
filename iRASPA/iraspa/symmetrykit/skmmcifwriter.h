/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <vector>
#include <tuple>
#include "skasymmetricatom.h"
#include "skspacegroup.h"
#include "skcell.h"

class SKmmCIFWriter
{
public:
  SKmmCIFWriter(RKString displayName, SKSpaceGroup &spacegroup, std::shared_ptr<SKCell> cell, double3 origin,
                std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms,
                bool atomsAreFractional = false, bool exportFractional = false, bool withProteinInfo = false);
  RKString string();
private:
  static RKString cifDataBlockName(const RKString &displayName);
  static RKString atomName(const std::shared_ptr<SKAsymmetricAtom> &atom, const RKString &chemicalElement);

  RKString _displayName;
  SKSpaceGroup &_spaceGroup;
  std::shared_ptr<SKCell> _cell;
  double3 _origin;
  std::vector<std::shared_ptr<SKAsymmetricAtom>> _atoms;
  bool _atomsAreFractional;
  bool _exportFractional;
  bool _withProteinInfo;
};
