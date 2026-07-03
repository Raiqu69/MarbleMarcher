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
#include "Settings.h"
#include "Level.h"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <optional>
#include <vector>

extern Settings game_settings;

class Overlays {
public:
  enum Texts {
    TITLE,
    PLAY,
    LEVELS,
    CONTROLS,
    SCREEN_SAVER,
    EXPLORE,
    EXIT,
    CREDITS,
    PAUSED,
    CONTINUE,
    RESTART,
    QUIT,
    MUSIC,
    MOUSE,
    SHADOWS,
    QUALITY,
    CONTROLS_L,
    CONTROLS_R,
    BACK,
    L0, L1, L2, L3, L4,
    L5, L6, L7, L8, L9,
    L10, L11, L12, L13, L14,
    NEXT,
    PREV,
    BACK2,
    NUM_TEXTS
  };
  static const int LEVELS_PER_PAGE = 15;
  static const int browser_per_page = 6;

  Overlays(const sf::Font* _font, const sf::Font* _font_mono);

  //Relative to 1280x720
  void SetScale(float scale) { draw_scale = scale; }

  Texts GetOption(Texts from, Texts to);
  int& GetLevelPage() { return level_page; }

  void UpdateMenu(float mouse_x, float mouse_y);
  void UpdateControls(float mouse_x, float mouse_y);
  void UpdateLevels(float mouse_x, float mouse_y);
  void UpdatePaused(float mouse_x, float mouse_y);
  void UpdateFractalBrowser(float mouse_x, float mouse_y, int category, int selected);

  void DrawMenu(sf::RenderWindow& window);
  void DrawControls(sf::RenderWindow& window);
  void DrawTimer(sf::RenderWindow& window, int t, bool is_high_score);
  void DrawLevelDesc(sf::RenderWindow& window, int level);
  void DrawFPS(sf::RenderWindow& window, int fps);
  void DrawPaused(sf::RenderWindow& window);
  void DrawArrow(sf::RenderWindow& window, const sf::Vector3f& v3);
  void DrawCredits(sf::RenderWindow& window, bool fullrun, int t);
  void DrawMidPoint(sf::RenderWindow& window, bool fullrun, int t);
  void DrawLevels(sf::RenderWindow& window);
  void DrawSumTime(sf::RenderWindow& window, int t);
  void DrawCheatsEnabled(sf::RenderWindow& window);
  void DrawCheats(sf::RenderWindow& window);
  void DrawFractalBrowser(sf::RenderWindow& window, int category, int selected);
  void DrawFreeFlyHUD(sf::RenderWindow& window, const FractalParams& params,
                      int speed_level, float fov, bool drift, int edit_param);
  void DrawExplorer2D(sf::RenderWindow& window, int kind, double magnification,
                      int iters, int palette, int precision_mode,
                      bool julia_live, float jc_x, float jc_y);

  int GetBrowserHover() const { return fractal_hover; }     // item index or -1
  int GetBrowserTabHover() const { return tab_hover; }      // category or -1

protected:
  void MakeText(const char* str, float x, float y, float size, const sf::Color& color, sf::Text& text, bool mono=false);
  void MakeTime(int t, float x, float y, float size, const sf::Color& color, sf::Text& text);
  void UpdateHover(Texts from, Texts to, float mouse_x, float mouse_y);

private:
  std::vector<sf::Text> all_text;
  bool all_hover[NUM_TEXTS];

  sf::SoundBuffer buff_hover;
  std::optional<sf::Sound> sound_hover;
  sf::SoundBuffer buff_click;
  std::optional<sf::Sound> sound_click;
  sf::SoundBuffer buff_count;
  std::optional<sf::Sound> sound_count;
  sf::SoundBuffer buff_go;
  std::optional<sf::Sound> sound_go;

  sf::Texture arrow_tex;
  std::optional<sf::Sprite> arrow_spr;

  float draw_scale;
  bool top_level;
  int level_page;
  int fractal_hover;
  int tab_hover;

  const sf::Font* font;
  const sf::Font* font_mono;
};
