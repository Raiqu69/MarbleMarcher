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
#include "Scene.h"
#include "Scores.h"
#include "Res.h"
#include <iostream>

static const float pi = 3.14159265359f;
static const float ground_force = 0.008f;
static const float air_force = 0.004f;
static const float ground_friction = 0.99f;
static const float air_friction = 0.995f;
static const float orbit_speed = 0.005f;
static const int max_marches = 10;
static const int num_phys_steps = 6;
static const float marble_bounce = 1.2f; //Range 1.0 to 2.0
static const float orbit_smooth = 0.995f;
static const float zoom_smooth = 0.85f;
static const float look_smooth = 0.75f;
static const float look_smooth_free_camera = 0.9f;
static const int frame_transition = 400;
static const int frame_orbit = 600;
static const int frame_deorbit = 800;
static const int frame_countdown = frame_deorbit + 3*60;
static const float default_zoom = 15.0f;
static const int fractal_iters = 16;
static const float gravity = 0.005f;
static const float ground_ratio = 1.15f;
static const int mus_switches[num_level_music] = {9, 15, 21, 24};
static const int num_levels_midpoint = 15;

// Free-fly speed levels (key 1 = fastest, key 9 = slowest).
// Key 0 = AUTO: speed adapts to the distance from the fractal surface.
// Each step ÷3 → level 9 is ~6500× slower than level 1 (gottlos langsam)
static const float FLY_SPEEDS[10] = {
    0.0f,      // 0: unused (auto mode)
    1.30e-3f,  // 1: fastest
    4.33e-4f,  // 2
    1.44e-4f,  // 3
    4.81e-5f,  // 4
    1.60e-5f,  // 5
    5.34e-6f,  // 6
    1.78e-6f,  // 7
    5.93e-7f,  // 8
    1.98e-7f,  // 9: gottlos langsam
};
// Auto mode: speed = DE * factor, clamped. Sprint still multiplies on top.
static const float auto_speed_factor = 0.025f;
static const float auto_speed_min    = 2e-7f;
static const float auto_speed_max    = 0.04f;

static void ModPi(float& a, float b) {
  if (a - b > pi) {
    a -= 2 * pi;
  } else if (a - b < -pi) {
    a += 2 * pi;
  }
}

Scene::Scene(sf::Music* level_music) :
  intro_needs_snap(true),
  play_single(false),
  is_fullrun(false),
  exposure(1.0f),
  cam_mat(Eigen::Matrix4f::Identity()),
  cam_look_x(0.0f),
  cam_look_y(0.0f),
  cam_dist(default_zoom),
  cam_pos(0.0f, 0.0f, 0.0f),
  cam_mode(CamMode::INTRO),
  marble_rad(1.0f),
  marble_pos(0.0f, 0.0f, 0.0f),
  marble_vel(0.0f, 0.0f, 0.0f),
  marble_mat(Eigen::Matrix3f::Identity()),
  flag_pos(0.0f, 0.0f, 0.0f),
  timer(0),
  sum_time(0),
  music(level_music),
  cur_level(0),
  fractal_type(0),
  shader_dirty(false),
  fly_speed_level(0),
  fly_iters(24),
  fly_vel(0.0f, 0.0f, 0.0f),
  fly_fov(90.0f),
  fly_fov_vel(0.0f),
  drift_on(false),
  drift_t(0.0f),
  anim_t(0.0f)
{
  ResetCheats();
  frac_params.setOnes();
  frac_params_smooth.setOnes();
  SnapCamera();
  (void)buff_goal.loadFromFile(goal_wav);
  sound_goal.emplace(buff_goal);
  (void)buff_bounce1.loadFromFile(bounce1_wav);
  sound_bounce1.emplace(buff_bounce1);
  (void)buff_bounce2.loadFromFile(bounce2_wav);
  sound_bounce2.emplace(buff_bounce2);
  (void)buff_bounce3.loadFromFile(bounce3_wav);
  sound_bounce3.emplace(buff_bounce3);
  (void)buff_shatter.loadFromFile(shatter_wav);
  sound_shatter.emplace(buff_shatter);
}

void Scene::LoadLevel(int level) {
  SetLevel(level);
  marble_pos = level_copy.start_pos;
  marble_rad = level_copy.marble_rad;
  flag_pos = level_copy.end_pos;
  cam_look_x = level_copy.start_look_x;
}

void Scene::SetMarble(float x, float y, float z, float r) {
  marble_rad = r;
  marble_pos = Eigen::Vector3f(x, y, z);
  marble_vel.setZero();
}

void Scene::SetFlag(float x, float y, float z) {
  flag_pos = Eigen::Vector3f(x, y, z);
}

void Scene::SetLevel(int level) {
  cur_level = level;
  level_copy = all_levels[level];
}

void Scene::SetMode(CamMode mode) {
  //Don't reset the timer if transitioning to screen saver
  if ((cam_mode == INTRO && mode == SCREEN_SAVER) ||
      (cam_mode == SCREEN_SAVER && mode == INTRO)) {
  } else {
    timer = 0;
    intro_needs_snap = true;
  }
  if (mode == SCREEN_SAVER) {
    cam_dist = 10.0f;
    cam_dist_smooth = 10.0f;
  }
  if (mode == FREE_FLY) {
    fly_speed_level = 0;  // auto speed
    fly_vel.setZero();
    fly_fov = 90.0f;
    fly_fov_vel = 0.0f;
    cam_pos = cam_pos_smooth;
    SnapCamera();
  } else {
    if (fractal_type != SEL_GAME) {
      fractal_type = SEL_GAME;
      shader_dirty = true;
    }
    drift_on = false;
  }
  cam_mode = mode;
}

void Scene::SetFractal(const FractalDef& def) {
  if (fractal_type != def.shader_sel) {
    fractal_type = def.shader_sel;
    shader_dirty = true;
  }
  frac_params = def.params;
  frac_params_smooth = frac_params;
  cam_pos = def.cam_pos;
  cam_look_x = def.look_x;
  cam_look_y = def.look_y;
  drift_on = false;
  drift_t = 0.0f;
  SnapCamera();
}

//Live parameter editor: nudge one of the 9 fractal parameters
void Scene::AdjustParam(int idx, float dir) {
  if (idx < 0 || idx >= num_fractal_params) { return; }
  static const float steps[num_fractal_params] = {
    0.003f,                    // scale
    0.006f, 0.006f,            // angles
    0.010f, 0.010f, 0.010f,    // shift
    0.008f, 0.008f, 0.008f     // color
  };
  frac_params[idx] += dir * steps[idx];
  if (idx >= 6) {  // colors stay in [0,1]
    frac_params[idx] = std::min(std::max(frac_params[idx], 0.0f), 1.0f);
  }
}

void Scene::SetFlySpeedLevel(int level) {
  if (level >= 0 && level <= 9) {
    fly_speed_level = level;
  }
}

int Scene::GetCountdownTime() const {
  if (cam_mode == DEORBIT && timer >= frame_deorbit) {
    return timer - frame_deorbit;
  } else if (cam_mode == MARBLE) {
    return timer + 3*60;
  } else if (cam_mode == GOAL) {
    return final_time + 3*60;
  } else {
    return -1;
  }
}

