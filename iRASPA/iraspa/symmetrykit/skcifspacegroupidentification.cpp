/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcifspacegroupidentification.h"
#include "skspacegroup.h"
#include "skspacegroupdatabase.h"
#include <unordered_set>

bool SKCIFSpaceGroupIdentification::symmetryOperationsMatch(const std::vector<SKSeitzIntegerMatrix> &lhs,
                                                           const std::vector<SKSeitzIntegerMatrix> &rhs)
{
  if (lhs.size() != rhs.size())
  {
    return false;
  }
  const std::unordered_set<SKSeitzIntegerMatrix> left(lhs.begin(), lhs.end());
  const std::unordered_set<SKSeitzIntegerMatrix> right(rhs.begin(), rhs.end());
  return left == right;
}

std::optional<int> SKCIFSpaceGroupIdentification::identifyHallNumber(
    const std::vector<SKSeitzIntegerMatrix> &operations,
    const std::vector<int> &candidateHallNumbers)
{
  if (operations.empty())
  {
    return std::nullopt;
  }

  std::vector<int> matches;
  matches.reserve(4);

  for (int hallNumber : candidateHallNumbers)
  {
    if (hallNumber < 1 || hallNumber > 530)
    {
      continue;
    }
    const SKSpaceGroup spaceGroup(hallNumber);
    const auto &databaseOperations = spaceGroup.spaceGroupSetting().fullSeitzMatrices().operations;
    std::vector<SKSeitzIntegerMatrix> databaseVector(databaseOperations.begin(), databaseOperations.end());
    if (symmetryOperationsMatch(operations, databaseVector))
    {
      matches.push_back(hallNumber);
    }
  }

  if (matches.empty())
  {
    return std::nullopt;
  }

  if (matches.size() == 1)
  {
    return matches[0];
  }

  // Prefer the reference setting when multiple Hall symbols share the same operation set.
  std::vector<int> referenceMatches;
  for (int hallNumber : matches)
  {
    if (SKSpaceGroupDataBase::spaceGroupData[static_cast<size_t>(hallNumber)].standardSetting())
    {
      referenceMatches.push_back(hallNumber);
    }
  }
  if (referenceMatches.size() == 1)
  {
    return referenceMatches[0];
  }
  if (!referenceMatches.empty())
  {
    return referenceMatches[0];
  }

  return matches[0];
}

std::vector<int> SKCIFSpaceGroupIdentification::candidateHallNumbers(int spaceGroupITNumber,
                                                                    std::optional<int> declaredHallNumber,
                                                                    const std::optional<RKString> &declaredHMSymbol)
{
  if (declaredHallNumber && *declaredHallNumber >= 1 && *declaredHallNumber <= 530)
  {
    const int spaceGroupNumber = static_cast<int>(
        SKSpaceGroupDataBase::spaceGroupData[static_cast<size_t>(*declaredHallNumber)].number());
    if (spaceGroupNumber >= 0 &&
        static_cast<size_t>(spaceGroupNumber) < SKSpaceGroupDataBase::spaceGroupHallData.size())
    {
      return SKSpaceGroupDataBase::spaceGroupHallData[static_cast<size_t>(spaceGroupNumber)];
    }
    return {*declaredHallNumber};
  }

  if (spaceGroupITNumber > 0 &&
      static_cast<size_t>(spaceGroupITNumber) < SKSpaceGroupDataBase::spaceGroupHallData.size())
  {
    return SKSpaceGroupDataBase::spaceGroupHallData[static_cast<size_t>(spaceGroupITNumber)];
  }

  if (declaredHMSymbol)
  {
    RKString needle = declaredHMSymbol->simplified();
    needle.remove(' ');
    needle = needle.toLower();

    std::vector<int> halls;
    for (int hallNumber = 1; hallNumber <= 530; ++hallNumber)
    {
      RKString stored = SKSpaceGroupDataBase::spaceGroupData[static_cast<size_t>(hallNumber)].HMString().simplified();
      stored.remove(' ');
      stored = stored.toLower();
      if (stored == needle)
      {
        halls.push_back(hallNumber);
      }
    }
    if (!halls.empty())
    {
      return halls;
    }
  }

  std::vector<int> all;
  all.reserve(530);
  for (int i = 1; i <= 530; ++i)
  {
    all.push_back(i);
  }
  return all;
}
