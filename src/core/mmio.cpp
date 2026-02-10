#include "core/mmio.h"

#include <algorithm>
#include <cstring>

namespace ps1emu {

static uint32_t cdrom_read_period_cycles(uint8_t mode);

void MmioBus::reset() {
  std::memset(raw_.data(), 0, raw_.size());
  gpu_gp1_fifo_.clear();
  reset_gpu_state();
  irq_stat_ = 0;
  irq_mask_ = 0;
  std::memset(dma_madr_, 0, sizeof(dma_madr_));
  std::memset(dma_bcr_, 0, sizeof(dma_bcr_));
  std::memset(dma_chcr_, 0, sizeof(dma_chcr_));
  dma_dpcr_ = 0;
  dma_dicr_ = 0;
  std::memset(timer_count_, 0, sizeof(timer_count_));
  std::memset(timer_mode_, 0, sizeof(timer_mode_));
  std::memset(timer_target_, 0, sizeof(timer_target_));
  std::memset(spu_regs_.data(), 0, spu_regs_.size() * sizeof(uint16_t));
  std::memset(cdrom_regs_.data(), 0, cdrom_regs_.size());
  cdrom_param_fifo_.clear();
  cdrom_response_fifo_.clear();
  cdrom_data_fifo_.clear();
  cdrom_index_ = 0;
  cdrom_status_ = 0;
  cdrom_irq_flags_ = 0;
  cdrom_irq_enable_ = 0;
  cdrom_mode_ = 0;
  cdrom_filter_file_ = 0;
  cdrom_filter_channel_ = 0;
  cdrom_session_ = 1;
  cdrom_error_ = false;
  cdrom_reading_ = false;
  cdrom_playing_ = false;
  cdrom_muted_ = false;
  cdrom_read_timer_ = 0;
  cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
  cdrom_last_read_lba_ = 0;
  cdrom_lba_ = 0;
  std::memset(timer_irq_enable_, 0, sizeof(timer_irq_enable_));
  std::memset(timer_irq_repeat_, 0, sizeof(timer_irq_repeat_));
  std::memset(timer_irq_on_overflow_, 0, sizeof(timer_irq_on_overflow_));
  std::memset(timer_irq_on_target_, 0, sizeof(timer_irq_on_target_));
  dma_active_channel_ = 0xFFFFFFFFu;
}

void MmioBus::reset_gpu_state() {
  gpu_gp0_ = 0;
  gpu_gp1_ = 0x14802000;
  gpu_gp0_fifo_.clear();
  gpu_read_fifo_.clear();
  gpu_read_latch_ = 0;
  gpu_read_pending_.clear();
  gpu_read_pending_delay_ = 0;
  gpu_texpage_x_ = 0;
  gpu_texpage_y_ = 0;
  gpu_semi_ = 0;
  gpu_tex_depth_ = 0;
  gpu_dither_ = false;
  gpu_draw_to_display_ = false;
  gpu_mask_set_ = false;
  gpu_mask_eval_ = false;
  gpu_display_disabled_ = true;
  gpu_irq_ = false;
  gpu_interlace_ = false;
  gpu_flip_ = false;
  gpu_hres2_ = false;
  gpu_hres1_ = 0;
  gpu_vres_ = false;
  gpu_vmode_pal_ = false;
  gpu_display_depth24_ = false;
  gpu_dma_dir_ = 0;
  gpu_field_ = false;
  gpu_field_cycle_accum_ = 0;
  gpu_busy_cycles_ = 0;
  gpu_display_x_ = 0;
  gpu_display_y_ = 0;
  gpu_h_range_start_ = 0x200;
  gpu_h_range_end_ = static_cast<uint16_t>(0x200 + 256 * 10);
  gpu_v_range_start_ = 0x10;
  gpu_v_range_end_ = static_cast<uint16_t>(0x10 + 240);
  gpu_tex_window_ = 0;
  gpu_draw_area_tl_ = 0;
  gpu_draw_area_br_ = (0x3FFu) | (0x1FFu << 10);
  gpu_draw_offset_ = 0;
  irq_stat_ &= static_cast<uint16_t>(~(1u << 1));
}

uint32_t MmioBus::compute_gpustat() const {
  constexpr size_t kGpuFifoLimit = 32;
  uint32_t stat = 0;
  stat |= (gpu_texpage_x_ & 0xFu);
  stat |= (gpu_texpage_y_ & 0x1u) << 4;
  stat |= (gpu_semi_ & 0x3u) << 5;
  stat |= (gpu_tex_depth_ & 0x3u) << 7;
  if (gpu_dither_) {
    stat |= (1u << 9);
  }
  if (gpu_draw_to_display_) {
    stat |= (1u << 10);
  }
  if (gpu_mask_set_) {
    stat |= (1u << 11);
  }
  if (gpu_mask_eval_) {
    stat |= (1u << 12);
  }
  uint32_t field = gpu_field_ ? 1u : 0u;
  uint32_t interlace_field = gpu_interlace_ ? field : 1u;
  if (interlace_field) {
    stat |= (1u << 13);
  }
  if (gpu_flip_) {
    stat |= (1u << 14);
  }
  stat |= ((gpu_texpage_y_ >> 1) & 0x1u) << 15;
  if (gpu_hres2_) {
    stat |= (1u << 16);
  }
  stat |= (gpu_hres1_ & 0x3u) << 17;
  if (gpu_vres_) {
    stat |= (1u << 19);
  }
  if (gpu_vmode_pal_) {
    stat |= (1u << 20);
  }
  if (gpu_display_depth24_) {
    stat |= (1u << 21);
  }
  if (gpu_interlace_) {
    stat |= (1u << 22);
  }
  if (gpu_display_disabled_) {
    stat |= (1u << 23);
  }
  if (gpu_irq_) {
    stat |= (1u << 24);
  }

  bool ready_cmd = (gpu_gp0_fifo_.size() < kGpuFifoLimit) && (gpu_busy_cycles_ == 0);
  bool ready_vram_to_cpu = !gpu_read_fifo_.empty();
  bool ready_dma_block = true;
  switch (gpu_dma_dir_ & 0x3u) {
    case 1:
      ready_dma_block = ready_cmd;
      break;
    case 2:
      ready_dma_block = ready_vram_to_cpu;
      break;
    default:
      ready_dma_block = true;
      break;
  }

  if (ready_cmd) {
    stat |= (1u << 26);
  }
  if (ready_vram_to_cpu) {
    stat |= (1u << 27);
  }
  if (ready_dma_block) {
    stat |= (1u << 28);
  }
  stat |= (gpu_dma_dir_ & 0x3u) << 29;
  if (field) {
    stat |= (1u << 31);
  }

  uint32_t dma_req = 0;
  switch (gpu_dma_dir_ & 0x3u) {
    case 0:
      dma_req = 0;
      break;
    case 1:
    case 2:
      dma_req = ready_dma_block ? 1u : 0u;
      break;
    case 3:
      dma_req = 1;
      break;
    default:
      dma_req = 0;
      break;
  }
  if (dma_req) {
    stat |= (1u << 25);
  }
  return stat;
}

static uint8_t bcd_to_int(uint8_t value) {
  return static_cast<uint8_t>(((value >> 4) & 0x0Fu) * 10 + (value & 0x0Fu));
}

static uint8_t int_to_bcd(uint32_t value) {
  value %= 100;
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

static uint32_t bcd_to_lba(uint8_t mm, uint8_t ss, uint8_t ff) {
  uint32_t m = bcd_to_int(mm);
  uint32_t s = bcd_to_int(ss);
  uint32_t f = bcd_to_int(ff);
  uint32_t lba = (m * 60 + s) * 75 + f;
  if (lba >= 150) {
    lba -= 150;
  } else {
    lba = 0;
  }
  return lba;
}

static void lba_to_bcd(uint32_t lba, uint8_t &mm, uint8_t &ss, uint8_t &ff) {
  uint32_t lba_adj = lba + 150;
  uint32_t total_seconds = lba_adj / 75;
  uint32_t frames = lba_adj % 75;
  uint32_t minutes = total_seconds / 60;
  uint32_t seconds = total_seconds % 60;
  mm = int_to_bcd(minutes);
  ss = int_to_bcd(seconds);
  ff = int_to_bcd(frames);
}

static uint8_t cdrom_status_byte(bool has_disc,
                                 bool reading,
                                 bool data_ready,
                                 bool error) {
  uint8_t status = 0;
  if (has_disc) {
    status |= 0x02;
  }
  if (reading) {
    status |= 0x10;
  }
  if (data_ready) {
    status |= 0x20;
  }
  if (error) {
    status |= 0x01;
  }
  return status;
}

static uint32_t recompute_dma_master(uint32_t dicr) {
  bool master = (dicr & (1u << 23)) != 0;
  uint32_t enables = (dicr >> 16) & 0x7F;
  uint32_t flags = (dicr >> 24) & 0x7F;
  bool irq = master && ((enables & flags) != 0);
  if (irq) {
    dicr |= (1u << 31);
  } else {
    dicr &= ~(1u << 31);
  }
  return dicr;
}

void MmioBus::cdrom_push_response(uint8_t value) {
  cdrom_response_fifo_.push_back(value);
}

void MmioBus::cdrom_raise_irq(uint8_t flags) {
  cdrom_irq_flags_ = flags;
  if ((cdrom_irq_enable_ & flags) != 0) {
    irq_stat_ |= (1u << 2);
  }
}

static bool cdrom_fill_data_fifo(CdromImage &image,
                                 uint32_t &lba,
                                 uint32_t &last_lba,
                                 bool &error,
                                 std::vector<uint8_t> &fifo);

void MmioBus::cdrom_maybe_fill_data() {
  if (!cdrom_reading_ || cdrom_error_ || !cdrom_image_.loaded()) {
    return;
  }
  if (!cdrom_data_fifo_.empty()) {
    return;
  }
  if (cdrom_read_timer_ > 0) {
    return;
  }
  if (cdrom_fill_data_fifo(cdrom_image_, cdrom_lba_, cdrom_last_read_lba_, cdrom_error_, cdrom_data_fifo_)) {
    cdrom_read_timer_ = cdrom_read_period_;
    cdrom_raise_irq(0x02);
  }
}

static bool cdrom_fill_data_fifo(CdromImage &image,
                                 uint32_t &lba,
                                 uint32_t &last_lba,
                                 bool &error,
                                 std::vector<uint8_t> &fifo) {
  std::vector<uint8_t> sector;
  if (!image.read_sector(lba, sector)) {
    error = true;
    return false;
  }
  fifo = std::move(sector);
  last_lba = lba;
  lba += 1;
  return true;
}

static uint32_t cdrom_read_period_cycles(uint8_t mode) {
  constexpr uint32_t kCpuHz = 33868800;
  constexpr uint32_t kSectorsPerSec = 75;
  uint32_t base = kCpuHz / kSectorsPerSec;
  bool double_speed = (mode & 0x80u) != 0;
  if (double_speed) {
    base = std::max(1u, base / 2);
  }
  return base;
}

void MmioBus::cdrom_execute_command(uint8_t cmd) {
  std::vector<uint8_t> params = cdrom_param_fifo_;
  cdrom_param_fifo_.clear();

  cdrom_error_ = false;
  cdrom_response_fifo_.clear();

  switch (cmd) {
    case 0x00: { // Sync
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x01: { // Getstat
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x02: { // Setloc
      if (params.size() >= 3) {
        cdrom_lba_ = bcd_to_lba(params[0], params[1], params[2]);
      } else {
        cdrom_error_ = true;
      }
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x03: { // Play
      cdrom_playing_ = true;
      cdrom_reading_ = false;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x04: // Forward
    case 0x05: { // Backward
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x06: // ReadN
    case 0x1B: { // ReadS
      cdrom_reading_ = true;
      cdrom_playing_ = false;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_read_timer_ = std::max(1u, cdrom_read_period_);
      cdrom_data_fifo_.clear();
      if (!cdrom_image_.loaded()) {
        cdrom_error_ = true;
      }
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x07: { // MotorOn
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x08: { // Stop
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_read_timer_ = 0;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x09: { // Pause
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_read_timer_ = 0;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0A: { // Init
      cdrom_mode_ = 0;
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_muted_ = false;
      cdrom_filter_file_ = 0;
      cdrom_filter_channel_ = 0;
      cdrom_session_ = 1;
      cdrom_read_timer_ = 0;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0B: { // Mute
      cdrom_muted_ = true;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0C: { // Demute
      cdrom_muted_ = false;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0D: { // Setfilter
      if (params.size() >= 2) {
        cdrom_filter_file_ = params[0];
        cdrom_filter_channel_ = params[1];
      }
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0E: { // Setmode
      if (!params.empty()) {
        cdrom_mode_ = params[0];
      }
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0F: { // Getparam
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(cdrom_mode_);
      cdrom_push_response(cdrom_filter_file_);
      cdrom_push_response(cdrom_filter_channel_);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x10: { // GetlocL
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(cdrom_last_read_lba_, mm, ss, ff);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(mm);
      cdrom_push_response(ss);
      cdrom_push_response(ff);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x11: { // GetlocP
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(cdrom_lba_, mm, ss, ff);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(mm);
      cdrom_push_response(ss);
      cdrom_push_response(ff);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x12: { // SetSession
      if (!params.empty()) {
        cdrom_session_ = params[0];
      }
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x13: { // GetTN
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      if (cdrom_image_.loaded()) {
        cdrom_push_response(0x01);
        cdrom_push_response(0x01);
      } else {
        cdrom_push_response(0x00);
        cdrom_push_response(0x00);
      }
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x14: { // GetTD
      uint8_t track = params.empty() ? 0 : params[0];
      uint32_t lba = 0;
      if (cdrom_image_.loaded()) {
        if (track == 0) {
          lba = cdrom_image_.end_lba();
        } else {
          int32_t start = cdrom_image_.start_lba();
          lba = start < 0 ? 0u : static_cast<uint32_t>(start);
        }
      }
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(lba, mm, ss, ff);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(mm);
      cdrom_push_response(ss);
      cdrom_push_response(ff);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x15: // SeekL
    case 0x16: { // SeekP
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_read_timer_ = 0;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x17: { // SetClock
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x18: { // GetClock
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x19: { // Test
      uint8_t sub = params.empty() ? 0 : params[0];
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      if (sub == 0x20) {
        cdrom_push_response(0x98);
        cdrom_push_response(0x06);
        cdrom_push_response(0x19);
        cdrom_push_response(0xC0);
      } else {
        cdrom_push_response(0x00);
        cdrom_push_response(0x00);
        cdrom_push_response(0x00);
        cdrom_push_response(0x00);
      }
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1A: { // GetID
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(0x00);
      cdrom_push_response(0x20);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1C: { // Reset
      cdrom_mode_ = 0;
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_muted_ = false;
      cdrom_filter_file_ = 0;
      cdrom_filter_channel_ = 0;
      cdrom_read_timer_ = 0;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1D: { // GetQ
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1E: { // ReadTOC
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
    default: {
      cdrom_error_ = true;
      cdrom_push_response(cdrom_status_byte(cdrom_image_.loaded(),
                                            cdrom_reading_,
                                            !cdrom_data_fifo_.empty(),
                                            cdrom_error_));
      cdrom_raise_irq(0x01);
      break;
    }
  }
}

uint32_t MmioBus::offset(uint32_t addr) const {
  return addr - kBase;
}

uint8_t MmioBus::read8(uint32_t addr) {
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint32_t reg = addr - 0x1F801800;
    if (reg == 0) {
      cdrom_status_ = cdrom_status_byte(cdrom_image_.loaded(),
                                        cdrom_reading_,
                                        !cdrom_data_fifo_.empty(),
                                        cdrom_error_);
      return static_cast<uint8_t>((cdrom_status_ & 0xFCu) | (cdrom_index_ & 0x03u));
    }
    if (reg == 1) {
      if (!cdrom_response_fifo_.empty()) {
        uint8_t value = cdrom_response_fifo_.front();
        cdrom_response_fifo_.erase(cdrom_response_fifo_.begin());
        return value;
      }
      return 0;
    }
    if (reg == 2) {
      cdrom_maybe_fill_data();
      if (!cdrom_data_fifo_.empty()) {
        uint8_t value = cdrom_data_fifo_.front();
        cdrom_data_fifo_.erase(cdrom_data_fifo_.begin());
        return value;
      }
      return 0;
    }
    if (reg == 3) {
      return cdrom_irq_flags_;
    }
  }

  uint32_t off = offset(addr);
  if (off < kSize) {
    return raw_[off];
  }
  return 0xFF;
}

uint16_t MmioBus::read16(uint32_t addr) {
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint16_t lo = read8(addr);
    uint16_t hi = read8(addr + 1);
    return static_cast<uint16_t>(lo | (hi << 8));
  }

  uint32_t off = offset(addr);
  if (off + 1 < kSize) {
    return static_cast<uint16_t>(raw_[off] | (raw_[off + 1] << 8));
  }
  return 0xFFFF;
}

uint32_t MmioBus::read32(uint32_t addr) {
  uint32_t off = offset(addr);
  if (off + 3 >= kSize) {
    return 0xFFFFFFFF;
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint32_t b0 = read8(addr);
    uint32_t b1 = read8(addr + 1);
    uint32_t b2 = read8(addr + 2);
    uint32_t b3 = read8(addr + 3);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }

  if (addr == 0x1F801070) { // I_STAT
    return irq_stat_;
  }
  if (addr == 0x1F801074) { // I_MASK
    return irq_mask_;
  }
  if (addr == 0x1F801810) { // GPU GP0
    if (!gpu_read_fifo_.empty()) {
      gpu_read_latch_ = gpu_read_fifo_.front();
      gpu_read_fifo_.erase(gpu_read_fifo_.begin());
    }
    return gpu_read_latch_;
  }
  if (addr == 0x1F801814) { // GPU GP1
    return compute_gpustat();
  }

  if (addr >= 0x1F801080 && addr < 0x1F8010F0) { // DMA channels
    uint32_t index = (addr - 0x1F801080) / 0x10;
    uint32_t reg = (addr - 0x1F801080) % 0x10;
    if (index < 7) {
      if (reg == 0x0) {
        return dma_madr_[index];
      }
      if (reg == 0x4) {
        return dma_bcr_[index];
      }
      if (reg == 0x8) {
        return dma_chcr_[index];
      }
    }
  }
  if (addr == 0x1F8010F0) { // DPCR
    return dma_dpcr_;
  }
  if (addr == 0x1F8010F4) { // DICR
    return dma_dicr_;
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) { // Timers
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        return timer_count_[timer];
      }
      if (reg == 0x4) {
        return timer_mode_[timer];
      }
      if (reg == 0x8) {
        return timer_target_[timer];
      }
    }
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) { // SPU
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      return spu_regs_[index];
    }
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) { // CD-ROM
    uint32_t index = addr - 0x1F801800;
    return cdrom_regs_[index];
  }

  return static_cast<uint32_t>(raw_[off]) |
         (static_cast<uint32_t>(raw_[off + 1]) << 8) |
         (static_cast<uint32_t>(raw_[off + 2]) << 16) |
         (static_cast<uint32_t>(raw_[off + 3]) << 24);
}

void MmioBus::write8(uint32_t addr, uint8_t value) {
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint32_t reg = addr - 0x1F801800;
    if (reg == 0) {
      cdrom_index_ = value & 0x03u;
    } else if (reg == 1) {
      cdrom_execute_command(value);
    } else if (reg == 2) {
      cdrom_param_fifo_.push_back(value);
    } else if (reg == 3) {
      cdrom_irq_enable_ = static_cast<uint8_t>(value & 0x1Fu);
      if (value & 0x1Fu) {
        cdrom_irq_flags_ = 0;
        irq_stat_ &= static_cast<uint16_t>(~(1u << 2));
      }
    }
  }

  uint32_t off = offset(addr);
  if (off < kSize) {
    raw_[off] = value;
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    cdrom_regs_[addr - 0x1F801800] = value;
  }
}

void MmioBus::write16(uint32_t addr, uint16_t value) {
  uint32_t off = offset(addr);
  if (off + 1 < kSize) {
    raw_[off] = static_cast<uint8_t>(value & 0xFF);
    raw_[off + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) {
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      spu_regs_[index] = value;
    }
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        timer_count_[timer] = value;
      } else if (reg == 0x4) {
        timer_mode_[timer] = value;
        timer_irq_enable_[timer] = (value & (1u << 4)) != 0;
        timer_irq_repeat_[timer] = (value & (1u << 5)) != 0;
        timer_irq_on_overflow_[timer] = (value & (1u << 6)) != 0;
        timer_irq_on_target_[timer] = (value & (1u << 7)) != 0;
        if (value & (1u << 3)) {
          timer_count_[timer] = 0;
        }
        if (value & (1u << 8)) {
          irq_stat_ &= static_cast<uint16_t>(~(1u << (4 + timer)));
        }
      } else if (reg == 0x8) {
        timer_target_[timer] = value;
      }
    }
  }

  if (addr == 0x1F801070) { // I_STAT
    irq_stat_ &= static_cast<uint16_t>(~value);
  } else if (addr == 0x1F801074) { // I_MASK
    irq_mask_ = value;
  }
}

void MmioBus::write32(uint32_t addr, uint32_t value) {
  uint32_t off = offset(addr);
  if (off + 3 >= kSize) {
    return;
  }

  if (addr == 0x1F801070) { // I_STAT
    irq_stat_ &= static_cast<uint16_t>(~value);
  } else if (addr == 0x1F801074) { // I_MASK
    irq_mask_ = static_cast<uint16_t>(value);
  }

  if (addr == 0x1F801810) { // GPU GP0
    gpu_gp0_ = value;
    gpu_gp0_fifo_.push_back(value);
    uint32_t penalty = gpu_gp0_fifo_.size() > 32 ? 4u : 2u;
    gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + penalty, 100000);
  } else if (addr == 0x1F801814) { // GPU GP1
    gpu_gp1_ = value;
    gpu_gp1_fifo_.push_back(value);
    uint32_t penalty = gpu_gp1_fifo_.size() > 32 ? 2u : 1u;
    gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + penalty, 100000);
    uint8_t cmd = static_cast<uint8_t>(value >> 24);
    switch (cmd) {
      case 0x00: { // Reset GPU
        reset_gpu_state();
        break;
      }
      case 0x01: { // Reset command buffer
        gpu_gp0_fifo_.clear();
        gpu_read_fifo_.clear();
        gpu_read_pending_.clear();
        gpu_read_pending_delay_ = 0;
        gpu_read_latch_ = 0;
        gpu_busy_cycles_ = 0;
        break;
      }
      case 0x02: { // Ack GPU IRQ
        gpu_irq_ = false;
        irq_stat_ &= static_cast<uint16_t>(~(1u << 1));
        break;
      }
      case 0x03: { // Display enable (0=on,1=off)
        gpu_display_disabled_ = (value & 0x1u) != 0;
        break;
      }
      case 0x04: { // DMA direction
        gpu_dma_dir_ = value & 0x3u;
        break;
      }
      case 0x05: { // Display start (VRAM)
        gpu_display_x_ = static_cast<uint16_t>(value & 0x3FFu);
        gpu_display_y_ = static_cast<uint16_t>((value >> 10) & 0x1FFu);
        break;
      }
      case 0x06: { // Horizontal display range
        gpu_h_range_start_ = static_cast<uint16_t>(value & 0xFFFu);
        gpu_h_range_end_ = static_cast<uint16_t>((value >> 12) & 0xFFFu);
        break;
      }
      case 0x07: { // Vertical display range
        gpu_v_range_start_ = static_cast<uint16_t>(value & 0x3FFu);
        gpu_v_range_end_ = static_cast<uint16_t>((value >> 10) & 0x3FFu);
        break;
      }
      case 0x08: { // Display mode
        gpu_hres1_ = value & 0x3u;
        gpu_vres_ = (value & (1u << 2)) != 0;
        gpu_vmode_pal_ = (value & (1u << 3)) != 0;
        gpu_display_depth24_ = (value & (1u << 4)) != 0;
        gpu_interlace_ = (value & (1u << 5)) != 0;
        gpu_hres2_ = (value & (1u << 6)) != 0;
        gpu_flip_ = (value & (1u << 7)) != 0;
        break;
      }
      case 0x10:
      case 0x11:
      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15:
      case 0x16:
      case 0x17:
      case 0x18:
      case 0x19:
      case 0x1A:
      case 0x1B:
      case 0x1C:
      case 0x1D:
      case 0x1E:
      case 0x1F: { // Get GPU info
        uint32_t index = value & 0x0Fu;
        uint32_t response = 0;
        bool has_response = true;
        switch (index) {
          case 0x02:
            response = gpu_tex_window_ & 0x00FFFFFFu;
            break;
          case 0x03:
            response = gpu_draw_area_tl_ & 0x00FFFFFFu;
            break;
          case 0x04:
            response = gpu_draw_area_br_ & 0x00FFFFFFu;
            break;
          case 0x05:
            response = gpu_draw_offset_ & 0x00FFFFFFu;
            break;
          case 0x07:
            response = 2;
            break;
          default:
            has_response = false;
            break;
        }
        if (has_response) {
          queue_gpu_read_data({response});
        }
        break;
      }
      default:
        break;
    }
  }

  if (addr >= 0x1F801080 && addr < 0x1F8010F0) {
    uint32_t index = (addr - 0x1F801080) / 0x10;
    uint32_t reg = (addr - 0x1F801080) % 0x10;
    if (index < 7) {
      if (reg == 0x0) {
        dma_madr_[index] = value;
      } else if (reg == 0x4) {
        dma_bcr_[index] = value;
      } else if (reg == 0x8) {
        dma_chcr_[index] = value;
        if (value & (1u << 24)) {
          dma_active_channel_ = index;
        }
      }
    }
  } else if (addr == 0x1F8010F0) {
    dma_dpcr_ = value;
  } else if (addr == 0x1F8010F4) {
    uint32_t clear = (value >> 24) & 0x7F;
    dma_dicr_ &= ~(clear << 24);
    dma_dicr_ = (dma_dicr_ & 0xFF000000u) | (value & 0x00FFFFFFu);
    dma_dicr_ = recompute_dma_master(dma_dicr_);
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        timer_count_[timer] = static_cast<uint16_t>(value);
      } else if (reg == 0x4) {
        timer_mode_[timer] = static_cast<uint16_t>(value);
      } else if (reg == 0x8) {
        timer_target_[timer] = static_cast<uint16_t>(value);
      }
    }
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) {
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      spu_regs_[index] = static_cast<uint16_t>(value);
    }
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    cdrom_regs_[addr - 0x1F801800] = static_cast<uint8_t>(value & 0xFF);
  }

  raw_[off] = static_cast<uint8_t>(value & 0xFF);
  raw_[off + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  raw_[off + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  raw_[off + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool MmioBus::irq_pending() const {
  return (irq_stat_ & irq_mask_) != 0;
}

uint16_t MmioBus::irq_stat() const {
  return irq_stat_;
}

uint16_t MmioBus::irq_mask() const {
  return irq_mask_;
}

void MmioBus::tick(uint32_t cycles) {
  constexpr uint32_t kCpuCyclesPerFrameNtsc = 33868800 / 60;
  constexpr uint32_t kCpuCyclesPerFramePal = 33868800 / 50;

  if (gpu_busy_cycles_ > 0) {
    if (gpu_busy_cycles_ > cycles) {
      gpu_busy_cycles_ -= cycles;
    } else {
      gpu_busy_cycles_ = 0;
    }
  }

  if (!gpu_read_pending_.empty()) {
    if (gpu_read_pending_delay_ > cycles) {
      gpu_read_pending_delay_ -= cycles;
    } else {
      gpu_read_pending_delay_ = 0;
      queue_gpu_read_data(std::move(gpu_read_pending_));
      gpu_read_pending_.clear();
    }
  }

  gpu_field_cycle_accum_ += cycles;
  uint32_t period = gpu_vmode_pal_ ? kCpuCyclesPerFramePal : kCpuCyclesPerFrameNtsc;
  if (period == 0) {
    period = kCpuCyclesPerFrameNtsc;
  }
  uint32_t field_period = gpu_interlace_ ? std::max(1u, period / 2) : period;
  if (gpu_field_cycle_accum_ >= field_period) {
    if (gpu_interlace_) {
      while (gpu_field_cycle_accum_ >= field_period) {
        gpu_field_cycle_accum_ -= field_period;
        gpu_field_ = !gpu_field_;
      }
    } else {
      gpu_field_cycle_accum_ %= field_period;
      gpu_field_ = false;
    }
  }

  if (cdrom_reading_ && !cdrom_error_ && cdrom_image_.loaded()) {
    if (cdrom_data_fifo_.empty()) {
      if (cdrom_read_timer_ > cycles) {
        cdrom_read_timer_ -= cycles;
      } else {
        cdrom_read_timer_ = 0;
      }
      cdrom_maybe_fill_data();
    }
  }

  for (int i = 0; i < 3; ++i) {
    uint32_t before = timer_count_[i];
    uint16_t mode = timer_mode_[i];
    if (mode & (1u << 2)) { // halted
      continue;
    }
    uint32_t after = (before + cycles) & 0xFFFFu;
    timer_count_[i] = static_cast<uint16_t>(after);
    uint16_t target = timer_target_[i];
    if (target != 0 && before < target && after >= target) {
      if (timer_irq_enable_[i] && timer_irq_on_target_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
      }
      if (mode & (1u << 3)) { // reset on target
        timer_count_[i] = 0;
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
    if (before > after && timer_irq_on_overflow_[i]) {
      if (timer_irq_enable_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
  }
}

uint32_t MmioBus::consume_dma_channel() {
  if (dma_active_channel_ == 3 && cdrom_data_fifo_.empty()) {
    return 0xFFFFFFFFu;
  }
  uint32_t channel = dma_active_channel_;
  dma_active_channel_ = 0xFFFFFFFFu;
  if (channel < 7) {
    bool master = (dma_dicr_ & (1u << 23)) != 0;
    bool enable = (dma_dicr_ & (1u << (16 + channel))) != 0;
    if (master && enable) {
      dma_dicr_ |= (1u << (24 + channel));
      dma_dicr_ = recompute_dma_master(dma_dicr_);
      irq_stat_ |= (1u << 3); // DMA IRQ
    }
    dma_chcr_[channel] &= ~(1u << 24);
    return channel;
  }
  return 0xFFFFFFFFu;
}

uint32_t MmioBus::dma_madr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_madr_[channel];
}

uint32_t MmioBus::dma_bcr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_bcr_[channel];
}

uint32_t MmioBus::dma_chcr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_chcr_[channel];
}

void MmioBus::set_dma_madr(uint32_t channel, uint32_t value) {
  if (channel >= 7) {
    return;
  }
  dma_madr_[channel] = value;
}

bool MmioBus::load_cdrom_image(const std::string &path, std::string &error) {
  return cdrom_image_.load(path, error);
}

size_t MmioBus::read_cdrom_data(uint8_t *dst, size_t len) {
  size_t read = 0;
  while (read < len) {
    cdrom_maybe_fill_data();
    if (cdrom_data_fifo_.empty()) {
      break;
    }
    dst[read++] = cdrom_data_fifo_.front();
    cdrom_data_fifo_.erase(cdrom_data_fifo_.begin());
  }
  return read;
}

bool MmioBus::has_gpu_commands() const {
  return !gpu_gp0_fifo_.empty();
}

std::vector<uint32_t> MmioBus::take_gpu_commands() {
  std::vector<uint32_t> out;
  out.swap(gpu_gp0_fifo_);
  return out;
}

void MmioBus::restore_gpu_commands(std::vector<uint32_t> remainder) {
  gpu_gp0_fifo_ = std::move(remainder);
}

bool MmioBus::has_gpu_control() const {
  return !gpu_gp1_fifo_.empty();
}

std::vector<uint32_t> MmioBus::take_gpu_control() {
  std::vector<uint32_t> out;
  out.swap(gpu_gp1_fifo_);
  return out;
}

void MmioBus::apply_gp0_state(uint32_t word) {
  uint8_t cmd = static_cast<uint8_t>(word >> 24);
  switch (cmd) {
    case 0xE1: { // Draw mode
      uint32_t mode = word & 0x00FFFFFFu;
      gpu_texpage_x_ = mode & 0xFu;
      gpu_texpage_y_ = ((mode >> 4) & 0x1u) | (((mode >> 11) & 0x1u) << 1);
      gpu_semi_ = (mode >> 5) & 0x3u;
      gpu_tex_depth_ = (mode >> 7) & 0x3u;
      gpu_dither_ = (mode & (1u << 9)) != 0;
      gpu_draw_to_display_ = (mode & (1u << 10)) != 0;
      break;
    }
    case 0xE2: { // Texture window
      gpu_tex_window_ = word & 0x00FFFFFFu;
      break;
    }
    case 0xE3: { // Draw area top-left
      gpu_draw_area_tl_ = word & 0x00FFFFFFu;
      break;
    }
    case 0xE4: { // Draw area bottom-right
      gpu_draw_area_br_ = word & 0x00FFFFFFu;
      break;
    }
    case 0xE5: { // Draw offset
      gpu_draw_offset_ = word & 0x00FFFFFFu;
      break;
    }
    case 0xE6: { // Mask bit
      gpu_mask_set_ = (word & 0x1u) != 0;
      gpu_mask_eval_ = (word & 0x2u) != 0;
      break;
    }
    case 0x1F: { // Interrupt request
      gpu_irq_ = true;
      irq_stat_ |= (1u << 1);
      break;
    }
    default:
      break;
  }
}

void MmioBus::queue_gpu_read_data(std::vector<uint32_t> words) {
  if (words.empty()) {
    return;
  }
  if (gpu_read_fifo_.empty()) {
    gpu_read_latch_ = words.front();
  }
  for (uint32_t word : words) {
    gpu_read_fifo_.push_back(word);
  }
}

void MmioBus::schedule_gpu_read_data(std::vector<uint32_t> words, uint32_t delay_cycles) {
  if (words.empty()) {
    return;
  }
  if (delay_cycles == 0 && gpu_read_pending_.empty()) {
    queue_gpu_read_data(std::move(words));
    return;
  }
  if (!gpu_read_pending_.empty()) {
    gpu_read_pending_.insert(gpu_read_pending_.end(), words.begin(), words.end());
  } else {
    gpu_read_pending_ = std::move(words);
    gpu_read_pending_delay_ = delay_cycles;
  }
}

void MmioBus::gpu_add_busy(uint32_t cycles) {
  if (cycles == 0) {
    return;
  }
  gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + cycles, 100000);
}

bool MmioBus::gpu_ready_for_commands() const {
  return (compute_gpustat() & (1u << 26)) != 0;
}

uint32_t MmioBus::gpu_dma_dir() const {
  return gpu_dma_dir_ & 0x3u;
}

uint32_t MmioBus::gpu_read_word() {
  if (!gpu_read_fifo_.empty()) {
    gpu_read_latch_ = gpu_read_fifo_.front();
    gpu_read_fifo_.erase(gpu_read_fifo_.begin());
  }
  return gpu_read_latch_;
}

} // namespace ps1emu