sf::Vector3f Scene::GetGoalDirection() const {
  Eigen::Vector3f goal_delta = marble_mat.transpose() * (flag_pos - marble_pos);
  goal_delta.y() = 0.0f;
  const float goal_dir = std::atan2(-goal_delta.z(), goal_delta.x());
  const float a = cam_look_x - goal_dir;
  const float b = std::abs(cam_look_y * 2.0f / pi);
  const float d = goal_delta.norm() / marble_rad;
  return sf::Vector3f(a, b, d);
}

sf::Music& Scene::GetCurMusic() const {
  for (int i = 0; i < num_level_music; ++i) {
    if (cur_level < mus_switches[i]) {
      return music[i];
    }
  }
  return music[0];
}

void Scene::StopAllMusic() {
  for (int i = 0; i < num_level_music; ++i) {
    music[i].stop();
  }
}

bool Scene::IsHighScore() const {
  if (cam_mode != GOAL) {
    return false;
  } else {
    return final_time == high_scores.Get(cur_level);
  }
}

void Scene::StartNewGame() {
  sum_time = 0;
  play_single = false;
  ResetCheats();
  SetLevel(high_scores.GetStartLevel());
  is_fullrun = high_scores.HasCompleted(num_levels - 1);
  HideObjects();
  SetMode(ORBIT);
}

void Scene::StartNextLevel() {
  if (play_single) {
    cam_mode = MARBLE;
    ResetLevel();
  } else if (cur_level + 1 == num_levels_midpoint && cam_mode != MIDPOINT) {
    cam_mode = MIDPOINT;
  } else if (cur_level + 1 >= num_levels) {
    cam_mode = FINAL;
  } else {
    SetLevel(cur_level + 1);
    HideObjects();
    SetMode(ORBIT);
    for (int i = 0; i < num_level_music; ++i) {
      if (cur_level == mus_switches[i]) {
        StopAllMusic();
        GetCurMusic().play();
      }
    }
  }
}

void Scene::StartSingle(int level) {
  play_single = true;
  is_fullrun = false;
  ResetCheats();
  SetLevel(level);
  HideObjects();
  SetMode(ORBIT);
}

void Scene::ResetLevel() {
  if (cam_mode == MARBLE || play_single) {
    SetMode(DEORBIT);
    timer = frame_deorbit;
    frac_params = level_copy.params;
    frac_params_smooth = frac_params;
    marble_pos = level_copy.start_pos;
    marble_vel.setZero();
    marble_rad = level_copy.marble_rad;
    marble_mat.setIdentity();
    flag_pos = level_copy.end_pos;
    cam_look_x = level_copy.start_look_x;
    cam_look_x_smooth = cam_look_x;
    cam_pos = cam_pos_smooth;
    cam_dist = default_zoom;
    cam_dist_smooth = cam_dist;
    cam_look_y = -0.3f;
    cam_look_y_smooth = cam_look_y;
  }
}

void Scene::ResetCheats() {
  enable_cheats = false;
  free_camera = false;
  gravity_type = 0;
  param_mod = -1;
  ignore_goal = false;
  hyper_speed = false;
  disable_motion = false;
  zoom_to_scale = false;
}

void Scene::UpdateCamera(float dx, float dy, float dz, bool speedup) {
  //Camera update depends on current mode
  const int iters = speedup ? 5 : 1;
  if (cam_mode == INTRO) {
    UpdateIntro(false);
  } else if (cam_mode == SCREEN_SAVER) {
    UpdateScreenSaver(dx, dy, dz);
  } else if (cam_mode == ORBIT) {
    for (int i = 0; i < iters; i++) {
      UpdateOrbit();
      if (cam_mode != ORBIT) {
        break;
      }
    }
  } else if (cam_mode == DEORBIT) {
    for (int i = 0; i < iters; i++) {
      UpdateDeOrbit(dx, dy, dz);
      if (cam_mode != DEORBIT) {
        break;
      }
    }
  } else if (cam_mode == MARBLE) {
    UpdateNormal(dx, dy, dz);
  } else if (cam_mode == GOAL || cam_mode == FINAL || cam_mode == MIDPOINT) {
    for (int i = 0; i < iters; i++) {
      UpdateGoal();
      if (cam_mode != GOAL) {
        break;
      }
    }
  }
}

void Scene::UpdateMarble(float dx, float dy) {
  //Ignore other modes
  if (cam_mode != MARBLE) {
    return;
  }

  //Normalize force if too big
  const float mag2 = dx*dx + dy*dy;
  if (mag2 > 1.0f) {
    const float mag = std::sqrt(mag2);
    dx /= mag;
    dy /= mag;
  }

  if (free_camera) {
    cam_pos += cam_mat.block<3,1>(0,2) * (-marble_rad * dy * 0.5f);
    cam_pos += cam_mat.block<3, 1>(0,0) * (marble_rad * dx * 0.5f);
    cam_pos_smooth = cam_pos_smooth*0.8f + cam_pos*0.2f;
  } else {
    //Apply all physics (gravity and collision)
    bool onGround = false;
    float max_delta_v = 0.0f;
    for (int i = 0; i < num_phys_steps; ++i) {
      float force = marble_rad * gravity / num_phys_steps;
      if (gravity_type == 1) { force *= 0.25f; } else if (gravity_type == 2) { force *= 4.0f; }
      if (level_copy.planet) {
        marble_vel -= marble_pos.normalized() * force;
      } else {
        marble_vel.y() -= force;
      }
      marble_pos += marble_vel / num_phys_steps;
      onGround |= MarbleCollision(max_delta_v);
    }

    //Play bounce sound if needed
    float bounce_delta_v = max_delta_v / marble_rad;
    if (bounce_delta_v > 0.5f) {
      sound_bounce1->play();
    } else if (bounce_delta_v > 0.25f) {
      sound_bounce2->play();
    } else if (bounce_delta_v > 0.1f) {
      sound_bounce3->setVolume(100.0f * (bounce_delta_v / 0.25f));
      sound_bounce3->play();
    }

    //Add force from keyboard
    float f = marble_rad * (onGround ? ground_force : air_force);
    if (hyper_speed) { f *= 4.0f; }
    const float cs = std::cos(cam_look_x);
    const float sn = std::sin(cam_look_x);
    const Eigen::Vector3f v(dx*cs - dy*sn, 0.0f, -dy*cs - dx*sn);
    marble_vel += (marble_mat * v) * f;

    //Apply friction
    marble_vel *= (onGround ? ground_friction : air_friction);
  }

  //Update animated fractals
  if (!disable_motion) {
    frac_params[1] = level_copy.params[1] + level_copy.anim_1 * std::sin(timer * 0.015f);
    frac_params[2] = level_copy.params[2] + level_copy.anim_2 * std::sin(timer * 0.015f);
    frac_params[4] = level_copy.params[4] + level_copy.anim_3 * std::sin(timer * 0.015f);
  }
  frac_params_smooth = frac_params;

  //Check if marble has hit flag post
  if (cam_mode != GOAL && !ignore_goal) {
    const bool flag_y_match = level_copy.planet ?
      marble_pos.y() <= flag_pos.y() && marble_pos.y() >= flag_pos.y() - 7*marble_rad :
      marble_pos.y() >= flag_pos.y() && marble_pos.y() <= flag_pos.y() + 7*marble_rad;
    if (flag_y_match) {
      const float fx = marble_pos.x() - flag_pos.x();
      const float fz = marble_pos.z() - flag_pos.z();
      if (fx*fx + fz*fz < 6 * marble_rad*marble_rad) {
        final_time = timer;
        if (!enable_cheats) {
          high_scores.Update(cur_level, final_time);
        }
        SetMode(GOAL);
        sound_goal->play();
      }
    }
  }

  //Check if marble passed the death barrier
  if (marble_pos.y() < (enable_cheats ? -999.0f : level_copy.kill_y)) {
    ResetLevel();
  }
}

