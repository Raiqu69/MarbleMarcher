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
#include "Overlays.h"
#include "Level.h"
#include "Fractals.h"
#include "Res.h"
#include "Scores.h"
#include <cstdio>

static const float pi = 3.14159265359f;
static const int num_level_pages = 1 + (num_levels - 1) / Overlays::LEVELS_PER_PAGE;
Settings game_settings;

static const char* CATEGORY_NAMES[NUM_FRAC_CATEGORIES] = {
  "3D Fractals", "Game Levels", "2D Fractals"
};
// Clickable tab hitboxes (relative to 1280x720)
static const float TAB_X[NUM_FRAC_CATEGORIES] = { 80.0f, 430.0f, 800.0f };
static const float TAB_W = 330.0f;

Overlays::Overlays(const sf::Font* _font, const sf::Font* _font_mono) :
  font(_font),
  font_mono(_font_mono),
  all_text(NUM_TEXTS, sf::Text(*_font)),
  draw_scale(1.0f),
  level_page(0),
  top_level(true),
  fractal_hover(-1),
  tab_hover(-1) {
  memset(all_hover, 0, sizeof(all_hover));
  (void)buff_hover.loadFromFile(menu_hover_wav);
  sound_hover.emplace(buff_hover);
  (void)buff_click.loadFromFile(menu_click_wav);
  sound_click.emplace(buff_click);
  (void)buff_count.loadFromFile(count_down_wav);
  sound_count.emplace(buff_count);
  (void)buff_go.loadFromFile(count_go_wav);
  sound_go.emplace(buff_go);
  arrow_tex.loadFromFile(arrow_png);
  arrow_tex.setSmooth(true);
  arrow_spr.emplace(arrow_tex);
  arrow_spr->setOrigin({arrow_spr->getLocalBounds().size.x / 2,
                        arrow_spr->getLocalBounds().size.y / 2});
}

Overlays::Texts Overlays::GetOption(Texts from, Texts to) {
  for (int i = from; i <= to; ++i) {
    if (all_hover[i]) {
      sound_click->play();
      return Texts(i);
    }
  }
  return Texts::TITLE;
}

void Overlays::UpdateMenu(float mouse_x, float mouse_y) {
  //Update text boxes
  MakeText("Marble\nMarcher", 60, 20, 72, sf::Color::White, all_text[TITLE]);
  MakeText("Play", 80, 225, 56, sf::Color::White, all_text[PLAY]);
  MakeText("Levels", 80, 287, 56, sf::Color::White, all_text[LEVELS]);
  MakeText("Explore Fractals", 80, 349, 56, sf::Color::White, all_text[EXPLORE]);
  MakeText("Screen Saver", 80, 411, 56, sf::Color::White, all_text[SCREEN_SAVER]);
  MakeText("Controls", 80, 473, 56, sf::Color::White, all_text[CONTROLS]);
  MakeText("Exit", 80, 535, 56, sf::Color::White, all_text[EXIT]);
  MakeText("\xA9""2019 CodeParade 1.1.1\nMusic by PettyTheft", 16, 652, 32, sf::Color::White, all_text[CREDITS], true);

  //Check if mouse intersects anything
  UpdateHover(PLAY, EXIT, mouse_x, mouse_y);
}

void Overlays::UpdateControls(float mouse_x, float mouse_y) {
  //Update text boxes
  MakeText("Controls", 640, 20, 72, sf::Color::White, all_text[TITLE]);
  const sf::FloatRect title_bounds = all_text[TITLE].getLocalBounds();
  all_text[TITLE].setOrigin({title_bounds.size.x / 2, title_bounds.size.y / 2});
  MakeText("Roll\nCamera\nZoom\nRestart\nPause", 40, 200, 46, sf::Color::White, all_text[CONTROLS_L]);
  MakeText("WASD or Arrows\nMouse\nScroll Wheel\nR or Right-Click\nEsc", 280, 200, 46, sf::Color::White, all_text[CONTROLS_R]);
  MakeText("Back", 60, 550, 40, sf::Color::White, all_text[BACK]);

  //Check if mouse intersects anything
  UpdateHover(BACK, BACK, mouse_x, mouse_y);
}

