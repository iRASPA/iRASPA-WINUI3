/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonStructureEditor.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#include "ribbonstructureeditor.h"
#include <cmath>

namespace
{
  bool ribbonFloatEqual(double left, double right)
  {
    return std::abs(left - right) < 1e-3;
  }

  bool ribbonColorIsWhite(const RKColor &color)
  {
    return ribbonFloatEqual(color.redF(), 1.0)
        && ribbonFloatEqual(color.greenF(), 1.0)
        && ribbonFloatEqual(color.blueF(), 1.0)
        && ribbonFloatEqual(color.alphaF(), 1.0);
  }
}

ProteinRibbonMeshParameters ribbonMeshParameters(const ProteinRibbonStructureEditor &editor)
{
  return ProteinRibbonMeshParameters(editor.ribbonSplineType(),
                                     editor.ribbonSubdivisionsPerSegment(),
                                     editor.ribbonCrossSectionRingResolution(),
                                     editor.ribbonCoilRadiusScale(),
                                     editor.ribbonWidthClamp(),
                                     editor.ribbonSheetArrowLengthExtent(),
                                     editor.ribbonSheetArrowWingPosition(),
                                     editor.ribbonSheetArrowPeakWidthFactor(),
                                     editor.ribbonNormalSmoothingRadius());
}

void setRibbonMeshParameters(ProteinRibbonStructureEditor &editor, const ProteinRibbonMeshParameters &parameters)
{
  editor.setRibbonSplineType(parameters.splineType);
  editor.setRibbonSubdivisionsPerSegment(parameters.subdivisionsPerSegment);
  editor.setRibbonCrossSectionRingResolution(parameters.crossSectionRingResolution);
  editor.setRibbonCoilRadiusScale(parameters.coilRadiusScale);
  editor.setRibbonWidthClamp(parameters.ribbonWidthClamp);
  editor.setRibbonSheetArrowLengthExtent(parameters.sheetArrowLengthExtent);
  editor.setRibbonSheetArrowWingPosition(parameters.sheetArrowWingPosition);
  editor.setRibbonSheetArrowPeakWidthFactor(parameters.sheetArrowPeakWidthFactor);
  editor.setRibbonNormalSmoothingRadius(parameters.normalSmoothingRadius);
}

void migrateLegacySheetArrowDefaultsIfNeeded(ProteinRibbonStructureEditor &editor)
{
  const double length = editor.ribbonSheetArrowLengthExtent();
  const double wing = editor.ribbonSheetArrowWingPosition();
  const double peak = editor.ribbonSheetArrowPeakWidthFactor();
  // Prior development defaults that produced paddles, invisible tips, or diamond/kite heads.
  const bool legacyPaddle = std::abs(length - 2.5) < 1.0e-9 && std::abs(wing - 1.0) < 1.0e-9 && std::abs(peak - 4.0) < 1.0e-9;
  const bool tooSubtle = std::abs(length - 1.5) < 1.0e-9 && std::abs(wing - 0.5) < 1.0e-9 && std::abs(peak - 1.5) < 1.0e-9;
  const bool longDiamond = std::abs(length - 2.0) < 1.0e-9 && std::abs(wing - 1.0) < 1.0e-9 && std::abs(peak - 2.5) < 1.0e-9;
  if (!(legacyPaddle || tooSubtle || longDiamond)) return;
  editor.setRibbonSheetArrowLengthExtent(1.5);
  editor.setRibbonSheetArrowWingPosition(1.0);
  editor.setRibbonSheetArrowPeakWidthFactor(2.5);
}

void applyDefaultRibbonAppearance(ProteinRibbonStructureEditor &editor)
{
  editor.setRibbonHDR(true);
  editor.setRibbonHDRExposure(1.5);
  editor.setRibbonHue(1.0);
  editor.setRibbonSaturation(1.0);
  editor.setRibbonValue(1.0);
  editor.setRibbonAmbientOcclusion(false);
  editor.setRibbonAmbientColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonDiffuseColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonSpecularColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonAmbientIntensity(0.2);
  editor.setRibbonDiffuseIntensity(1.0);
  editor.setRibbonSpecularIntensity(1.0);
  editor.setRibbonShininess(6.0);
  editor.setRibbonEdgeCueing(RKEdgeCueing::off);
}

