/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
    D.Dubbeldam@uva.nl            https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html
    S.Calero@tue.nl               https://www.tue.nl/en/research/researchers/sofia-calero/
    t.j.h.vlugt@tudelft.nl        http://homepage.tudelft.nl/v9k6y

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************************************************************/

#pragma  once

#include <cmath>
#include <cstddef>
#include <optional>
#include <mathkit.h>
#include "rkrenderkitprotocols.h"
#include "zipreader.h"

/// Probe particles for adsorption / framework surface-area work.
/// Raw values match Cocoa Structure.ProbeMolecule for archive compatibility:
/// custom=9 (was the old multiple_values sentinel), Connolly appended as 10,
/// multiple_values moved to 11 for mixed selection in the UI only.
enum class ProbeMolecule: int64_t
{
  helium = 0,
  methane = 1,
  nitrogen = 2,
  hydrogen = 3,
  water = 4,
  co2 = 5,
  xenon = 6,
  krypton = 7,
  argon = 8,
  custom = 9,
  /// Vanishing probe (ε = 0, σ = 0).
  connolly = 10,
  multiple_values = 11
};

/// Named Lennard-Jones (ε, σ) for a probe, or nullopt for custom / multiple_values.
inline std::optional<double2> probeMoleculeNamedParameters(ProbeMolecule probe)
{
  switch (probe)
  {
    case ProbeMolecule::helium:   return double2(10.9, 2.64);
    case ProbeMolecule::nitrogen: return double2(36.0, 3.31);
    case ProbeMolecule::methane:  return double2(158.5, 3.72);
    case ProbeMolecule::hydrogen: return double2(36.7, 2.958);
    case ProbeMolecule::water:    return double2(89.633, 3.097);
    case ProbeMolecule::co2:      return double2(236.1, 3.72);
    case ProbeMolecule::xenon:    return double2(226.14, 3.949);
    case ProbeMolecule::krypton:  return double2(162.58, 3.6274);
    case ProbeMolecule::argon:    return double2(119.8, 3.34);
    case ProbeMolecule::connolly: return double2(0.0, 0.0);
    case ProbeMolecule::custom:
    case ProbeMolecule::multiple_values:
      return std::nullopt;
  }
  return std::nullopt;
}

/// Match stored ε/σ to a named probe (Cocoa ProbeMolecule.matching).
inline ProbeMolecule probeMoleculeMatching(double2 parameters, double tolerance = 1.0e-6)
{
  static constexpr ProbeMolecule named[] = {
      ProbeMolecule::helium, ProbeMolecule::methane, ProbeMolecule::nitrogen,
      ProbeMolecule::hydrogen, ProbeMolecule::water, ProbeMolecule::co2,
      ProbeMolecule::xenon, ProbeMolecule::krypton, ProbeMolecule::argon,
      ProbeMolecule::connolly};
  for (ProbeMolecule probe : named)
  {
    if (auto candidate = probeMoleculeNamedParameters(probe))
    {
      if (std::abs(candidate->x - parameters.x) <= tolerance &&
          std::abs(candidate->y - parameters.y) <= tolerance)
        return probe;
    }
  }
  return ProbeMolecule::custom;
}

/// Menu order for Cell / Appearance probe popups (Cocoa selectableCases).
inline constexpr ProbeMolecule kSelectableProbeMolecules[] = {
    ProbeMolecule::helium, ProbeMolecule::methane, ProbeMolecule::nitrogen,
    ProbeMolecule::hydrogen, ProbeMolecule::water, ProbeMolecule::co2,
    ProbeMolecule::xenon, ProbeMolecule::krypton, ProbeMolecule::argon,
    ProbeMolecule::connolly, ProbeMolecule::custom};

inline constexpr size_t kSelectableProbeMoleculeCount =
    sizeof(kSelectableProbeMolecules) / sizeof(kSelectableProbeMolecules[0]);

inline int selectableProbeMoleculeIndex(ProbeMolecule probe)
{
  for (size_t i = 0; i < kSelectableProbeMoleculeCount; ++i)
  {
    if (kSelectableProbeMolecules[i] == probe)
      return static_cast<int>(i);
  }
  return -1;
}

class VolumetricDataViewer
{
public:

  virtual  ~VolumetricDataViewer() = 0;

  virtual bool drawAdsorptionSurface() const  = 0;
  virtual void setDrawAdsorptionSurface(bool state)  = 0;
  virtual int encompassingPowerOfTwoCubicGridSize() const = 0;
  virtual bool isImmutable() const = 0;
  virtual std::pair<double,double> range() const = 0;
  virtual int3 dimensions() const = 0;
  virtual double3 spacing() const = 0;
  virtual RKByteArray data() const = 0;
  virtual double average() const = 0;
  virtual double variance() const = 0;

