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

#include "rkstring.h"
#include "structure.h"

class StructuralPropertyViewer
{
public:
  virtual ~StructuralPropertyViewer() = 0;

  virtual void recomputeDensityProperties() = 0;

  virtual double computeVoidFractionAccelerated() const noexcept(false) = 0;
  virtual double computeNitrogenSurfaceAreaAccelerated() const noexcept(false) = 0;

  // The area of the well surface: the sheet of positions where the probe rests at the floor of its
  // energy well against the framework. That is the surface a molecule actually sits on, and so the
  // accessible surface area. The zero-energy isosurface, further out, is where the probe would be
  // turning back rather than resting, and it measures roughly twice as much area --- for silicalite
  // 704 against 344 square metre per gram, where BET measurements land between 300 and 450. Both are
  // reported: the energy isosurface as the nitrogen surface area, this one as the well-surface area.
  virtual double computeWellSurfaceAreaAccelerated() const noexcept(false) = 0;

  virtual double computeVoidFraction() const noexcept = 0 ;
  virtual double computeNitrogenSurfaceArea() const noexcept = 0;

  //   var structureType: Structure.StructureType {get}
  virtual RKString structureMaterialType() const = 0;
  virtual ProbeMolecule frameworkProbeMolecule() const = 0;
  virtual double frameworkProbeEpsilon() const = 0;
  virtual double frameworkProbeSigma() const = 0;
  virtual double structureMass() const = 0;
  virtual double structureDensity() const = 0;
  virtual double structureHeliumVoidFraction() const = 0;
  virtual double structureSpecificVolume() const = 0;
  virtual double structureAccessiblePoreVolume() const = 0;
  virtual double structureVolumetricNitrogenSurfaceArea() const = 0;
  virtual double structureGravimetricNitrogenSurfaceArea() const = 0;
  virtual double structureVolumetricWellSurfaceArea() const = 0;
  virtual double structureGravimetricWellSurfaceArea() const = 0;
  virtual double structureVolumetricGeometricSurfaceArea() const = 0;
  virtual double structureGravimetricGeometricSurfaceArea() const = 0;
  virtual double structureVolumetricVanDerWaalsGeometricSurfaceArea() const = 0;
  virtual double structureGravimetricVanDerWaalsGeometricSurfaceArea() const = 0;
  virtual int structureNumberOfChannelSystems() const = 0;
  virtual int structureNumberOfInaccessiblePockets() const = 0;
  virtual int structureDimensionalityOfPoreSystem() const = 0;
  virtual double structureLargestCavityDiameter() const = 0;
  virtual double structureRestrictingPoreLimitingDiameter() const = 0;
  virtual double structureLargestCavityDiameterAlongAViablePath() const = 0;
};

class StructuralPropertyEditor: public StructuralPropertyViewer
{
public:
  virtual ~StructuralPropertyEditor() = 0;

  //   var structureType: Structure.StructureType {get}
  virtual void setStructureMaterialType(RKString value) = 0;
  virtual void setFrameworkProbeMolecule(ProbeMolecule molecule) = 0;
  /// Sets the probe and copies named ε/σ when available (Cocoa applyFrameworkProbeMolecule).
  virtual void applyFrameworkProbeMolecule(ProbeMolecule molecule) = 0;
  virtual void setFrameworkProbeEpsilon(double value) = 0;
  virtual void setFrameworkProbeSigma(double value) = 0;
  virtual void setStructureMass(double value) = 0;
  virtual void setStructureDensity(double value) = 0;
  virtual void setStructureHeliumVoidFraction(double value) = 0;
  virtual void setStructureSpecificVolume(double value) = 0;
  virtual void setStructureAccessiblePoreVolume(double value) = 0;
  virtual void setStructureVolumetricNitrogenSurfaceArea(double value) = 0;
  virtual void setStructureGravimetricNitrogenSurfaceArea(double value) = 0;
  virtual void setStructureVolumetricWellSurfaceArea(double value) = 0;
  virtual void setStructureGravimetricWellSurfaceArea(double value) = 0;
  virtual void setStructureVolumetricGeometricSurfaceArea(double value) = 0;
  virtual void setStructureGravimetricGeometricSurfaceArea(double value) = 0;
  virtual void setStructureVolumetricVanDerWaalsGeometricSurfaceArea(double value) = 0;
  virtual void setStructureGravimetricVanDerWaalsGeometricSurfaceArea(double value) = 0;
  virtual void setStructureNumberOfChannelSystems(int value) = 0;
  virtual void setStructureNumberOfInaccessiblePockets(int value) = 0;
  virtual void setStructureDimensionalityOfPoreSystem(int value) = 0;
  virtual void setStructureLargestCavityDiameter(double value) = 0;
  virtual void setStructureRestrictingPoreLimitingDiameter(double value) = 0;
  virtual void setStructureLargestCavityDiameterAlongAViablePath(double value) = 0;
};
