#include "ui/gui_app.h"

#include "core/app_paths.h"
#include "ui/sdl_backend.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>

namespace ps1emu {

namespace {

SDL_Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  SDL_Color color;
  color.r = r;
  color.g = g;
  color.b = b;
  color.a = a;
  return color;
}

constexpr uint32_t kCyclesMin = 1;
constexpr uint32_t kCyclesMax = 200000000;
constexpr uint32_t kTraceMin = 1;
constexpr uint32_t kTraceMax = 100000000;

bool parse_uint32(const std::string &text, uint32_t &out) {
  std::string trimmed;
  trimmed.reserve(text.size());
  for (char ch : text) {
    if (!std::isspace(static_cast<unsigned char>(ch))) {
      trimmed.push_back(ch);
    }
  }
  if (trimmed.empty()) {
    return false;
  }
  for (char ch : trimmed) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  try {
    unsigned long long value = std::stoull(trimmed);
    if (value > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool validate_uint32_range(const std::string &text, uint32_t min_value, uint32_t max_value, uint32_t &out) {
  if (!parse_uint32(text, out)) {
    return false;
  }
  if (out < min_value || out > max_value) {
    return false;
  }
  return true;
}

} // namespace

bool GuiApp::run(const std::string &config_path) {
  config_path_ = config_path;
  if (!init_sdl()) {
    return false;
  }

  core_ready_ = core_.initialize(config_path_);
  if (!core_ready_) {
    status_message_ = "Failed to initialize core. Check config.";
  } else {
    status_message_ = "Core initialized.";
    core_.set_trace_enabled(trace_enabled_);
    core_.set_trace_period_cycles(trace_period_cycles_);
    core_.set_watchdog_enabled(watchdog_enabled_);
  }
  bios_input_ = core_.config().bios_path;
  cdrom_input_ = core_.config().cdrom_image;
  cycles_input_ = std::to_string(session_cycles_per_frame_);
  trace_input_ = std::to_string(trace_period_cycles_);

  bool running = true;
  while (running) {
    mouse_pressed_ = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handle_event(event, running);
    }

    if (core_ready_ && session_running_) {
      core_.run_for_cycles(session_cycles_per_frame_);
    }

    render();
    SDL_Delay(16);
  }

  if (core_ready_) {
    core_.shutdown();
  }
  shutdown_sdl();
  return true;
}

bool GuiApp::init_sdl() {
  shutdown_sdl();
  if (!init_sdl_video_with_fallback()) {
    std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
    return false;
  }

  if (TTF_WasInit() == 0) {
    if (TTF_Init() != 0) {
      std::cerr << "SDL_ttf init failed: " << TTF_GetError() << "\n";
      SDL_Quit();
      return false;
    }
  }

  window_ = SDL_CreateWindow("PS1 Emulator",
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             width_,
                             height_,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window_) {
    std::cerr << "SDL window failed: " << SDL_GetError() << "\n";
    shutdown_sdl();
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) {
    std::cerr << "SDL renderer failed: " << SDL_GetError() << "\n";
    shutdown_sdl();
    return false;
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  return true;
}

void GuiApp::shutdown_sdl() {
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }
  TTF_Quit();
  SDL_Quit();
}

void GuiApp::handle_event(const SDL_Event &event, bool &running) {
  switch (event.type) {
    case SDL_QUIT:
      running = false;
      break;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
        width_ = event.window.data1;
        height_ = event.window.data2;
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT) {
        mouse_down_ = true;
        mouse_pressed_ = true;
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        mouse_down_ = false;
      }
      break;
    case SDL_MOUSEMOTION:
      mouse_x_ = event.motion.x;
      mouse_y_ = event.motion.y;
      break;
    case SDL_TEXTINPUT:
      if (cycles_input_active_) {
        cycles_input_ += event.text.text;
        cycles_input_dirty_ = true;
      } else if (trace_input_active_) {
        trace_input_ += event.text.text;
        trace_input_dirty_ = true;
      } else if (bios_input_active_) {
        bios_input_ += event.text.text;
        bios_input_dirty_ = true;
      } else if (cdrom_input_active_) {
        cdrom_input_ += event.text.text;
        cdrom_input_dirty_ = true;
      }
      break;
    case SDL_KEYDOWN:
      if (cycles_input_active_) {
        if (event.key.keysym.sym == SDLK_BACKSPACE && !cycles_input_.empty()) {
          cycles_input_.pop_back();
          cycles_input_dirty_ = true;
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
          cycles_input_active_ = false;
          SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
          cycles_input_ = std::to_string(session_cycles_per_frame_);
          cycles_input_dirty_ = false;
          cycles_input_active_ = false;
          SDL_StopTextInput();
        }
      } else if (trace_input_active_) {
        if (event.key.keysym.sym == SDLK_BACKSPACE && !trace_input_.empty()) {
          trace_input_.pop_back();
          trace_input_dirty_ = true;
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
          trace_input_active_ = false;
          SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
          trace_input_ = std::to_string(trace_period_cycles_);
          trace_input_dirty_ = false;
          trace_input_active_ = false;
          SDL_StopTextInput();
        }
      } else if (bios_input_active_) {
        if (event.key.keysym.sym == SDLK_BACKSPACE && !bios_input_.empty()) {
          bios_input_.pop_back();
          bios_input_dirty_ = true;
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
          bios_input_active_ = false;
          SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
          bios_input_ = core_.config().bios_path;
          bios_input_dirty_ = false;
          bios_input_active_ = false;
          SDL_StopTextInput();
        }
      } else if (cdrom_input_active_) {
        if (event.key.keysym.sym == SDLK_BACKSPACE && !cdrom_input_.empty()) {
          cdrom_input_.pop_back();
          cdrom_input_dirty_ = true;
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
          cdrom_input_active_ = false;
          SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
          cdrom_input_ = core_.config().cdrom_image;
          cdrom_input_dirty_ = false;
          cdrom_input_active_ = false;
          SDL_StopTextInput();
        }
      }
      break;
    default:
      break;
  }
}

void GuiApp::render() {
  SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
  SDL_RenderClear(renderer_);

  draw_background();
  draw_top_bar();
  draw_sidebar();

  switch (current_view_) {
    case View::Library:
      draw_library_view();
      break;
    case View::Settings:
      draw_settings_view();
      break;
    case View::Session:
      draw_session_view();
      break;
  }

  if (bios_picker_open_) {
    draw_bios_picker();
  }
  if (cdrom_picker_open_) {
    draw_cdrom_picker();
  }

  SDL_RenderPresent(renderer_);
}

void GuiApp::draw_background() {
  SDL_Color top = rgb(248, 244, 239);
  SDL_Color bottom = rgb(232, 241, 248);

  for (int y = 0; y < height_; ++y) {
    float t = static_cast<float>(y) / static_cast<float>(height_);
    uint8_t r = static_cast<uint8_t>(top.r + t * (bottom.r - top.r));
    uint8_t g = static_cast<uint8_t>(top.g + t * (bottom.g - top.g));
    uint8_t b = static_cast<uint8_t>(top.b + t * (bottom.b - top.b));
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderDrawLine(renderer_, 0, y, width_, y);
  }
}

void GuiApp::draw_top_bar() {
  SDL_Rect bar{0, 0, width_, 64};
  fill_rect(bar, rgb(255, 255, 255, 230));
  draw_rect(bar, rgb(220, 220, 220, 255));

  draw_text(24, 18, "PS1 Emulator", rgb(27, 27, 27), 22, true);
  draw_text(width_ - 320, 22, status_message_, rgb(47, 110, 122), 14, false);
}

void GuiApp::draw_sidebar() {
  SDL_Rect side{0, 64, 220, height_ - 64};
  fill_rect(side, rgb(251, 250, 248, 235));
  draw_rect(side, rgb(220, 220, 220, 255));

  int y = 96;
  SDL_Rect lib{24, y, 172, 44};
  SDL_Rect set{24, y + 60, 172, 44};
  SDL_Rect ses{24, y + 120, 172, 44};

  if (draw_button(lib, "Library")) {
    current_view_ = View::Library;
  }
  if (draw_button(set, "Settings")) {
    current_view_ = View::Settings;
  }
  if (draw_button(ses, "Session")) {
    current_view_ = View::Session;
  }
}

void GuiApp::draw_library_view() {
  SDL_Rect panel{240, 88, width_ - 260, height_ - 120};
  fill_rect(panel, rgb(255, 255, 255, 235));
  draw_rect(panel, rgb(220, 220, 220, 255));

  draw_text(panel.x + 24, panel.y + 18, "Library", rgb(27, 27, 27), 20, true);
  draw_text(panel.x + 24, panel.y + 52, "No games added yet.", rgb(88, 88, 88), 16, false);

  SDL_Rect button{panel.x + 24, panel.y + 92, 220, 44};
  if (draw_button(button, "Add Game Folder")) {
    status_message_ = "Coming soon: game library scanning.";
  }
}

void GuiApp::draw_settings_view() {
  SDL_Rect panel{240, 88, width_ - 260, height_ - 120};
  fill_rect(panel, rgb(255, 255, 255, 235));
  draw_rect(panel, rgb(220, 220, 220, 255));

  draw_text(panel.x + 24, panel.y + 18, "Settings", rgb(27, 27, 27), 20, true);
  draw_text(panel.x + 24, panel.y + 56, "Config file:", rgb(88, 88, 88), 14, false);
  draw_text(panel.x + 24, panel.y + 78, config_path_, rgb(47, 110, 122), 14, false);

  const Config &cfg = core_.config();
  std::string bios_status = cfg.bios_path.empty() ? "HLE BIOS (stub)" : "Real BIOS";
  draw_text(panel.x + 24, panel.y + 118, "BIOS:", rgb(88, 88, 88), 14, false);
  draw_text(panel.x + 80, panel.y + 118, bios_status, rgb(214, 110, 44), 14, true);

  draw_text(panel.x + 24, panel.y + 150, "BIOS path:", rgb(88, 88, 88), 14, false);
  bios_input_rect_ = SDL_Rect{panel.x + 24, panel.y + 172, 520, 36};
  SDL_Color box = bios_input_active_ ? rgb(255, 248, 242) : rgb(246, 243, 239);
  SDL_Color border = bios_input_active_ ? rgb(214, 110, 44) : rgb(220, 220, 220);
  fill_rect(bios_input_rect_, box);
  draw_rect(bios_input_rect_, border, 1);
  std::string bios_text = bios_input_.empty() ? "path/to/bios.bin" : bios_input_;
  SDL_Color bios_color = bios_input_.empty() ? rgb(150, 150, 150) : rgb(27, 27, 27);
  draw_text(bios_input_rect_.x + 10, bios_input_rect_.y + 9, bios_text, bios_color, 14, false);

  if (mouse_pressed_ && !bios_picker_open_) {
    bool inside = mouse_x_ >= bios_input_rect_.x &&
                  mouse_x_ < bios_input_rect_.x + bios_input_rect_.w &&
                  mouse_y_ >= bios_input_rect_.y &&
                  mouse_y_ < bios_input_rect_.y + bios_input_rect_.h;
    if (inside) {
      bios_input_active_ = true;
      cdrom_input_active_ = false;
      SDL_StartTextInput();
    } else if (bios_input_active_) {
      bios_input_active_ = false;
      SDL_StopTextInput();
    }
  }

  SDL_Rect browse{panel.x + 24, panel.y + 220, 180, 42};
  SDL_Rect import_btn{panel.x + 212, panel.y + 220, 180, 42};
  SDL_Rect save{panel.x + 400, panel.y + 220, 180, 42};
  SDL_Rect reload{panel.x + 588, panel.y + 220, 180, 42};

  if (!bios_picker_open_ && draw_button(browse, "Browse BIOS")) {
    scan_bios_candidates();
    bios_picker_open_ = true;
  }
  if (!bios_picker_open_ && draw_button(import_btn, "Import BIOS")) {
    if (bios_input_.empty()) {
      status_message_ = "No BIOS path selected.";
    } else {
      std::error_code ec;
      if (!std::filesystem::exists(bios_input_, ec)) {
        status_message_ = "BIOS path does not exist.";
      } else {
        std::string error;
        std::string bios_dir = app_data_dir() + "/bios";
        if (ensure_directory(bios_dir, error)) {
          std::filesystem::path src(bios_input_);
          std::filesystem::path dst = std::filesystem::path(bios_dir) / src.filename();
          std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
          if (ec) {
            status_message_ = "Failed to copy BIOS.";
          } else {
            bios_input_ = dst.string();
            bios_input_dirty_ = true;
            status_message_ = "BIOS imported to app data.";
          }
        } else {
          status_message_ = error;
        }
      }
    }
  }
  if (!bios_picker_open_ && draw_button(save, "Save BIOS Path")) {
    std::string error;
    if (update_config_value(config_path_, "bios.path", bios_input_, error)) {
      bios_input_dirty_ = false;
      core_.shutdown();
      core_ready_ = core_.initialize(config_path_);
      status_message_ = core_ready_ ? "BIOS path saved." : "Saved BIOS path but core failed.";
    } else {
      status_message_ = error;
    }
  }
  if (!bios_picker_open_ && draw_button(reload, "Reload Config")) {
    core_.shutdown();
    core_ready_ = core_.initialize(config_path_);
    status_message_ = core_ready_ ? "Config reloaded." : "Failed to reload config.";
  }

  draw_text(panel.x + 24, panel.y + 280, "CD-ROM image:", rgb(88, 88, 88), 14, false);
  cdrom_input_rect_ = SDL_Rect{panel.x + 24, panel.y + 302, 620, 36};
  SDL_Color cd_box = cdrom_input_active_ ? rgb(255, 248, 242) : rgb(246, 243, 239);
  SDL_Color cd_border = cdrom_input_active_ ? rgb(214, 110, 44) : rgb(220, 220, 220);
  fill_rect(cdrom_input_rect_, cd_box);
  draw_rect(cdrom_input_rect_, cd_border, 1);
  std::string cd_text = cdrom_input_.empty() ? "path/to/game.cue or .iso" : cdrom_input_;
  SDL_Color cd_color = cdrom_input_.empty() ? rgb(150, 150, 150) : rgb(27, 27, 27);
  draw_text(cdrom_input_rect_.x + 10, cdrom_input_rect_.y + 9, cd_text, cd_color, 14, false);

  if (mouse_pressed_ && !cdrom_picker_open_) {
    bool inside = mouse_x_ >= cdrom_input_rect_.x &&
                  mouse_x_ < cdrom_input_rect_.x + cdrom_input_rect_.w &&
                  mouse_y_ >= cdrom_input_rect_.y &&
                  mouse_y_ < cdrom_input_rect_.y + cdrom_input_rect_.h;
    if (inside) {
      cdrom_input_active_ = true;
      bios_input_active_ = false;
      SDL_StartTextInput();
    } else if (cdrom_input_active_) {
      cdrom_input_active_ = false;
      SDL_StopTextInput();
    }
  }

  SDL_Rect cd_browse{panel.x + 24, panel.y + 350, 180, 42};
  SDL_Rect cd_save{panel.x + 212, panel.y + 350, 180, 42};
  SDL_Rect cd_clear{panel.x + 400, panel.y + 350, 180, 42};

  if (!cdrom_picker_open_ && draw_button(cd_browse, "Browse Disc")) {
    scan_cdrom_candidates();
    cdrom_picker_open_ = true;
  }
  if (!cdrom_picker_open_ && draw_button(cd_save, "Save Disc Path")) {
    std::string error;
    if (update_config_value(config_path_, "cdrom.image", cdrom_input_, error)) {
      cdrom_input_dirty_ = false;
      core_.shutdown();
      core_ready_ = core_.initialize(config_path_);
      status_message_ = core_ready_ ? "CD-ROM path saved." : "Saved CD-ROM path but core failed.";
    } else {
      status_message_ = error;
    }
  }
  if (!cdrom_picker_open_ && draw_button(cd_clear, "Clear Disc")) {
    cdrom_input_.clear();
    cdrom_input_dirty_ = true;
  }

  draw_text(panel.x + 24, panel.y + 410, "CPU mode:", rgb(88, 88, 88), 14, false);
  std::string mode_text = "Auto";
  if (cfg.cpu_mode == CpuMode::Interpreter) {
    mode_text = "Interpreter";
  } else if (cfg.cpu_mode == CpuMode::Dynarec) {
    mode_text = "Dynarec";
  }
  draw_text(panel.x + 120, panel.y + 410, mode_text, rgb(47, 110, 122), 14, true);
}

void GuiApp::scan_bios_candidates() {
  bios_candidates_.clear();
  bios_candidate_offset_ = 0;

  const std::string dirs[] = {"./Bios", "./bios"};
  std::string data_dir = app_data_dir() + "/bios";
  const std::string extra_dirs[] = {data_dir};

  for (const auto &dir : dirs) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
      continue;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      auto path = entry.path();
      std::string ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (ext == ".bin" || ext == ".rom") {
        bios_candidates_.push_back(path.string());
      }
    }
  }
  for (const auto &dir : extra_dirs) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
      continue;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      auto path = entry.path();
      std::string ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (ext == ".bin" || ext == ".rom") {
        bios_candidates_.push_back(path.string());
      }
    }
  }
  std::sort(bios_candidates_.begin(), bios_candidates_.end());
}

