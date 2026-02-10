#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#ifdef PS1EMU_GPU_SDL
#include <SDL2/SDL.h>
#include "ui/sdl_backend.h"
#endif

static bool write_all(int fd, const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  size_t written = 0;
  while (written < size) {
    ssize_t rc = write(fd, bytes + written, size - written);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    written += static_cast<size_t>(rc);
  }
  return true;
}

static bool read_exact(int fd, void *data, size_t size) {
  uint8_t *bytes = static_cast<uint8_t *>(data);
  size_t read_total = 0;
  while (read_total < size) {
    ssize_t rc = read(fd, bytes + read_total, size - read_total);
    if (rc == 0) {
      return false;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    read_total += static_cast<size_t>(rc);
  }
  return true;
}

static bool read_line_fd(std::string &out) {
  static std::string buffer;
  for (;;) {
    size_t pos = buffer.find('\n');
    if (pos != std::string::npos) {
      out = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      return true;
    }
    char temp[256];
    ssize_t rc = read(STDIN_FILENO, temp, sizeof(temp));
    if (rc == 0) {
      return false;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    buffer.append(temp, static_cast<size_t>(rc));
  }
}

static bool write_line_fd(const std::string &line) {
  std::string data = line;
  data.push_back('\n');
  return write_all(STDOUT_FILENO, data.data(), data.size());
}

static bool read_frame(uint16_t &out_type, std::vector<uint8_t> &out_payload) {
  uint8_t header[8];
  if (!read_exact(STDIN_FILENO, header, sizeof(header))) {
    return false;
  }

  uint32_t length = static_cast<uint32_t>(header[0]) |
                    (static_cast<uint32_t>(header[1]) << 8) |
                    (static_cast<uint32_t>(header[2]) << 16) |
                    (static_cast<uint32_t>(header[3]) << 24);
  out_type = static_cast<uint16_t>(header[4]) |
             (static_cast<uint16_t>(header[5]) << 8);

  if (length > 16 * 1024 * 1024) {
    return false;
  }
  out_payload.resize(length);
  if (length == 0) {
    return true;
  }
  return read_exact(STDIN_FILENO, out_payload.data(), length);
}

static bool write_frame(uint16_t type, const std::vector<uint8_t> &payload) {
  uint32_t length = static_cast<uint32_t>(payload.size());
  uint16_t flags = 0;
  uint8_t header[8];
  header[0] = static_cast<uint8_t>(length & 0xFF);
  header[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
  header[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
  header[3] = static_cast<uint8_t>((length >> 24) & 0xFF);
  header[4] = static_cast<uint8_t>(type & 0xFF);
  header[5] = static_cast<uint8_t>((type >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>(flags & 0xFF);
  header[7] = static_cast<uint8_t>((flags >> 8) & 0xFF);

  if (!write_all(STDOUT_FILENO, header, sizeof(header))) {
    return false;
  }
  if (payload.empty()) {
    return true;
  }
  return write_all(STDOUT_FILENO, payload.data(), payload.size());
}

static uint16_t color24_to_15(uint32_t color) {
  uint8_t r = static_cast<uint8_t>(color & 0xFFu);
  uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFFu);
  uint8_t b = static_cast<uint8_t>((color >> 16) & 0xFFu);
  return static_cast<uint16_t>(((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

static uint32_t color15_to_32(uint16_t color) {
  uint8_t r = static_cast<uint8_t>((color & 0x1Fu) << 3);
  uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x1Fu) << 3);
  uint8_t b = static_cast<uint8_t>(((color >> 10) & 0x1Fu) << 3);
  return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

struct Vertex {
  int x = 0;
  int y = 0;
  uint32_t color = 0;
  uint8_t u = 0;
  uint8_t v = 0;
};

class SoftwareGpu {
public:
  SoftwareGpu() {
    vram_.resize(kVramWidth * kVramHeight);
    draw_x1_ = 0;
    draw_y1_ = 0;
    draw_x2_ = kVramWidth - 1;
    draw_y2_ = kVramHeight - 1;
  }

  void set_headless(bool headless) { headless_ = headless; }

  bool init_display() {
#ifdef PS1EMU_GPU_SDL
    if (headless_) {
      return true;
    }
    shutdown_display();
    if (!ps1emu::init_sdl_video_with_fallback()) {
      headless_ = true;
      return true;
    }
    {
      window_ = SDL_CreateWindow("PS1 GPU",
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 display_width_ * scale_,
                                 display_height_ * scale_,
                                 SDL_WINDOW_SHOWN);
      if (!window_) {
        shutdown_display();
        headless_ = true;
        return true;
      }
      renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
      if (!renderer_) {
        shutdown_display();
        headless_ = true;
        return true;
      }
      SDL_RenderSetLogicalSize(renderer_, display_width_, display_height_);
      texture_ = SDL_CreateTexture(renderer_,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   display_width_,
                                   display_height_);
      if (!texture_) {
        shutdown_display();
        headless_ = true;
        return true;
      }
      frame_.resize(static_cast<size_t>(display_width_) * display_height_);
    }
    return true;
#else
    return true;
#endif
  }

  void shutdown_display() {
#ifdef PS1EMU_GPU_SDL
    if (texture_) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
    if (renderer_) {
      SDL_DestroyRenderer(renderer_);
      renderer_ = nullptr;
    }
    if (window_) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    SDL_Quit();
#endif
  }

  void handle_packet(const std::vector<uint32_t> &words) {
    if (words.empty()) {
      return;
    }
    uint8_t cmd = static_cast<uint8_t>(words[0] >> 24);
    if (cmd == 0x00 || cmd == 0x01) {
      return;
    }
    if (cmd == 0x02 && words.size() >= 3) {
      uint16_t color = color24_to_15(words[0]);
      int16_t x = static_cast<int16_t>(words[1] & 0xFFFF);
      int16_t y = static_cast<int16_t>((words[1] >> 16) & 0xFFFF);
      uint16_t w = static_cast<uint16_t>(words[2] & 0xFFFF);
      uint16_t h = static_cast<uint16_t>((words[2] >> 16) & 0xFFFF);
      draw_rect(x, y, w, h, color, false);
      return;
    }
    if (cmd >= 0x20 && cmd <= 0x3F) {
      handle_polygon(words);
      return;
    }
    if (cmd >= 0x40 && cmd <= 0x5F) {
      handle_line(words);
      return;
    }
    if (cmd >= 0x60 && cmd <= 0x7F) {
      handle_rect(words);
      return;
    }
    if (cmd >= 0x80 && cmd <= 0x9F && words.size() >= 4) {
      handle_vram_copy(words);
      return;
    }
    if (cmd == 0xA0) {
      handle_image_load(words);
      return;
    }
    if (cmd >= 0xE1 && cmd <= 0xE6) {
      handle_state(cmd, words[0]);
      return;
    }
  }

  void handle_gp1(uint32_t word) {
    uint8_t cmd = static_cast<uint8_t>(word >> 24);
    switch (cmd) {
      case 0x00: { // Reset GPU
        std::fill(vram_.begin(), vram_.end(), 0);
        display_enabled_ = false;
        display_x_ = 0;
        display_y_ = 0;
        h_range_start_ = 0x200;
        h_range_end_ = 0x200 + 256 * 10;
        v_range_start_ = 0x10;
        v_range_end_ = 0x10 + 240;
        draw_x1_ = 0;
        draw_y1_ = 0;
        draw_x2_ = kVramWidth - 1;
        draw_y2_ = kVramHeight - 1;
        draw_offset_x_ = 0;
        draw_offset_y_ = 0;
        texpage_x_ = 0;
        texpage_y_ = 0;
        tex_depth_ = 0;
        blend_mode_ = 0;
        dithering_enabled_ = false;
        draw_to_display_ = false;
        mask_set_ = false;
        mask_eval_ = false;
        rect_flip_x_ = false;
        rect_flip_y_ = false;
        tex_window_mask_x_ = 0;
        tex_window_mask_y_ = 0;
        tex_window_offset_x_ = 0;
        tex_window_offset_y_ = 0;
        display_flip_x_ = false;
        display_depth24_ = false;
        set_display_mode(0x00000000);
        break;
      }
      case 0x03: { // Display enable (0=on,1=off)
        display_enabled_ = ((word & 0x1u) == 0);
        break;
      }
      case 0x05: { // Display start (VRAM)
        display_x_ = static_cast<int>(word & 0x3FFu);
        display_y_ = static_cast<int>((word >> 10) & 0x1FFu);
        break;
      }
      case 0x06: { // Horizontal display range (store for future)
        h_range_start_ = static_cast<int>(word & 0xFFFu);
        h_range_end_ = static_cast<int>((word >> 12) & 0xFFFu);
        apply_display_ranges();
        break;
      }
      case 0x07: { // Vertical display range (store for future)
        v_range_start_ = static_cast<int>(word & 0x3FFu);
        v_range_end_ = static_cast<int>((word >> 10) & 0x3FFu);
        apply_display_ranges();
        break;
      }
      case 0x08: { // Display mode
        set_display_mode(word);
        break;
      }
      default:
        break;
    }
  }

  std::vector<uint8_t> read_vram_region(int x, int y, int w, int h) {
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0) {
      return out;
    }
    out.reserve(static_cast<size_t>(w) * h * 2);
    for (int yy = 0; yy < h; ++yy) {
      int sy = y + yy;
      for (int xx = 0; xx < w; ++xx) {
        int sx = x + xx;
        uint16_t color = 0;
        if (in_vram(sx, sy)) {
          color = vram_[static_cast<size_t>(sy) * kVramWidth + sx];
        }
        out.push_back(static_cast<uint8_t>(color & 0xFF));
        out.push_back(static_cast<uint8_t>((color >> 8) & 0xFF));
      }
    }
    return out;
  }

  void present() {
#ifdef PS1EMU_GPU_SDL
    if (headless_ || !texture_ || !renderer_) {
      return;
    }

    if (!display_enabled_) {
      SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
      SDL_RenderClear(renderer_);
      SDL_RenderPresent(renderer_);
      return;
    }

    int field = interlaced_ ? (field_parity_ ? 1 : 0) : 0;
    for (int y = 0; y < display_height_; ++y) {
      int src_y = display_y_ + y + field;
      if (interlaced_ && src_y >= kVramHeight) {
        src_y = kVramHeight - 1;
      }
      for (int x = 0; x < display_width_; ++x) {
        int pixel_index = display_flip_x_ ? (display_width_ - 1 - x) : x;
        if (display_depth24_) {
          int byte_x = display_x_ * 2 + pixel_index * 3;
          if (byte_x + 2 >= kVramWidth * 2) {
            frame_[static_cast<size_t>(y) * display_width_ + x] = 0xFF000000u;
            continue;
          }
          uint8_t r = vram_byte(byte_x, src_y);
          uint8_t g = vram_byte(byte_x + 1, src_y);
          uint8_t b = vram_byte(byte_x + 2, src_y);
          frame_[static_cast<size_t>(y) * display_width_ + x] =
              0xFF000000u | (static_cast<uint32_t>(r) << 16) |
              (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        } else {
          int src_x = display_x_ + pixel_index;
          uint16_t color = vram_[static_cast<size_t>(src_y) * kVramWidth + src_x];
          frame_[static_cast<size_t>(y) * display_width_ + x] = color15_to_32(color);
        }
      }
    }

    SDL_UpdateTexture(texture_, nullptr, frame_.data(), display_width_ * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);

    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
      if (evt.type == SDL_QUIT) {
        running_ = false;
      }
    }
#endif
    if (interlaced_) {
      field_parity_ = !field_parity_;
    }
  }

  bool running() const { return running_; }

private:
  static constexpr int kVramWidth = 1024;
  static constexpr int kVramHeight = 512;

  void handle_state(uint8_t cmd, uint32_t word) {
    if (cmd == 0xE1) { // draw mode
      uint32_t mode = word & 0x00FFFFFFu;
      texpage_x_ = static_cast<int>(mode & 0x0Fu) * 64;
      texpage_y_ = (mode & 0x10u) ? 256 : 0;
      tex_depth_ = static_cast<int>((mode >> 7) & 0x3u);
      blend_mode_ = static_cast<int>((mode >> 5) & 0x3u);
      dithering_enabled_ = (mode & (1u << 9)) != 0;
      draw_to_display_ = (mode & (1u << 10)) != 0;
      rect_flip_x_ = (mode & (1u << 12)) != 0;
      rect_flip_y_ = (mode & (1u << 13)) != 0;
    } else if (cmd == 0xE3) { // draw area top-left
      draw_x1_ = static_cast<int>(word & 0x3FFu);
      draw_y1_ = static_cast<int>((word >> 10) & 0x3FFu);
    } else if (cmd == 0xE4) { // draw area bottom-right
      draw_x2_ = static_cast<int>(word & 0x3FFu);
      draw_y2_ = static_cast<int>((word >> 10) & 0x3FFu);
    } else if (cmd == 0xE5) { // draw offset
      int32_t x = static_cast<int32_t>(word & 0x7FFu);
      int32_t y = static_cast<int32_t>((word >> 11) & 0x7FFu);
      if (x & 0x400) {
        x |= ~0x7FF;
      }
      if (y & 0x400) {
        y |= ~0x7FF;
      }
      draw_offset_x_ = static_cast<int>(x);
      draw_offset_y_ = static_cast<int>(y);
    } else if (cmd == 0xE2) { // texture window
      tex_window_mask_x_ = static_cast<int>(word & 0x1Fu);
      tex_window_mask_y_ = static_cast<int>((word >> 5) & 0x1Fu);
      tex_window_offset_x_ = static_cast<int>((word >> 10) & 0x1Fu);
      tex_window_offset_y_ = static_cast<int>((word >> 15) & 0x1Fu);
    } else if (cmd == 0xE6) { // mask bit setting
      mask_set_ = (word & 0x1u) != 0;
      mask_eval_ = (word & 0x2u) != 0;
    }
  }

  void set_display_mode(uint32_t word) {
    int hres = static_cast<int>(word & 0x3u);
    bool hres2 = (word & (1u << 6)) != 0;
    interlaced_ = (word & (1u << 5)) != 0;
    display_flip_x_ = (word & (1u << 7)) != 0;
    display_depth24_ = (word & (1u << 4)) != 0;
    int width = 320;
    if (hres2) {
      width = 368;
    } else {
      switch (hres) {
        case 0:
          width = 256;
          break;
        case 1:
          width = 320;
          break;
        case 2:
          width = 512;
          break;
        case 3:
          width = 640;
          break;
        default:
          width = 320;
          break;
      }
    }
    int height = (word & (1u << 2)) ? 480 : 240;
    mode_width_ = width;
    mode_height_ = height;
    apply_display_ranges();
  }

  void apply_display_ranges() {
    int width = mode_width_;
    int height = mode_height_;

    if (h_range_end_ > h_range_start_) {
      int span = h_range_end_ - h_range_start_;
      int cycles_per_pixel = 8;
      if (mode_width_ == 256) {
        cycles_per_pixel = 10;
      } else if (mode_width_ == 320) {
        cycles_per_pixel = 8;
      } else if (mode_width_ == 368) {
        cycles_per_pixel = 7;
      } else if (mode_width_ == 512) {
        cycles_per_pixel = 5;
      } else if (mode_width_ == 640) {
        cycles_per_pixel = 4;
      }
      int derived = span / cycles_per_pixel;
      derived = (derived + 2) & ~3;
      if (derived >= 16) {
        width = std::clamp(derived, 16, 640);
      }
    }
    if (v_range_end_ > v_range_start_) {
      int span = v_range_end_ - v_range_start_;
      if (span >= 16) {
        height = std::clamp(span, 16, 480);
      }
    }

    update_display_size(width, height);
  }

  void update_display_size(int width, int height) {
    if (width <= 0 || height <= 0) {
      return;
    }
    if (width == display_width_ && height == display_height_) {
      return;
    }
    display_width_ = width;
    display_height_ = height;
#ifdef PS1EMU_GPU_SDL
    if (!headless_ && renderer_) {
      if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
      }
      SDL_RenderSetLogicalSize(renderer_, display_width_, display_height_);
      texture_ = SDL_CreateTexture(renderer_,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   display_width_,
                                   display_height_);
    }
#endif
    frame_.assign(static_cast<size_t>(display_width_) * display_height_, 0);
  }

  void handle_vram_copy(const std::vector<uint32_t> &words) {
    int src_x = static_cast<int16_t>(words[1] & 0xFFFF);
    int src_y = static_cast<int16_t>((words[1] >> 16) & 0xFFFF);
    int dst_x = static_cast<int16_t>(words[2] & 0xFFFF);
    int dst_y = static_cast<int16_t>((words[2] >> 16) & 0xFFFF);
    int w = static_cast<int>(words[3] & 0xFFFF);
    int h = static_cast<int>((words[3] >> 16) & 0xFFFF);
    if (w <= 0 || h <= 0) {
      return;
    }
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int sx = src_x + x;
        int sy = src_y + y;
        int dx = dst_x + x;
        int dy = dst_y + y;
        if (!in_vram(sx, sy) || !in_vram(dx, dy)) {
          continue;
        }
        vram_[static_cast<size_t>(dy) * kVramWidth + dx] =
            vram_[static_cast<size_t>(sy) * kVramWidth + sx];
      }
    }
  }

  void handle_image_load(const std::vector<uint32_t> &words) {
    if (words.size() < 3) {
      return;
    }
    int dst_x = static_cast<int16_t>(words[1] & 0xFFFF);
    int dst_y = static_cast<int16_t>((words[1] >> 16) & 0xFFFF);
    int w = static_cast<int>(words[2] & 0xFFFF);
    int h = static_cast<int>((words[2] >> 16) & 0xFFFF);
    if (w <= 0 || h <= 0) {
      return;
    }
    size_t pixel_count = static_cast<size_t>(w) * h;
    size_t word_index = 3;
    size_t pixel_index = 0;
    while (pixel_index < pixel_count && word_index < words.size()) {
      uint32_t packed = words[word_index++];
      uint16_t p0 = static_cast<uint16_t>(packed & 0xFFFF);
      uint16_t p1 = static_cast<uint16_t>((packed >> 16) & 0xFFFF);
      for (int i = 0; i < 2 && pixel_index < pixel_count; ++i) {
        uint16_t pixel = (i == 0) ? p0 : p1;
        int x = dst_x + static_cast<int>(pixel_index % static_cast<size_t>(w));
        int y = dst_y + static_cast<int>(pixel_index / static_cast<size_t>(w));
        if (in_vram(x, y)) {
          vram_[static_cast<size_t>(y) * kVramWidth + x] = pixel;
        }
        pixel_index++;
      }
    }
  }

  void handle_polygon(const std::vector<uint32_t> &words) {
    if (words.size() < 4) {
      return;
    }
    uint8_t cmd = static_cast<uint8_t>(words[0] >> 24);
    bool gouraud = (cmd & 0x10) != 0;
    bool textured = (cmd & 0x04) != 0;
    bool quad = (cmd & 0x08) != 0;
    bool semi = (cmd & 0x02) != 0;
    bool raw = (cmd & 0x01) != 0;

    size_t vertices = quad ? 4 : 3;
    std::vector<Vertex> verts(vertices);
    size_t index = 0;
    verts[0].color = words[0] & 0x00FFFFFFu;
    index = 1;
    int clut_x = 0;
    int clut_y = 0;
    int tpage_x = texpage_x_;
    int tpage_y = texpage_y_;
    int tex_depth = tex_depth_;
    bool have_clut = false;
    bool have_tpage = false;
    uint16_t tpage_attr = 0;
    for (size_t v = 0; v < vertices; ++v) {
      if (index >= words.size()) {
        return;
      }
      uint32_t xy = words[index++];
      verts[v].x = static_cast<int16_t>(xy & 0xFFFF) + draw_offset_x_;
      verts[v].y = static_cast<int16_t>((xy >> 16) & 0xFFFF) + draw_offset_y_;
      if (gouraud && v > 0) {
        if (index >= words.size()) {
          return;
        }
        verts[v].color = words[index++] & 0x00FFFFFFu;
      } else if (!gouraud) {
        verts[v].color = verts[0].color;
      }
      if (textured) {
        if (index >= words.size()) {
          return;
        }
        uint32_t uv = words[index++];
        verts[v].u = static_cast<uint8_t>(uv & 0xFF);
        verts[v].v = static_cast<uint8_t>((uv >> 8) & 0xFF);
        if (!have_clut) {
          uint16_t clut = static_cast<uint16_t>(uv >> 16);
          clut_x = static_cast<int>(clut & 0x3Fu) * 16;
          clut_y = static_cast<int>((clut >> 6) & 0x1FFu);
          have_clut = true;
        } else if (!have_tpage) {
          tpage_attr = static_cast<uint16_t>(uv >> 16);
          tpage_x = static_cast<int>(tpage_attr & 0x0Fu) * 64;
          tpage_y = (tpage_attr & 0x10u) ? 256 : 0;
          tex_depth = static_cast<int>((tpage_attr >> 7) & 0x3u);
          have_tpage = true;
        }
      }
    }

    if (textured) {
      int poly_blend = have_tpage ? static_cast<int>((tpage_attr >> 5) & 0x3u)
                                  : blend_mode_;
      draw_textured_triangle(verts[0],
                             verts[1],
                             verts[2],
                             tex_depth,
                             tpage_x,
                             tpage_y,
                             clut_x,
                             clut_y,
                             semi,
                             poly_blend,
                             gouraud,
                             raw);
      if (quad) {
        draw_textured_triangle(verts[0],
                               verts[2],
                               verts[3],
                               tex_depth,
                               tpage_x,
                               tpage_y,
                               clut_x,
                               clut_y,
                               semi,
                               poly_blend,
                               gouraud,
                               raw);
      }
    } else {
      draw_triangle(verts[0], verts[1], verts[2], gouraud, semi);
      if (quad) {
        draw_triangle(verts[0], verts[2], verts[3], gouraud, semi);
      }
    }
  }

  void handle_rect(const std::vector<uint32_t> &words) {
    if (words.size() < 2) {
      return;
    }
    uint8_t cmd = static_cast<uint8_t>(words[0] >> 24);
    bool textured = (cmd & 0x04) != 0;
    bool semi = (cmd & 0x02) != 0;
    bool raw = (cmd & 0x01) != 0;
    uint32_t size_code = (cmd >> 3) & 0x3;
    int w = 0;
    int h = 0;
    if (size_code == 0) {
      if (words.size() < 3) {
        return;
      }
      w = static_cast<int>(words[2] & 0xFFFF);
      h = static_cast<int>((words[2] >> 16) & 0xFFFF);
    } else if (size_code == 1) {
      w = 1;
      h = 1;
    } else if (size_code == 2) {
      w = 8;
      h = 8;
    } else {
      w = 16;
      h = 16;
    }

    int x = static_cast<int16_t>(words[1] & 0xFFFF) + draw_offset_x_;
    int y = static_cast<int16_t>((words[1] >> 16) & 0xFFFF) + draw_offset_y_;
    uint16_t color = color24_to_15(words[0]);

    if (textured && words.size() > 2) {
      uint32_t uv = words[2];
      uint8_t u = static_cast<uint8_t>(uv & 0xFF);
      uint8_t v = static_cast<uint8_t>((uv >> 8) & 0xFF);
      uint16_t clut = static_cast<uint16_t>(uv >> 16);
      int clut_x = static_cast<int>(clut & 0x3Fu) * 16;
      int clut_y = static_cast<int>((clut >> 6) & 0x1FFu);
      draw_textured_rect(x,
                         y,
                         w,
                         h,
                         u,
                         v,
                         tex_depth_,
                         texpage_x_,
                         texpage_y_,
                         clut_x,
                         clut_y,
                         semi,
                         raw,
                         words[0] & 0x00FFFFFFu);
      return;
    }

    draw_rect(x, y, w, h, color, semi);
  }

  void handle_line(const std::vector<uint32_t> &words) {
    if (words.size() < 3) {
      return;
    }
    uint8_t cmd = static_cast<uint8_t>(words[0] >> 24);
    bool gouraud = (cmd & 0x10) != 0;
    bool polyline = (cmd & 0x08) != 0;
    bool semi = (cmd & 0x02) != 0;

    auto decode_xy = [&](uint32_t word, int &x, int &y) {
      x = static_cast<int16_t>(word & 0xFFFF) + draw_offset_x_;
      y = static_cast<int16_t>((word >> 16) & 0xFFFF) + draw_offset_y_;
    };

    size_t index = 0;
    uint32_t color0 = words[index++] & 0x00FFFFFFu;
    int x0 = 0;
    int y0 = 0;
    if (index >= words.size()) {
      return;
    }
    decode_xy(words[index++], x0, y0);

    while (index < words.size()) {
      uint32_t color1 = color0;
      if (gouraud) {
        if (index >= words.size()) {
          return;
        }
        color1 = words[index++] & 0x00FFFFFFu;
      }
      if (index >= words.size()) {
        return;
      }
      uint32_t word = words[index++];
      if (polyline && (word & 0xF000F000u) == 0x50005000u) {
        break;
      }
      int x1 = 0;
      int y1 = 0;
      decode_xy(word, x1, y1);
      draw_line(x0, y0, x1, y1, color0, color1, gouraud, semi);
      x0 = x1;
      y0 = y1;
      color0 = color1;

      if (!polyline) {
        break;
      }
    }
  }

  void draw_line(int x0,
                 int y0,
                 int x1,
                 int y1,
                 uint32_t color0,
                 uint32_t color1,
                 bool gouraud,
                 bool semi) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int steps = std::max(dx, dy);
    float inv = steps > 0 ? 1.0f / static_cast<float>(steps) : 0.0f;

    int r0 = static_cast<int>(color0 & 0xFF);
    int g0 = static_cast<int>((color0 >> 8) & 0xFF);
    int b0 = static_cast<int>((color0 >> 16) & 0xFF);
    int r1 = static_cast<int>(color1 & 0xFF);
    int g1 = static_cast<int>((color1 >> 8) & 0xFF);
    int b1 = static_cast<int>((color1 >> 16) & 0xFF);

    int step = 0;
    for (;;) {
      uint32_t color = color0;
      if (gouraud && steps > 0) {
        float t = step * inv;
        int r = static_cast<int>(r0 + (r1 - r0) * t);
        int g = static_cast<int>(g0 + (g1 - g0) * t);
        int b = static_cast<int>(b0 + (b1 - b0) * t);
        color = (static_cast<uint32_t>(b) << 16) |
                (static_cast<uint32_t>(g) << 8) |
                static_cast<uint32_t>(r);
      }
      set_pixel(x0, y0, color24_to_15(color), semi);

      if (x0 == x1 && y0 == y1) {
        break;
      }
      int e2 = err * 2;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
      step++;
    }
  }

  void draw_rect(int x, int y, int w, int h, uint16_t color, bool semi) {
    if (w <= 0 || h <= 0) {
      return;
    }
    for (int yy = 0; yy < h; ++yy) {
      for (int xx = 0; xx < w; ++xx) {
        set_pixel(x + xx, y + yy, color, semi);
      }
    }
  }

  void draw_textured_rect(int x,
                          int y,
                          int w,
                          int h,
                          uint8_t u,
                          uint8_t v,
                          int tex_depth,
                          int tpage_x,
                          int tpage_y,
                          int clut_x,
                          int clut_y,
                          bool semi,
                          bool raw,
                          uint32_t modulate) {
    if (w <= 0 || h <= 0) {
      return;
    }
    for (int yy = 0; yy < h; ++yy) {
      for (int xx = 0; xx < w; ++xx) {
        int tex_u = static_cast<int>(u) + (rect_flip_x_ ? -xx : xx);
        int tex_v = static_cast<int>(v) + (rect_flip_y_ ? -yy : yy);
        tex_u &= 0xFF;
        tex_v &= 0xFF;
        uint16_t color = 0;
        bool transparent = false;
        if (!sample_texture(tex_u, tex_v, tex_depth, tpage_x, tpage_y, clut_x, clut_y, color, transparent)) {
          continue;
        }
        if (transparent) {
          continue;
        }
        uint16_t shaded = color;
        if (!raw) {
          shaded = modulate_color(color, modulate);
        }
        bool apply_semi = semi && (color & 0x8000u);
        set_pixel(x + xx, y + yy, static_cast<uint16_t>(shaded & 0x7FFFu), apply_semi);
      }
    }
  }

  static float edge(const Vertex &a, const Vertex &b, float x, float y) {
    return (x - static_cast<float>(a.x)) * (static_cast<float>(b.y) - static_cast<float>(a.y)) -
           (y - static_cast<float>(a.y)) * (static_cast<float>(b.x) - static_cast<float>(a.x));
  }

  void draw_triangle(const Vertex &v0,
                     const Vertex &v1,
                     const Vertex &v2,
                     bool gouraud,
                     bool semi) {
    int min_x = std::max(draw_x1_, std::min({v0.x, v1.x, v2.x}));
    int max_x = std::min(draw_x2_, std::max({v0.x, v1.x, v2.x}));
    int min_y = std::max(draw_y1_, std::min({v0.y, v1.y, v2.y}));
    int max_y = std::min(draw_y2_, std::max({v0.y, v1.y, v2.y}));
    if (min_x > max_x || min_y > max_y) {
      return;
    }

    float area = edge(v0, v1, static_cast<float>(v2.x), static_cast<float>(v2.y));
    if (area == 0.0f) {
      return;
    }
    float inv_area = 1.0f / area;

    for (int y = min_y; y <= max_y; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        float w0 = edge(v1, v2, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        float w1 = edge(v2, v0, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        float w2 = edge(v0, v1, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
          continue;
        }
        uint32_t color = v0.color;
        if (gouraud) {
          auto c0 = v0.color;
          auto c1 = v1.color;
          auto c2 = v2.color;
          float r = ((c0 & 0xFF) * w0 + (c1 & 0xFF) * w1 + (c2 & 0xFF) * w2);
          float g = (((c0 >> 8) & 0xFF) * w0 + ((c1 >> 8) & 0xFF) * w1 + ((c2 >> 8) & 0xFF) * w2);
          float b = (((c0 >> 16) & 0xFF) * w0 + ((c1 >> 16) & 0xFF) * w1 + ((c2 >> 16) & 0xFF) * w2);
          color = (static_cast<uint32_t>(b) << 16) |
                  (static_cast<uint32_t>(g) << 8) |
                  static_cast<uint32_t>(r);
        }
        set_pixel(x, y, color24_to_15(color), semi);
      }
    }
  }

  void draw_textured_triangle(const Vertex &v0,
                              const Vertex &v1,
                              const Vertex &v2,
                              int tex_depth,
                              int tpage_x,
                              int tpage_y,
                              int clut_x,
                              int clut_y,
                              bool semi,
                              int blend_mode,
                              bool gouraud,
                              bool raw) {
    int min_x = std::max(draw_x1_, std::min({v0.x, v1.x, v2.x}));
    int max_x = std::min(draw_x2_, std::max({v0.x, v1.x, v2.x}));
    int min_y = std::max(draw_y1_, std::min({v0.y, v1.y, v2.y}));
    int max_y = std::min(draw_y2_, std::max({v0.y, v1.y, v2.y}));
    if (min_x > max_x || min_y > max_y) {
      return;
    }

    float area = edge(v0, v1, static_cast<float>(v2.x), static_cast<float>(v2.y));
    if (area == 0.0f) {
      return;
    }
    float inv_area = 1.0f / area;

    for (int y = min_y; y <= max_y; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        float w0 = edge(v1, v2, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        float w1 = edge(v2, v0, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        float w2 = edge(v0, v1, static_cast<float>(x), static_cast<float>(y)) * inv_area;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
          continue;
        }
        float u = v0.u * w0 + v1.u * w1 + v2.u * w2;
        float v = v0.v * w0 + v1.v * w1 + v2.v * w2;
        uint16_t color = 0;
        bool transparent = false;
        if (!sample_texture(static_cast<int>(u),
                            static_cast<int>(v),
                            tex_depth,
                            tpage_x,
                            tpage_y,
                            clut_x,
                            clut_y,
                            color,
                            transparent)) {
          continue;
        }
        if (transparent) {
          continue;
        }
        uint32_t modulate = v0.color;
        if (gouraud) {
          auto c0 = v0.color;
          auto c1 = v1.color;
          auto c2 = v2.color;
          float r = ((c0 & 0xFF) * w0 + (c1 & 0xFF) * w1 + (c2 & 0xFF) * w2);
          float g = (((c0 >> 8) & 0xFF) * w0 + ((c1 >> 8) & 0xFF) * w1 + ((c2 >> 8) & 0xFF) * w2);
          float b = (((c0 >> 16) & 0xFF) * w0 + ((c1 >> 16) & 0xFF) * w1 + ((c2 >> 16) & 0xFF) * w2);
          modulate = (static_cast<uint32_t>(b) << 16) |
                     (static_cast<uint32_t>(g) << 8) |
                     static_cast<uint32_t>(r);
        }
        uint16_t shaded = color;
        if (!raw) {
          shaded = modulate_color(color, modulate);
        }
        bool apply_semi = semi && (color & 0x8000u);
        set_pixel(x, y, static_cast<uint16_t>(shaded & 0x7FFFu), apply_semi, blend_mode);
      }
    }
  }

  bool sample_texture(int u,
                      int v,
                      int tex_depth,
                      int tpage_x,
                      int tpage_y,
                      int clut_x,
                      int clut_y,
                      uint16_t &out_color,
                      bool &out_transparent) {
    out_transparent = false;
    apply_texture_window(u, v);
    u &= 0xFF;
    v &= 0xFF;
    if (tex_depth == 2) { // 15-bit direct
      int x = tpage_x + u;
      int y = tpage_y + v;
      if (!in_vram(x, y)) {
        return false;
      }
      out_color = vram_[static_cast<size_t>(y) * kVramWidth + x];
      return true;
    }
    if (tex_depth == 1) { // 8-bit CLUT
      int word_x = tpage_x + (u / 2);
      int y = tpage_y + v;
      if (!in_vram(word_x, y)) {
        return false;
      }
      uint16_t word = vram_[static_cast<size_t>(y) * kVramWidth + word_x];
      uint8_t index = (u & 1) ? static_cast<uint8_t>(word >> 8) : static_cast<uint8_t>(word & 0xFFu);
      if (index == 0) {
        out_transparent = true;
        out_color = 0;
        return true;
      }
      out_color = clut_lookup(index, clut_x, clut_y);
      return true;
    }
    int word_x = tpage_x + (u / 4);
    int y = tpage_y + v;
    if (!in_vram(word_x, y)) {
      return false;
    }
    uint16_t word = vram_[static_cast<size_t>(y) * kVramWidth + word_x];
    uint8_t index = static_cast<uint8_t>((word >> ((u & 3) * 4)) & 0xFu);
    if (index == 0) {
      out_transparent = true;
      out_color = 0;
      return true;
    }
    out_color = clut_lookup(index, clut_x, clut_y);
    return true;
  }

  void apply_texture_window(int &u, int &v) const {
    int mask_x = tex_window_mask_x_ * 8;
    int mask_y = tex_window_mask_y_ * 8;
    int offset_x = tex_window_offset_x_ * 8;
    int offset_y = tex_window_offset_y_ * 8;

    if (mask_x) {
      u = (u & ~mask_x) | (offset_x & mask_x);
    }
    if (mask_y) {
      v = (v & ~mask_y) | (offset_y & mask_y);
    }
  }

  uint16_t clut_lookup(uint8_t index, int clut_x, int clut_y) {
    int x = clut_x + index;
    int y = clut_y;
    if (!in_vram(x, y)) {
      return 0;
    }
    return vram_[static_cast<size_t>(y) * kVramWidth + x];
  }

  bool in_vram(int x, int y) const {
    return x >= 0 && x < kVramWidth && y >= 0 && y < kVramHeight;
  }

  uint8_t vram_byte(int byte_x, int y) const {
    if (byte_x < 0 || byte_x >= kVramWidth * 2 || y < 0 || y >= kVramHeight) {
      return 0;
    }
    int word_x = byte_x >> 1;
    uint16_t word = vram_[static_cast<size_t>(y) * kVramWidth + word_x];
    if (byte_x & 1) {
      return static_cast<uint8_t>(word >> 8);
    }
    return static_cast<uint8_t>(word & 0xFFu);
  }

  static uint16_t blend_colors(uint16_t dst, uint16_t src, int mode) {
    int dr = dst & 0x1F;
    int dg = (dst >> 5) & 0x1F;
    int db = (dst >> 10) & 0x1F;
    int sr = src & 0x1F;
    int sg = (src >> 5) & 0x1F;
    int sb = (src >> 10) & 0x1F;
    int r = 0;
    int g = 0;
    int b = 0;
    switch (mode & 0x3) {
      case 0:
        r = (dr + sr) >> 1;
        g = (dg + sg) >> 1;
        b = (db + sb) >> 1;
        break;
      case 1:
        r = std::min(31, dr + sr);
        g = std::min(31, dg + sg);
        b = std::min(31, db + sb);
        break;
      case 2:
        r = std::max(0, dr - sr);
        g = std::max(0, dg - sg);
        b = std::max(0, db - sb);
        break;
      case 3:
        r = std::min(31, dr + (sr >> 2));
        g = std::min(31, dg + (sg >> 2));
        b = std::min(31, db + (sb >> 2));
        break;
    }
    return static_cast<uint16_t>((b << 10) | (g << 5) | r);
  }

  static uint16_t modulate_color(uint16_t texel, uint32_t color) {
    int tr = (texel & 0x1F) << 3;
    int tg = ((texel >> 5) & 0x1F) << 3;
    int tb = ((texel >> 10) & 0x1F) << 3;
    int cr = static_cast<int>(color & 0xFF);
    int cg = static_cast<int>((color >> 8) & 0xFF);
    int cb = static_cast<int>((color >> 16) & 0xFF);
    int r = (tr * cr + 127) / 255;
    int g = (tg * cg + 127) / 255;
    int b = (tb * cb + 127) / 255;
    return static_cast<uint16_t>(((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
  }

  static uint16_t dither_color(uint16_t color, int x, int y) {
    static const int matrix[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    int d = (matrix[y & 3][x & 3] - 8) >> 2; // range -2..1
    int r = (color & 0x1F) + d;
    int g = ((color >> 5) & 0x1F) + d;
    int b = ((color >> 10) & 0x1F) + d;
    r = std::clamp(r, 0, 31);
    g = std::clamp(g, 0, 31);
    b = std::clamp(b, 0, 31);
    return static_cast<uint16_t>((b << 10) | (g << 5) | r);
  }

  void set_pixel(int x, int y, uint16_t color, bool semi) {
    set_pixel(x, y, color, semi, blend_mode_);
  }

  void set_pixel(int x, int y, uint16_t color, bool semi, int blend_mode) {
    if (x < draw_x1_ || x > draw_x2_ || y < draw_y1_ || y > draw_y2_) {
      return;
    }
    if (!in_vram(x, y)) {
      return;
    }
    if (!draw_to_display_) {
      int dx1 = display_x_ + display_width_ - 1;
      int dy1 = display_y_ + display_height_ - 1;
      if (x >= display_x_ && x <= dx1 && y >= display_y_ && y <= dy1) {
        return;
      }
    }
    size_t idx = static_cast<size_t>(y) * kVramWidth + x;
    if (mask_eval_ && (vram_[idx] & 0x8000u)) {
      return;
    }
    uint16_t src = static_cast<uint16_t>(color & 0x7FFFu);
    if (dithering_enabled_) {
      src = dither_color(src, x, y);
    }
    if (semi) {
      uint16_t dst = static_cast<uint16_t>(vram_[idx] & 0x7FFFu);
      uint16_t blended = blend_colors(dst, src, blend_mode);
      vram_[idx] = mask_set_ ? static_cast<uint16_t>(blended | 0x8000u) : blended;
    } else {
      vram_[idx] = mask_set_ ? static_cast<uint16_t>(src | 0x8000u) : src;
    }
  }

  bool headless_ = false;
  bool running_ = true;
  bool display_enabled_ = true;
  bool display_flip_x_ = false;
  bool display_depth24_ = false;
  bool interlaced_ = false;
  bool field_parity_ = false;
  int h_range_start_ = 0;
  int h_range_end_ = 0;
  int v_range_start_ = 0;
  int v_range_end_ = 0;

#ifdef PS1EMU_GPU_SDL
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;
#endif
  std::vector<uint32_t> frame_;
  std::vector<uint16_t> vram_;

  int draw_x1_ = 0;
  int draw_y1_ = 0;
  int draw_x2_ = kVramWidth - 1;
  int draw_y2_ = kVramHeight - 1;
  int draw_offset_x_ = 0;
  int draw_offset_y_ = 0;
  int texpage_x_ = 0;
  int texpage_y_ = 0;
  int tex_depth_ = 0;
  int blend_mode_ = 0;
  bool mask_set_ = false;
  bool mask_eval_ = false;
  bool dithering_enabled_ = false;
  bool draw_to_display_ = false;
  bool rect_flip_x_ = false;
  bool rect_flip_y_ = false;
  int tex_window_mask_x_ = 0;
  int tex_window_mask_y_ = 0;
  int tex_window_offset_x_ = 0;
  int tex_window_offset_y_ = 0;

  int display_x_ = 0;
  int display_y_ = 0;
  int display_width_ = 320;
  int display_height_ = 240;
  int mode_width_ = 320;
  int mode_height_ = 240;
  int scale_ = 2;
};

int main() {
  const char *headless_env = getenv("PS1EMU_HEADLESS");
  bool headless = headless_env && headless_env[0] != '\0';

  SoftwareGpu gpu;
  gpu.set_headless(headless);
  gpu.init_display();

  std::string line;
  if (!read_line_fd(line)) {
    return 1;
  }

  if (line == "HELLO GPU 1") {
    if (!write_line_fd("READY GPU 1")) {
      return 1;
    }
  } else {
    write_line_fd("ERROR");
    return 1;
  }

  while (read_line_fd(line)) {
    if (line == "PING") {
      write_line_fd("PONG");
      continue;
    }
    if (line == "FRAME_MODE") {
      write_line_fd("FRAME_READY");
      break;
    }
    if (line == "SHUTDOWN") {
      break;
    }
    write_line_fd("ERROR");
  }

  while (gpu.running()) {
    uint16_t type = 0;
    std::vector<uint8_t> payload;
    if (!read_frame(type, payload)) {
      break;
    }
    if (type == 0x0001) {
      std::vector<uint32_t> words;
      words.reserve(payload.size() / 4);
      for (size_t i = 0; i + 3 < payload.size(); i += 4) {
        uint32_t word = static_cast<uint32_t>(payload[i]) |
                        (static_cast<uint32_t>(payload[i + 1]) << 8) |
                        (static_cast<uint32_t>(payload[i + 2]) << 16) |
                        (static_cast<uint32_t>(payload[i + 3]) << 24);
        words.push_back(word);
      }
      gpu.handle_packet(words);
      gpu.present();

      uint32_t count = static_cast<uint32_t>(payload.size() / 4);
      std::vector<uint8_t> ack(4);
      ack[0] = static_cast<uint8_t>(count & 0xFF);
      ack[1] = static_cast<uint8_t>((count >> 8) & 0xFF);
      ack[2] = static_cast<uint8_t>((count >> 16) & 0xFF);
      ack[3] = static_cast<uint8_t>((count >> 24) & 0xFF);
      write_frame(0x0002, ack);
      continue;
    }
    if (type == 0x0003) {
      for (size_t i = 0; i + 3 < payload.size(); i += 4) {
        uint32_t word = static_cast<uint32_t>(payload[i]) |
                        (static_cast<uint32_t>(payload[i + 1]) << 8) |
                        (static_cast<uint32_t>(payload[i + 2]) << 16) |
                        (static_cast<uint32_t>(payload[i + 3]) << 24);
        gpu.handle_gp1(word);
      }
      write_frame(0x0002, {});
      continue;
    }
    if (type == 0x0004) {
      if (payload.size() >= 8) {
        uint16_t x = static_cast<uint16_t>(payload[0]) |
                     static_cast<uint16_t>(payload[1] << 8);
        uint16_t y = static_cast<uint16_t>(payload[2]) |
                     static_cast<uint16_t>(payload[3] << 8);
        uint16_t w = static_cast<uint16_t>(payload[4]) |
                     static_cast<uint16_t>(payload[5] << 8);
        uint16_t h = static_cast<uint16_t>(payload[6]) |
                     static_cast<uint16_t>(payload[7] << 8);
        std::vector<uint8_t> data = gpu.read_vram_region(x, y, w, h);
        write_frame(0x0005, data);
      } else {
        write_frame(0x0005, {});
      }
      continue;
    }
    write_frame(0x0002, {});
  }

  gpu.shutdown_display();
  return 0;
}
