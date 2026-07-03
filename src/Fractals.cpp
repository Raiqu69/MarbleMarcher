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
#include "Fractals.h"
#include <vector>
#include <string>

static const float pi = 3.14159265359f;

static FractalParams MakeParams(float s, float a1, float a2,
                                float sx, float sy, float sz,
                                float r, float g, float b) {
  FractalParams p;
  p << s, a1, a2, sx, sy, sz, r, g, b;
  return p;
}

static FractalDef MakeDef(const char* name, const char* desc, int sel,
                          const FractalParams& params,
                          const Eigen::Vector3f& cam, float lx, float ly) {
  FractalDef d;
  d.name = name; d.desc = desc; d.shader_sel = sel;
  d.params = params; d.cam_pos = cam; d.look_x = lx; d.look_y = ly;
  return d;
}

static const std::vector<FractalDef>& Fractals3D() {
  static std::vector<FractalDef> v = [] {
    std::vector<FractalDef> f;
    f.push_back(MakeDef("Sponge Classic", "The game's fractal - beautiful and familiar",
      SEL_GAME, MakeParams(1.6f, 2.0f, pi, -4.0f, -1.0f, -1.0f, 0.2f, 0.7f, 1.0f),
      Eigen::Vector3f(0.0f, 2.5f, 8.0f), 0.0f, -0.2f));
    f.push_back(MakeDef("Twisted Sponge", "Rotational Menger variant",
      SEL_GAME, MakeParams(1.9f, 1.3f, 0.9f, -1.5f, -0.5f, -0.5f, 0.9f, 0.2f, 0.5f),
      Eigen::Vector3f(0.0f, 2.0f, 7.0f), 0.0f, -0.2f));
    f.push_back(MakeDef("Sierpinski Pyramid", "Triangular IFS pyramid",
      SEL_SIERPINSKI, MakeParams(2.0f, 0.0f, 0.0f, -1.0f, -1.0f, -1.0f, 1.0f, 0.5f, 0.1f),
      Eigen::Vector3f(0.0f, 1.5f, 5.0f), 0.0f, -0.2f));
    f.push_back(MakeDef("Mandelbox", "Box fold + sphere inversion  (scale -2)",
      SEL_MANDELBOX, MakeParams(-2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.3f, 0.9f),
      Eigen::Vector3f(0.0f, 2.0f, 9.0f), 0.0f, -0.15f));
    f.push_back(MakeDef("Octahedral Fractal", "Six-fold symmetric angular IFS",
      SEL_OCTAHEDRAL, MakeParams(2.5f, 0.0f, 0.0f, -1.5f, -1.5f, -1.5f, 1.0f, 0.8f, 0.2f),
      Eigen::Vector3f(0.0f, 1.0f, 4.0f), 0.0f, -0.2f));
    f.push_back(MakeDef("Quaternion Julia", "3D slice of z^2+c in quaternion space",
      SEL_QUAT_JULIA, MakeParams(1.0f, 0.2f, 0.0f, -0.2f, 0.6f, 0.2f, 0.4f, 0.7f, 1.0f),
      Eigen::Vector3f(0.0f, 0.3f, 3.5f), 0.0f, 0.0f));
    f.push_back(MakeDef("Mandelbox Julia", "Mandelbox with fixed Julia parameter c",
      SEL_MBOX_JULIA, MakeParams(-2.0f, 0.0f, 0.0f, -1.5f, -0.1f, 0.3f, 1.0f, 0.4f, 0.2f),
      Eigen::Vector3f(0.0f, 0.5f, 5.0f), 0.0f, 0.0f));
    f.push_back(MakeDef("Mandelbulb", "Power-8 bulb - z = z^n + c in spherical coords",
      SEL_MANDELBULB, MakeParams(8.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.5f, 0.2f),
      Eigen::Vector3f(0.0f, 0.3f, 2.5f), 0.0f, 0.0f));
    f.push_back(MakeDef("Juliabulb", "Mandelbulb with fixed Julia parameter c",
      SEL_JULIABULB, MakeParams(8.0f, 0.0f, 0.0f, -0.2f, 0.8f, 0.0f, 1.0f, 0.6f, 0.3f),
      Eigen::Vector3f(0.0f, 0.3f, 2.5f), 0.0f, 0.0f));
    f.push_back(MakeDef("Icosahedral IFS", "Icosahedral symmetry kaleidoscopic IFS",
      SEL_ICOSA, MakeParams(1.618f, 0.0f, 0.0f, -1.0f, -1.0f, -1.0f, 0.8f, 0.5f, 1.0f),
      Eigen::Vector3f(0.0f, 0.5f, 5.0f), 0.0f, 0.0f));
    f.push_back(MakeDef("Apollonian Gasket", "Infinite sphere packing - very organic",
      SEL_APOLLONIAN, MakeParams(1.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f, 0.6f, 0.3f),
      Eigen::Vector3f(0.0f, 1.8f, 0.0f), 0.0f, -0.9f));
    f.push_back(MakeDef("Pseudo-Kleinian", "Cave-like limit set structures",
      SEL_KLEINIAN, MakeParams(1.0f, 0.0f, 0.0f, 0.92436f, 0.90756f, 0.92436f, 0.5f, 0.8f, 1.0f),
      Eigen::Vector3f(0.0f, 0.8f, 0.0f), 0.0f, -0.3f));
    f.push_back(MakeDef("Menger-Mandelbox", "Hybrid fold - alien architecture",
      SEL_MENGER_MBOX, MakeParams(1.8f, 0.0f, 0.0f, -2.8f, -2.8f, -2.8f, 0.7f, 0.9f, 0.4f),
      Eigen::Vector3f(0.0f, 3.0f, 9.0f), 0.0f, -0.2f));
    // Non-fractal math objects. Param meaning differs (see frag.glsl):
    //   Tesseract: Angle1/2 = rotation speeds, ShiftX = 4D projection distance
    //   Mobius:    Angle1 = band width, Angle2 = band thickness
    //   Klein:     Angle1 = neck hook reach, Angle2 = tube radius
    f.push_back(MakeDef("Tesseract", "Rotating 4D hypercube - Angle1/2 spin speeds",
      SEL_TESSERACT, MakeParams(1.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.4f, 0.8f, 1.0f),
      Eigen::Vector3f(0.0f, 0.5f, 7.0f), 0.0f, -0.05f));
    f.push_back(MakeDef("Mobius Strip", "One-sided band - Angle1 width, Angle2 thickness",
      SEL_MOBIUS, MakeParams(1.5f, 0.35f, 0.06f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.8f),
      Eigen::Vector3f(0.0f, 1.5f, 4.5f), 0.0f, -0.3f));
    f.push_back(MakeDef("Klein Bottle", "Non-orientable surface - Angle1 hook, Angle2 tube",
      SEL_KLEIN, MakeParams(1.3f, 0.6f, 0.14f, 0.0f, 0.0f, 0.0f, 0.6f, 0.9f, 0.5f),
      Eigen::Vector3f(0.0f, 0.3f, 4.5f), 0.0f, -0.05f));
    return f;
  }();
  return v;
}

