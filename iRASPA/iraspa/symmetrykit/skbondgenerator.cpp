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

#include "skbondgenerator.h"
#include "skasymmetricatom.h"
#include "skelement.h"
#include <algorithm>
#include <cmath>

namespace
{
  // How much further apart than their covalent radii two atoms may sit and still be called bonded.
  const double bondLengthTolerance = 0.56;

  // Closer than this the two are not two atoms at all but one atom written down twice.
  const double duplicateDistance = 0.1;

  // The half of the 3x3x3 neighbourhood a grid sweep needs: the cell itself, and one of each pair of
  // opposing neighbours. Every unordered pair of cells is then reached exactly once, so no pair of
  // atoms is tested twice and none is missed.
  const int neighbourOffsets[14][3] =
  {
    { 0, 0, 0}, { 1, 0, 0}, { 1, 1, 0}, { 0, 1, 0}, {-1, 1, 0},
    { 0, 0, 1}, { 1, 0, 1}, { 1, 1, 1}, { 0, 1, 1}, {-1, 1, 1},
    {-1, 0, 1}, {-1,-1, 1}, { 0,-1, 1}, { 1,-1, 1}
  };

  // The longest bond these particular atoms could make. Sizing the grid by the elements actually
  // present rather than by a fixed distance keeps the cells as small as the structure allows, and
  // guarantees no pair the all-pairs sweep would have bonded falls outside the neighbourhood.
  double longestPossibleBond(const std::vector<double> &covalentRadii)
  {
    double largestCovalentRadius = 0.0;
    for(const double radius : covalentRadii)
    {
      largestCovalentRadius = std::max(largestCovalentRadius, radius);
    }
    return 2.0 * largestCovalentRadius + bondLengthTolerance;
  }

  // As many cells as fit at the full bond length, so that a cell is never narrower than a bond and a
  // partner is never further away than the neighbouring cell. Cells much finer than that gain
  // nothing: a grid with far more cells than atoms is mostly empty, and sweeping the empty ones
  // costs more than the comparisons it saves. Cells are therefore widened until there are few enough
  // of them, which is always safe — only narrowing them could lose a pair.
  int3 cellsPerAxis(double3 widths, double cutoff, size_t atomCount)
  {
    if(!(cutoff > 0.0)) return int3(0, 0, 0);

    const size_t cellBudget = std::max(static_cast<size_t>(27), 8 * atomCount);
    double budgetPerAxis = std::cbrt(static_cast<double>(cellBudget));
    budgetPerAxis = std::max(budgetPerAxis, 3.0);

    auto axisCount = [cutoff, budgetPerAxis](double width) -> int
    {
      const double count = width / cutoff;
      if(!(count >= 0.0)) return 0;   // also catches a degenerate cell measuring NaN
      return static_cast<int>(std::min(count, budgetPerAxis));
    };

    return int3(axisCount(widths.x), axisCount(widths.y), axisCount(widths.z));
  }

  // Visits every pair of atoms close enough to be worth measuring, each pair once, lower index first.
  // The positions are normalized onto the grid, each component in [0,1). 'wrapAround' says whether
  // the far faces of the grid are neighbours, which they are for a periodic cell and are not for a
  // bounding box. False when the grid is too coarse to be built, leaving all pairs to the caller.
  template<typename PairHandler>
  bool enumerateNeighbourPairs(const std::vector<double3> &normalizedPositions,
                               int3 numberOfCells,
                               bool wrapAround,
                               PairHandler &&handlePair)
  {
    if(numberOfCells.x < 3 || numberOfCells.y < 3 || numberOfCells.z < 3) return false;

    const size_t totalNumberOfCells = static_cast<size_t>(numberOfCells.x) *
                                      static_cast<size_t>(numberOfCells.y) *
                                      static_cast<size_t>(numberOfCells.z);
    const size_t atomCount = normalizedPositions.size();

    // One singly-linked list of atoms per cell: 'head' names the first atom of a cell and 'next' the
    // one after it, so binning is a single pass and costs one int per atom.
    std::vector<int> head(totalNumberOfCells, -1);
    std::vector<int> next(atomCount, -1);

    auto axisIndex = [](double normalized, int count) -> int
    {
      const int index = static_cast<int>(normalized * static_cast<double>(count));
      return std::min(std::max(index, 0), count - 1);
    };

    for(size_t i = 0; i < atomCount; i++)
    {
      const int k1 = axisIndex(normalizedPositions[i].x, numberOfCells.x);
      const int k2 = axisIndex(normalizedPositions[i].y, numberOfCells.y);
      const int k3 = axisIndex(normalizedPositions[i].z, numberOfCells.z);

      const int cell = k1 + k2 * numberOfCells.x + k3 * numberOfCells.x * numberOfCells.y;
      next[i] = head[static_cast<size_t>(cell)];
      head[static_cast<size_t>(cell)] = static_cast<int>(i);
    }

    for(int k3 = 0; k3 < numberOfCells.z; k3++)
    {
      for(int k2 = 0; k2 < numberOfCells.y; k2++)
      {
        for(int k1 = 0; k1 < numberOfCells.x; k1++)
        {
          const int cellI = k1 + k2 * numberOfCells.x + k3 * numberOfCells.x * numberOfCells.y;
          if(head[static_cast<size_t>(cellI)] < 0) continue;

          for(const int (&offset)[3] : neighbourOffsets)
          {
            int o1 = k1 + offset[0];
            int o2 = k2 + offset[1];
            int o3 = k3 + offset[2];

            if(wrapAround)
            {
              o1 = (o1 + numberOfCells.x) % numberOfCells.x;
              o2 = (o2 + numberOfCells.y) % numberOfCells.y;
              o3 = (o3 + numberOfCells.z) % numberOfCells.z;
            }
            else if(o1 < 0 || o1 >= numberOfCells.x ||
                    o2 < 0 || o2 >= numberOfCells.y ||
                    o3 < 0 || o3 >= numberOfCells.z)
            {
              continue;
            }

            const int cellJ = o1 + o2 * numberOfCells.x + o3 * numberOfCells.x * numberOfCells.y;

            for(int i = head[static_cast<size_t>(cellI)]; i >= 0; i = next[static_cast<size_t>(i)])
            {
              for(int j = head[static_cast<size_t>(cellJ)]; j >= 0; j = next[static_cast<size_t>(j)])
              {
                // Within one cell only half the pairs, and never an atom against itself.
                if(cellI == cellJ && i >= j) continue;
                handlePair(static_cast<size_t>(std::min(i, j)), static_cast<size_t>(std::max(i, j)));
              }
            }
          }
        }
      }
    }
    return true;
  }

