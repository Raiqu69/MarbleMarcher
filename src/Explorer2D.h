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
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <vector>

// Signed fixed-point big number: 384 bits = 32 integer + 352 fractional.
// Enough precision for Mandelbrot zooms down to ~1e-100. Only add and
// multiply are needed (navigation is add-only, the reference orbit is z^2+c).
struct BigFixed {
  static const int LIMBS = 12;   // 12 x 32 bit, little-endian magnitude
  std::uint32_t m[LIMBS];
  int sign;                      // -1, 0, +1

  BigFixed();
  static BigFixed FromDouble(double v);
  double ToDouble() const;
  BigFixed Neg() const;
  static BigFixed Add(const BigFixed& a, const BigFixed& b);
  static BigFixed Mul(const BigFixed& a, const BigFixed& b);
};

// Flat 2D fractal explorer: pan, zoom-to-cursor, palettes, live Julia c.
// Mandelbrot supports perturbation deep zoom (reference orbit on the CPU).
class Explorer2D {
public:
  Explorer2D();
  bool Load(float res_x, float res_y);
  void SetKind(int k);
  void ResetView();
  void CyclePalette() { palette = (palette + 1) % 4; }
  void ToggleJuliaLive() { julia_live = !julia_live; }

  // mx/my are window fractions in [0,1]; pan_x/pan_y in [-1,1] from WASD;
  // zoom_dir in [-1,1] from Q/E keys (smooth keyboard zoom)
  void Update(float mx_frac, float my_frac, bool lmb, float wheel, float pan_x, float pan_y,
              float zoom_dir);
  void Write();

  // 0 = float, 1 = emulated double, 2 = perturbation
  int PrecisionMode() const;

  sf::Shader& GetShader() { return shader; }
  int    GetKind() const { return kind; }
  int    GetPalette() const { return palette; }
  double GetMagnification() const { return base_zoom / zoom; }
  int    Iters() const;
  bool   JuliaLive() const { return julia_live; }
  float  JuliaCX() const { return jc_x; }
  float  JuliaCY() const { return jc_y; }

private:
  void MoveCenter(double dx, double dy);
  void SetCenter(double x, double y);
  void ComputeReference();
  void ComputeIfsSkip();

  sf::Shader shader;
  int    kind;
  int    palette;
  // View center: BigFixed is the source of truth (deep zoom), the doubles
  // are a mirror used by the shallow render paths.
  BigFixed hp_cx, hp_cy;
  double cx, cy;
  double zoom;            // fractal units per half screen-height
  double base_zoom;       // initial zoom of the current kind
  bool   julia_live;
  float  jc_x, jc_y;
  bool   have_prev;
  double prev_ux, prev_uy;
  float  res_x, res_y;

  // Perturbation reference orbits (Mandelbrot & Julia deep zoom)
  bool   ref_dirty;
  int    ref_count;     // primary orbit (view center)
  int    ref_count2;    // secondary orbit (Julia critical orbit)
  std::vector<std::uint8_t> ref_pixels;
  sf::Texture ref_tex;

  // CPU-assisted IFS deep zoom (Sierpinski triangle, carpet & Koch)
  double ifs_sx, ifs_sy;  // center after the skipped fold iterations
  double ifs_scale;       // zoom scaled up by the skipped iterations
  int    ifs_skip;        // number of skipped iterations
  // Koch also carries the accumulated orthogonal fold frame (its folds reflect,
  // unlike Sierpinski's translate-only folds) and a gate: the branch-dependent
  // pre-transform can only be pre-applied when the screen stays within one branch.
  double koch_mat[4];     // 2x2 frame R applied to the on-screen delta
  bool   koch_deep;       // true = residual path active (pre-transform pre-applied)
};
