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

#include <iostream>
#include <vector>
#include <unordered_set>
#include <mathkit.h>
#include <type_traits>
#include <foundationkit.h>
#include "skbond.h"
#include "skatomcopy.h"

// Monotonic counter bumped whenever atom visibility flags or the shape of an atom tree change.
// Consumers that derive expensive data from the tree (such as the ribbon visibility mask, which
// would otherwise be recomputed for every draw call) cache it against this generation. Both the
// mutations and the reads happen on the main thread, the same assumption the tree itself makes.
int64_t skAtomVisibilityGeneration();
void skInvalidateAtomVisibilityGeneration();

class  SKAsymmetricAtom
{
public:
    SKAsymmetricAtom();
    SKAsymmetricAtom(const SKAsymmetricAtom &assymetricAtom);
    SKAsymmetricAtom(RKString displayName, int elementIdentifier);
    SKAsymmetricAtom(RKString displayName, int elementIdentifier, double occupancy);
    virtual ~SKAsymmetricAtom();
    enum class SKAsymmetricAtomType: int64_t
    {
      container = 0, asymmetric = 1
    };

    enum class Hybridization: int64_t
    {
      untyped = 0, sp_linear = 1, sp2_trigonal = 2, sp3_tetrahedral = 3, square_planar = 4, trigonal_bipyramidal = 5, square_pyramidal = 6, octahedral = 7
    };

    int64_t asymmetricIndex() {return _asymmetricIndex;}
    void setAsymmetricIndex(int64_t value) {_asymmetricIndex = value;}

    RKString displayName() const  {return _displayName;}
    void setDisplayName(RKString newValue)  {_displayName = newValue;}
    double3 position() const  {return _position;}
    void setPosition(double3 newValue)  {_position = newValue;}
    void setPositionX(double newValue)  {_position.x = newValue;}
    void setPositionY(double newValue)  {_position.y = newValue;}
    void setPositionZ(double newValue)  {_position.z = newValue;}
    double charge() const  {return _charge;}
    void setCharge(double newValue)  {_charge = newValue;}

    int64_t tag() {return _tag;}
    void setTag(int64_t tag) {_tag = tag;}
    bool isVisible() {return _isVisible;}
    void toggleVisibility();
    void setVisibility(bool visibility) {_isVisible = visibility; skInvalidateAtomVisibilityGeneration();}

    RKColor color() {return _color;}
    void setColor(RKColor color) {_color = color;}
    double drawRadius() {return _drawRadius;}
    void setDrawRadius(double radius) {_drawRadius = radius;}

    double2 potentialParameters() {return _potentialParameters;}
    void setPotentialParameters(double2 value) {_potentialParameters = value;}

    RKString uniqueForceFieldName() const  {return _uniqueForceFieldName;}
    void setUniqueForceFieldName(RKString newValue)  {_uniqueForceFieldName = newValue;}
    int64_t elementIdentifier() const  {return _elementIdentifier;}
    void setElementIdentifier(int64_t newValue)  {_elementIdentifier = newValue;}

    double bondDistanceCriteria() const {return _bondDistanceCriteria;}
    void setBondDistanceCriteria(double bondDistanceCriteria) {_bondDistanceCriteria = bondDistanceCriteria;}