  template<typename PairHandler>
  void enumerateAllPairs(size_t atomCount, PairHandler &&handlePair)
  {
    for(size_t i = 0; i < atomCount; i++)
    {
      for(size_t j = i + 1; j < atomCount; j++)
      {
        handlePair(i, j);
      }
    }
  }

  // A bond to an atom that turned out to be a duplicate is a bond drawn twice, so it is dropped.
  std::vector<std::shared_ptr<SKBond>> withoutDuplicateAtoms(const std::vector<std::shared_ptr<SKBond>> &bonds)
  {
    std::vector<std::shared_ptr<SKBond>> filteredBonds;
    std::copy_if(bonds.begin(), bonds.end(), std::back_inserter(filteredBonds),
                 [](std::shared_ptr<SKBond> bond)
                 {
                   return bond->atom1()->type() == SKAtomCopy::AtomCopyType::copy &&
                          bond->atom2()->type() == SKAtomCopy::AtomCopyType::copy;
                 });
    return filteredBonds;
  }

  // What the pair test needs to know about an atom, read off the tree once and held in flat arrays.
  // Reaching through SKAtomCopy::parent() instead costs a weak_ptr lock — an atomic compare-exchange,
  // and another interlocked decrement when the temporary dies — and the pair test runs millions of
  // times per structure to read two numbers that never change while it does.
  struct AtomProperties
  {
    std::vector<double> covalentRadius;
    std::vector<double> occupancy;
    std::vector<int64_t> asymmetricIndex;
    std::vector<char> hasParent;
  };

  AtomProperties readAtomProperties(const std::vector<std::shared_ptr<SKAtomCopy>> &atoms)
  {
    AtomProperties properties;
    const size_t atomCount = atoms.size();
    properties.covalentRadius.resize(atomCount, 0.0);
    properties.occupancy.resize(atomCount, 1.0);
    properties.asymmetricIndex.resize(atomCount, -1);
    properties.hasParent.resize(atomCount, 0);

    for(size_t i = 0; i < atomCount; i++)
    {
      const std::shared_ptr<SKAsymmetricAtom> parent = atoms[i]->parent();
      if(!parent) continue;
      properties.hasParent[i] = 1;
      properties.covalentRadius[i] =
        PredefinedElements::predefinedElements[parent->elementIdentifier()]._covalentRadius;
      properties.occupancy[i] = parent->occupancy();
      properties.asymmetricIndex[i] = atoms[i]->asymmetricIndex();
    }
    return properties;
  }
}

