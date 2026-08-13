/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonSplineType.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"

enum class ProteinRibbonSplineType
{
  bSpline,
  catmullRom
};

inline RKString proteinRibbonSplineTypeDisplayName(ProteinRibbonSplineType type)
{
  switch (type)
  {
  case ProteinRibbonSplineType::bSpline: return RKString("B-Spline");
  case ProteinRibbonSplineType::catmullRom: return RKString("Catmull-Rom");
  }
  return RKString();
}

inline RKString proteinRibbonSplineTypeRawValue(ProteinRibbonSplineType type)
{
  return proteinRibbonSplineTypeDisplayName(type);
}

inline ProteinRibbonSplineType proteinRibbonSplineTypeFromName(const RKString &name)
{
  if (proteinRibbonSplineTypeDisplayName(ProteinRibbonSplineType::catmullRom) == name) { return ProteinRibbonSplineType::catmullRom; }
  return ProteinRibbonSplineType::bSpline;
}
