/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonColorSet.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <mathkit.h>
#include "proteinribbonsecondarystructure.h"

enum class ProteinRibbonColorSet
{
  standardAcademic,
  modernUI,
  biophysicalProperties,
  infographic,
  rasMol,
  pymol,
  vmd,
  jmolShapely,
  chimera
};

inline constexpr ProteinRibbonColorSet kAllProteinRibbonColorSets[] = {
  ProteinRibbonColorSet::standardAcademic,
  ProteinRibbonColorSet::modernUI,
  ProteinRibbonColorSet::biophysicalProperties,
  ProteinRibbonColorSet::infographic,
  ProteinRibbonColorSet::rasMol,
  ProteinRibbonColorSet::pymol,
  ProteinRibbonColorSet::vmd,
  ProteinRibbonColorSet::jmolShapely,
  ProteinRibbonColorSet::chimera
};

inline RKString proteinRibbonColorSetDisplayName(ProteinRibbonColorSet colorSet)
{
  switch (colorSet)
  {
  case ProteinRibbonColorSet::standardAcademic: return RKString("Standard Academic");
  case ProteinRibbonColorSet::modernUI: return RKString("Modern UI");
  case ProteinRibbonColorSet::biophysicalProperties: return RKString("Biophysical Properties");
  case ProteinRibbonColorSet::infographic: return RKString("Infographic");
  case ProteinRibbonColorSet::rasMol: return RKString("RasMol");
  case ProteinRibbonColorSet::pymol: return RKString("PyMOL");
  case ProteinRibbonColorSet::vmd: return RKString("VMD");
  case ProteinRibbonColorSet::jmolShapely: return RKString("Jmol Shapely");
  case ProteinRibbonColorSet::chimera: return RKString("Chimera");
  }
  return RKString();
}

inline ProteinRibbonColorSet proteinRibbonColorSetFromName(const RKString &name)
{
  for (const ProteinRibbonColorSet colorSet : kAllProteinRibbonColorSets)
  {
    if (proteinRibbonColorSetDisplayName(colorSet).toLower() == name.toLower()) { return colorSet; }
  }
  return ProteinRibbonColorSet::standardAcademic;
}

inline float3 proteinRibbonColorSetCoilColor(ProteinRibbonColorSet colorSet)
{
  switch (colorSet)
  {
  case ProteinRibbonColorSet::standardAcademic: return float3(0.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::modernUI: return float3(0.25f, 0.27f, 0.30f);
  case ProteinRibbonColorSet::biophysicalProperties: return float3(1.0f, 0.2f, 0.6f);
  case ProteinRibbonColorSet::infographic: return float3(0.85f, 0.75f, 0.60f);
  case ProteinRibbonColorSet::rasMol: return float3(1.0f, 1.0f, 1.0f);
  case ProteinRibbonColorSet::pymol: return float3(0.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::vmd: return float3(1.0f, 1.0f, 1.0f);
  case ProteinRibbonColorSet::jmolShapely: return float3(1.0f, 1.0f, 1.0f);
  case ProteinRibbonColorSet::chimera: return float3(190.0f/255.0f, 190.0f/255.0f, 190.0f/255.0f);
  }
  return float3();
}

inline float3 proteinRibbonColorSetHelixColor(ProteinRibbonColorSet colorSet)
{
  switch (colorSet)
  {
  case ProteinRibbonColorSet::standardAcademic: return float3(1.0f, 0.0f, 1.0f);
  case ProteinRibbonColorSet::modernUI: return float3(0.0f, 0.55f, 0.65f);
  case ProteinRibbonColorSet::biophysicalProperties: return float3(0.05f, 0.25f, 0.65f);
  case ProteinRibbonColorSet::infographic: return float3(0.75f, 0.65f, 0.90f);
  case ProteinRibbonColorSet::rasMol: return float3(240.0f/255.0f, 0.0f, 128.0f/255.0f);
  case ProteinRibbonColorSet::pymol: return float3(1.0f, 0.0f, 0.0f);
  case ProteinRibbonColorSet::vmd: return float3(0.65f, 0.0f, 0.65f);
  case ProteinRibbonColorSet::jmolShapely: return float3(1.0f, 0.0f, 128.0f/255.0f);
  case ProteinRibbonColorSet::chimera: return float3(1.0f, 69.0f/255.0f, 0.0f);
  }
  return float3();
}

inline float3 proteinRibbonColorSetSheetColor(ProteinRibbonColorSet colorSet)
{
  switch (colorSet)
  {
  case ProteinRibbonColorSet::standardAcademic: return float3(1.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::modernUI: return float3(0.95f, 0.60f, 0.15f);
  case ProteinRibbonColorSet::biophysicalProperties: return float3(0.40f, 0.75f, 1.0f);
  case ProteinRibbonColorSet::infographic: return float3(0.60f, 0.90f, 0.75f);
  case ProteinRibbonColorSet::rasMol: return float3(1.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::pymol: return float3(1.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::vmd: return float3(1.0f, 1.0f, 0.0f);
  case ProteinRibbonColorSet::jmolShapely: return float3(1.0f, 200.0f/255.0f, 0.0f);
  case ProteinRibbonColorSet::chimera: return float3(160.0f/255.0f, 32.0f/255.0f, 240.0f/255.0f);
  }
  return float3();
}

inline float3 proteinRibbonColorSetColor(ProteinRibbonColorSet colorSet, ProteinRibbonSecondaryStructure structure)
{
  switch (structure)
  {
  case ProteinRibbonSecondaryStructure::coil: return proteinRibbonColorSetCoilColor(colorSet);
  case ProteinRibbonSecondaryStructure::helix: return proteinRibbonColorSetHelixColor(colorSet);
  case ProteinRibbonSecondaryStructure::sheet: return proteinRibbonColorSetSheetColor(colorSet);
  }
  return float3();
}