void GuiApp::scan_cdrom_candidates() {
  cdrom_candidates_.clear();
  cdrom_candidate_offset_ = 0;

  const std::string dirs[] = {"./test-roms", "./roms", "./games"};
  std::string data_dir = app_data_dir() + "/roms";
  const std::string extra_dirs[] = {data_dir};

  auto add_from_dir = [&](const std::string &dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
      return;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      auto path = entry.path();
      std::string ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (ext == ".cue" || ext == ".iso" || ext == ".bin") {
        cdrom_candidates_.push_back(path.string());
      }
    }
  };

  for (const auto &dir : dirs) {
    add_from_dir(dir);
  }
  for (const auto &dir : extra_dirs) {
    add_from_dir(dir);
  }
  std::sort(cdrom_candidates_.begin(), cdrom_candidates_.end());
}

void GuiApp::draw_cdrom_picker() {
  SDL_Rect overlay{0, 0, width_, height_};
  fill_rect(overlay, rgb(20, 20, 20, 120));

  SDL_Rect panel{width_ / 2 - 300, height_ / 2 - 220, 600, 440};
  fill_rect(panel, rgb(255, 255, 255, 245));
  draw_rect(panel, rgb(220, 220, 220, 255));

  draw_text(panel.x + 20, panel.y + 16, "Select a game image", rgb(27, 27, 27), 16, true);
  draw_text(panel.x + 20, panel.y + 44, "Supported: .cue, .bin, .iso", rgb(88, 88, 88), 12, false);

  int list_y = panel.y + 80;
  int item_height = 32;
  int visible = 9;
  int start = cdrom_candidate_offset_;
  int end = std::min(start + visible, static_cast<int>(cdrom_candidates_.size()));

  if (cdrom_candidates_.empty()) {
    draw_text(panel.x + 20, list_y, "No disc images found.", rgb(214, 110, 44), 14, true);
  }

  for (int i = start; i < end; ++i) {
    SDL_Rect item{panel.x + 20, list_y + (i - start) * (item_height + 6), panel.w - 40, item_height};
    if (draw_button(item, cdrom_candidates_[i])) {
      cdrom_input_ = cdrom_candidates_[i];
      cdrom_input_dirty_ = true;
      cdrom_picker_open_ = false;
      SDL_StopTextInput();
      status_message_ = "Selected CD-ROM image.";
    }
  }

  SDL_Rect rescan{panel.x + 20, panel.y + panel.h - 56, 140, 36};
  SDL_Rect close{panel.x + panel.w - 160, panel.y + panel.h - 56, 140, 36};

  if (draw_button(rescan, "Rescan")) {
    scan_cdrom_candidates();
  }
  if (draw_button(close, "Close")) {
    cdrom_picker_open_ = false;
  }
}