void Scene::UpdateIntro(bool ssaver) {
  //Update the timer
  const float t = -2.0f + timer * 0.002f;
  timer += 1;

  //Get rotational parameters
  const float dist = (ssaver ? 10.0f : 8.0f);
  const Eigen::Vector3f orbit_pt(0.0f, 3.0f, 0.0f);
  const Eigen::Vector3f perp_vec(std::sin(t), 0.0f, std::cos(t));
  cam_pos = orbit_pt + perp_vec * dist;
  cam_pos_smooth = cam_pos_smooth*0.9f + cam_pos*0.1f;

  //Solve for the look direction
  cam_look_x = std::atan2(perp_vec.x(), perp_vec.z());
  if (!ssaver) { cam_look_x += 0.5f; }
  ModPi(cam_look_x_smooth, cam_look_x);
  cam_look_x_smooth = cam_look_x_smooth*0.9f + cam_look_x*0.1f;

  //Update look y
  cam_look_y = (ssaver ? -0.25f : -0.41f);
  cam_look_y_smooth = cam_look_y_smooth*0.9f + cam_look_y*0.1f;

  //Update the camera matrix
  marble_mat.setIdentity();
  MakeCameraRotation();
  cam_mat.block<3, 1>(0, 3) = cam_pos_smooth;

  //Update demo fractal
  frac_params[0] = 1.6f;
  frac_params[1] = 2.0f + 0.5f*std::cos(timer * 0.0021f);
  frac_params[2] = pi + 0.5f*std::cos(timer * 0.000287f);
  frac_params[3] = -4.0f + 0.5f*std::sin(timer * 0.00161f);
  frac_params[4] = -1.0f + 0.1f*std::sin(timer * 0.00123f);
  frac_params[5] = -1.0f + 0.1f*std::cos(timer * 0.00137f);
  frac_params[6] = -0.2f;
  frac_params[7] = -0.1f;
  frac_params[8] = -0.6f;
  frac_params_smooth = frac_params;

  //Make sure marble and flag are hidden
  HideObjects();

  if (intro_needs_snap) {
    SnapCamera();
    intro_needs_snap = false;
  }
}

void Scene::UpdateScreenSaver(float dx, float dy, float dz) {
  timer += 1;

  //Same fractal animation as the intro screen saver
  frac_params[0] = 1.6f;
  frac_params[1] = 2.0f + 0.5f*std::cos(timer * 0.0021f);
  frac_params[2] = pi + 0.5f*std::cos(timer * 0.000287f);
  frac_params[3] = -4.0f + 0.5f*std::sin(timer * 0.00161f);
  frac_params[4] = -1.0f + 0.1f*std::sin(timer * 0.00123f);
  frac_params[5] = -1.0f + 0.1f*std::cos(timer * 0.00137f);
  frac_params[6] = -0.2f;
  frac_params[7] = -0.1f;
  frac_params[8] = -0.6f;
  frac_params_smooth = frac_params;

  //Zoom with mouse wheel
  cam_dist *= std::pow(2.0f, -dz);
  cam_dist = std::min(std::max(cam_dist, 3.0f), 30.0f);
  cam_dist_smooth = cam_dist_smooth*zoom_smooth + cam_dist*(1.0f - zoom_smooth);

  //Rotate with mouse
  cam_look_x += dx;
  cam_look_y += dy;
  cam_look_y = std::min(std::max(cam_look_y, -pi/2.0f), pi/2.0f);
  while (cam_look_x >  pi) { cam_look_x -= 2.0f*pi; }
  while (cam_look_x < -pi) { cam_look_x += 2.0f*pi; }
  ModPi(cam_look_x_smooth, cam_look_x);
  cam_look_x_smooth = cam_look_x_smooth*look_smooth + cam_look_x*(1.0f - look_smooth);
  cam_look_y_smooth = cam_look_y_smooth*look_smooth + cam_look_y*(1.0f - look_smooth);

  //Build camera matrix and position camera around fractal center
  marble_mat.setIdentity();
  MakeCameraRotation();
  const Eigen::Vector3f look_target(0.0f, 3.0f, 0.0f);
  cam_pos = look_target + cam_mat.block<3,3>(0,0) * Eigen::Vector3f(0.0f, 0.0f, cam_dist_smooth);
  cam_pos_smooth = cam_pos;
  cam_mat.block<3,1>(0,3) = cam_pos_smooth;

  HideObjects();

  if (intro_needs_snap) {
    SnapCamera();
    intro_needs_snap = false;
  }
}

void Scene::UpdateFreeFlyCam(float look_dx, float look_dy, float move_lr, float move_ud, float move_fb, float sprint_mult, float zoom_delta) {
  static const float fly_friction   = 0.82f;

  //Advance the free-running clock (drives the 4D tesseract rotation)
  anim_t += 1.0f / 60.0f;

  //Fractal parameters: frozen, unless drift mode morphs them slowly
  if (drift_on) {
    drift_t += 1.0f / 60.0f;
    frac_params_smooth = frac_params;
    frac_params_smooth[1] += 0.10f * std::sin(drift_t * 0.31f);
    frac_params_smooth[2] += 0.10f * std::sin(drift_t * 0.23f + 1.7f);
    frac_params_smooth[3] += 0.05f * std::sin(drift_t * 0.17f + 0.5f);
    frac_params_smooth[4] += 0.05f * std::sin(drift_t * 0.13f + 2.1f);
    frac_params_smooth[5] += 0.05f * std::sin(drift_t * 0.11f + 4.2f);
  } else {
    frac_params_smooth = frac_params;
  }

  //Look direction — instantaneous response for FPS feel
  cam_look_x += look_dx;
  cam_look_y += look_dy;
  cam_look_y = std::min(std::max(cam_look_y, -pi/2.0f), pi/2.0f);
  while (cam_look_x >  pi) { cam_look_x -= 2.0f*pi; }
  while (cam_look_x < -pi) { cam_look_x += 2.0f*pi; }
  cam_look_x_smooth = cam_look_x;
  cam_look_y_smooth = cam_look_y;

  //Build camera matrix
  marble_mat.setIdentity();
  MakeCameraRotation();

  //Movement: accelerate toward target velocity, then friction
  Eigen::Vector3f move_input(move_lr, move_ud, -move_fb);
  if (move_input.norm() > 1.0f) { move_input.normalize(); }
  const Eigen::Vector3f move_world = cam_mat.block<3,3>(0,0) * move_input;
  //Distance to the fractal surface drives auto speed AND adaptive detail
  const float d_cam = std::max(DE_Fly(cam_pos), 1e-8f);

  //Raise fold iterations as the camera closes in, so new detail keeps
  //appearing the deeper you go (up to the float precision limit)
  fly_iters = std::min(48, std::max(24, 24 + (int)(8.0f * std::log10(0.02f / d_cam))));

  float base_speed;
  if (fly_speed_level == 0) {
    //AUTO: scale with distance to the fractal surface — slower the closer you get
    base_speed = std::min(std::max(d_cam * auto_speed_factor, auto_speed_min), auto_speed_max);
  } else {
    base_speed = FLY_SPEEDS[fly_speed_level];
  }
  const float speed = base_speed * sprint_mult;
  const Eigen::Vector3f target_vel = move_world * speed;
  fly_vel = fly_vel * fly_friction + target_vel * (1.0f - fly_friction);

  //Zoom: scroll wheel narrows/widens FOV (like a lens), range 5°–170°
  fly_fov_vel = fly_fov_vel * 0.80f - zoom_delta * 12.0f;
  fly_fov = std::max(5.0f, std::min(170.0f, fly_fov + fly_fov_vel));

  cam_pos += fly_vel;
  cam_pos_smooth = cam_pos;
  cam_mat.block<3,1>(0,3) = cam_pos_smooth;

  HideObjects();
}

