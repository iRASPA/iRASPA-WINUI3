/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonRepresentationStyle.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <vector>

enum class ProteinRibbonRepresentationStyle
{
  defaultStyle,
  fancy,
  custom
};

inline RKString proteinRibbonRepresentationStyleDisplayName(ProteinRibbonRepresentationStyle style)
{
  switch (style)
  {
  case ProteinRibbonRepresentationStyle::defaultStyle: return RKString("Default");
  case ProteinRibbonRepresentationStyle::fancy: return RKString("Fancy");
  case ProteinRibbonRepresentationStyle::custom: return RKString("Custom");
  }
  return RKString();
}

inline ProteinRibbonRepresentationStyle proteinRibbonRepresentationStyleFromName(const RKString &name)
{
  for (const ProteinRibbonRepresentationStyle style : {ProteinRibbonRepresentationStyle::defaultStyle,
                                                      ProteinRibbonRepresentationStyle::fancy,
                                                      ProteinRibbonRepresentationStyle::custom})
  {
    if (proteinRibbonRepresentationStyleDisplayName(style) == name) { return style; }
  }
  return ProteinRibbonRepresentationStyle::defaultStyle;
}

inline std::vector<ProteinRibbonRepresentationStyle> proteinRibbonRepresentationSelectableCases()
{
  return {ProteinRibbonRepresentationStyle::defaultStyle, ProteinRibbonRepresentationStyle::fancy};
}
