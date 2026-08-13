/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include "skseitzintegermatrix.h"
#include <stdexcept>
#include <string>
#include <vector>

class SKCIFSymmetryOperationParserError : public std::runtime_error
{
public:
  explicit SKCIFSymmetryOperationParserError(const std::string &message) : std::runtime_error(message) {}
};

/// Parses CIF symmetry strings such as `'+x,+y,+z'` or `'1/2+x,1/2+y,+z'` into `SKSeitzIntegerMatrix`.
class SKCIFSymmetryOperationParser
{
public:
  static SKSeitzIntegerMatrix parse(const RKString &xyz);
  static std::vector<SKSeitzIntegerMatrix> parseOperations(const std::vector<RKString> &xyzStrings);

private:
  static std::pair<int3, double> parseComponent(const std::string &input);
};