void Scene::UpdateOrbit() {
  //Update the timer
  const float t = timer * orbit_speed;
  float a = std::min(float(timer) / float(frame_transition), 1.0f);
  a *= a/(2*a*(a - 1) + 1);
  timer += 1;
  sum_time += 1;

  //Get marble location and rotational parameters
  const float orbit_dist = level_copy.orbit_dist;
  const Eigen::Vector3f orbit_pt(0.0f, orbit_dist, 0.0f);
  const Eigen::Vector3f perp_vec(std::sin(t), 0.0f, std::cos(t));
  cam_pos = orbit_pt + perp_vec * (orbit_dist * 2.5f);
  cam_pos_smooth = cam_pos_smooth*orbit_smooth + cam_pos*(1 - orbit_smooth);
  
  //Solve for the look direction
  cam_look_x = std::atan2(cam_pos_smooth.x(), cam_pos_smooth.z());
  ModPi(cam_look_x_smooth, cam_look_x);
  cam_look_x_smooth = cam_look_x_smooth*(1 - a) + cam_look_x*a;
  
  //Update look smoothing
  cam_look_y = -0.3f;
  cam_look_y_smooth = cam_look_y_smooth*orbit_smooth + cam_look_y*(1 - orbit_smooth);
  
  //Update the camera matrix
  marble_mat.setIdentity();
  MakeCameraRotation();
  cam_mat.block<3, 1>(0, 3) = cam_pos_smooth;

  //Update fractal parameters
  ModPi(frac_params[1], level_copy.params[1]);
  ModPi(frac_params[2], level_copy.params[2]);
  frac_params_smooth = frac_params * (1.0f - a) + level_copy.params * a;

  //When done transitioning display the marble and flag
  if (timer >= frame_transition) {
    marble_pos = level_copy.start_pos;
    marble_rad = level_copy.marble_rad;
    flag_pos = level_copy.end_pos;
  }

  //When done transitioning, setup level
  if (timer >= frame_orbit) {
    frac_params = level_copy.params;
    cam_look_x = cam_look_x_smooth;
    cam_pos = cam_pos_smooth;
    cam_dist = default_zoom;
    cam_dist_smooth = cam_dist;
    cam_mode = DEORBIT;
  }
}

void Scene::UpdateDeOrbit(float dx, float dy, float dz) {
  //Update the timer
  const float t = timer * orbit_speed;
  float b = std::min(float(std::max(timer - frame_orbit, 0)) / float(frame_deorbit - frame_orbit), 1.0f);
  b *= b/(2*b*(b - 1) + 1);
  timer += 1;
  sum_time += 1;

  if (timer > frame_deorbit + 1) {
    UpdateCameraOnly(dx, dy, dz);
  } else {
    //Get marble location and rotational parameters
    const float orbit_dist = level_copy.orbit_dist;
    const Eigen::Vector3f orbit_pt(0.0f, orbit_dist, 0.0f);
    const Eigen::Vector3f perp_vec(std::sin(t), 0.0f, std::cos(t));
    const Eigen::Vector3f orbit_cam_pos = orbit_pt + perp_vec * (orbit_dist * 2.5f);
    cam_pos = cam_pos*orbit_smooth + orbit_cam_pos*(1 - orbit_smooth);

    //Solve for the look direction
    const float start_look_x = level_copy.start_look_x;
    cam_look_x = std::atan2(cam_pos.x(), cam_pos.z());
    ModPi(cam_look_x, start_look_x);

    //Solve for the look direction
    cam_look_x_smooth = cam_look_x*(1 - b) + start_look_x*b;

    //Update look smoothing
    cam_look_y = -0.3f;
    cam_look_y_smooth = cam_look_y_smooth*orbit_smooth + cam_look_y*(1 - orbit_smooth);

    //Update the camera rotation matrix
    MakeCameraRotation();

    //Update the camera position
    Eigen::Vector3f marble_cam_pos = marble_pos + cam_mat.block<3, 3>(0, 0) * Eigen::Vector3f(0.0f, 0.0f, marble_rad * cam_dist_smooth);
    marble_cam_pos += Eigen::Vector3f(0.0f, marble_rad * cam_dist_smooth * 0.1f, 0.0f);
    cam_pos_smooth = cam_pos*(1 - b) + marble_cam_pos*b;
    cam_mat.block<3, 1>(0, 3) = cam_pos_smooth;

    //Required for a smooth transition later on
    cam_look_x = cam_look_x_smooth;
    cam_look_y = cam_look_y_smooth;
  }

  //When done deorbiting, transition to play
  if (timer > frame_countdown) {
    cam_mode = MARBLE;
    cam_pos = cam_pos_smooth;
    timer = 0;
  }
}