void GuiApp::draw_bios_picker() {
  SDL_Rect overlay{0, 0, width_, height_};
  fill_rect(overlay, rgb(20, 20, 20, 120));

  SDL_Rect panel{width_ / 2 - 260, height_ / 2 - 200, 520, 400};
  fill_rect(panel, rgb(255, 255, 255, 245));
  draw_rect(panel, rgb(220, 220, 220, 255));

  draw_text(panel.x + 20, panel.y + 16, "Select BIOS from ./Bios or ./bios", rgb(27, 27, 27), 16, true);
  draw_text(panel.x + 20, panel.y + 44, "Click a file to use it.", rgb(88, 88, 88), 12, false);

  int list_y = panel.y + 80;
  int item_height = 32;
  int visible = 8;
  int start = bios_candidate_offset_;
  int end = std::min(start + visible, static_cast<int>(bios_candidates_.size()));

  if (bios_candidates_.empty()) {
    draw_text(panel.x + 20, list_y, "No BIOS files found.", rgb(214, 110, 44), 14, true);
  }

  for (int i = start; i < end; ++i) {
    SDL_Rect item{panel.x + 20, list_y + (i - start) * (item_height + 6), panel.w - 40, item_height};
    if (draw_button(item, bios_candidates_[i])) {
      bios_input_ = bios_candidates_[i];
      bios_input_dirty_ = true;
      bios_picker_open_ = false;
      SDL_StopTextInput();
      status_message_ = "Selected BIOS file.";
    }
  }

  SDL_Rect rescan{panel.x + 20, panel.y + panel.h - 56, 140, 36};
  SDL_Rect close{panel.x + panel.w - 160, panel.y + panel.h - 56, 140, 36};

  if (draw_button(rescan, "Rescan")) {
    scan_bios_candidates();
  }
  if (draw_button(close, "Close")) {
    bios_picker_open_ = false;
  }
}

