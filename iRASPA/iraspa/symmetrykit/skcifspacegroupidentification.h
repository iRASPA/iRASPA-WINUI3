/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include "skseitzintegermatrix.h"
#include <optional>
#include <vector>

/// Hall-setting identification from CIF `_symmetry_equiv_pos_as_xyz` / `_space_group_symop_operation_xyz` lists.
class SKCIFSpaceGroupIdentification
{
public:
  /// Returns true when both operation lists describe the same symmetry set (order independent).
  static bool symmetryOperationsMatch(const std::vector<SKSeitzIntegerMatrix> &lhs,
                                      const std::vector<SKSeitzIntegerMatrix> &rhs);

  /// Identifies the Hall setting whose database operations match the CIF symmetry operations.
  static std::optional<int> identifyHallNumber(const std::vector<SKSeitzIntegerMatrix> &operations,
                                               const std::vector<int> &candidateHallNumbers);

  /// Candidate Hall numbers for CIF symmetry-operation matching.
  static std::vector<int> candidateHallNumbers(int spaceGroupITNumber,
                                               std::optional<int> declaredHallNumber,
                                               const std::optional<RKString> &declaredHMSymbol);
};
