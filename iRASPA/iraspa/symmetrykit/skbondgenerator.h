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

#include <memory>
#include <vector>
#include <mathkit.h>
#include "skatomcopy.h"
#include "skbond.h"
#include "skcell.h"

// Which atoms are bonded, asked of a whole structure at once. Every structure type wants the same
// thing of its atoms — the pairs closer than the sum of their covalent radii — and answering it by
// testing all pairs is quadratic in the atom count, which a protein has far too many of. The atoms
// are binned into a grid of cells no narrower than the longest bond the structure's own elements
// could make, so each atom is compared against the fourteen cells that could hold a partner and no
// others. The pairs that come out are exactly the pairs the all-pairs sweep would have found: the
// grid only decides which comparisons are worth making, never which of them count as a bond.
//
// A structure too small to hold three cells along some axis is swept instead. There is nothing to
// gain from a grid that coarse, and below three cells the wrap-around that lets a periodic cell find
// its partners across the boundary would visit some pairs twice.
//
// Both entry points set every atom's copy type before they start and mark as duplicates the ones
// found sitting on top of another, which is what the returned bonds have already been filtered on.
struct SKBondGenerator
{
  // Cartesian positions with no periodic images: a molecule, or a protein read from a file that
  // carries no cell. Atoms pair only when their occupancies are either both whole or both partial.
  static std::vector<std::shared_ptr<SKBond>> bonds(const std::vector<std::shared_ptr<SKAtomCopy>> &atoms,
                                                    const std::vector<double3> &positions);

  // Cartesian positions under the minimum-image convention of the cell, which is what lets a bond be
  // 'external': one whose partner is reached only by stepping into the neighbouring unit cell.
  static std::vector<std::shared_ptr<SKBond>> periodicBonds(const std::vector<std::shared_ptr<SKAtomCopy>> &atoms,
                                                            const std::vector<double3> &positions,
                                                            std::shared_ptr<SKCell> cell);
};
