/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <iostream>
#include <cstdint>

enum class Symmorphicity: int64_t
{
  asymmorphic = 0, symmorphic = 1, hemisymmorphic = 2
};

enum class Centring: int64_t
{
  none = 0, primitive = 1, body = 2, a_face = 3, b_face = 4, c_face = 5, face = 6, base = 7, r = 8, h = 9, d = 10
};

enum class Holohedry: int64_t
{
  none = 0, triclinic = 1, monoclinic = 2, orthorhombic = 3, tetragonal = 4, trigonal = 5, hexagonal = 6, cubic = 7
};
