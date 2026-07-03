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
#include "Level.h"
#include "Overlays.h"
#include "Fractals.h"
#include "Explorer2D.h"
#include "Res.h"
#include "SelectRes.h"
#include "Scores.h"
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#define ERROR_MSG(x) MessageBox(nullptr, TEXT(x), TEXT("ERROR"), MB_OK);
#else
#define ERROR_MSG(x) std::cerr << x << std::endl;
#endif

//Constants
static const float mouse_sensitivity = 0.000625f;
static const float wheel_sensitivity = 0.2f;
static const float music_vol = 75.0f;
static const float target_fps = 60.0f;

//Game modes
enum GameMode {
  MAIN_MENU,
  PLAYING,
  PAUSED,
  SCREEN_SAVER,
  FRACTAL_SELECT,   // unified fractal browser (3D / levels / 2D)
  FREE_FLY,
  EXPLORE_2D,       // flat 2D fractal explorer
  CONTROLS,
  LEVELS,
  CREDITS,
  MIDPOINT
};

//Global variables
static sf::Vector2i mouse_pos;
static bool all_keys[sf::Keyboard::KeyCount] = { 0 };
static bool mouse_clicked = false;
static bool show_cheats = false;
static GameMode game_mode = MAIN_MENU;
static int browser_cat = CAT_3D;
static int browser_sel[NUM_FRAC_CATEGORIES] = { 0, 0, 0 };
static bool hud_visible = true;
static int edit_param = -1;          // selected editor param, -1 = editor closed
static bool want_screenshot = false;
static bool settings_shader_dirty = false;

//Shader specialization: the fractal formula and graphics options are baked
//into the shader at compile time by patching #define lines in the source.
static std::string LoadFileStr(const char* path) {
  std::ifstream fin(path, std::ios::binary);
  std::stringstream ss;
  ss << fin.rdbuf();
  return ss.str();
}
static void PatchDefine(std::string& src, const std::string& name, const std::string& value) {
  const std::string tag = "#define " + name + " ";
  const size_t pos = src.find(tag);
  if (pos == std::string::npos) { return; }
  const size_t val_start = pos + tag.size();
  size_t val_end = src.find('\n', val_start);
  if (val_end == std::string::npos) { val_end = src.size(); }
  src.replace(val_start, val_end - val_start, value);
}
static bool BuildFractalShader(sf::Shader& shader, const std::string& vert_src,
                               const std::string& frag_src_orig, int sel) {
  std::string frag_src = frag_src_orig;
  PatchDefine(frag_src, "FRACTAL_TYPE_SEL", std::to_string(sel));
  PatchDefine(frag_src, "SHADOWS_ENABLED", game_settings.shadows ? "1" : "0");
  PatchDefine(frag_src, "ANTIALIASING_SAMPLES", game_settings.quality >= 2 ? "2" : "1");
  PatchDefine(frag_src, "MAX_MARCHES", game_settings.quality == 0 ? "400" : "1000");
  return shader.loadFromMemory(vert_src, frag_src);
}

// Helper for enum-class key indexing
static int KI(sf::Keyboard::Key k) { return static_cast<int>(k); }

float GetVol() {
  if (game_settings.mute) {
    return 0.0f;
  } else if (game_mode == PAUSED) {
    return music_vol / 4;
  } else {
    return music_vol;
  }
}

void LockMouse(sf::RenderWindow& window) {
  window.setMouseCursorVisible(false);
  const sf::Vector2u size = window.getSize();
  mouse_pos = sf::Vector2i(size.x / 2, size.y / 2);
  sf::Mouse::setPosition(mouse_pos);
}
void UnlockMouse(sf::RenderWindow& window) {
  window.setMouseCursorVisible(true);
}

void PauseGame(sf::RenderWindow& window, Scene& scene) {
  game_mode = PAUSED;
  scene.GetCurMusic().setVolume(GetVol());
  UnlockMouse(window);
  scene.SetExposure(0.5f);
}

int DirExists(const char *path) {
  struct stat info;
  if (stat(path, &info) != 0) {
    return 0;
  } else if (info.st_mode & S_IFDIR) {
    return 1;
  }
  return 0;
}