static const std::vector<FractalDef>& FractalsLevels() {
  static std::vector<std::string> descs;
  static std::vector<FractalDef> v = [] {
    std::vector<FractalDef> f;
    descs.reserve(num_levels);
    for (int i = 0; i < num_levels; ++i) {
      descs.push_back("Game level " + std::to_string(i + 1));
      f.push_back(MakeDef(all_levels[i].txt, descs.back().c_str(), SEL_GAME,
        all_levels[i].params, all_levels[i].start_pos * 1.3f,
        all_levels[i].start_look_x, -0.3f));
    }
    return f;
  }();
  return v;
}

static const char* NAMES_2D[NUM_2D_KINDS] = {
  "Mandelbrot", "Burning Ship", "Tricorn", "Newton z^3-1", "Julia Set",
  "Sierpinski Triangle", "Sierpinski Carpet", "Koch Snowflake"
};
static const char* DESCS_2D[NUM_2D_KINDS] = {
  "The classic - deep zoom down to 10^13",
  "Fiery ships from |Re|,|Im| - deep zoom ready",
  "The anti-Mandelbrot - conjugate before squaring",
  "Root basins of z^3-1 in red, green and blue",
  "z^2+c with live c - move the mouse, Space to freeze",
  "The classic gasket - endless triangular lace",
  "The Menger sponge's face - holes all the way down",
  "A curve of infinite length around a finite area"
};

int NumFractals(int category) {
  if (category == CAT_3D)     { return (int)Fractals3D().size(); }
  if (category == CAT_LEVELS) { return num_levels; }
  if (category == CAT_2D)     { return NUM_2D_KINDS; }
  return 0;
}

const FractalDef& GetFractal(int category, int index) {
  if (category == CAT_LEVELS) { return FractalsLevels()[index]; }
  return Fractals3D()[index];
}

const char* FractalName(int category, int index) {
  if (category == CAT_2D) { return NAMES_2D[index]; }
  return GetFractal(category, index).name;
}

const char* FractalDesc(int category, int index) {
  if (category == CAT_2D) { return DESCS_2D[index]; }
  return GetFractal(category, index).desc;
}