    bool3 isFixed() const  {return _isFixed;}
    void setIsFixed(bool3 newValue)  {_isFixed = newValue;}
    int64_t serialNumber() const  {return _serialNumber;}
    void setSerialNumber(int64_t newValue)  {_serialNumber = newValue;}
    char16_t remotenessIndicator() const  {return _remotenessIndicator;}
    void setRemotenessIndicator(char newValue)  {_remotenessIndicator = newValue;}
    char16_t branchDesignator() const  {return _branchDesignator;}
    void setBranchDesignator(char newValue)  {_branchDesignator = newValue;}
    char16_t alternateLocationIndicator() const  {return _alternateLocationIndicator;}
    void setAlternateLocationIndicator(char newValue)  {_alternateLocationIndicator = newValue;}
    RKString residueName() const  {return _residueName;}
    void setResidueName(RKString newValue)  {_residueName = newValue;}
    char16_t chainIdentifier() const  {return _chainIdentifier;}
    void setChainIdentifier(char newValue)  {_chainIdentifier = newValue;}
    int64_t residueSequenceNumber() const  {return _residueSequenceNumber;}
    void setResidueSequenceNumber(int64_t newValue)  {_residueSequenceNumber = newValue;}
    char16_t codeForInsertionOfResidues() const  {return _codeForInsertionOfResidues;}
    void setCodeForInsertionOfResidues(char newValue)  {_codeForInsertionOfResidues = newValue;}
    double occupancy() const  {return _occupancy;}
    void setOccupancy(double newValue)  {_occupancy = newValue;}
    double temperaturefactor() const  {return _temperaturefactor;}
    void setTemperaturefactor(double newValue)  {_temperaturefactor = newValue;}
    RKString segmentIdentifier() const  {return _segmentIdentifier;}
    void setSegmentIdentifier(RKString newValue)  {_segmentIdentifier = newValue;}
    int64_t asymetricID() const  {return _asymetricID;}
    //void setAsymetricID(int newValue)  {_asymetricID = newValue;}

    bool ligandAtom() const  {return _ligandAtom;}
    void setLigandAtom(bool newValue)  {_ligandAtom = newValue;}
    bool backBoneAtom() const  {return _backBoneAtom;}
    void backBoneAtom(bool newValue)  {_backBoneAtom = newValue;}
    bool fractional() const  {return _fractional;}
    void fractional(bool newValue)  {_fractional = newValue;}
    bool solvent() const  {return _solvent;}
    void setSolvent(bool newValue)  {_solvent = newValue;}

    std::vector<std::shared_ptr<SKAtomCopy>>& copies()  {return _copies;}
    void setCopies(std::vector<std::shared_ptr<SKAtomCopy>> copies) {_copies = copies;}
private:
    int64_t _versionNumber{2};
    int64_t _asymmetricIndex;
    RKString _displayName = RKString("Default");
    double3 _position = double3(0,0,0);
    double _charge = 0;

    RKString _uniqueForceFieldName;
    int64_t _elementIdentifier = 0;
    RKColor _color = RKColor::fromRgb(0,255,0,255);
    double _drawRadius = 1.0;
    double _bondDistanceCriteria = 1.0;
    double2 _potentialParameters = double2(0.0,0.0);

    int64_t _tag = 0;
    [[maybe_unused]] SKAsymmetricAtomType _symmetryType = SKAsymmetricAtomType::asymmetric;
    Hybridization _hybridization = Hybridization::untyped;

    // atom properties (bonds are visible depending on whether the atoms of the bonds are visible)
    bool3 _isFixed = bool3(false, false, false);
    bool _isVisible = true;
    bool _isVisibleEnabled = true;

    int64_t _serialNumber = 0;
    char16_t _remotenessIndicator = ' ';         // character 'A','B','C','D',...
    char16_t _branchDesignator = ' ';            // character '1','2','3',...
    int64_t _asymetricID = 0;                    // positive integer
    char16_t _alternateLocationIndicator = ' ';  // character ' ' or 'A','B',...
    RKString _residueName = RKString("");           // empty or 3 characters
    char16_t _chainIdentifier = ' ';             // empty or 'A','B','C',...
    int64_t _residueSequenceNumber = 0;          // positive integer
    char16_t _codeForInsertionOfResidues = ' ';  // empty or 'A','B','C',...
    double _occupancy = 1.0;
    double _temperaturefactor = 0.0;
    RKString _segmentIdentifier = RKString("");     // empty or 4 characters

    bool _ligandAtom = false;
    bool _backBoneAtom = false;
    bool _fractional = false;
    bool _solvent = false;

    double3 _displacement = double3();

    // the crystallographic copies of the atom
    std::vector<std::shared_ptr<SKAtomCopy>> _copies;

    friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<SKAtomCopy>>& val);
    friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<SKAtomCopy>>& val);

    friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<SKAsymmetricAtom> &);
    friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<SKAsymmetricAtom> &);
};

