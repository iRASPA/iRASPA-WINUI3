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

#pragma once

#include "rkstring.h"
#include "rkcolor.h"
#include "rkdate.h"
#include <renderkit.h>
#include "skboundingbox.h"
#include "skcell.h"
#include "displayable.h"

enum class ObjectType : int64_t
{
  none = -1, object = 0, structure = 1, crystal = 2, molecularCrystal = 3, molecule = 4, protein = 5, proteinCrystal = 6,
  proteinCrystalSolvent = 7, crystalSolvent = 8, molecularCrystalSolvent = 9,
  crystalEllipsoidPrimitive = 10, crystalCylinderPrimitive = 11, crystalPolygonalPrismPrimitive = 12,
  ellipsoidPrimitive = 13, cylinderPrimitive = 14, polygonalPrismPrimitive = 15,
  gridVolume = 16, RASPADensityVolume = 17, VTKDensityVolume = 18, VASPDensityVolume = 19, GaussianCubeVolume = 20
};

class Object: public DisplayableProtocol, public RKRenderObject, public RKRenderLocalAxesSource
{
public:
  Object();
  virtual ~Object() {;}

  virtual std::shared_ptr<Object> shallowClone();

  virtual ObjectType structureType()  {return ObjectType::object;}

  Object(const std::shared_ptr<const SKStructure> structure);
  Object(const Object &object);
  Object(const std::shared_ptr<const Object> object);

  RKString displayName() const override final {return _displayName;}
  void setDisplayName(RKString name) override final {_displayName = name;}

  bool isVisible() const override  final{return _isVisible;}
  void setVisibility(bool visibility) {_isVisible = visibility;}

  std::shared_ptr<SKCell> cell() const override final {return _cell;}

  void setCell(std::shared_ptr<SKCell> cell) {_cell = cell;}

  virtual SKBoundingBox boundingBox() const; // has to be overwriten for subclasses of Object
  virtual void reComputeBoundingBox();       // has to be overwriten for subclasses of Object
  SKBoundingBox transformedBoundingBox() const;

  double rotationDelta() {return _rotationDelta;}
  void setRotationDelta(double angle) {_rotationDelta = angle;}
  void setOrientation(simd_quatd orientation) {_orientation = orientation;}
  simd_quatd orientation() const override final {return _orientation;}
  double3 origin() const override final {return _origin;}
  void setOrigin(double3 value) {_origin = value;}
  void setOriginX(double value) {_origin.x = value;}
  void setOriginY(double value) {_origin.y = value;}
  void setOriginZ(double value) {_origin.z = value;}

  // unit cell
  virtual bool drawUnitCell() const {return _drawUnitCell;}
  virtual double unitCellScaleFactor() const {return _unitCellScaleFactor;}
  virtual RKColor unitCellDiffuseColor() const {return _unitCellDiffuseColor;}
  virtual double unitCellDiffuseIntensity() const {return _unitCellDiffuseIntensity;}
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderUnitCellSpheres() const  {return {};}
  virtual std::vector<RKInPerInstanceAttributesBonds> renderUnitCellCylinders() const {return {};}

  void setDrawUnitCell(bool state) {_drawUnitCell = state;}
  void setUnitCellScaleFactor(double value) {_unitCellScaleFactor = value;}
  void setUnitCellDiffuseColor(RKColor color) {_unitCellDiffuseColor = color;}
  void setUnitCellDiffuseIntensity(double value) {_unitCellDiffuseIntensity = value;}

  // Protocol: RKRenderLocalAxesSource
  RKLocalAxes &renderLocalAxes() override {return _localAxes;}

  // info
  RKString authorFirstName() {return _authorFirstName;}
  void setAuthorFirstName(RKString name) {_authorFirstName = name;}
  RKString authorMiddleName() {return _authorMiddleName;}
  void setAuthorMiddleName(RKString name) {_authorMiddleName = name;}
  RKString authorLastName()  {return _authorLastName;}
  void setAuthorLastName(RKString name) {_authorLastName = name;}
  RKString authorOrchidID() {return _authorOrchidID;}
  void setAuthorOrchidID(RKString name) {_authorOrchidID = name;}
  RKString authorResearcherID() {return _authorResearcherID;}
  void setAuthorResearcherID(RKString name) {_authorResearcherID = name;}
  RKString authorAffiliationUniversityName() {return _authorAffiliationUniversityName;}
  void setAuthorAffiliationUniversityName(RKString name) {_authorAffiliationUniversityName = name;}
  RKString authorAffiliationFacultyName() {return _authorAffiliationFacultyName;}
  void setAuthorAffiliationFacultyName(RKString name) {_authorAffiliationFacultyName = name;}
  RKString authorAffiliationInstituteName()  {return _authorAffiliationInstituteName;}
  void setAuthorAffiliationInstituteName(RKString name) {_authorAffiliationInstituteName = name;}
  RKString authorAffiliationCityName() {return _authorAffiliationCityName;}
  void setAuthorAffiliationCityName(RKString name) {_authorAffiliationCityName = name;}
  RKString authorAffiliationCountryName() {return _authorAffiliationCountryName;}
  void setAuthorAffiliationCountryName(RKString name) {_authorAffiliationCountryName = name;}

  RKDate creationDate() {return _creationDate;}
  void setCreationDate(RKDate date) {_creationDate = date;}
protected:
  RKString _displayName = RKString("object");
  bool _isVisible = true;

  double3 _origin = double3(0.0, 0.0, 0.0);
  double3 _scaling = double3(1.0, 1.0, 1.0);
  bool _periodic = false;
  simd_quatd _orientation = simd_quatd(1.0, double3(0.0, 0.0, 0.0));
  double _rotationDelta = 5.0;
  std::shared_ptr<SKCell> _cell;

  bool _drawUnitCell = true;
  double _unitCellScaleFactor = 1.0;
  RKColor _unitCellDiffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  double _unitCellDiffuseIntensity = 1.0;

  RKLocalAxes _localAxes;

  RKString _authorFirstName = RKString("");
  RKString _authorMiddleName = RKString("");
  RKString _authorLastName = RKString("");
  RKString _authorOrchidID = RKString("");
  RKString _authorResearcherID = RKString("");
  RKString _authorAffiliationUniversityName = RKString("");
  RKString _authorAffiliationFacultyName = RKString("");
  RKString _authorAffiliationInstituteName = RKString("");
  RKString _authorAffiliationCityName = RKString("");
  RKString _authorAffiliationCountryName = RKString("Netherlands");

  RKDate _creationDate = RKDate::currentDate();
private:
  int64_t _versionNumber{1};

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<Object> &object);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<Object> &object);
};