void applyFancyRibbonAppearanceDefault(ProteinRibbonStructureEditor &editor)
{
  // Cocoa ProteinRibbonStructureEditor.applyFancyRibbonAppearance
  editor.setRibbonHDR(true);
  editor.setRibbonHDRExposure(2.5);
  editor.setRibbonHue(1.0);
  editor.setRibbonSaturation(1.0);
  editor.setRibbonValue(1.0);
  editor.setRibbonAmbientOcclusion(true);
  editor.setRibbonAmbientColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonDiffuseColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonSpecularColor(RKColor::fromRgb(255, 255, 255, 255));
  editor.setRibbonAmbientIntensity(0.2);
  editor.setRibbonDiffuseIntensity(1.0);
  editor.setRibbonSpecularIntensity(1.0);
  editor.setRibbonShininess(4.0);
  editor.setRibbonEdgeCueing(RKEdgeCueing::off);
}

void applyIllustrativeRibbonAppearance(ProteinRibbonStructureEditor &editor)
{
  applyFancyRibbonAppearanceDefault(editor);
  editor.setRibbonEdgeCueing(RKEdgeCueing::contoursAndHalos);
}

void applyRibbonRepresentationStyle(ProteinRibbonStructureEditor &editor, ProteinRibbonRepresentationStyle style)
{
  editor.setRibbonRepresentationStyle(style);
  switch (style)
  {
  case ProteinRibbonRepresentationStyle::defaultStyle:
    applyDefaultRibbonAppearance(editor);
    break;
  case ProteinRibbonRepresentationStyle::fancy:
    applyFancyRibbonAppearanceDefault(editor);
    break;
  case ProteinRibbonRepresentationStyle::illustrative:
    applyIllustrativeRibbonAppearance(editor);
    break;
  case ProteinRibbonRepresentationStyle::custom:
    break;
  }
}

bool matchesDefaultRibbonAppearance(const ProteinRibbonStructureEditor &editor)
{
  return editor.ribbonHDR()
      && ribbonFloatEqual(editor.ribbonHDRExposure(), 1.5)
      && ribbonFloatEqual(editor.ribbonHue(), 1.0)
      && ribbonFloatEqual(editor.ribbonSaturation(), 1.0)
      && ribbonFloatEqual(editor.ribbonValue(), 1.0)
      && !editor.ribbonAmbientOcclusion()
      && ribbonColorIsWhite(editor.ribbonAmbientColor())
      && ribbonColorIsWhite(editor.ribbonDiffuseColor())
      && ribbonColorIsWhite(editor.ribbonSpecularColor())
      && ribbonFloatEqual(editor.ribbonAmbientIntensity(), 0.2)
      && ribbonFloatEqual(editor.ribbonDiffuseIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonSpecularIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonShininess(), 6.0);
}

bool matchesFancyRibbonAppearance(const ProteinRibbonStructureEditor &editor)
{
  return editor.ribbonHDR()
      && ribbonFloatEqual(editor.ribbonHDRExposure(), 2.5)
      && ribbonFloatEqual(editor.ribbonHue(), 1.0)
      && ribbonFloatEqual(editor.ribbonSaturation(), 1.0)
      && ribbonFloatEqual(editor.ribbonValue(), 1.0)
      && editor.ribbonAmbientOcclusion()
      && ribbonColorIsWhite(editor.ribbonAmbientColor())
      && ribbonColorIsWhite(editor.ribbonDiffuseColor())
      && ribbonColorIsWhite(editor.ribbonSpecularColor())
      && ribbonFloatEqual(editor.ribbonAmbientIntensity(), 0.2)
      && ribbonFloatEqual(editor.ribbonDiffuseIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonSpecularIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonShininess(), 4.0);
}