void Overlays::UpdateLevels(float mouse_x, float mouse_y) {
  //Update text boxes
  const int page_start = level_page * LEVELS_PER_PAGE;
  const int page_end = page_start + LEVELS_PER_PAGE;
  for (int j = 0; j < LEVELS_PER_PAGE; ++j) {
    const int i = page_start + j;
    if (i < num_levels) {
      const float y = 80.0f + float(j / 3) * 120.0f;
      const float x = 60.0f + float(j % 3) * 400.0f;
      const char* txt = all_levels[i].txt;
      MakeText(txt, x, y, 32, sf::Color::White, all_text[j + L0]);
      const sf::FloatRect text_bounds = all_text[j + L0].getLocalBounds();
    } else {
      all_text[j + L0] = sf::Text(*font);
    }
  }
  if (level_page > 0) {
    MakeText("<", 540, 652, 48, sf::Color::White, all_text[PREV]);
  } else {
    all_text[PREV] = sf::Text(*font);
  }
  MakeText("Back", 590, 660, 40, sf::Color::White, all_text[BACK2]);
  if (level_page < num_level_pages - 1) {
    MakeText(">", 732, 652, 48, sf::Color::White, all_text[NEXT]);
  } else {
    all_text[NEXT] = sf::Text(*font);
  }

  //Check if mouse intersects anything
  UpdateHover(L0, BACK2, mouse_x, mouse_y);
}

void Overlays::UpdatePaused(float mouse_x, float mouse_y) {
  //Update text boxes
  MakeText("Paused", 540, 288, 54, sf::Color::White, all_text[PAUSED]);
  MakeText("Continue", 370, 356, 40, sf::Color::White, all_text[CONTINUE]);
  MakeText("Restart", 620, 356, 40, sf::Color::White, all_text[RESTART]);
  MakeText("Quit", 845, 356, 40, sf::Color::White, all_text[QUIT]);

  const char* music_txt = (game_settings.mute ? "Music [OFF]" : "Music [ON]");
  MakeText(music_txt, 410, 460, 40, sf::Color::White, all_text[MUSIC]);

  const char* mouse_txt;
  if (game_settings.mouse_sensitivity == 0) {
    mouse_txt = "Mouse [NORMAL]";
  } else if (game_settings.mouse_sensitivity == 1) {
    mouse_txt = "Mouse [LOW]";
  } else {
    mouse_txt = "Mouse [VERY LOW]";
  }
  MakeText(mouse_txt, 410, 508, 40, sf::Color::White, all_text[MOUSE]);

  const char* shadow_txt = (game_settings.shadows ? "Shadows [ON]" : "Shadows [OFF]");
  MakeText(shadow_txt, 410, 556, 40, sf::Color::White, all_text[SHADOWS]);

  const char* quality_txt;
  if (game_settings.quality == 0) {
    quality_txt = "Quality [LOW]";
  } else if (game_settings.quality == 1) {
    quality_txt = "Quality [MEDIUM]";
  } else {
    quality_txt = "Quality [HIGH]";
  }
  MakeText(quality_txt, 410, 604, 40, sf::Color::White, all_text[QUALITY]);

  //Check if mouse intersects anything
  UpdateHover(CONTINUE, QUALITY, mouse_x, mouse_y);
}

void Overlays::DrawMenu(sf::RenderWindow& window) {
  for (int i = TITLE; i <= CREDITS; ++i) {
    window.draw(all_text[i]);
  }
}

void Overlays::DrawControls(sf::RenderWindow& window) {
  for (int i = CONTROLS_L; i <= BACK; ++i) {
    window.draw(all_text[i]);
  }
}