void Scene::UpdateCameraOnly(float dx, float dy, float dz) {
  //Update camera zoom
  if (param_mod >= 0) {
    const float new_param = level_copy.params[param_mod] + dz*0.01f;
    level_copy.params[param_mod] = frac_params_smooth[param_mod] = frac_params[param_mod] = new_param;
  } else if (zoom_to_scale) {
    level_copy.marble_rad *= std::pow(2.0f, -dz);
    level_copy.marble_rad = std::min(std::max(level_copy.marble_rad, 0.0006f), 0.6f);
    marble_rad = marble_rad*zoom_smooth + level_copy.marble_rad*(1 - zoom_smooth);
  } else {
    cam_dist *= std::pow(2.0f, -dz);
    cam_dist = std::min(std::max(cam_dist, 5.0f), 30.0f);
  }
  cam_dist_smooth = cam_dist_smooth*zoom_smooth + cam_dist*(1 - zoom_smooth);

  //Update look direction
  cam_look_x += dx;
  cam_look_y += dy;
  cam_look_y = std::min(std::max(cam_look_y, -pi / 2), pi / 2);
  while (cam_look_x > pi) { cam_look_x -= 2 * pi; }
  while (cam_look_x < -pi) { cam_look_x += 2 * pi; }

  //Update look smoothing
  const float a = (free_camera ? look_smooth_free_camera : look_smooth);
  ModPi(cam_look_x_smooth, cam_look_x);
  cam_look_x_smooth = cam_look_x_smooth*a + cam_look_x*(1 - a);
  cam_look_y_smooth = cam_look_y_smooth*a + cam_look_y*(1 - a);

  //Setup rotation matrix for planets
  if (level_copy.planet) {
    marble_mat.col(1) = marble_pos.normalized();
    marble_mat.col(2) = -marble_mat.col(1).cross(marble_mat.col(0)).normalized();
    marble_mat.col(0) = -marble_mat.col(2).cross(marble_mat.col(1)).normalized();
  } else {
    marble_mat.setIdentity();
  }

  //Update the camera matrix
  MakeCameraRotation();
  if (!free_camera) {
    cam_pos = marble_pos + cam_mat.block<3, 3>(0, 0) * Eigen::Vector3f(0.0f, 0.0f, marble_rad * cam_dist_smooth);
    cam_pos += marble_mat.col(1) * (marble_rad * cam_dist_smooth * 0.1f);
    cam_pos_smooth = cam_pos;
  }
  cam_mat.block<3, 1>(0, 3) = cam_pos_smooth;
}

void Scene::UpdateNormal(float dx, float dy, float dz) {
  //Update camera
  UpdateCameraOnly(dx, dy, dz);

  //Update timer
  timer += 1;
  sum_time += 1;
}

void Scene::UpdateGoal() {
  //Update the timer
  const float t = timer * 0.01f;
  float a = std::min(t / 75.0f, 1.0f);
  timer += 1;
  if (cur_level != num_levels_midpoint - 1 && cur_level != num_levels - 1) {
    sum_time += 1;
  }

  //Get marble location and rotational parameters
  const float flag_dist = marble_rad * 6.5f;
  const Eigen::Vector3f orbit_pt = flag_pos + marble_mat * Eigen::Vector3f(0.0f, flag_dist, 0.0f);
  const Eigen::Vector3f perp_vec = Eigen::Vector3f(std::sin(t), 0.0f, std::cos(t));
  cam_pos = orbit_pt + marble_mat * perp_vec * (flag_dist * 3.5f);
  cam_pos_smooth = cam_pos_smooth*(1 - a) + cam_pos*a;

  //Solve for the look direction
  cam_look_x = std::atan2(perp_vec.x(), perp_vec.z());
  ModPi(cam_look_x_smooth, cam_look_x);
  cam_look_x_smooth = cam_look_x_smooth*(1 - a) + cam_look_x*a;

  //Update look smoothing
  cam_look_y = -0.25f;
  cam_look_y_smooth = cam_look_y_smooth*0.99f + cam_look_y*(1 - 0.99f);

  //Update the camera matrix
  MakeCameraRotation();
  cam_mat.block<3, 1>(0, 3) = cam_pos_smooth;

  //Animate marble
  marble_vel += (orbit_pt - marble_pos) * 0.005f;
  marble_pos += marble_vel;
  if (marble_vel.norm() > marble_rad*0.02f) {
    marble_vel *= 0.95f;
  }

  if (timer > 300 && cam_mode != FINAL && cam_mode != MIDPOINT) {
    StartNextLevel();
  }
}

void Scene::MakeCameraRotation() {
  cam_mat.setIdentity();
  const Eigen::AngleAxisf aa_x_smooth(cam_look_x_smooth, Eigen::Vector3f::UnitY());
  const Eigen::AngleAxisf aa_y_smooth(cam_look_y_smooth, Eigen::Vector3f::UnitX());
  cam_mat.block<3, 3>(0, 0) = marble_mat * (aa_x_smooth * aa_y_smooth).toRotationMatrix();
}

void Scene::SnapCamera() {
  cam_look_x_smooth = cam_look_x;
  cam_look_y_smooth = cam_look_y;
  cam_dist_smooth = cam_dist;
  cam_pos_smooth = cam_pos;
}

void Scene::HideObjects() {
  marble_pos = Eigen::Vector3f(999.0f, 999.0f, 999.0f);
  flag_pos = Eigen::Vector3f(999.0f, 999.0f, 999.0f);
  marble_vel.setZero();
}

void Scene::Write(sf::Shader& shader) const {
  shader.setUniform("iMat", sf::Glsl::Mat4(cam_mat.data()));

  shader.setUniform("iMarblePos", free_camera ?
    sf::Glsl::Vec3(999.0f, 999.0f, 999.0f) :
    sf::Glsl::Vec3(marble_pos.x(), marble_pos.y(), marble_pos.z())
  );
  shader.setUniform("iMarbleRad", marble_rad);

  shader.setUniform("iFlagScale", level_copy.planet ? -marble_rad : marble_rad);
  shader.setUniform("iFlagPos", free_camera ?
    sf::Glsl::Vec3(-999.0f, -999.0f, -999.0f) :
    sf::Glsl::Vec3(flag_pos.x(), flag_pos.y(), flag_pos.z())
  );

  shader.setUniform("iFracScale", frac_params_smooth[0]);
  shader.setUniform("iFracAng1", frac_params_smooth[1]);
  shader.setUniform("iFracAng2", frac_params_smooth[2]);
  shader.setUniform("iFracShift", sf::Glsl::Vec3(frac_params_smooth[3], frac_params_smooth[4], frac_params_smooth[5]));
  shader.setUniform("iFracCol", sf::Glsl::Vec3(frac_params_smooth[6], frac_params_smooth[7], frac_params_smooth[8]));

  shader.setUniform("iExposure", exposure);

  // FOV: use fly_fov in FREE_FLY, default 60° (= original √3 focal dist) elsewhere
  const float fov_to_use = (cam_mode == FREE_FLY) ? fly_fov : 60.0f;
  shader.setUniform("iFov", fov_to_use);

  // Fold iterations: adaptive in free-fly, the original 24 everywhere else
  shader.setUniform("iFracIter", (cam_mode == FREE_FLY) ? fly_iters : 24);

  // Free-running clock for time-based objects (4D tesseract rotation)
  shader.setUniform("iTime", anim_t);
}