void GuiApp::draw_session_view() {
  SDL_Rect panel{240, 88, width_ - 260, height_ - 120};
  fill_rect(panel, rgb(255, 255, 255, 235));
  draw_rect(panel, rgb(220, 220, 220, 255));

  draw_text(panel.x + 24, panel.y + 18, "Session", rgb(27, 27, 27), 20, true);
  draw_text(panel.x + 24, panel.y + 52, "Controls", rgb(88, 88, 88), 16, false);

  SDL_Rect run_toggle{panel.x + 24, panel.y + 88, 200, 44};
  SDL_Rect run_frame{panel.x + 24, panel.y + 144, 200, 44};
  SDL_Rect run60{panel.x + 24, panel.y + 200, 200, 44};
  SDL_Rect run1000{panel.x + 24, panel.y + 256, 200, 44};
  SDL_Rect dump{panel.x + 24, panel.y + 312, 200, 44};

  if (draw_button(run_toggle, session_running_ ? "Stop Run" : "Start Run")) {
    if (!core_ready_) {
      status_message_ = "Core not initialized.";
    } else {
      session_running_ = !session_running_;
      status_message_ = session_running_ ? "Running (per-frame cycles)." : "Run paused.";
    }
  }
  if (draw_button(run_frame, "Run 1 frame")) {
    if (!core_ready_) {
      status_message_ = "Core not initialized.";
    } else {
      core_.run_for_cycles(session_cycles_per_frame_);
      status_message_ = "Ran one frame of CPU cycles.";
    }
  }
  if (draw_button(run60, "Run 60 cycles")) {
    if (!core_ready_) {
      status_message_ = "Core not initialized.";
    } else {
      core_.run_for_cycles(60);
      status_message_ = "Ran 60 cycles.";
    }
  }
  if (draw_button(run1000, "Run 1000 cycles")) {
    if (!core_ready_) {
      status_message_ = "Core not initialized.";
    } else {
      core_.run_for_cycles(1000);
      status_message_ = "Ran 1000 cycles.";
    }
  }
  if (draw_button(dump, "Dump dynarec")) {
    core_.dump_dynarec_profile();
    status_message_ = "Dynarec profile dumped to console.";
  }

  draw_text(panel.x + 260, panel.y + 88, "Runtime", rgb(88, 88, 88), 16, false);
  draw_text(panel.x + 260, panel.y + 112, "Cycles/frame", rgb(88, 88, 88), 14, false);
  std::string cycles_hint = "Min " + std::to_string(kCyclesMin) + " Max " + std::to_string(kCyclesMax);
  draw_text(panel.x + 420, panel.y + 112, cycles_hint, rgb(120, 120, 120), 12, false);

  uint32_t parsed_cycles = 0;
  bool cycles_valid = validate_uint32_range(cycles_input_, kCyclesMin, kCyclesMax, parsed_cycles);
  bool cycles_invalid = cycles_input_dirty_ && !cycles_valid;

  cycles_input_rect_ = SDL_Rect{panel.x + 260, panel.y + 132, 140, 32};
  SDL_Color cycles_box = cycles_invalid ? rgb(255, 230, 230)
                                        : (cycles_input_active_ ? rgb(255, 248, 242) : rgb(246, 243, 239));
  SDL_Color cycles_border = cycles_invalid ? rgb(200, 60, 60)
                                           : (cycles_input_active_ ? rgb(214, 110, 44) : rgb(220, 220, 220));
  fill_rect(cycles_input_rect_, cycles_box);
  draw_rect(cycles_input_rect_, cycles_border, 1);
  std::string cycles_text = cycles_input_.empty() ? "33868800" : cycles_input_;
  SDL_Color cycles_color = cycles_input_.empty() ? rgb(150, 150, 150) : rgb(27, 27, 27);
  draw_text(cycles_input_rect_.x + 8, cycles_input_rect_.y + 7, cycles_text, cycles_color, 14, false);

  SDL_Rect cycles_apply{panel.x + 410, panel.y + 132, 108, 32};
  if (draw_button(cycles_apply, "Apply")) {
    if (!validate_uint32_range(cycles_input_, kCyclesMin, kCyclesMax, parsed_cycles)) {
      status_message_ = "Cycles/frame must be between " + std::to_string(kCyclesMin) + " and " +
                        std::to_string(kCyclesMax) + ".";
      cycles_input_dirty_ = true;
    } else {
      session_cycles_per_frame_ = parsed_cycles;
      cycles_input_ = std::to_string(session_cycles_per_frame_);
      cycles_input_dirty_ = false;
      status_message_ = "Cycles/frame updated.";
    }
  }

  SDL_Rect rate1{panel.x + 260, panel.y + 172, 60, 32};
  SDL_Rect rate2{panel.x + 326, panel.y + 172, 60, 32};
  SDL_Rect rate4{panel.x + 392, panel.y + 172, 60, 32};
  SDL_Rect rate8{panel.x + 458, panel.y + 172, 60, 32};

  if (draw_button(rate1, "1x")) {
    session_cycles_per_frame_ = 33868800 / 60;
    cycles_input_ = std::to_string(session_cycles_per_frame_);
    cycles_input_dirty_ = false;
    status_message_ = "Cycles/frame set to 1x.";
  }
  if (draw_button(rate2, "2x")) {
    session_cycles_per_frame_ = (33868800 / 60) * 2;
    cycles_input_ = std::to_string(session_cycles_per_frame_);
    cycles_input_dirty_ = false;
    status_message_ = "Cycles/frame set to 2x.";
  }
  if (draw_button(rate4, "4x")) {
    session_cycles_per_frame_ = (33868800 / 60) * 4;
    cycles_input_ = std::to_string(session_cycles_per_frame_);
    cycles_input_dirty_ = false;
    status_message_ = "Cycles/frame set to 4x.";
  }
  if (draw_button(rate8, "8x")) {
    session_cycles_per_frame_ = (33868800 / 60) * 8;
    cycles_input_ = std::to_string(session_cycles_per_frame_);
    cycles_input_dirty_ = false;
    status_message_ = "Cycles/frame set to 8x.";
  }

  draw_text(panel.x + 260, panel.y + 216, "Trace period (cycles)", rgb(88, 88, 88), 14, false);
  std::string trace_hint = "Min " + std::to_string(kTraceMin) + " Max " + std::to_string(kTraceMax);
  draw_text(panel.x + 420, panel.y + 216, trace_hint, rgb(120, 120, 120), 12, false);

  uint32_t parsed_trace = 0;
  bool trace_valid = validate_uint32_range(trace_input_, kTraceMin, kTraceMax, parsed_trace);
  bool trace_invalid = trace_input_dirty_ && !trace_valid;
  trace_input_rect_ = SDL_Rect{panel.x + 260, panel.y + 236, 140, 32};
  SDL_Color trace_box = trace_invalid ? rgb(255, 230, 230)
                                      : (trace_input_active_ ? rgb(255, 248, 242) : rgb(246, 243, 239));
  SDL_Color trace_border = trace_invalid ? rgb(200, 60, 60)
                                         : (trace_input_active_ ? rgb(214, 110, 44) : rgb(220, 220, 220));
  fill_rect(trace_input_rect_, trace_box);
  draw_rect(trace_input_rect_, trace_border, 1);
  std::string trace_text = trace_input_.empty() ? "1000000" : trace_input_;
  SDL_Color trace_color = trace_input_.empty() ? rgb(150, 150, 150) : rgb(27, 27, 27);
  draw_text(trace_input_rect_.x + 8, trace_input_rect_.y + 7, trace_text, trace_color, 14, false);

  SDL_Rect trace_apply{panel.x + 410, panel.y + 236, 108, 32};
  if (draw_button(trace_apply, "Apply")) {
    if (!validate_uint32_range(trace_input_, kTraceMin, kTraceMax, parsed_trace)) {
      status_message_ = "Trace period must be between " + std::to_string(kTraceMin) + " and " +
                        std::to_string(kTraceMax) + ".";
      trace_input_dirty_ = true;
    } else {
      trace_period_cycles_ = parsed_trace;
      trace_input_ = std::to_string(trace_period_cycles_);
      trace_input_dirty_ = false;
      if (core_ready_) {
        core_.set_trace_period_cycles(trace_period_cycles_);
      }
      status_message_ = "Trace period updated.";
    }
  }

  SDL_Rect trace_preset{panel.x + 260, panel.y + 276, 220, 32};
  if (draw_button(trace_preset, "Trace presets")) {
    static const uint32_t periods[] = {1000000, 250000, 50000, 10000};
    const int period_count = static_cast<int>(sizeof(periods) / sizeof(periods[0]));
    trace_period_index_ = (trace_period_index_ + 1) % period_count;
    trace_period_cycles_ = periods[trace_period_index_];
    trace_input_ = std::to_string(trace_period_cycles_);
    trace_input_dirty_ = false;
    if (core_ready_) {
      core_.set_trace_period_cycles(trace_period_cycles_);
    }
    status_message_ = "Trace period updated.";
  }

  SDL_Rect trace_btn{panel.x + 260, panel.y + 316, 220, 36};
  SDL_Rect watchdog_btn{panel.x + 260, panel.y + 360, 220, 36};
  SDL_Rect core_status{panel.x + 260, panel.y + 412, 220, 36};
  if (draw_button(trace_btn, trace_enabled_ ? "Trace: On" : "Trace: Off")) {
    trace_enabled_ = !trace_enabled_;
    if (core_ready_) {
      core_.set_trace_enabled(trace_enabled_);
    }
    status_message_ = trace_enabled_ ? "CPU trace enabled." : "CPU trace disabled.";
  }
  if (draw_button(watchdog_btn, watchdog_enabled_ ? "Watchdog: On" : "Watchdog: Off")) {
    watchdog_enabled_ = !watchdog_enabled_;
    if (core_ready_) {
      core_.set_watchdog_enabled(watchdog_enabled_);
    }
    status_message_ = watchdog_enabled_ ? "Boot watchdog enabled." : "Boot watchdog disabled.";
  }

  fill_rect(core_status, rgb(246, 243, 239));
  draw_rect(core_status, rgb(220, 220, 220), 1);
  draw_text(core_status.x + 12, core_status.y + 10,
            core_ready_ ? "Core online" : "Core offline",
            core_ready_ ? rgb(47, 110, 122) : rgb(214, 110, 44), 14, true);

  if (mouse_pressed_) {
    bool inside_cycles = mouse_x_ >= cycles_input_rect_.x &&
                         mouse_x_ < cycles_input_rect_.x + cycles_input_rect_.w &&
                         mouse_y_ >= cycles_input_rect_.y &&
                         mouse_y_ < cycles_input_rect_.y + cycles_input_rect_.h;
    bool inside_trace = mouse_x_ >= trace_input_rect_.x &&
                        mouse_x_ < trace_input_rect_.x + trace_input_rect_.w &&
                        mouse_y_ >= trace_input_rect_.y &&
                        mouse_y_ < trace_input_rect_.y + trace_input_rect_.h;
    if (inside_cycles) {
      cycles_input_active_ = true;
      trace_input_active_ = false;
      bios_input_active_ = false;
      cdrom_input_active_ = false;
      SDL_StartTextInput();
    } else if (inside_trace) {
      trace_input_active_ = true;
      cycles_input_active_ = false;
      bios_input_active_ = false;
      cdrom_input_active_ = false;
      SDL_StartTextInput();
    } else if (cycles_input_active_ || trace_input_active_) {
      cycles_input_active_ = false;
      trace_input_active_ = false;
      SDL_StopTextInput();
    }
  }
}