void Overlays::DrawTimer(sf::RenderWindow& window, int t, bool is_high_score) {
  sf::Text text(*font);
  if (t < 0) {
    return;
  } else if (t < 3*60) {
    //Create text for the number
    char txt[] = "0";
    txt[0] = '3' - (t / 60);
    MakeText(txt, 640, 50, 140, sf::Color::White, text);

    //Play count sound if needed
    if (t % 60 == 0) {
      sound_count->play();
    }
  } else if (t < 4*60) {
    MakeText("GO!", 640, 50, 140, sf::Color::White, text);

    //Play go sound if needed
    if (t == 3*60) {
      sound_go->play();
    }
  } else {
    //Create timer text
    const int score = t - 3 * 60;
    const sf::Color col = (is_high_score ? sf::Color::Green : sf::Color::White);
    MakeTime(score, 530, 10, 60, col, text);
  }

  if (t < 4*60) {
    //Apply zoom animation
    const float fpart = float(t % 60) / 60.0f;
    const float zoom = 0.8f + fpart*0.2f;
    const std::uint8_t alpha = static_cast<std::uint8_t>(255.0f*(1.0f - fpart*fpart));
    text.setScale({zoom, zoom});
    text.setFillColor(sf::Color(255, 255, 255, alpha));
    text.setOutlineColor(sf::Color(0, 0, 0, alpha));

    //Center text
    const sf::FloatRect text_bounds = text.getLocalBounds();
    text.setOrigin({text_bounds.size.x / 2, text_bounds.size.y / 2});
  }

  //Draw the text
  window.draw(text);
}

void Overlays::DrawLevelDesc(sf::RenderWindow& window, int level) {
  sf::Text text(*font);
  MakeText(all_levels[level].txt, 640, 60, 48, sf::Color::White, text);
  const sf::FloatRect text_bounds = text.getLocalBounds();
  text.setOrigin({text_bounds.size.x / 2, text_bounds.size.y / 2});
  window.draw(text);
}

void Overlays::DrawFPS(sf::RenderWindow& window, int fps) {
  sf::Text text(*font);
  std::string fps_str = std::to_string(fps) + "fps";
  const sf::Color col = (fps < 50 ? sf::Color::Red : sf::Color::White);
  MakeText(fps_str.c_str(), 1280, 720, 24, col, text, false);
  const sf::FloatRect text_bounds = text.getLocalBounds();
  text.setOrigin({text_bounds.size.x, text_bounds.size.y});
  window.draw(text);
}

void Overlays::DrawPaused(sf::RenderWindow& window) {
  for (int i = PAUSED; i <= QUALITY; ++i) {
    window.draw(all_text[i]);
  }
}

void Overlays::DrawArrow(sf::RenderWindow& window, const sf::Vector3f& v3) {
  const float x_scale = 250.0f * v3.y + 520.0f * (1.0f - v3.y);
  const float x = 640.0f + x_scale * std::cos(v3.x);
  const float y = 360.0f + 250.0f * std::sin(v3.x);
  const std::uint8_t alpha = static_cast<std::uint8_t>(102.0f * std::max(0.0f, std::min(1.0f, (v3.z - 5.0f) / 30.0f)));
  if (alpha > 0) {
    arrow_spr->setScale({draw_scale * 0.1f, draw_scale * 0.1f});
    arrow_spr->setRotation(sf::degrees(90.0f + v3.x * 180.0f / pi));
    arrow_spr->setPosition({draw_scale * x, draw_scale * y});
    arrow_spr->setColor(sf::Color(255, 255, 255, alpha));
    window.draw(*arrow_spr);
  }
}

void Overlays::DrawCredits(sf::RenderWindow& window, bool fullrun, int t) {
  const char* txt =
    "  Congratulations, you beat all the levels!\n\n\n\n"
    "As a reward, cheats have been unlocked!\n"
    "Activate them with the F1 key during gameplay.\n\n"
    "Thanks for playing!";
  sf::Text text(*font);
  MakeText(txt, 100, 100, 44, sf::Color::White, text);
  text.setLineSpacing(1.3f);
  window.draw(text);

  if (fullrun) {
    sf::Text time_txt(*font_mono);
    MakeTime(t, 640, 226, 72, sf::Color::White, time_txt);
    const sf::FloatRect text_bounds = time_txt.getLocalBounds();
    time_txt.setOrigin({text_bounds.size.x / 2, text_bounds.size.y / 2});
    window.draw(time_txt);
  }
}