//Hard-coded to match the fractal
float Scene::DE(const Eigen::Vector3f& pt) const {
  //Easier to work with names
  const float frac_scale = frac_params_smooth[0];
  const float frac_angle1 = frac_params_smooth[1];
  const float frac_angle2 = frac_params_smooth[2];
  const Eigen::Vector3f frac_shift = frac_params_smooth.segment<3>(3);
  const Eigen::Vector3f frac_color = frac_params_smooth.segment<3>(6);

  Eigen::Vector4f p;
  p << pt, 1.0f;
  for (int i = 0; i < fractal_iters; ++i) {
    //absFold
    p.segment<3>(0) = p.segment<3>(0).cwiseAbs();
    //rotZ
    const float rotz_c = std::cos(frac_angle1);
    const float rotz_s = std::sin(frac_angle1);
    const float rotz_x = rotz_c*p.x() + rotz_s*p.y();
    const float rotz_y = rotz_c*p.y() - rotz_s*p.x();
    p.x() = rotz_x; p.y() = rotz_y;
    //mengerFold
    float a = std::min(p.x() - p.y(), 0.0f);
    p.x() -= a; p.y() += a;
    a = std::min(p.x() - p.z(), 0.0f);
    p.x() -= a; p.z() += a;
    a = std::min(p.y() - p.z(), 0.0f);
    p.y() -= a; p.z() += a;
    //rotX
    const float rotx_c = std::cos(frac_angle2);
    const float rotx_s = std::sin(frac_angle2);
    const float rotx_y = rotx_c*p.y() + rotx_s*p.z();
    const float rotx_z = rotx_c*p.z() - rotx_s*p.y();
    p.y() = rotx_y; p.z() = rotx_z;
    //scaleTrans
    p *= frac_scale;
    p.segment<3>(0) += frac_shift;
  }
  const Eigen::Vector3f a = p.segment<3>(0).cwiseAbs() - Eigen::Vector3f(6.0f, 6.0f, 6.0f);
  return (std::min(std::max(std::max(a.x(), a.y()), a.z()), 0.0f) + a.cwiseMax(0.0f).norm()) / p.w();
}

