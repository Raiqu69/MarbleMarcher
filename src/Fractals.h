/* This file is part of the Marble Marcher (https://github.com/HackerPoet/MarbleMarcher).
* Copyright(C) 2018 CodeParade
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include "Level.h"
#include <Eigen/Dense>

// Compile-time DE selectors. Must match the #if FRACTAL_TYPE_SEL chain in frag.glsl.
enum ShaderSel {
  SEL_GAME = 0,        // de_fractal   (all game levels + sponge variants)
  SEL_SIERPINSKI,      // de_sierpinski_tet
  SEL_MANDELBOX,       // de_mandelbox
  SEL_OCTAHEDRAL,      // de_octahedral
  SEL_QUAT_JULIA,      // de_quaternion_julia
  SEL_MBOX_JULIA,      // de_mandelbox_julia
  SEL_MANDELBULB,      // de_mandelbulb
  SEL_ICOSA,           // de_icosahedral
  SEL_APOLLONIAN,      // de_apollonian
  SEL_KLEINIAN,        // de_pseudo_kleinian
  SEL_JULIABULB,       // de_juliabulb
  SEL_MENGER_MBOX,     // de_menger_mandelbox
  SEL_TESSERACT,       // de_tesseract   (rotating 4D hypercube, projected)
  SEL_MOBIUS,          // de_mobius      (Möbius strip)
  SEL_KLEIN            // de_klein       (Klein bottle)
};

// One entry in the fractal browser (3D formulas and game levels).
struct FractalDef {
  const char*     name;
  const char*     desc;
  int             shader_sel;
  FractalParams   params;
  Eigen::Vector3f cam_pos;
  float           look_x;
  float           look_y;
};

// Browser categories
enum FracCategory {
  CAT_3D     = 0,
  CAT_LEVELS = 1,
  CAT_2D     = 2,
  NUM_FRAC_CATEGORIES = 3
};

// 2D explorer kinds (indices within CAT_2D)
enum Explorer2DKind {
  KIND_MANDELBROT = 0,
  KIND_BURNING_SHIP,
  KIND_TRICORN,
  KIND_NEWTON,
  KIND_JULIA,
  KIND_SIERPINSKI_TRI,
  KIND_CARPET,
  KIND_KOCH,
  NUM_2D_KINDS
};

int NumFractals(int category);
const FractalDef& GetFractal(int category, int index);  // CAT_3D and CAT_LEVELS only
const char* FractalName(int category, int index);       // all categories
const char* FractalDesc(int category, int index);       // all categories