void Overlays::DrawMidPoint(sf::RenderWindow& window, bool fullrun, int t) {
  const char* txt =
    "            You've done well so far.\n\n\n\n"
    "      But this is only the beginning.\n"
    "If you need a quick break, take it now.\n"
    "The challenge levels are coming up...";
  sf::Text text(*font);
  MakeText(txt, 205, 100, 44, sf::Color::White, text);
  text.setLineSpacing(1.3f);
  window.draw(text);

  if (fullrun) {
    sf::Text time_txt(*font_mono);
    MakeTime(t, 640, 226, 72, sf::Color::White, time_txt);
    const sf::FloatRect text_bounds = time_txt.getLocalBounds();
    time_txt.setOrigin({text_bounds.size.x / 2, text_bounds.size.y / 2});
    window.draw(time_txt);
  }
}

void Overlays::DrawLevels(sf::RenderWindow& window) {
  //Draw the level names
  for (int i = L0; i <= BACK2; ++i) {
    window.draw(all_text[i]);
  }
  //Draw the times
  const int page_start = level_page * LEVELS_PER_PAGE;
  const int page_end = page_start + LEVELS_PER_PAGE;
  for (int i = page_start; i < page_end; ++i) {
    if (i < num_levels && high_scores.HasCompleted(i)) {
      sf::Text text(*font_mono);
      const int j = i % LEVELS_PER_PAGE;
      const float y = 98.0f + float(j / 3) * 120.0f;
      const float x = 148.0f + float(j % 3) * 400.0f;
      MakeTime(high_scores.Get(i), x, y, 48, sf::Color(64, 255, 64), text);
      window.draw(text);
    }
  }
}

void Overlays::DrawSumTime(sf::RenderWindow& window, int t) {
  sf::Text text(*font_mono);
  MakeTime(t, 10, 680, 32, sf::Color::White, text);
  window.draw(text);
}

void Overlays::DrawCheatsEnabled(sf::RenderWindow& window) {
  sf::Text text(*font);
  MakeText("Cheats Enabled", 10, 680, 32, sf::Color::White, text);
  window.draw(text);
}

void Overlays::DrawCheats(sf::RenderWindow& window) {
  sf::Text text(*font_mono);
  const char* txt =
    "[ C ] Color change\n"
    "[ F ] Free camera\n"
    "[ G ] Gravity strength\n"
    "[ H ] Hyperspeed toggle\n"
    "[ I ] Ignore goal\n"
    "[ M ] Motion disable\n"
    "[ P ] Planet toggle\n"
    "[ Z ] Zoom to scale\n"
    "[1-9] Scroll fractal parameter\n";
  MakeText(txt, 460, 160, 32, sf::Color::White, text, true);
  window.draw(text);
}

void Overlays::MakeText(const char* str, float x, float y, float size, const sf::Color& color, sf::Text& text, bool mono) {
  text.setString(str);
  text.setFont(mono ? *font_mono : *font);
  text.setCharacterSize(int(size * draw_scale));
  text.setLetterSpacing(0.8f);
  text.setPosition({(x - 2.0f) * draw_scale, (y - 2.0f) * draw_scale});
  text.setFillColor(color);
  text.setOutlineThickness(3.0f * draw_scale);
  text.setOutlineColor(sf::Color::Black);
}

void Overlays::MakeTime(int t, float x, float y, float size, const sf::Color& color, sf::Text& text) {
  //Create timer text
  char txt[] = "00:00:00";
  const int t_all = std::min(t, 59 * (60 * 60 + 60 + 1));
  const int t_ms = t_all % 60;
  const int t_sec = (t_all / 60) % 60;
  const int t_min = t_all / (60 * 60);
  txt[0] = '0' + t_min / 10; txt[1] = '0' + t_min % 10;
  txt[3] = '0' + t_sec / 10; txt[4] = '0' + t_sec % 10;
  txt[6] = '0' + t_ms / 10;  txt[7] = '0' + t_ms % 10;
  MakeText(txt, x, y, size, color, text, true);
}