  virtual double adsorptionSurfaceOpacity() const  = 0;
  virtual void setAdsorptionSurfaceOpacity(double value) = 0;
  virtual double adsorptionTransparencyThreshold() const = 0;
  virtual void setAdsorptionTransparencyThreshold(double value) = 0;
  virtual double adsorptionSurfaceIsoValue() const = 0;
  virtual void setAdsorptionSurfaceIsoValue(double value) = 0;
  virtual ProbeMolecule adsorptionSurfaceProbeMolecule() const = 0;
  virtual void setAdsorptionSurfaceProbeMolecule(ProbeMolecule value) = 0;
  virtual double adsorptionSurfaceProbeEpsilon() const = 0;
  virtual void setAdsorptionSurfaceProbeEpsilon(double value) = 0;
  virtual double adsorptionSurfaceProbeSigma() const = 0;
  virtual void setAdsorptionSurfaceProbeSigma(double value) = 0;
  /// Sets the probe and copies named ε/σ when the molecule has them (Cocoa applyAdsorptionSurfaceProbeMolecule).
  virtual void applyAdsorptionSurfaceProbeMolecule(ProbeMolecule value) = 0;

  virtual RKEnergySurfaceType adsorptionSurfaceRenderingMethod() const = 0;
  virtual void setAdsorptionSurfaceRenderingMethod(RKEnergySurfaceType type) = 0;
  virtual RKPredefinedVolumeRenderingTransferFunction adsorptionVolumeTransferFunction() const = 0;
  virtual void setAdsorptionVolumeTransferFunction(RKPredefinedVolumeRenderingTransferFunction function) = 0;
  virtual double adsorptionVolumeStepLength() const = 0;
  virtual void setAdsorptionVolumeStepLength(double value) = 0;

  virtual double adsorptionSurfaceHue() const = 0;
  virtual void setAdsorptionSurfaceHue(double value) = 0;
  virtual double adsorptionSurfaceSaturation() const = 0;
  virtual void setAdsorptionSurfaceSaturation(double value) = 0;
  virtual double adsorptionSurfaceValue() const = 0;
  virtual void setAdsorptionSurfaceValue(double value) = 0;

  virtual bool adsorptionSurfaceFrontSideHDR() const = 0;
  virtual void setAdsorptionSurfaceFrontSideHDR(bool state) = 0;
  virtual double adsorptionSurfaceFrontSideHDRExposure() const = 0;
  virtual void setAdsorptionSurfaceFrontSideHDRExposure(double value) = 0;
  virtual RKColor adsorptionSurfaceFrontSideAmbientColor() const = 0;
  virtual void setAdsorptionSurfaceFrontSideAmbientColor(RKColor color) = 0;
  virtual RKColor adsorptionSurfaceFrontSideDiffuseColor() const = 0;
  virtual void setAdsorptionSurfaceFrontSideDiffuseColor(RKColor color) = 0;
  virtual RKColor adsorptionSurfaceFrontSideSpecularColor() const = 0;
  virtual void setAdsorptionSurfaceFrontSideSpecularColor(RKColor color) = 0;
  virtual double adsorptionSurfaceFrontSideDiffuseIntensity() const = 0;
  virtual void setAdsorptionSurfaceFrontSideDiffuseIntensity(double value) = 0;
  virtual double adsorptionSurfaceFrontSideAmbientIntensity() const = 0;
  virtual void setAdsorptionSurfaceFrontSideAmbientIntensity(double value) = 0;
  virtual double adsorptionSurfaceFrontSideSpecularIntensity() const = 0;
  virtual void setAdsorptionSurfaceFrontSideSpecularIntensity(double value) = 0;
  virtual double adsorptionSurfaceFrontSideShininess() const = 0;
  virtual void setAdsorptionSurfaceFrontSideShininess(double value) = 0;

  virtual bool adsorptionSurfaceBackSideHDR() const = 0;
  virtual void setAdsorptionSurfaceBackSideHDR(bool state) = 0;
  virtual double adsorptionSurfaceBackSideHDRExposure() const = 0;
  virtual void setAdsorptionSurfaceBackSideHDRExposure(double value) = 0;
  virtual RKColor adsorptionSurfaceBackSideAmbientColor() const = 0;
  virtual void setAdsorptionSurfaceBackSideAmbientColor(RKColor color) = 0;
  virtual RKColor adsorptionSurfaceBackSideDiffuseColor() const = 0;
  virtual void setAdsorptionSurfaceBackSideDiffuseColor(RKColor color) = 0;
  virtual RKColor adsorptionSurfaceBackSideSpecularColor() const = 0;
  virtual void setAdsorptionSurfaceBackSideSpecularColor(RKColor color) = 0;
  virtual double adsorptionSurfaceBackSideDiffuseIntensity() const = 0;
  virtual void setAdsorptionSurfaceBackSideDiffuseIntensity(double value) = 0;
  virtual double adsorptionSurfaceBackSideAmbientIntensity() const = 0;
  virtual void setAdsorptionSurfaceBackSideAmbientIntensity(double value) = 0;
  virtual double adsorptionSurfaceBackSideSpecularIntensity() const = 0;
  virtual void setAdsorptionSurfaceBackSideSpecularIntensity(double value) = 0;
  virtual double adsorptionSurfaceBackSideShininess() const = 0;
  virtual void setAdsorptionSurfaceBackSideShininess(double value) = 0;
};

class VolumetricDataEditor: public VolumetricDataViewer
{
  public:
    virtual  ~VolumetricDataEditor() = 0;

    virtual void setEncompassingPowerOfTwoCubicGridSize(int value) = 0;
};