#if defined(_WIN32)
int WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nCmdShow) {
#else
int main(int argc, char *argv[]) {
#endif
  //Load the font
  sf::Font font;
  if (!font.openFromFile(Orbitron_Bold_ttf)) {
    ERROR_MSG("Unable to load font");
    return 1;
  }
  //Load the mono font
  sf::Font font_mono;
  if (!font_mono.openFromFile(Inconsolata_Bold_ttf)) {
    ERROR_MSG("Unable to load mono font");
    return 1;
  }

  //Load the music
  sf::Music menu_music;
  (void)menu_music.openFromFile(menu_ogg);
  menu_music.setLooping(true);
  sf::Music level_music[num_level_music];
  (void)level_music[0].openFromFile(level1_ogg);
  level_music[0].setLooping(true);
  (void)level_music[1].openFromFile(level2_ogg);
  level_music[1].setLooping(true);
  (void)level_music[2].openFromFile(level3_ogg);
  level_music[2].setLooping(true);
  (void)level_music[3].openFromFile(level4_ogg);
  level_music[3].setLooping(true);
  sf::Music credits_music;
  (void)credits_music.openFromFile(credits_ogg);
  credits_music.setLooping(true);

  //Get the directory for saving and loading high scores
#ifdef _WIN32
  const std::string save_dir = std::string(std::getenv("APPDATA")) + "\\MarbleMarcher";
#else
  const std::string save_dir = std::string(std::getenv("HOME")) + "/.MarbleMarcher";
#endif

  if (!DirExists(save_dir.c_str())) {
#if defined(_WIN32)
    bool success = CreateDirectory(save_dir.c_str(), NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    bool success = mkdir(save_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
#endif
    if (!success) {
      ERROR_MSG("Failed to create save directory");
      return 1;
    }
  }
  const std::string save_file = save_dir + "/scores.bin";
  const std::string settings_file = save_dir + "/settings.bin";

  //Load scores if available
  high_scores.Load(save_file);
  game_settings.Load(settings_file);

  //Have user select the resolution
  SelectRes select_res(&font_mono);
  const Resolution* resolution = select_res.Run();
  bool fullscreen = select_res.FullScreen();
  if (resolution == nullptr) {
    return 0;
  }

  //GL settings
  sf::ContextSettings settings;
  settings.majorVersion = 2;
  settings.minorVersion = 0;

  //Create the window
  sf::VideoMode screen_size;
  std::uint32_t window_style;
  sf::State window_state;
  const sf::Vector2i screen_center(resolution->width / 2, resolution->height / 2);
  if (fullscreen) {
    screen_size = sf::VideoMode::getDesktopMode();
    window_style = sf::Style::Default;
    window_state = sf::State::Fullscreen;
  } else {
    screen_size = sf::VideoMode({(unsigned)resolution->width, (unsigned)resolution->height});
    window_style = sf::Style::Close;
    window_state = sf::State::Windowed;
  }
  sf::RenderWindow window(screen_size, "Marble Marcher", window_style, window_state, settings);
  window.setVerticalSyncEnabled(true);
  window.setKeyRepeatEnabled(false);
  window.requestFocus();

  //If native resolution is the same, then we don't need a render texture
  if ((unsigned)resolution->width == screen_size.size.x && (unsigned)resolution->height == screen_size.size.y) {
    fullscreen = false;
  }

  //Create the render texture if needed
  sf::RenderTexture renderTexture;
  if (fullscreen) {
    renderTexture.resize({(unsigned)resolution->width, (unsigned)resolution->height}, settings);
    renderTexture.setSmooth(true);
    renderTexture.setActive(true);
    window.setActive(false);
  }

  //Make sure shader is supported (needs active GL context)
  if (!sf::Shader::isAvailable()) {
    ERROR_MSG("Graphics card does not support shaders");
    return 1;
  }
  //Load shader sources once; the fragment shader is recompiled with patched
  //#defines whenever the fractal or the graphics settings change.
  const std::string vert_src = LoadFileStr(vert_glsl);
  const std::string frag_src = LoadFileStr(frag_glsl);
  sf::Shader shader;
  if (!BuildFractalShader(shader, vert_src, frag_src, SEL_GAME)) {
    ERROR_MSG("Failed to compile shaders");
    return 1;
  }

  //Create the 2D fractal explorer
  Explorer2D explorer;
  if (!explorer.Load((float)resolution->width, (float)resolution->height)) {
    ERROR_MSG("Failed to compile 2D explorer shader");
    return 1;
  }

  //Create the fractal scene
  Scene scene(level_music);
  const sf::Glsl::Vec2 window_res((float)resolution->width, (float)resolution->height);
  shader.setUniform("iResolution", window_res);
  scene.Write(shader);

  //Create screen rectangle
  sf::RectangleShape rect;
  rect.setSize(window_res);
  rect.setPosition({0.f, 0.f});

  //Create the menus
  Overlays overlays(&font, &font_mono);
  overlays.SetScale(float(screen_size.size.x) / 1280.0f);
  menu_music.setVolume(GetVol());
  menu_music.play();

  //Launch whatever is selected in the fractal browser
  auto launch_selected = [&]() {
    const int idx = browser_sel[browser_cat];
    if (browser_cat == CAT_2D) {
      game_mode = EXPLORE_2D;
      explorer.SetKind(idx);
      UnlockMouse(window);
    } else {
      game_mode = FREE_FLY;
      scene.SetMode(Scene::FREE_FLY);
      scene.SetFractal(GetFractal(browser_cat, idx));
      edit_param = -1;
      LockMouse(window);
    }
  };

  //Main loop
  sf::Clock clock;
  float smooth_fps = target_fps;
  float lag_ms = 0.0f;
  while (window.isOpen()) {
    float mouse_wheel = 0.0f;
    while (const auto event = window.pollEvent()) {
      if (event->is<sf::Event::Closed>()) {
        window.close();
        break;
      } else if (event->is<sf::Event::FocusLost>()) {
        if (game_mode == PLAYING) {
          PauseGame(window, scene);
        }
      } else if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>()) {
        const sf::Keyboard::Key keycode = keyEvent->code;
        if (keycode == sf::Keyboard::Key::Unknown) { continue; }
        if (game_mode == CREDITS) {
          game_mode = MAIN_MENU;
          UnlockMouse(window);
          scene.SetMode(Scene::INTRO);
          scene.SetExposure(1.0f);
          credits_music.stop();
          menu_music.setVolume(GetVol());
          menu_music.play();
        } else if (game_mode == MIDPOINT) {
          game_mode = PLAYING;
          scene.SetExposure(1.0f);
          credits_music.stop();
          scene.StartNextLevel();
        } else if (keycode == sf::Keyboard::Key::Escape) {
          if (game_mode == MAIN_MENU) {
            window.close();
            break;
          } else if (game_mode == CONTROLS || game_mode == LEVELS) {
            game_mode = MAIN_MENU;
            scene.SetExposure(1.0f);
          } else if (game_mode == SCREEN_SAVER) {
            game_mode = MAIN_MENU;
            scene.SetMode(Scene::INTRO);
            UnlockMouse(window);
          } else if (game_mode == FRACTAL_SELECT) {
            //Back to wherever the browser was opened from
            if (scene.GetMode() == Scene::FREE_FLY) {
              game_mode = FREE_FLY;
              LockMouse(window);
            } else {
              game_mode = MAIN_MENU;
            }
          } else if (game_mode == EXPLORE_2D) {
            game_mode = MAIN_MENU;
            scene.SetMode(Scene::INTRO);
            UnlockMouse(window);
          } else if (game_mode == FREE_FLY) {
            game_mode = MAIN_MENU;
            scene.SetMode(Scene::INTRO);
            UnlockMouse(window);
          } else if (game_mode == PAUSED) {
            game_mode = PLAYING;
            scene.GetCurMusic().setVolume(GetVol());
            scene.SetExposure(1.0f);
            LockMouse(window);
          } else if (game_mode == PLAYING) {
            PauseGame(window, scene);
          }
        } else if (keycode == sf::Keyboard::Key::R) {
          if (game_mode == PLAYING) {
            scene.ResetLevel();
          } else if (game_mode == EXPLORE_2D) {
            explorer.ResetView();
          }
        } else if (keycode == sf::Keyboard::Key::F1) {
          if (game_mode == PLAYING && high_scores.HasCompleted(num_levels - 1)) {
            show_cheats = !show_cheats;
            scene.EnbaleCheats();
          }
        } else if (keycode == sf::Keyboard::Key::C) {
          scene.Cheat_ColorChange();
        } else if (keycode == sf::Keyboard::Key::Up) {
          if (game_mode == FRACTAL_SELECT) {
            const int count = NumFractals(browser_cat);
            browser_sel[browser_cat] = (browser_sel[browser_cat] - 1 + count) % count;
          } else if (game_mode == FREE_FLY && edit_param >= 0) {
            edit_param = (edit_param - 1 + num_fractal_params) % num_fractal_params;
          }
        } else if (keycode == sf::Keyboard::Key::Down) {
          if (game_mode == FRACTAL_SELECT) {
            const int count = NumFractals(browser_cat);
            browser_sel[browser_cat] = (browser_sel[browser_cat] + 1) % count;
          } else if (game_mode == FREE_FLY && edit_param >= 0) {
            edit_param = (edit_param + 1) % num_fractal_params;
          }
        } else if (keycode == sf::Keyboard::Key::Left) {
          if (game_mode == FRACTAL_SELECT) {
            browser_cat = (browser_cat - 1 + NUM_FRAC_CATEGORIES) % NUM_FRAC_CATEGORIES;
          }
        } else if (keycode == sf::Keyboard::Key::Right) {
          if (game_mode == FRACTAL_SELECT) {
            browser_cat = (browser_cat + 1) % NUM_FRAC_CATEGORIES;
          }
        } else if (keycode == sf::Keyboard::Key::PageUp) {
          if (game_mode == FRACTAL_SELECT) {
            browser_sel[browser_cat] = std::max(browser_sel[browser_cat] - Overlays::browser_per_page, 0);
          }
        } else if (keycode == sf::Keyboard::Key::PageDown) {
          if (game_mode == FRACTAL_SELECT) {
            const int count = NumFractals(browser_cat);
            browser_sel[browser_cat] = std::min(browser_sel[browser_cat] + Overlays::browser_per_page, count - 1);
          }
        } else if (keycode == sf::Keyboard::Key::Enter) {
          if (game_mode == FRACTAL_SELECT) {
            launch_selected();
          }
        } else if (keycode == sf::Keyboard::Key::F) {
          if (game_mode == MAIN_MENU) {
            game_mode = FRACTAL_SELECT;
          } else if (game_mode == SCREEN_SAVER) {
            game_mode = FRACTAL_SELECT;
            scene.SetMode(Scene::INTRO);
            UnlockMouse(window);
          } else if (game_mode == FREE_FLY) {
            //Open the browser with the current view frozen behind it
            game_mode = FRACTAL_SELECT;
            UnlockMouse(window);
          } else if (game_mode == EXPLORE_2D) {
            game_mode = FRACTAL_SELECT;
          } else if (game_mode == FRACTAL_SELECT) {
            //Toggle back to wherever the browser was opened from
            if (scene.GetMode() == Scene::FREE_FLY) {
              game_mode = FREE_FLY;
              LockMouse(window);
            } else {
              game_mode = MAIN_MENU;
            }
          } else {
            scene.Cheat_FreeCamera();
          }
        } else if (keycode == sf::Keyboard::Key::E) {
          if (game_mode == FREE_FLY) {
            edit_param = (edit_param < 0) ? 0 : -1;
          }
        } else if (keycode == sf::Keyboard::Key::V) {
          if (game_mode == FREE_FLY) {
            scene.ToggleDrift();
          }
        } else if (keycode == sf::Keyboard::Key::Space) {
          if (game_mode == EXPLORE_2D && explorer.GetKind() == KIND_JULIA) {
            explorer.ToggleJuliaLive();
          }
        } else if (keycode == sf::Keyboard::Key::F12) {
          want_screenshot = true;
        } else if (keycode == sf::Keyboard::Key::G) {
          scene.Cheat_Gravity();
        } else if (keycode == sf::Keyboard::Key::H) {
          if (game_mode == FREE_FLY || game_mode == EXPLORE_2D) {
            hud_visible = !hud_visible;
          } else {
            scene.Cheat_HyperSpeed();
          }
        } else if (keycode == sf::Keyboard::Key::I) {
          scene.Cheat_IgnoreGoal();
        } else if (keycode == sf::Keyboard::Key::M) {
          scene.Cheat_Motion();
        } else if (keycode == sf::Keyboard::Key::P) {
          if (game_mode == EXPLORE_2D) {
            explorer.CyclePalette();
          } else {
            scene.Cheat_Planet();
          }
        } else if (keycode == sf::Keyboard::Key::Z) {
          if (scene.GetParamMod() == -1) {
            scene.Cheat_Zoom();
          } else {
            scene.Cheat_Param(-1);
          }
        }
        if (keycode >= sf::Keyboard::Key::Num0 && keycode <= sf::Keyboard::Key::Num9) {
          if (game_mode == FREE_FLY) {
            const int level = (keycode == sf::Keyboard::Key::Num0)
              ? 0
              : (KI(keycode) - KI(sf::Keyboard::Key::Num1) + 1);
            scene.SetFlySpeedLevel(level);
          } else {
            scene.Cheat_Param(KI(keycode) - KI(sf::Keyboard::Key::Num1));
          }
        }
        all_keys[KI(keycode)] = true;
      } else if (const auto* keyEvent = event->getIf<sf::Event::KeyReleased>()) {
        const sf::Keyboard::Key keycode = keyEvent->code;
        if (keycode == sf::Keyboard::Key::Unknown) { continue; }
        all_keys[KI(keycode)] = false;
      } else if (const auto* mbEvent = event->getIf<sf::Event::MouseButtonPressed>()) {
        if (mbEvent->button == sf::Mouse::Button::Left) {
          mouse_pos = mbEvent->position;
          mouse_clicked = true;
          if (game_mode == MAIN_MENU) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::PLAY, Overlays::EXIT);
            if (selected == Overlays::PLAY) {
              game_mode = PLAYING;
              menu_music.stop();
              scene.StartNewGame();
              scene.GetCurMusic().setVolume(GetVol());
              scene.GetCurMusic().play();
              LockMouse(window);
            } else if (selected == Overlays::CONTROLS) {
              game_mode = CONTROLS;
            } else if (selected == Overlays::LEVELS) {
              game_mode = LEVELS;
              overlays.GetLevelPage() = 0;
              scene.SetExposure(0.5f);
            } else if (selected == Overlays::SCREEN_SAVER) {
              game_mode = SCREEN_SAVER;
              scene.SetMode(Scene::SCREEN_SAVER);
              LockMouse(window);
            } else if (selected == Overlays::EXPLORE) {
              game_mode = FRACTAL_SELECT;
            } else if (selected == Overlays::EXIT) {
              window.close();
              break;
            }
          } else if (game_mode == CONTROLS) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::BACK, Overlays::BACK);
            if (selected == Overlays::BACK) {
              game_mode = MAIN_MENU;
            }
          } else if (game_mode == LEVELS) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::L0, Overlays::BACK2);
            if (selected == Overlays::BACK2) {
              game_mode = MAIN_MENU;
              scene.SetExposure(1.0f);
            } else if (selected == Overlays::PREV) {
              overlays.GetLevelPage() -= 1;
            } else if (selected == Overlays::NEXT) {
              overlays.GetLevelPage() += 1;
            } else if (selected >= Overlays::L0 && selected <= Overlays::L14) {
              const int level = selected - Overlays::L0 + overlays.GetLevelPage() * Overlays::LEVELS_PER_PAGE;
              if (high_scores.HasUnlocked(level)) {
                game_mode = PLAYING;
                menu_music.stop();
                scene.SetExposure(1.0f);
                scene.StartSingle(level);
                scene.GetCurMusic().setVolume(GetVol());
                scene.GetCurMusic().play();
                LockMouse(window);
              }
            }
          } else if (game_mode == SCREEN_SAVER) {
            scene.SetMode(Scene::INTRO);
            game_mode = MAIN_MENU;
            UnlockMouse(window);
          } else if (game_mode == FRACTAL_SELECT) {
            const int tab = overlays.GetBrowserTabHover();
            const int hover = overlays.GetBrowserHover();
            if (tab >= 0) {
              browser_cat = tab;
            } else if (hover >= 0) {
              browser_sel[browser_cat] = hover;
              launch_selected();
            }
          } else if (game_mode == PAUSED) {
            const Overlays::Texts selected = overlays.GetOption(Overlays::CONTINUE, Overlays::QUALITY);
            if (selected == Overlays::CONTINUE) {
              game_mode = PLAYING;
              scene.GetCurMusic().setVolume(GetVol());
              scene.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::RESTART) {
              game_mode = PLAYING;
              scene.ResetLevel();
              scene.GetCurMusic().setVolume(GetVol());
              scene.SetExposure(1.0f);
              LockMouse(window);
            } else if (selected == Overlays::QUIT) {
              if (scene.IsSinglePlay()) {
                game_mode = LEVELS;
              } else {
                game_mode = MAIN_MENU;
                scene.SetExposure(1.0f);
              }
              scene.SetMode(Scene::INTRO);
              scene.StopAllMusic();
              menu_music.setVolume(GetVol());
              menu_music.play();
            } else if (selected == Overlays::MUSIC) {
              game_settings.mute = !game_settings.mute;
              for (int i = 0; i < num_level_music; ++i) {
                level_music[i].setVolume(GetVol());
              }
            } else if (selected == Overlays::MOUSE) {
              game_settings.mouse_sensitivity = (game_settings.mouse_sensitivity + 1) % 3;
            } else if (selected == Overlays::SHADOWS) {
              game_settings.shadows = game_settings.shadows ? 0 : 1;
              settings_shader_dirty = true;
            } else if (selected == Overlays::QUALITY) {
              game_settings.quality = (game_settings.quality + 1) % 3;
              settings_shader_dirty = true;
            }
          }
        } else if (mbEvent->button == sf::Mouse::Button::Right) {
          if (game_mode == PLAYING) {
            scene.ResetLevel();
          }
        }
      } else if (const auto* mbEvent = event->getIf<sf::Event::MouseButtonReleased>()) {
        if (mbEvent->button == sf::Mouse::Button::Left) {
          mouse_pos = mbEvent->position;
          mouse_clicked = false;
        }
      } else if (const auto* mmEvent = event->getIf<sf::Event::MouseMoved>()) {
        mouse_pos = mmEvent->position;
      } else if (const auto* mwEvent = event->getIf<sf::Event::MouseWheelScrolled>()) {
        mouse_wheel += mwEvent->delta;
      }
    }

    //Check if the game was beat
    if (scene.GetMode() == Scene::FINAL && game_mode != CREDITS) {
      game_mode = CREDITS;
      scene.StopAllMusic();
      scene.SetExposure(0.5f);
      credits_music.play();
    } else if (scene.GetMode() == Scene::MIDPOINT && game_mode != MIDPOINT) {
      game_mode = MIDPOINT;
      scene.StopAllMusic();
      scene.SetExposure(0.5f);
      credits_music.play();
    }

    //Main game update
    if (game_mode == MAIN_MENU) {
      scene.UpdateCamera();
      overlays.UpdateMenu((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == CONTROLS) {
      scene.UpdateCamera();
      overlays.UpdateControls((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == LEVELS) {
      scene.UpdateCamera();
      overlays.UpdateLevels((float)mouse_pos.x, (float)mouse_pos.y);
    } else if (game_mode == SCREEN_SAVER) {
      const sf::Vector2i mouse_delta = mouse_pos - screen_center;
      sf::Mouse::setPosition(screen_center, window);
      float ms = mouse_sensitivity;
      if (game_settings.mouse_sensitivity == 1) { ms *= 0.5f; }
      else if (game_settings.mouse_sensitivity == 2) { ms *= 0.25f; }
      scene.UpdateCamera(float(-mouse_delta.x) * ms, float(-mouse_delta.y) * ms, mouse_wheel * wheel_sensitivity);
    } else if (game_mode == FRACTAL_SELECT) {
      //Keep animating the background unless a frozen free-fly view is behind
      if (scene.GetMode() != Scene::FREE_FLY) {
        scene.UpdateCamera();
      }
      //Mouse wheel flips pages
      if (mouse_wheel != 0.0f) {
        const int count = NumFractals(browser_cat);
        int& bsel = browser_sel[browser_cat];
        if (mouse_wheel < 0.0f) {
          bsel = std::min(bsel + Overlays::browser_per_page, count - 1);
        } else {
          bsel = std::max(bsel - Overlays::browser_per_page, 0);
        }
      }
      overlays.UpdateFractalBrowser((float)mouse_pos.x, (float)mouse_pos.y,
                                    browser_cat, browser_sel[browser_cat]);
    } else if (game_mode == EXPLORE_2D) {
      const float pan_x = (all_keys[KI(sf::Keyboard::Key::D)] ? 1.0f : 0.0f) -
                          (all_keys[KI(sf::Keyboard::Key::A)] ? 1.0f : 0.0f);
      const float pan_y = (all_keys[KI(sf::Keyboard::Key::W)] ? 1.0f : 0.0f) -
                          (all_keys[KI(sf::Keyboard::Key::S)] ? 1.0f : 0.0f);
      const float zoom_dir = (all_keys[KI(sf::Keyboard::Key::E)] ? 1.0f : 0.0f) -
                             (all_keys[KI(sf::Keyboard::Key::Q)] ? 1.0f : 0.0f);
      const sf::Vector2u win_size = window.getSize();
      explorer.Update((float)mouse_pos.x / (float)win_size.x,
                      (float)mouse_pos.y / (float)win_size.y,
                      mouse_clicked, mouse_wheel, pan_x, pan_y, zoom_dir);
    } else if (game_mode == FREE_FLY) {
      const sf::Vector2i mouse_delta = mouse_pos - screen_center;
      sf::Mouse::setPosition(screen_center, window);
      float ms = mouse_sensitivity;
      if (game_settings.mouse_sensitivity == 1) { ms *= 0.5f; }
      else if (game_settings.mouse_sensitivity == 2) { ms *= 0.25f; }
      const float move_fb = (all_keys[KI(sf::Keyboard::Key::W)] ? 1.0f : 0.0f) +
                            (all_keys[KI(sf::Keyboard::Key::S)] ? -1.0f : 0.0f);
      const float move_lr = (all_keys[KI(sf::Keyboard::Key::D)] ? 1.0f : 0.0f) +
                            (all_keys[KI(sf::Keyboard::Key::A)] ? -1.0f : 0.0f);
      const float move_ud = (all_keys[KI(sf::Keyboard::Key::Space)] ? 1.0f : 0.0f) +
                            (all_keys[KI(sf::Keyboard::Key::LControl)] ? -1.0f : 0.0f);
      const float sprint_mult = all_keys[KI(sf::Keyboard::Key::LShift)] ? 8.0f : 1.0f;
      scene.UpdateFreeFlyCam(float(-mouse_delta.x) * ms, float(-mouse_delta.y) * ms,
                             move_lr, move_ud, move_fb, sprint_mult, mouse_wheel);
      //Parameter editor: hold Left/Right to adjust the selected parameter
      if (edit_param >= 0) {
        const float dir = (all_keys[KI(sf::Keyboard::Key::Right)] ? 1.0f : 0.0f) -
                          (all_keys[KI(sf::Keyboard::Key::Left)] ? 1.0f : 0.0f);
        if (dir != 0.0f) {
          scene.AdjustParam(edit_param, dir * (all_keys[KI(sf::Keyboard::Key::LShift)] ? 5.0f : 1.0f));
        }
      }
    } else if (game_mode == PLAYING || game_mode == CREDITS || game_mode == MIDPOINT) {
      //Collect keyboard input
      const float force_lr =
        (all_keys[KI(sf::Keyboard::Key::Left)] || all_keys[KI(sf::Keyboard::Key::A)] ? -1.0f : 0.0f) +
        (all_keys[KI(sf::Keyboard::Key::Right)] || all_keys[KI(sf::Keyboard::Key::D)] ? 1.0f : 0.0f);
      const float force_ud =
        (all_keys[KI(sf::Keyboard::Key::Down)] || all_keys[KI(sf::Keyboard::Key::S)] ? -1.0f : 0.0f) +
        (all_keys[KI(sf::Keyboard::Key::Up)] || all_keys[KI(sf::Keyboard::Key::W)] ? 1.0f : 0.0f);

      //Collect mouse input
      const sf::Vector2i mouse_delta = mouse_pos - screen_center;
      sf::Mouse::setPosition(screen_center, window);
      float ms = mouse_sensitivity;
      if (game_settings.mouse_sensitivity == 1) {
        ms *= 0.5f;
      } else if (game_settings.mouse_sensitivity == 2) {
        ms *= 0.25f;
      }
      const float cam_lr = float(-mouse_delta.x) * ms;
      const float cam_ud = float(-mouse_delta.y) * ms;
      const float cam_z = mouse_wheel * wheel_sensitivity;

      //Apply forces to marble and camera
      scene.UpdateMarble(force_lr, force_ud);
      scene.UpdateCamera(cam_lr, cam_ud, cam_z, mouse_clicked);
    } else if (game_mode == PAUSED) {
      overlays.UpdatePaused((float)mouse_pos.x, (float)mouse_pos.y);
    }

    //Recompile the fractal shader if the fractal or graphics settings changed
    if (scene.PopShaderDirty() || settings_shader_dirty) {
      settings_shader_dirty = false;
      if (!BuildFractalShader(shader, vert_src, frag_src, scene.GetShaderSel())) {
        ERROR_MSG("Failed to recompile fractal shader");
      }
      shader.setUniform("iResolution", window_res);
    }

    bool skip_frame = false;
    if (lag_ms >= 1000.0f / target_fps) {
      //If there is too much lag, just do another frame of physics and skip the draw
      lag_ms -= 1000.0f / target_fps;
      skip_frame = true;
    } else {
      //Update the shader values (2D explorer has its own flat shader)
      sf::Shader* active_shader = &shader;
      if (game_mode == EXPLORE_2D) {
        explorer.Write();
        active_shader = &explorer.GetShader();
      } else {
        scene.Write(shader);
      }

      //Setup full-screen shader
      sf::RenderStates states = sf::RenderStates::Default;
      states.shader = active_shader;

      //Draw the fractal
      if (fullscreen) {
        //Draw to the render texture
        renderTexture.draw(rect, states);
        renderTexture.display();

        //Draw render texture to main window
        sf::Sprite sprite(renderTexture.getTexture());
        sprite.setScale({float(screen_size.size.x) / float(resolution->width),
                         float(screen_size.size.y) / float(resolution->height)});
        window.draw(sprite);
      } else {
        //Draw directly to the main window
        window.draw(rect, states);
      }
    }

    //Draw text overlays to the window
    if (game_mode == MAIN_MENU) {
      overlays.DrawMenu(window);
    } else if (game_mode == CONTROLS) {
      overlays.DrawControls(window);
    } else if (game_mode == LEVELS) {
      overlays.DrawLevels(window);
    } else if (game_mode == PLAYING) {
      if (scene.GetMode() == Scene::ORBIT && scene.GetMarble().x() < 998.0f) {
        overlays.DrawLevelDesc(window, scene.GetLevel());
      } else if (scene.GetMode() == Scene::MARBLE && !scene.IsFreeCamera()) {
        overlays.DrawArrow(window, scene.GetGoalDirection());
      }
      if (!scene.HasCheats() || scene.GetCountdownTime() < 4 * 60) {
        overlays.DrawTimer(window, scene.GetCountdownTime(), scene.IsHighScore());
      }
      if (!scene.HasCheats() && scene.IsFullRun() && !scene.IsFreeCamera()) {
        overlays.DrawSumTime(window, scene.GetSumTime());
      }
      if (scene.HasCheats() && !scene.IsFreeCamera()) {
        overlays.DrawCheatsEnabled(window);
      }
      if (show_cheats) {
        overlays.DrawCheats(window);
      }
    } else if (game_mode == PAUSED) {
      overlays.DrawPaused(window);
      if (scene.HasCheats()) {
        overlays.DrawCheatsEnabled(window);
      }
    } else if (game_mode == FRACTAL_SELECT) {
      overlays.DrawFractalBrowser(window, browser_cat, browser_sel[browser_cat]);
    } else if (game_mode == FREE_FLY) {
      if (hud_visible) {
        overlays.DrawFreeFlyHUD(window, scene.GetParams(), scene.GetFlySpeedLevel(),
                                scene.GetFov(), scene.IsDrifting(), edit_param);
      }
    } else if (game_mode == EXPLORE_2D) {
      if (hud_visible) {
        overlays.DrawExplorer2D(window, explorer.GetKind(), explorer.GetMagnification(),
                                explorer.Iters(), explorer.GetPalette(), explorer.PrecisionMode(),
                                explorer.JuliaLive(), explorer.JuliaCX(), explorer.JuliaCY());
      }
    } else if (game_mode == CREDITS) {
      overlays.DrawCredits(window, scene.IsFullRun(), scene.GetSumTime());
    } else if (game_mode == MIDPOINT) {
      overlays.DrawMidPoint(window, scene.IsFullRun(), scene.GetSumTime());
    }
    const bool hud_hidden = !hud_visible && (game_mode == FREE_FLY || game_mode == EXPLORE_2D);
    if (!scene.IsFreeCamera() && !hud_hidden) {
      overlays.DrawFPS(window, int(smooth_fps + 0.5f));
    }

    if (!skip_frame) {
      //Screenshot: grab the back buffer before presenting
      if (want_screenshot) {
        want_screenshot = false;
        sf::Texture snap_tex;
        if (snap_tex.resize(window.getSize())) {
          snap_tex.update(window);
          const std::string shot_dir = save_dir + "/screenshots";
          std::error_code ec;
          std::filesystem::create_directories(shot_dir, ec);
          char shot_name[64];
          const std::time_t now_t = std::time(nullptr);
#ifdef _WIN32
          std::tm tm_buf;
          localtime_s(&tm_buf, &now_t);
#else
          std::tm tm_buf;
          localtime_r(&now_t, &tm_buf);
#endif
          std::strftime(shot_name, sizeof(shot_name), "/mm_%Y%m%d_%H%M%S.png", &tm_buf);
          (void)snap_tex.copyToImage().saveToFile(shot_dir + shot_name);
        }
      }

      //Finally display to the screen
      window.display();

      //If V-Sync is running higher than desired fps, slow down!
      const float s = clock.restart().asSeconds();
      if (s > 0.0f) {
        smooth_fps = smooth_fps*0.9f + std::min(1.0f / s, target_fps)*0.1f;
      }
      const float time_diff_ms = 1000.0f * (1.0f / target_fps - s);
      if (time_diff_ms > 0) {
        sf::sleep(sf::seconds(time_diff_ms / 1000.0f));
        lag_ms = std::max(lag_ms - time_diff_ms, 0.0f);
      } else if (time_diff_ms < 0) {
        lag_ms += std::max(-time_diff_ms, 0.0f);
      }
    }
  }

  //Stop all music
  menu_music.stop();
  scene.StopAllMusic();
  credits_music.stop();
  high_scores.Save(save_file);
  game_settings.Save(settings_file);

#ifdef _DEBUG
  system("pause");
#endif
  return 0;
}