void Overlays::DrawFreeFlyHUD(sf::RenderWindow& window, const FractalParams& params,
                              int speed_level, float fov, bool drift, int edit_param) {
  sf::Text text(*font_mono);
  char buf[128];
  if (speed_level == 0) {
    std::snprintf(buf, sizeof(buf), "Spd AUTO   FOV %3.0f%s", fov, drift ? "   Drift ON" : "");
  } else {
    std::snprintf(buf, sizeof(buf), "Spd %d/9   FOV %3.0f%s", speed_level, fov, drift ? "   Drift ON" : "");
  }
  MakeText(buf, 10, 10, 26, sf::Color(220, 220, 220), text, true);
  window.draw(text);

  MakeText("[0]Auto [1-9]Spd [E]dit [V]Drift [H]ud [F12]Shot [F]Menu",
           10, 42, 20, sf::Color(140, 140, 140), text, true);
  window.draw(text);

  if (edit_param >= 0) {
    static const char* PARAM_NAMES[num_fractal_params] = {
      "Scale ", "Angle1", "Angle2", "ShiftX", "ShiftY", "ShiftZ",
      "Col R ", "Col G ", "Col B "
    };
    MakeText("Parameter Editor", 10, 90, 24, sf::Color(255, 200, 50), text, true);
    window.draw(text);
    for (int i = 0; i < num_fractal_params; ++i) {
      std::snprintf(buf, sizeof(buf), "%s %s % 8.4f",
                    (i == edit_param ? ">" : " "), PARAM_NAMES[i], params[i]);
      const sf::Color col = (i == edit_param) ? sf::Color(255, 64, 64) : sf::Color(200, 200, 200);
      MakeText(buf, 10, 122.0f + i * 26.0f, 22, col, text, true);
      window.draw(text);
    }
    MakeText("[Up/Down] param  [Left/Right] adjust  (+Shift = fast)",
             10, 122.0f + num_fractal_params * 26.0f + 8.0f, 18,
             sf::Color(140, 140, 140), text, true);
    window.draw(text);
  }
}

void Overlays::DrawExplorer2D(sf::RenderWindow& window, int kind, double magnification,
                              int iters, int palette, int precision_mode,
                              bool julia_live, float jc_x, float jc_y) {
  sf::Text text(*font_mono);
  char buf[160];
  static const char* PREC_NAMES[3] = { "float", "double", "perturb" };
  const char* prec = PREC_NAMES[(precision_mode >= 0 && precision_mode <= 2) ? precision_mode : 0];
  if (magnification < 100000.0) {
    std::snprintf(buf, sizeof(buf), "%s   zoom %.1fx   iter %d   pal %d   [%s]",
                  FractalName(CAT_2D, kind), magnification, iters, palette + 1, prec);
  } else {
    std::snprintf(buf, sizeof(buf), "%s   zoom %.2ex   iter %d   pal %d   [%s]",
                  FractalName(CAT_2D, kind), magnification, iters, palette + 1, prec);
  }
  MakeText(buf, 10, 10, 26, sf::Color(220, 220, 220), text, true);
  window.draw(text);

  if (kind == KIND_JULIA) {
    std::snprintf(buf, sizeof(buf), "c = %+.4f %+.4fi   %s",
                  jc_x, jc_y, julia_live ? "[LIVE - Space freezes]" : "[frozen - Space unfreezes]");
    MakeText(buf, 10, 42, 22, sf::Color(255, 200, 50), text, true);
    window.draw(text);
  }

  MakeText("[Drag/WASD]Pan [Scroll/E/Q]Zoom [P]alette [R]eset [H]ud [F12]Shot [F]Menu",
           10, kind == KIND_JULIA ? 74.0f : 42.0f, 20, sf::Color(140, 140, 140), text, true);
  window.draw(text);
}

