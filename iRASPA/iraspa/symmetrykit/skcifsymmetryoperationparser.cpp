/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcifsymmetryoperationparser.h"
#include "skrotationmatrix.h"
#include <cctype>
#include <cmath>

namespace
{
RKString trimQuotesAndSpace(const RKString &xyz)
{
  std::string s = xyz.trimmed().utf8();
  while (!s.empty() && (s.front() == '\'' || s.front() == '"' || s.front() == ' '))
  {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == '\'' || s.back() == '"' || s.back() == ' '))
  {
    s.pop_back();
  }
  return RKString(s);
}

int3 translationFromFractional(const double3 &translation)
{
  auto convert = [](double v) -> int
  {
    int r = static_cast<int>(std::rint(v * 24.0));
    return ((r % 24) + 24) % 24;
  };
  return int3(convert(translation.x), convert(translation.y), convert(translation.z));
}
} // namespace

SKSeitzIntegerMatrix SKCIFSymmetryOperationParser::parse(const RKString &xyz)
{
  const RKString trimmed = trimQuotesAndSpace(xyz);
  std::vector<std::string> components;
  {
    std::string current;
    for (char c : trimmed.utf8())
    {
      if (c == ',')
      {
        components.push_back(current);
        current.clear();
      }
      else
      {
        current.push_back(c);
      }
    }
    components.push_back(current);
  }

  if (components.size() != 3)
  {
    throw SKCIFSymmetryOperationParserError("invalidFormat: " + trimmed.utf8());
  }

  int3 column0(0, 0, 0);
  int3 column1(0, 0, 0);
  int3 column2(0, 0, 0);
  double3 translation(0.0, 0.0, 0.0);

  for (int outputIndex = 0; outputIndex < 3; ++outputIndex)
  {
    const auto [coefficients, componentTranslation] = parseComponent(components[static_cast<size_t>(outputIndex)]);
    if (outputIndex == 0)
    {
      column0.x = coefficients.x;
      column1.x = coefficients.y;
      column2.x = coefficients.z;
      translation.x = componentTranslation;
    }
    else if (outputIndex == 1)
    {
      column0.y = coefficients.x;
      column1.y = coefficients.y;
      column2.y = coefficients.z;
      translation.y = componentTranslation;
    }
    else
    {
      column0.z = coefficients.x;
      column1.z = coefficients.y;
      column2.z = coefficients.z;
      translation.z = componentTranslation;
    }
  }

  const double3 fractional = double3::fract(translation);
  SKRotationMatrix rotation(column0, column1, column2);
  return SKSeitzIntegerMatrix(rotation, translationFromFractional(fractional));
}

std::vector<SKSeitzIntegerMatrix> SKCIFSymmetryOperationParser::parseOperations(const std::vector<RKString> &xyzStrings)
{
  std::vector<SKSeitzIntegerMatrix> operations;
  operations.reserve(xyzStrings.size());
  for (const RKString &xyz : xyzStrings)
  {
    operations.push_back(parse(xyz));
  }
  return operations;
}

std::pair<int3, double> SKCIFSymmetryOperationParser::parseComponent(const std::string &input)
{
  int3 coefficients(0, 0, 0);
  double translation = 0.0;

  size_t index = 0;
  const size_t end = input.size();

  while (index < end)
  {
    double sign = 1.0;

    if (input[index] == '+')
    {
      ++index;
    }
    else if (input[index] == '-')
    {
      sign = -1.0;
      ++index;
    }

    bool hasNumber = false;
    double numerator = 0.0;
    double denominator = 1.0;

    while (index < end && std::isdigit(static_cast<unsigned char>(input[index])))
    {
      hasNumber = true;
      numerator = numerator * 10.0 + static_cast<double>(input[index] - '0');
      ++index;
    }

    if (index < end && input[index] == '/')
    {
      ++index;
      denominator = 0.0;
      while (index < end && std::isdigit(static_cast<unsigned char>(input[index])))
      {
        denominator = denominator * 10.0 + static_cast<double>(input[index] - '0');
        ++index;
      }
      if (denominator == 0.0)
      {
        throw SKCIFSymmetryOperationParserError("invalidFormat: " + input);
      }
    }

    const double value = sign * (hasNumber ? numerator / denominator : 1.0);

    if (index < end)
    {
      const char axis = static_cast<char>(std::tolower(static_cast<unsigned char>(input[index])));
      if (axis == 'x' || axis == 'y' || axis == 'z')
      {
        if (std::fabs(value) != 1.0)
        {
          throw SKCIFSymmetryOperationParserError("invalidCoefficient: " + input);
        }
        const int32_t coeff = static_cast<int32_t>(value);
        if (axis == 'x') { coefficients.x += coeff; }
        else if (axis == 'y') { coefficients.y += coeff; }
        else { coefficients.z += coeff; }
        ++index;
      }
      else if (hasNumber)
      {
        translation += value;
      }
      else
      {
        throw SKCIFSymmetryOperationParserError("invalidFormat: " + input);
      }
    }
    else if (hasNumber)
    {
      translation += value;
    }
    else
    {
      throw SKCIFSymmetryOperationParserError("invalidFormat: " + input);
    }
  }

  return {coefficients, translation};
}
