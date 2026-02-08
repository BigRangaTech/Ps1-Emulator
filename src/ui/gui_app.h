#ifndef PS1EMU_GUI_APP_H
#define PS1EMU_GUI_APP_H

#include "core/emu_core.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ps1emu {

class GuiApp {
public:
  bool run(const std::string &config_path);

private:
  enum class View {
    Library,
    Settings,
    Session
  };

  bool init_sdl();
  void shutdown_sdl();
  void handle_event(const SDL_Event &event, bool &running);
  void render();

  void draw_background();
  void draw_top_bar();
  void draw_sidebar();
  void draw_library_view();
  void draw_settings_view();
  void draw_session_view();
  void draw_bios_picker();
  void scan_bios_candidates();

  bool draw_button(const SDL_Rect &rect, const std::string &label);
  void draw_label(const SDL_Rect &rect, const std::string &text);
  void fill_rect(const SDL_Rect &rect, SDL_Color color);
  void draw_rect(const SDL_Rect &rect, SDL_Color color, int thickness = 1);
  void draw_text(int x, int y, const std::string &text, SDL_Color color, int pt_size, bool bold = false);

  TTF_Font *load_font(int pt_size, bool bold);

  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  int width_ = 1200;
  int height_ = 720;

  bool mouse_down_ = false;
  bool mouse_pressed_ = false;
  int mouse_x_ = 0;
  int mouse_y_ = 0;

  View current_view_ = View::Library;
  std::string config_path_;
  std::string status_message_ = "Ready.";

  EmulatorCore core_;
  bool core_ready_ = false;
  bool show_dynarec_dump_ = false;

  bool bios_input_active_ = false;
  bool bios_input_dirty_ = false;
  SDL_Rect bios_input_rect_{0, 0, 0, 0};
  std::string bios_input_;

  bool bios_picker_open_ = false;
  std::vector<std::string> bios_candidates_;
  int bios_candidate_offset_ = 0;
};

} // namespace ps1emu

#endif
