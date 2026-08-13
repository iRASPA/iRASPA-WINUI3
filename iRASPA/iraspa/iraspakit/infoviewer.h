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
#include "rkdate.h"

/*
class BasicInfoViewer
{
public:
  virtual ~BasicInfoViewer() = 0;
  virtual RKString authorFirstName() = 0;
  virtual void setAuthorFirstName(RKString name) = 0;
  virtual RKString authorMiddleName() = 0;
  virtual void setAuthorMiddleName(RKString name) = 0;
  virtual RKString authorLastName() = 0;
  virtual void setAuthorLastName(RKString name) = 0;
  virtual RKString authorOrchidID() = 0;
  virtual void setAuthorOrchidID(RKString name) = 0;
  virtual RKString authorResearcherID() = 0;
  virtual void setAuthorResearcherID(RKString name) = 0;
  virtual RKString authorAffiliationUniversityName() = 0;
  virtual void setAuthorAffiliationUniversityName(RKString name) = 0;
  virtual RKString authorAffiliationFacultyName() = 0;
  virtual void setAuthorAffiliationFacultyName(RKString name) = 0;
  virtual RKString authorAffiliationInstituteName() = 0;
  virtual void setAuthorAffiliationInstituteName(RKString name) = 0;
  virtual RKString authorAffiliationCityName() = 0;
  virtual void setAuthorAffiliationCityName(RKString name) = 0;
  virtual RKString authorAffiliationCountryName() = 0;
  virtual void setAuthorAffiliationCountryName(RKString name) = 0;
};*/

class InfoViewer
{
public:
  enum class TemperatureScale: int64_t
  {
    Kelvin = 0, Celsius = 1, multiple_values = 2
  };

  enum class PressureScale: int64_t
  {
    Pascal = 0, bar = 1, multiple_values = 2
  };

  enum class CreationMethod: int64_t
  {
    unknown = 0, simulation = 1, experimental = 2, multiple_values = 3
  };

  enum class UnitCellRelaxationMethod: int64_t
  {
    unknown = 0, allFree = 1, fixedAnglesIsotropic = 2, fixedAnglesAnistropic = 3, betaAnglefixed = 4, fixedVolumeFreeAngles = 5, allFixed = 6, multiple_values = 7
  };

  enum class IonsRelaxationAlgorithm: int64_t
  {
    unknown = 0, none = 1, simplex = 2, simulatedAnnealing = 3, geneticAlgorithm = 4, steepestDescent = 5, conjugateGradient = 6,
    quasiNewton = 7, NewtonRaphson = 8, BakersMinimization = 9, multiple_values = 10
  };

  enum class IonsRelaxationCheck: int64_t
  {
    unknown = 0, none = 1, allPositiveEigenvalues = 2, someSmallNegativeEigenvalues = 3, someSignificantNegativeEigenvalues = 4,
    manyNegativeEigenvalues = 5, multiple_values = 6
  };

  virtual ~InfoViewer() = 0;
  //virtual RKDate creationDate() = 0;
  virtual RKString creationTemperature() = 0;
  virtual TemperatureScale creationTemperatureScale() = 0;
  virtual RKString creationPressure() = 0;
  virtual PressureScale creationPressureScale() = 0;
  virtual CreationMethod creationMethod() = 0;
  virtual UnitCellRelaxationMethod creationUnitCellRelaxationMethod() = 0;
  virtual RKString creationAtomicPositionsSoftwarePackage() = 0;
  virtual IonsRelaxationAlgorithm creationAtomicPositionsIonsRelaxationAlgorithm() = 0;
  virtual IonsRelaxationCheck creationAtomicPositionsIonsRelaxationCheck() = 0;
  virtual RKString creationAtomicPositionsForcefield() = 0;
  virtual RKString creationAtomicPositionsForcefieldDetails() = 0;
  virtual RKString creationAtomicChargesSoftwarePackage() = 0;
  virtual RKString creationAtomicChargesAlgorithms() = 0;
  virtual RKString creationAtomicChargesForcefield() = 0;
  virtual RKString creationAtomicChargesForcefieldDetails() = 0;

