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
#include <ostream>
#include <string>
#include <vector>
#include <mathkit.h>
#include "skasymmetricunit.h"
#include "sktransformationmatrix.h"
#include "skintegersymmetryoperationset.h"

class SKSpaceGroupSetting
{
public:  
  SKSpaceGroupSetting(int64_t number, int64_t spaceGroupNumber, int64_t order, char ext, RKString qualifier, RKString HM, RKString Hall,
                      bool inversionAtOrigin, int3 inversionCenter, Symmorphicity symmorphicity, bool standard, Centring centring,
                      std::vector<int3> latticeTranslations, int64_t pointGroupNumber, std::string schoenflies, std::string generators,
                      std::string encoding, SKAsymmetricUnit asymmetricUnit, SKTransformationMatrix transformationMatrix);
  SKIntegerSymmetryOperationSet fullSeitzMatrices() const;
  std::vector<SKSeitzIntegerMatrix> SeitzMatricesWithoutTranslation() const;

  int64_t number() const {return _spaceGroupNumber;}
  int64_t order() const {return _order;}
  int64_t HallNumber() const {return _HallNumber;}
  RKString HallString() const {return _HallString;}
  RKString HMString() const {return _HMString;}
  int64_t pointGroupNumber() const {return _pointGroupNumber;}
  RKString qualifier() const {return _qualifier;}
  // Origin/setting extension: 0, 1, 2 (and rarely H/R). Cocoa's spacegroupQualifiers
  // prefixes the qualifier with "N:" when this is non-zero (e.g. "1:abc").
  char extension() const {return _ext;}
  // Space-group Schoenflies (e.g. Oh^7), not the point-group symbol (Oh).
  RKString schoenflies() const {return RKString(_schoenflies);}
  Symmorphicity symmorphicity() const {return _symmorphicity;}
  RKString symmorphicityString() const;
  RKString centringString() const;

  const std::string encodedGenerators() const {return _encodedGenerators;}
  const std::string encodedSeitz() const {return _encodedSeitz;}

  // Whether this is the standard setting of its space-group number; the search
  // by symmetry operations prefers it when several settings match.
  bool standardSetting() const {return _standard;}

  bool inversionAtOrigin() const {return _inversionAtOrigin;}
  int3 inversionCenter() const {return _inversionCenter;}

  SKTransformationMatrix transformationMatrix() const {return _transformationMatrix;}

  const std::vector<int3> latticeTranslations() const {return _latticeTranslations;}
  Centring centring() const {return _centring;}

  // check
  SKAsymmetricUnit asymmetricUnit() const {return _asymmetricUnit;}

  friend std::ostream& operator<<(std::ostream& os, const SKSpaceGroupSetting& setting);
private:
  int64_t _HallNumber = 1;
  int64_t _spaceGroupNumber = 1;      // space group number (1-230)
  int64_t _order;
  char _ext;                         // '1', '2', 'H', 'R' or '\0'
  RKString _qualifier;                // e.g. "-cba" or "b1"
  RKString _HMString;                 // H-M symbol; nul-terminated string
  RKString _HallString;               // Hall symbol; nul-terminated string
  std::string _encodedGenerators;    // encoded seitz matrix-generators
  std::string _encodedSeitz;         // encoded seitz matrix
  bool _inversionAtOrigin;
  int3 _inversionCenter;
  bool _standard = false;
  Symmorphicity _symmorphicity = Symmorphicity::asymmorphic;
  Centring _centring = Centring::primitive;
  std::vector<int3> _latticeTranslations;
  std::string _schoenflies;
  int64_t _pointGroupNumber;
  SKAsymmetricUnit _asymmetricUnit = { {10,0}, {20,1}, {30,2} };
  SKTransformationMatrix _transformationMatrix;  // the inverse of the transformation to "standard" setting (so: standard to unconventional setting)
};