// Gallery / older archives used atom-like ambient 0.1 and shininess 4.0 with AO off —
// otherwise the same as Cocoa Default. That no longer matches Default (0.2 / 6.0) or Fancy.
bool matchesLegacyDefaultRibbonAppearance(const ProteinRibbonStructureEditor &editor)
{
  return editor.ribbonHDR()
      && ribbonFloatEqual(editor.ribbonHDRExposure(), 1.5)
      && ribbonFloatEqual(editor.ribbonHue(), 1.0)
      && ribbonFloatEqual(editor.ribbonSaturation(), 1.0)
      && ribbonFloatEqual(editor.ribbonValue(), 1.0)
      && !editor.ribbonAmbientOcclusion()
      && ribbonColorIsWhite(editor.ribbonAmbientColor())
      && ribbonColorIsWhite(editor.ribbonDiffuseColor())
      && ribbonColorIsWhite(editor.ribbonSpecularColor())
      && ribbonFloatEqual(editor.ribbonAmbientIntensity(), 0.1)
      && ribbonFloatEqual(editor.ribbonDiffuseIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonSpecularIntensity(), 1.0)
      && ribbonFloatEqual(editor.ribbonShininess(), 4.0);
}

void recheckRibbonRepresentationStyle(ProteinRibbonStructureEditor &editor)
{
  // Default and Fancy are drawn without cues, so any cueing at all leaves them behind: the cued
  // Fancy material is Illustrative, and one cue on its own is neither.
  const bool noCues = editor.ribbonEdgeCueing() == RKEdgeCueing::off;

  if (noCues && matchesDefaultRibbonAppearance(editor))
  {
    editor.setRibbonRepresentationStyle(ProteinRibbonRepresentationStyle::defaultStyle);
  }
  else if (noCues && matchesLegacyDefaultRibbonAppearance(editor))
  {
    // Upgrade gallery / older Default lighting (0.1 / 4.0) to the Cocoa Default preset.
    applyRibbonRepresentationStyle(editor, ProteinRibbonRepresentationStyle::defaultStyle);
  }
  else if (matchesFancyRibbonAppearance(editor))
  {
    editor.setRibbonRepresentationStyle(
        noCues ? ProteinRibbonRepresentationStyle::fancy
               : (editor.ribbonEdgeCueing() == RKEdgeCueing::contoursAndHalos)
                     ? ProteinRibbonRepresentationStyle::illustrative
                     : ProteinRibbonRepresentationStyle::custom);
  }
  else
  {
    editor.setRibbonRepresentationStyle(ProteinRibbonRepresentationStyle::custom);
  }
}

void finalizeRibbonRepresentationStyleAfterLoad(ProteinRibbonStructureEditor &editor, int64_t archiveVersion)
{
  // Version 6+ stores the representation-style name. Trust Default/Fancy/Illustrative and re-apply
  // their lighting so a saved "Default" cannot be reclassified as Fancy when lighting drifted
  // (Appearance Reload used to show the stored label without rechecking first).
  if (archiveVersion >= 6)
  {
    const ProteinRibbonRepresentationStyle style = editor.ribbonRepresentationStyle();
    if (style == ProteinRibbonRepresentationStyle::defaultStyle
        || style == ProteinRibbonRepresentationStyle::fancy
        || style == ProteinRibbonRepresentationStyle::illustrative)
    {
      applyRibbonRepresentationStyle(editor, style);
      return;
    }
  }
  else if (archiveVersion >= 4)
  {
    // Pre-style archives only distinguished presets by AO; normalize to the current presets.
    applyRibbonRepresentationStyle(editor,
        editor.ribbonAmbientOcclusion() ? ProteinRibbonRepresentationStyle::fancy
                                        : ProteinRibbonRepresentationStyle::defaultStyle);
    return;
  }

  // Gallery proteins often still carry legacy Default lighting (ambient 0.1, shininess 4).
  if (matchesLegacyDefaultRibbonAppearance(editor))
  {
    applyRibbonRepresentationStyle(editor, ProteinRibbonRepresentationStyle::defaultStyle);
    return;
  }

  recheckRibbonRepresentationStyle(editor);
}