  virtual RKString experimentalMeasurementRadiation() = 0;
  virtual RKString experimentalMeasurementWaveLength() = 0;
  virtual RKString experimentalMeasurementThetaMin() = 0;
  virtual RKString experimentalMeasurementThetaMax() = 0;
  virtual RKString experimentalMeasurementIndexLimitsHmin() = 0;
  virtual RKString experimentalMeasurementIndexLimitsHmax() = 0;
  virtual RKString experimentalMeasurementIndexLimitsKmin() = 0;
  virtual RKString experimentalMeasurementIndexLimitsKmax() = 0;
  virtual RKString experimentalMeasurementIndexLimitsLmin() = 0;
  virtual RKString experimentalMeasurementIndexLimitsLmax() = 0;
  virtual RKString experimentalMeasurementNumberOfSymmetryIndependentReflections() = 0;
  virtual RKString experimentalMeasurementSoftware() = 0;
  virtual RKString experimentalMeasurementRefinementDetails() = 0;
  virtual RKString experimentalMeasurementGoodnessOfFit() = 0;
  virtual RKString experimentalMeasurementRFactorGt() = 0;
  virtual RKString experimentalMeasurementRFactorAll() = 0;

  virtual RKString chemicalFormulaMoiety() = 0;
  virtual RKString chemicalFormulaSum() = 0;
  virtual RKString chemicalNameSystematic() = 0;

  virtual RKString citationArticleTitle() = 0;
  virtual RKString citationJournalTitle() = 0;
  virtual RKString citationAuthors() = 0;
  virtual RKString citationJournalVolume() = 0;
  virtual RKString citationJournalNumber() = 0;
  virtual RKString citationJournalPageNumbers() = 0;
  virtual RKString citationDOI() = 0;
  virtual RKDate citationPublicationDate() = 0;
  virtual RKString citationDatebaseCodes() = 0;
};

class InfoEditor: public InfoViewer
{
public:
  virtual ~InfoEditor() = 0;
  //virtual void setCreationDate(RKDate date) = 0;
  virtual void setCreationTemperature(RKString name) = 0;
  virtual void setCreationTemperatureScale(TemperatureScale scale) = 0;
  virtual void setCreationPressure(RKString pressure) = 0;
  virtual void setCreationPressureScale(PressureScale scale) = 0;
  virtual void setCreationMethod(CreationMethod method) = 0;
  virtual void setCreationUnitCellRelaxationMethod(UnitCellRelaxationMethod method) = 0;
  virtual void setCreationAtomicPositionsSoftwarePackage(RKString name) = 0;
  virtual void setCreationAtomicPositionsIonsRelaxationAlgorithm(IonsRelaxationAlgorithm algorithm) = 0;
  virtual void setCreationAtomicPositionsIonsRelaxationCheck(IonsRelaxationCheck check) = 0;
  virtual void setCreationAtomicPositionsForcefield(RKString name) = 0;
  virtual void setCreationAtomicPositionsForcefieldDetails(RKString name) = 0;
  virtual void setCreationAtomicChargesSoftwarePackage(RKString name) = 0;
  virtual void setCreationAtomicChargesAlgorithms(RKString name) = 0;
  virtual void setCreationAtomicChargesForcefield(RKString name) = 0;
  virtual void setCreationAtomicChargesForcefieldDetails(RKString name) = 0;

  virtual void setExperimentalMeasurementRadiation(RKString name) = 0;
  virtual void setExperimentalMeasurementWaveLength(RKString name) = 0;
  virtual void setExperimentalMeasurementThetaMin(RKString name) = 0;
  virtual void setExperimentalMeasurementThetaMax(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsHmin(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsHmax(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsKmin(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsKmax(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsLmin(RKString name) = 0;
  virtual void setExperimentalMeasurementIndexLimitsLmax(RKString name) = 0;
  virtual void setExperimentalMeasurementNumberOfSymmetryIndependentReflections(RKString name) = 0;
  virtual void setExperimentalMeasurementSoftware(RKString name) = 0;
  virtual void setExperimentalMeasurementGoodnessOfFit(RKString goodness) = 0;
  virtual void setExperimentalMeasurementRefinementDetails(RKString name) = 0;
  virtual void setExperimentalMeasurementRFactorGt(RKString name) = 0;
  virtual void setExperimentalMeasurementRFactorAll(RKString name) = 0;

  virtual void setChemicalFormulaMoiety(RKString name) = 0;
  virtual void setChemicalFormulaSum(RKString name) = 0;
  virtual void setChemicalNameSystematic(RKString name) = 0;

  virtual void setCitationArticleTitle(RKString name) = 0;
  virtual void setCitationJournalTitle(RKString name) = 0;
  virtual void setCitationAuthors(RKString name) = 0;
  virtual void setCitationJournalVolume(RKString name) = 0;
  virtual void setCitationJournalNumber(RKString name) = 0;
  virtual void setCitationJournalPageNumbers(RKString name) = 0;
  virtual void setCitationDOI(RKString name) = 0;
  virtual void setCitationPublicationDate(RKDate date) = 0;
  virtual void setCitationDatebaseCodes(RKString name) = 0;
};