void Overlays::UpdateFractalBrowser(float mouse_x, float mouse_y, int category, int selected) {
  const int prev_item = fractal_hover;
  const int prev_tab = tab_hover;
  fractal_hover = -1;
  tab_hover = -1;

  //Category tabs
  const float tab_y_min = 88.0f * draw_scale;
  const float tab_y_max = 135.0f * draw_scale;
  for (int t = 0; t < NUM_FRAC_CATEGORIES; ++t) {
    if (mouse_x >= TAB_X[t] * draw_scale && mouse_x < (TAB_X[t] + TAB_W) * draw_scale &&
        mouse_y >= tab_y_min && mouse_y < tab_y_max) {
      tab_hover = t;
    }
  }

  //Items on the current page
  const int count = NumFractals(category);
  const int page  = selected / browser_per_page;
  const int start = page * browser_per_page;
  const int end   = std::min(start + browser_per_page, count);
  for (int i = start; i < end; ++i) {
    const int vp = i - start;
    const float y_min = (160.0f + vp * 82.0f - 4.0f) * draw_scale;
    const float y_max = y_min + 76.0f * draw_scale;
    if (mouse_y >= y_min && mouse_y < y_max) {
      fractal_hover = i;
      break;
    }
  }

  if ((fractal_hover >= 0 && fractal_hover != prev_item) ||
      (tab_hover >= 0 && tab_hover != prev_tab)) {
    sound_hover->play();
  }
}

void Overlays::DrawFractalBrowser(sf::RenderWindow& window, int category, int selected) {
  sf::Text title(*font);
  sf::Text body(*font_mono);

  MakeText("Fractal Explorer", 60, 14, 48, sf::Color::White, title);
  window.draw(title);

  //Category tabs
  for (int t = 0; t < NUM_FRAC_CATEGORIES; ++t) {
    sf::Color col = sf::Color(150, 150, 150);
    if (t == category) { col = sf::Color(255, 200, 50); }
    if (t == tab_hover) { col = sf::Color(255, 64, 64); }
    MakeText(CATEGORY_NAMES[t], TAB_X[t], 90, 40, col, title);
    window.draw(title);
  }

  const int count = NumFractals(category);
  const int page = selected / browser_per_page;
  const int start = page * browser_per_page;
  const int end = std::min(start + browser_per_page, count);
  const int num_pages = (count + browser_per_page - 1) / browser_per_page;

  for (int i = start; i < end; ++i) {
    const int vp = i - start;
    const float y = 160.0f + vp * 82.0f;
    sf::Color nameCol = sf::Color::White;
    if (fractal_hover == i) { nameCol = sf::Color(255, 64, 64); }
    else if (selected == i) { nameCol = sf::Color(255, 200, 50); }
    MakeText(FractalName(category, i), 100, y, 36, nameCol, title);
    window.draw(title);
    MakeText(FractalDesc(category, i), 100, y + 42, 20, sf::Color(140, 140, 140), body, true);
    window.draw(body);
  }

  if (num_pages > 1) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "- Page %d / %d -", page + 1, num_pages);
    sf::Text page_text(*font_mono);
    MakeText(buf, 570, 656, 26, sf::Color(140, 140, 140), page_text, true);
    window.draw(page_text);
  }

  MakeText("[Left/Right] Category   [Up/Down] Select   [Enter] Go   [Scroll] Page   [Esc] Back",
           80, 692, 20, sf::Color(120, 120, 120), body, true);
  window.draw(body);
}

void Overlays::UpdateHover(Texts from, Texts to, float mouse_x, float mouse_y) {
  for (int i = from; i <= to; ++i) {
    const sf::FloatRect bounds = all_text[i].getGlobalBounds();
    if (bounds.contains({mouse_x, mouse_y})) {
      all_text[i].setFillColor(sf::Color(255, 64, 64));
      if (!all_hover[i]) {
        sound_hover->play();
        all_hover[i] = true;
      }
    } else {
      all_hover[i] = false;
    }
  }
}