bool GuiApp::draw_button(const SDL_Rect &rect, const std::string &label) {
  bool hover = mouse_x_ >= rect.x && mouse_x_ < rect.x + rect.w &&
               mouse_y_ >= rect.y && mouse_y_ < rect.y + rect.h;

  SDL_Color base = rgb(246, 243, 239);
  SDL_Color accent = rgb(214, 110, 44);
  SDL_Color border = rgb(220, 220, 220);
  if (hover) {
    base = rgb(255, 248, 242);
    border = rgb(214, 110, 44);
  }

  fill_rect(rect, base);
  draw_rect(rect, border, 1);
  draw_text(rect.x + 14, rect.y + 12, label, hover ? accent : rgb(27, 27, 27), 14, true);

  return hover && mouse_pressed_;
}

void GuiApp::draw_label(const SDL_Rect &rect, const std::string &text) {
  draw_text(rect.x, rect.y, text, rgb(27, 27, 27), 14, false);
}

void GuiApp::fill_rect(const SDL_Rect &rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer_, &rect);
}

void GuiApp::draw_rect(const SDL_Rect &rect, SDL_Color color, int thickness) {
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
  SDL_Rect r = rect;
  for (int i = 0; i < thickness; ++i) {
    SDL_RenderDrawRect(renderer_, &r);
    r.x += 1;
    r.y += 1;
    r.w -= 2;
    r.h -= 2;
  }
}