//CPU mirror of the free-fly shader DEs. Only drives the adaptive fly speed,
//so a rough estimate is fine — each formula matches its GLSL counterpart.
float Scene::DE_Fly(const Eigen::Vector3f& pt) const {
  typedef Eigen::Vector3f V3;
  const float s = frac_params_smooth[0];
  const V3 shift = frac_params_smooth.segment<3>(3);
  const auto fract = [](float x) { return x - std::floor(x); };

  switch (fractal_type) {
  case SEL_SIERPINSKI: {
    Eigen::Vector4f p; p << pt, 1.0f;
    for (int i = 0; i < fractal_iters; ++i) {
      float a;
      a = std::min(p.x() + p.y(), 0.0f); p.x() -= a; p.y() -= a;
      a = std::min(p.x() + p.z(), 0.0f); p.x() -= a; p.z() -= a;
      a = std::min(p.y() + p.z(), 0.0f); p.y() -= a; p.z() -= a;
      p *= s;
      p.segment<3>(0) += shift;
    }
    const float md = std::max(std::max(-p.x() - p.y() - p.z(), p.x() + p.y() - p.z()),
                              std::max(-p.x() + p.y() + p.z(), p.x() - p.y() + p.z()));
    return (md - 1.0f) / (p.w() * 1.7320508f);
  }
  case SEL_MANDELBOX:
  case SEL_MBOX_JULIA: {
    V3 p = pt;
    const V3 c = (fractal_type == SEL_MANDELBOX) ? V3(pt + shift) : shift;
    float dr = 1.0f;
    for (int i = 0; i < fractal_iters; ++i) {
      p = p.cwiseMin(1.0f).cwiseMax(-1.0f) * 2.0f - p;
      const float r2 = p.squaredNorm();
      if (r2 < 0.25f) { p *= 4.0f; dr *= 4.0f; }
      else if (r2 < 1.0f) { p /= r2; dr /= r2; }
      p = p * s + c;
      dr = dr * std::abs(s) + 1.0f;
    }
    return (p.norm() - std::abs(std::abs(s) - 1.0f)) / dr;
  }
  case SEL_OCTAHEDRAL: {
    Eigen::Vector4f p; p << pt, 1.0f;
    const V3 n1(0.7071f, 0.7071f, 0.0f), n2(0.7071f, 0.0f, 0.7071f), n3(0.0f, 0.7071f, 0.7071f);
    for (int i = 0; i < fractal_iters; ++i) {
      V3 q = p.segment<3>(0).cwiseAbs();
      q -= 2.0f * std::min(0.0f, q.dot(n1)) * n1;
      q -= 2.0f * std::min(0.0f, q.dot(n2)) * n2;
      q -= 2.0f * std::min(0.0f, q.dot(n3)) * n3;
      p.segment<3>(0) = q * s + shift;
      p.w() *= s;
    }
    const float md = std::max(std::max(-p.x() - p.y() - p.z(), p.x() + p.y() - p.z()),
                              std::max(-p.x() + p.y() + p.z(), p.x() - p.y() + p.z()));
    return (md - 1.0f) / (p.w() * 1.7320508f);
  }
  case SEL_QUAT_JULIA: {
    Eigen::Vector4f z(pt.x(), pt.y(), pt.z(), 0.0f);
    const Eigen::Vector4f c(shift.x(), shift.y(), shift.z(), frac_params_smooth[1]);
    Eigen::Vector4f dz(1.0f, 0.0f, 0.0f, 0.0f);
    float r2 = 1.0f;
    for (int i = 0; i < fractal_iters; ++i) {
      const Eigen::Vector4f zq = z, dq = dz;
      dz = 2.0f * Eigen::Vector4f(
        zq.x()*dq.x() - zq.y()*dq.y() - zq.z()*dq.z() - zq.w()*dq.w(),
        zq.x()*dq.y() + zq.y()*dq.x() + zq.z()*dq.w() - zq.w()*dq.z(),
        zq.x()*dq.z() - zq.y()*dq.w() + zq.z()*dq.x() + zq.w()*dq.y(),
        zq.x()*dq.w() + zq.y()*dq.z() - zq.z()*dq.y() + zq.w()*dq.x());
      z = Eigen::Vector4f(zq.x()*zq.x() - zq.y()*zq.y() - zq.z()*zq.z() - zq.w()*zq.w(),
                          2.0f*zq.x()*zq.y(), 2.0f*zq.x()*zq.z(), 2.0f*zq.x()*zq.w()) + c;
      r2 = z.squaredNorm();
      if (r2 > 4.0f) { break; }
    }
    const float dr2 = std::max(dz.squaredNorm(), 1e-12f);
    return 0.5f * std::sqrt(r2 / dr2) * std::log(std::max(r2, 1.001f));
  }
  case SEL_MANDELBULB:
  case SEL_JULIABULB: {
    const float n = s;
    const bool julia = (fractal_type == SEL_JULIABULB);
    V3 z = pt;
    float dr = 1.0f, r = z.norm();
    for (int i = 0; i < fractal_iters; ++i) {
      r = z.norm();
      if (r > 2.0f) { break; }
      const float theta = std::acos(std::min(std::max(z.z() / std::max(r, 1e-12f), -1.0f), 1.0f)) * n;
      const float phi = std::atan2(z.y(), z.x()) * n;
      dr = std::pow(r, n - 1.0f) * n * dr + (julia ? 0.0f : 1.0f);
      const float zr = std::pow(r, n);
      z = zr * V3(std::sin(theta)*std::cos(phi), std::sin(phi)*std::sin(theta), std::cos(theta));
      z += julia ? shift : pt;
    }
    r = z.norm();
    return 0.5f * std::log(std::max(r, 1e-6f)) * r / std::max(dr, 1e-9f);
  }
  case SEL_ICOSA: {
    Eigen::Vector4f p; p << pt, 1.0f;
    const float phi_g = 1.6180339887f, ni = 0.52573111f;
    const V3 k1(ni, phi_g*ni, 0.0f), k2(0.0f, ni, phi_g*ni), k3(phi_g*ni, 0.0f, ni);
    for (int i = 0; i < fractal_iters; ++i) {
      V3 q = p.segment<3>(0).cwiseAbs();
      float d;
      d = q.dot(k1); if (d < 0.0f) { q -= 2.0f*d*k1; }
      d = q.dot(k2); if (d < 0.0f) { q -= 2.0f*d*k2; }
      d = q.dot(k3); if (d < 0.0f) { q -= 2.0f*d*k3; }
      d = q.dot(k1); if (d < 0.0f) { q -= 2.0f*d*k1; }
      d = q.dot(k2); if (d < 0.0f) { q -= 2.0f*d*k2; }
      p.segment<3>(0) = q * s + shift;
      p.w() *= s;
    }
    return (p.segment<3>(0).norm() - 1.0f) / p.w();
  }
  case SEL_APOLLONIAN: {
    V3 p = pt;
    float scale = 1.0f;
    for (int i = 0; i < 10; ++i) {
      p.x() = -1.0f + 2.0f*fract(0.5f*p.x() + 0.5f);
      p.y() = -1.0f + 2.0f*fract(0.5f*p.y() + 0.5f);
      p.z() = -1.0f + 2.0f*fract(0.5f*p.z() + 0.5f);
      const float r2 = std::max(p.squaredNorm(), 1e-12f);
      const float k = s / r2;
      p *= k; scale *= k;
    }
    return 0.25f * std::abs(p.y()) / scale;
  }
  case SEL_KLEINIAN: {
    const V3 csize = shift.cwiseAbs();
    V3 p = pt;
    float scale = 1.0f;
    for (int i = 0; i < 10; ++i) {
      p = 2.0f*p.cwiseMin(csize).cwiseMax(-csize) - p;
      const float r2 = std::max(p.squaredNorm(), 1e-12f);
      const float k = std::max(s / r2, 1.0f);
      p *= k; scale *= k;
    }
    return 0.45f * std::abs(p.y()) / scale;
  }
  case SEL_MENGER_MBOX: {
    Eigen::Vector4f p; p << pt, 1.0f;
    for (int i = 0; i < 12; ++i) {
      p.segment<3>(0) = p.segment<3>(0).cwiseAbs();
      float a = std::min(p.x() - p.y(), 0.0f); p.x() -= a; p.y() += a;
      a = std::min(p.x() - p.z(), 0.0f); p.x() -= a; p.z() += a;
      a = std::min(p.y() - p.z(), 0.0f); p.y() -= a; p.z() += a;
      p.segment<3>(0) = p.segment<3>(0).cwiseMin(1.0f).cwiseMax(-1.0f)*2.0f - p.segment<3>(0);
      const float r2 = p.segment<3>(0).squaredNorm();
      if (r2 < 0.25f) { p *= 4.0f; }
      else if (r2 < 1.0f) { p /= r2; }
      p *= s;
      p.segment<3>(0) += shift;
    }
    const V3 a = p.segment<3>(0).cwiseAbs() - V3(6.0f, 6.0f, 6.0f);
    return (std::min(std::max(std::max(a.x(), a.y()), a.z()), 0.0f) + a.cwiseMax(0.0f).norm()) / p.w();
  }
  case SEL_TESSERACT: {
    //Mirror of tessVert + 32-edge min in frag.glsl
    const V3 q = pt / s;
    const float t = anim_t;
    const float a1 = t * 0.37f * frac_params_smooth[1];
    const float a2 = t * 0.29f * frac_params_smooth[2];
    const float a3 = t * 0.21f;
    const float a4 = t * 0.15f;
    const float s1 = std::sin(a1), c1 = std::cos(a1);
    const float s2 = std::sin(a2), c2 = std::cos(a2);
    const float s3 = std::sin(a3), c3 = std::cos(a3);
    const float s4 = std::sin(a4), c4 = std::cos(a4);
    const float wd = std::max(3.0f + frac_params_smooth[3], 2.2f);
    V3 V[16];
    for (int i = 0; i < 16; ++i) {
      float x = (i & 1) ? 1.0f : -1.0f;
      float y = (i & 2) ? 1.0f : -1.0f;
      float z = (i & 4) ? 1.0f : -1.0f;
      float w = (i & 8) ? 1.0f : -1.0f;
      float nx = c1*x - s1*w, nw = c1*w + s1*x; x = nx; w = nw;   // rotXW
      float ny = c2*y - s2*w; nw = c2*w + s2*y; y = ny; w = nw;   // rotYW
      float nz = c3*z - s3*w; nw = c3*w + s3*z; z = nz; w = nw;   // rotZW
      nx = c4*x + s4*y; ny = c4*y - s4*x; x = nx; y = ny;         // rotZ (xy)
      const float pr = wd / (wd - w);
      V[i] = V3(x, y, z) * pr;
    }
    const float r = 0.03f;
    float d = 1e9f;
    for (int i = 0; i < 16; ++i) {
      for (int k = 0; k < 4; ++k) {
        const int j = i ^ (1 << k);
        if (j <= i) { continue; }
        const V3 pa = q - V[i], ba = V[j] - V[i];
        const float h = std::min(std::max(pa.dot(ba) / std::max(ba.dot(ba), 1e-8f), 0.0f), 1.0f);
        d = std::min(d, (pa - ba*h).norm() - r);
      }
    }
    return d * s;
  }
  case SEL_MOBIUS: {
    const V3 q = pt / s;
    const float ang = std::atan2(q.y(), q.x());
    const float rad = std::sqrt(q.x()*q.x() + q.y()*q.y()) - 1.0f;
    const float ca = std::cos(ang*0.5f), sa = std::sin(ang*0.5f);
    const float u =  ca*rad + sa*q.z();
    const float v = -sa*rad + ca*q.z();
    const float dx = std::abs(u) - frac_params_smooth[1];
    const float dy = std::abs(v) - frac_params_smooth[2];
    const float mx = std::max(dx, 0.0f), my = std::max(dy, 0.0f);
    const float d = std::sqrt(mx*mx + my*my) + std::min(std::max(dx, dy), 0.0f);
    return d * s * 0.7f;
  }
  case SEL_KLEIN: {
    const V3 q = pt / s;
    V3 bp = q - V3(0.0f, -0.2f, 0.0f);
    bp.y() *= 0.85f;
    const float bulb = std::abs(bp.norm() - 0.9f) - 0.05f;
    const float tr = frac_params_smooth[2];
    const float hk = frac_params_smooth[1];
    const V3 A(0.0f, 0.62f, 0.0f), B(0.0f, 1.50f, 0.0f), C(hk, 1.70f, 0.0f),
             E(hk, 0.20f, 0.0f), F(hk*0.2f, -0.55f, 0.0f);
    const auto seg = [&](const V3& a, const V3& b) {
      const V3 pa = q - a, ba = b - a;
      const float h = std::min(std::max(pa.dot(ba) / std::max(ba.dot(ba), 1e-8f), 0.0f), 1.0f);
      return (pa - ba*h).norm() - tr;
    };
    float neck = seg(A, B);
    neck = std::min(neck, seg(B, C));
    neck = std::min(neck, seg(C, E));
    neck = std::min(neck, seg(E, F));
    const float k = 0.08f;
    const float hh = std::min(std::max(0.5f + 0.5f*(neck - bulb)/k, 0.0f), 1.0f);
    const float d = (neck*(1.0f - hh) + bulb*hh) - k*hh*(1.0f - hh);
    return d * s;
  }
  default:
    return DE(pt);  // game formula (levels, sponge variants)
  }
}