std::vector<std::shared_ptr<SKBond>> SKBondGenerator::bonds(const std::vector<std::shared_ptr<SKAtomCopy>> &atoms,
                                                            const std::vector<double3> &positions)
{
  for(const std::shared_ptr<SKAtomCopy> &atom : atoms)
  {
    atom->setType(SKAtomCopy::AtomCopyType::copy);
  }

  const AtomProperties properties = readAtomProperties(atoms);

  std::vector<std::shared_ptr<SKBond>> bonds;

  // Always the lower index first, as the all-pairs sweep has it, so which atom of a coincident pair
  // is called the duplicate does not depend on the order the cells happen to be visited in.
  auto testPair = [&](size_t i, size_t j)
  {
    if(!properties.hasParent[i] || !properties.hasParent[j]) return;

    const double occupancyA = properties.occupancy[i];
    const double occupancyB = properties.occupancy[j];
    if(!((occupancyA == 1.0 && occupancyB == 1.0) ||
         (occupancyA < 1.0 && occupancyB < 1.0))) return;

    const double bondLength = (positions[i] - positions[j]).length();
    if(bondLength < properties.covalentRadius[i] + properties.covalentRadius[j] + bondLengthTolerance)
    {
      if(bondLength < duplicateDistance)
      {
        atoms[i]->setType(SKAtomCopy::AtomCopyType::duplicate);
      }
      bonds.push_back(std::make_shared<SKBond>(atoms[i], atoms[j]));
    }
  };

  // The grid spans the atoms themselves. The padding keeps an atom on the far face inside the last
  // cell rather than one past it.
  double3 minimumPosition = positions.empty() ? double3(0.0, 0.0, 0.0) : positions.front();
  double3 maximumPosition = minimumPosition;
  for(const double3 &position : positions)
  {
    minimumPosition = double3(std::min(minimumPosition.x, position.x),
                              std::min(minimumPosition.y, position.y),
                              std::min(minimumPosition.z, position.z));
    maximumPosition = double3(std::max(maximumPosition.x, position.x),
                              std::max(maximumPosition.y, position.y),
                              std::max(maximumPosition.z, position.z));
  }
  const double3 gridWidths = double3(maximumPosition.x - minimumPosition.x + 0.1,
                                     maximumPosition.y - minimumPosition.y + 0.1,
                                     maximumPosition.z - minimumPosition.z + 0.1);

  const int3 numberOfCells = cellsPerAxis(gridWidths, longestPossibleBond(properties.covalentRadius), atoms.size());

  std::vector<double3> normalizedPositions;
  normalizedPositions.reserve(positions.size());
  for(const double3 &position : positions)
  {
    normalizedPositions.push_back(double3((position.x - minimumPosition.x) / gridWidths.x,
                                          (position.y - minimumPosition.y) / gridWidths.y,
                                          (position.z - minimumPosition.z) / gridWidths.z));
  }

  if(!enumerateNeighbourPairs(normalizedPositions, numberOfCells, false, testPair))
  {
    enumerateAllPairs(atoms.size(), testPair);
  }

  return withoutDuplicateAtoms(bonds);
}

std::vector<std::shared_ptr<SKBond>> SKBondGenerator::periodicBonds(const std::vector<std::shared_ptr<SKAtomCopy>> &atoms,
                                                                    const std::vector<double3> &positions,
                                                                    std::shared_ptr<SKCell> cell)
{
  for(const std::shared_ptr<SKAtomCopy> &atom : atoms)
  {
    atom->setType(SKAtomCopy::AtomCopyType::copy);
  }

  const AtomProperties properties = readAtomProperties(atoms);

  std::vector<std::shared_ptr<SKBond>> bonds;

  auto testPair = [&](size_t i, size_t j)
  {
    if(!properties.hasParent[i] || !properties.hasParent[j]) return;

    const double3 separationVector = positions[i] - positions[j];
    const double3 periodicSeparationVector = cell->applyUnitCellBoundaryCondition(separationVector);
    const double bondCriteria =
      properties.covalentRadius[i] + properties.covalentRadius[j] + bondLengthTolerance;
    const double bondLength = periodicSeparationVector.length();

    if(bondLength < bondCriteria)
    {
      if(bondLength < duplicateDistance)
      {
        // a duplicate when: (a) both occupancies are 1.0, or (b) when they are the same asymmetric type
        if(!(properties.occupancy[i] < 1.0 || properties.occupancy[j] < 1.0) ||
           properties.asymmetricIndex[i] == properties.asymmetricIndex[j])
        {
          atoms[i]->setType(SKAtomCopy::AtomCopyType::duplicate);
        }
      }
      // The partner is only reachable across the boundary when it is out of range where it stands.
      const SKBond::BoundaryType boundaryType = separationVector.length() > bondCriteria
                                                ? SKBond::BoundaryType::external
                                                : SKBond::BoundaryType::internal;
      bonds.push_back(std::make_shared<SKBond>(atoms[i], atoms[j], boundaryType));
    }
  };

  // The grid is laid over the unit cell in fractional coordinates, so its cells wrap the way the
  // minimum-image convention does and a partner across the boundary lands in a neighbouring cell.
  // Cells are counted along each axis by the width perpendicular to the other two, which is the
  // distance that has to cover a bond however sheared the cell is.
  const int3 numberOfCells = cellsPerAxis(cell->perpendicularWidths(),
                                          longestPossibleBond(properties.covalentRadius), atoms.size());

  std::vector<double3> normalizedPositions;
  normalizedPositions.reserve(positions.size());
  for(const double3 &position : positions)
  {
    normalizedPositions.push_back(double3::fract(cell->convertToFractional(position)));
  }

  if(!enumerateNeighbourPairs(normalizedPositions, numberOfCells, true, testPair))
  {
    enumerateAllPairs(atoms.size(), testPair);
  }

  return withoutDuplicateAtoms(bonds);
}