TTF_Font *GuiApp::load_font(int pt_size, bool bold) {
  const char *font_candidates[] = {
      "assets/fonts/AtkinsonHyperlegible-Regular.ttf",
      "assets/fonts/SpaceGrotesk-Regular.ttf",
      "assets/fonts/IBM-Plex-Sans-Regular.ttf",
      "/app/share/ps1emu/assets/fonts/AtkinsonHyperlegible-Regular.ttf",
      "/app/share/ps1emu/assets/fonts/SpaceGrotesk-Regular.ttf",
      "/app/share/ps1emu/assets/fonts/IBM-Plex-Sans-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};

  for (const char *path : font_candidates) {
    TTF_Font *font = TTF_OpenFont(path, pt_size);
    if (font) {
      if (bold) {
        TTF_SetFontStyle(font, TTF_STYLE_BOLD);
      }
      return font;
    }
  }
  return nullptr;
}

void GuiApp::draw_text(int x, int y, const std::string &text, SDL_Color color, int pt_size, bool bold) {
  TTF_Font *font = load_font(pt_size, bold);
  if (!font) {
    return;
  }
  SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) {
    TTF_CloseFont(font);
    return;
  }
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
  if (!texture) {
    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
    return;
  }
  SDL_Rect dst{x, y, surface->w, surface->h};
  SDL_RenderCopy(renderer_, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
  SDL_FreeSurface(surface);
  TTF_CloseFont(font);
}

} // namespace ps1emu