//Hard-coded to match the fractal
Eigen::Vector3f Scene::NP(const Eigen::Vector3f& pt) const {
  //Easier to work with names
  const float frac_scale = frac_params_smooth[0];
  const float frac_angle1 = frac_params_smooth[1];
  const float frac_angle2 = frac_params_smooth[2];
  const Eigen::Vector3f frac_shift = frac_params_smooth.segment<3>(3);
  const Eigen::Vector3f frac_color = frac_params_smooth.segment<3>(6);

  static std::vector<Eigen::Vector4f, Eigen::aligned_allocator<Eigen::Vector4f>> p_hist;
  p_hist.clear();
  Eigen::Vector4f p;
  p << pt, 1.0f;
  //Fold the point, keeping history
  for (int i = 0; i < fractal_iters; ++i) {
    //absFold
    p_hist.push_back(p);
    p.segment<3>(0) = p.segment<3>(0).cwiseAbs();
    //rotZ
    const float rotz_c = std::cos(frac_angle1);
    const float rotz_s = std::sin(frac_angle1);
    const float rotz_x = rotz_c*p.x() + rotz_s*p.y();
    const float rotz_y = rotz_c*p.y() - rotz_s*p.x();
    p.x() = rotz_x; p.y() = rotz_y;
    //mengerFold
    p_hist.push_back(p);
    float a = std::min(p.x() - p.y(), 0.0f);
    p.x() -= a; p.y() += a;
    a = std::min(p.x() - p.z(), 0.0f);
    p.x() -= a; p.z() += a;
    a = std::min(p.y() - p.z(), 0.0f);
    p.y() -= a; p.z() += a;
    //rotX
    const float rotx_c = std::cos(frac_angle2);
    const float rotx_s = std::sin(frac_angle2);
    const float rotx_y = rotx_c*p.y() + rotx_s*p.z();
    const float rotx_z = rotx_c*p.z() - rotx_s*p.y();
    p.y() = rotx_y; p.z() = rotx_z;
    //scaleTrans
    p *= frac_scale;
    p.segment<3>(0) += frac_shift;
  }
  //Get the nearest point
  Eigen::Vector3f n = p.segment<3>(0).cwiseMax(-6.0f).cwiseMin(6.0f);
  //Then unfold the nearest point (reverse order)
  for (int i = 0; i < fractal_iters; ++i) {
    //scaleTrans
    n.segment<3>(0) -= frac_shift;
    n /= frac_scale;
    //rotX
    const float rotx_c = std::cos(-frac_angle2);
    const float rotx_s = std::sin(-frac_angle2);
    const float rotx_y = rotx_c*n.y() + rotx_s*n.z();
    const float rotx_z = rotx_c*n.z() - rotx_s*n.y();
    n.y() = rotx_y; n.z() = rotx_z;
    //mengerUnfold
    p = p_hist.back(); p_hist.pop_back();
    const float mx = std::max(p[0], p[1]);
    if (std::min(p[0], p[1]) < std::min(mx, p[2])) {
      std::swap(n[1], n[2]);
    }
    if (mx < p[2]) {
      std::swap(n[0], n[2]);
    }
    if (p[0] < p[1]) {
      std::swap(n[0], n[1]);
    }
    //rotZ
    const float rotz_c = std::cos(-frac_angle1);
    const float rotz_s = std::sin(-frac_angle1);
    const float rotz_x = rotz_c*n.x() + rotz_s*n.y();
    const float rotz_y = rotz_c*n.y() - rotz_s*n.x();
    n.x() = rotz_x; n.y() = rotz_y;
    //absUnfold
    p = p_hist.back(); p_hist.pop_back();
    if (p[0] < 0.0f) {
      n[0] = -n[0];
    }
    if (p[1] < 0.0f) {
      n[1] = -n[1];
    }
    if (p[2] < 0.0f) {
      n[2] = -n[2];
    }
  }
  return n;
}

bool Scene::MarbleCollision(float& delta_v) {
  //Check if the distance estimate indicates a collision
  const float de = DE(marble_pos);
  if (de >= marble_rad) {
    return de < marble_rad * ground_ratio;
  }
  
  //Check if the marble has been crushed by the fractal
  if (de < marble_rad * 0.001f) {
    sound_shatter->play();
    marble_pos.y() = -9999.0f;
    return false;
  }

  //Find the nearest point and compute offset
  const Eigen::Vector3f np = NP(marble_pos);
  const Eigen::Vector3f d = np - marble_pos;
  const Eigen::Vector3f dn = d.normalized();

  //Apply the offset to the marble's position and velocity
  const float dv = marble_vel.dot(dn);
  delta_v = std::max(delta_v, dv);
  marble_pos -= dn * marble_rad - d;
  marble_vel -= dn * (dv * marble_bounce);
  return true;
}

void Scene::Cheat_ColorChange() {
  if (!enable_cheats) { return; }
  level_copy.params[6] = frac_params_smooth[6] = frac_params[6] = float((rand() % 201) - 100) * 0.01f;
  level_copy.params[7] = frac_params_smooth[7] = frac_params[7] = float((rand() % 201) - 100) * 0.01f;
  level_copy.params[8] = frac_params_smooth[8] = frac_params[8] = float((rand() % 201) - 100) * 0.01f;
}
void Scene::Cheat_FreeCamera() {
  if (!enable_cheats) { return; }
  free_camera = !free_camera;
}
void Scene::Cheat_Gravity() {
  if (!enable_cheats) { return; }
  gravity_type = (gravity_type + 1) % 3;
}
void Scene::Cheat_HyperSpeed() {
  if (!enable_cheats) { return; }
  hyper_speed = !hyper_speed;
}
void Scene::Cheat_IgnoreGoal() {
  if (!enable_cheats) { return; }
  ignore_goal = !ignore_goal;
}
void Scene::Cheat_Motion() {
  if (!enable_cheats) { return; }
  disable_motion = !disable_motion;
}
void Scene::Cheat_Planet() {
  if (!enable_cheats) { return; }
  level_copy.planet = !level_copy.planet;
}
void Scene::Cheat_Zoom() {
  if (!enable_cheats) { return; }
  zoom_to_scale = !zoom_to_scale;
}
void Scene::Cheat_Param(int param) {
  if (!enable_cheats) { return; }
  param_mod = param;
}
