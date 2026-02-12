#include "core/mmio.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ps1emu {

static uint32_t cdrom_read_period_cycles(uint8_t mode);
static constexpr uint32_t kJoyData = 0x1F801040;
static constexpr uint32_t kJoyStat = 0x1F801044;
static constexpr uint32_t kJoyMode = 0x1F801048;
static constexpr uint32_t kJoyCtrl = 0x1F80104A;
static constexpr uint32_t kJoyBaud = 0x1F80104E;
static constexpr uint32_t kSio1Data = 0x1F801050;
static constexpr uint32_t kSio1Stat = 0x1F801054;
static constexpr uint32_t kSio1Mode = 0x1F801058;
static constexpr uint32_t kSio1Ctrl = 0x1F80105A;
static constexpr uint32_t kSio1Misc = 0x1F80105C;
static constexpr uint32_t kSio1Baud = 0x1F80105E;
static constexpr uint16_t kJoyStatTxReady = 1u << 0;
static constexpr uint16_t kJoyStatRxReady = 1u << 1;
static constexpr uint16_t kJoyStatTxEmpty = 1u << 2;
static constexpr uint16_t kJoyStatDsr = 1u << 7;
static constexpr uint32_t kSpuCtrlAddr = 0x1F801DAA;
static constexpr uint32_t kSpuStatAddr = 0x1F801DAE;

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
  std::memset(timer_cycle_accum_, 0, sizeof(timer_cycle_accum_));
  std::memset(timer_sync_waiting_, 0, sizeof(timer_sync_waiting_));
  gpu_line_cycle_accum_ = 0;
  gpu_line_ = 0;
  std::memset(spu_regs_.data(), 0, spu_regs_.size() * sizeof(uint16_t));
  std::memset(cdrom_regs_.data(), 0, cdrom_regs_.size());
  cdrom_param_fifo_.clear();
  cdrom_response_fifo_.clear();
  cdrom_data_fifo_.clear();
  cdrom_xa_audio_queue_.clear();
  cdrom_pending_.clear();
  cdrom_irq_queue_.clear();
  cdrom_index_ = 0;
  cdrom_status_ = 0;
  cdrom_irq_flags_ = 0;
  cdrom_irq_enable_ = 0;
  cdrom_request_ = 0;
  cdrom_vol_ll_ = 0;
  cdrom_vol_lr_ = 0;
  cdrom_vol_rl_ = 0;
  cdrom_vol_rr_ = 0;
  cdrom_vol_apply_ = 0;
  cdrom_mode_ = 0;
  cdrom_filter_file_ = 0;
  cdrom_filter_channel_ = 0;
  cdrom_session_ = 1;
  cdrom_error_ = false;
  cdrom_reading_ = false;
  cdrom_playing_ = false;
  cdrom_muted_ = false;
  cdrom_seeking_ = false;
  cdrom_read_timer_ = 0;
  cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
  cdrom_last_read_lba_ = 0;
  cdrom_lba_ = 0;
  cdrom_last_mode_ = 0;
  cdrom_last_file_ = 0;
  cdrom_last_channel_ = 0;
  cdrom_last_submode_ = 0;
  cdrom_last_coding_ = 0;
  std::memset(timer_irq_enable_, 0, sizeof(timer_irq_enable_));
  std::memset(timer_irq_repeat_, 0, sizeof(timer_irq_repeat_));
  std::memset(timer_irq_on_overflow_, 0, sizeof(timer_irq_on_overflow_));
  std::memset(timer_irq_on_target_, 0, sizeof(timer_irq_on_target_));
  std::memset(timer_irq_toggle_, 0, sizeof(timer_irq_toggle_));
  dma_pending_mask_ = 0;
  joy_mode_ = 0;
  joy_ctrl_ = 0;
  joy_baud_ = 0;
  joy_rx_data_ = 0xFF;
  joy_rx_ready_ = false;
  joy_ack_ = false;
  joy_irq_pending_ = false;
  joy_tx_queue_.clear();
  joy_tx_delay_cycles_ = 0;
  joy_response_queue_.clear();
  joy_session_active_ = false;
  joy_phase_ = 0;
  joy_device_ = 0;
  sio1_mode_ = 0;
  sio1_ctrl_ = 0;
  sio1_baud_ = 0;
  sio1_misc_ = 0;
  sio1_rx_data_ = 0xFF;
  sio1_rx_ready_ = false;
  spu_ctrl_ = 0;
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
      ready_dma_block = ready_cmd;
      break;
    case 3:
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
      dma_req = ready_dma_block ? 1u : 0u;
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
                                 bool error,
                                 bool playing,
                                 bool seeking) {
  uint8_t status = 0;
  if (has_disc) {
    status |= 0x02;
  }
  if (seeking) {
    status |= 0x08;
  }
  if (reading) {
    status |= 0x10;
  }
  if (data_ready) {
    status |= 0x20;
  }
  if (playing) {
    status |= 0x40;
  }
  if (error) {
    status |= 0x01;
  }
  return status;
}

static constexpr uint32_t kCdromSeekDelayCycles = 33868800 / 60;
static constexpr uint32_t kCdromGetIdDelayCycles = 33868800 / 120;
static constexpr uint32_t kCdromTocDelayCycles = 33868800 / 30;
static constexpr uint32_t kCdromCmdDelayCycles = 1024;

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

static bool cdrom_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_CDROM");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static bool irq_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_IRQ");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static bool gpustat_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_GPUSTAT");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static bool gpu_cmd_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_GPU_CMDS");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static bool gpu_read_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_GPU_READ");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static bool dma_log_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *env = std::getenv("PS1EMU_LOG_DMA");
    cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return cached == 1;
}

static uint32_t joy_byte_delay_cycles(uint16_t baud) {
  uint32_t divisor = baud ? static_cast<uint32_t>(baud) : 0x0088u;
  uint32_t cycles = divisor * 8u;
  if (cycles < 32u) {
    cycles = 32u;
  }
  if (cycles > 20000u) {
    cycles = 20000u;
  }
  return cycles;
}

void MmioBus::cdrom_push_response(uint8_t value) {
  cdrom_response_fifo_.push_back(value);
}

void MmioBus::cdrom_push_response_block(const std::vector<uint8_t> &values) {
  cdrom_response_fifo_.insert(cdrom_response_fifo_.end(), values.begin(), values.end());
}

void MmioBus::cdrom_queue_response(uint32_t delay_cycles,
                                   uint8_t irq_flags,
                                   std::vector<uint8_t> response,
                                   bool clear_seeking) {
  CdromPendingResponse pending;
  pending.delay_cycles = delay_cycles;
  pending.irq_flags = irq_flags;
  pending.response = std::move(response);
  pending.clear_seeking = clear_seeking;
  cdrom_pending_.push_back(std::move(pending));
}

void MmioBus::cdrom_raise_irq(uint8_t flags) {
  uint8_t masked = static_cast<uint8_t>(flags & 0x1Fu);
  if (masked == 0) {
    return;
  }
  if (cdrom_irq_flags_ != 0) {
    cdrom_irq_queue_.push_back(masked);
    return;
  }
  cdrom_irq_flags_ = masked;
  cdrom_update_irq_line();
}

void MmioBus::cdrom_update_irq_line() {
  if ((cdrom_irq_flags_ & cdrom_irq_enable_) != 0) {
    irq_stat_ |= (1u << 2);
  } else {
    irq_stat_ &= static_cast<uint16_t>(~(1u << 2));
  }
}

void MmioBus::cdrom_set_irq_enable(uint8_t enable) {
  cdrom_irq_enable_ = static_cast<uint8_t>(enable & 0x1Fu);
  cdrom_update_irq_line();
}

struct CdromSectorMeta {
  uint8_t mode = 1;
  uint8_t file = 0;
  uint8_t channel = 0;
  uint8_t submode = 0;
  uint8_t coding = 0;
  bool is_xa = false;
  bool xa_audio = false;
  bool form2 = false;
  uint32_t data_offset = 0;
  uint32_t data_size = 2048;
};

static CdromSectorMeta cdrom_parse_sector(const std::vector<uint8_t> &raw) {
  CdromSectorMeta meta;
  if (raw.size() < 0x10) {
    meta.data_offset = 0;
    meta.data_size = static_cast<uint32_t>(raw.size());
    return meta;
  }

  bool sync_ok = raw.size() >= 12 && raw[0] == 0x00 && raw[11] == 0x00;
  for (int i = 1; i <= 10 && sync_ok; ++i) {
    if (raw[static_cast<size_t>(i)] != 0xFF) {
      sync_ok = false;
    }
  }

  if (!sync_ok || raw.size() < 0x10) {
    meta.data_offset = 0;
    meta.data_size = static_cast<uint32_t>(raw.size());
    return meta;
  }

  meta.mode = raw[0x0F];
  if (meta.mode == 2 && raw.size() >= 0x18) {
    meta.is_xa = true;
    meta.file = raw[0x10];
    meta.channel = raw[0x11];
    meta.submode = raw[0x12];
    meta.coding = raw[0x13];
    meta.form2 = (meta.submode & 0x20u) != 0;
    meta.xa_audio = ((meta.submode & 0x04u) != 0) && ((meta.submode & 0x40u) != 0);
    meta.data_offset = 0x18;
    meta.data_size = meta.form2 ? 0x914u : 0x800u;
  } else {
    meta.data_offset = 0x10;
    meta.data_size = 0x800;
  }
  return meta;
}

static std::vector<uint8_t> cdrom_build_whole_sector(const std::vector<uint8_t> &data,
                                                     uint32_t lba,
                                                     uint8_t mode,
                                                     bool mode2) {
  std::vector<uint8_t> out(0x924, 0x00);
  uint8_t mm = 0, ss = 0, ff = 0;
  lba_to_bcd(lba, mm, ss, ff);
  out[0] = mm;
  out[1] = ss;
  out[2] = ff;
  out[3] = mode;

  size_t data_offset = mode2 ? 0x0Cu : 0x04u;
  size_t copy_len = std::min<size_t>(data.size(), out.size() - data_offset);
  if (copy_len > 0) {
    std::copy(data.begin(), data.begin() + static_cast<long>(copy_len), out.begin() + static_cast<long>(data_offset));
  }
  return out;
}


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
  CdromFillResult result = cdrom_fill_data_fifo();
  if (result == CdromFillResult::Delivered) {
    cdrom_read_timer_ = std::max(1u, cdrom_read_period_);
    cdrom_raise_irq(0x02);
  } else if (result == CdromFillResult::Skipped) {
    cdrom_read_timer_ = std::max(1u, cdrom_read_period_);
  }
}

MmioBus::CdromFillResult MmioBus::cdrom_fill_data_fifo() {
  constexpr size_t kMaxXaQueue = 64;
  std::vector<uint8_t> raw;
  if (!cdrom_image_.read_sector_raw(cdrom_lba_, raw)) {
    cdrom_error_ = true;
    return CdromFillResult::Error;
  }

  CdromSectorMeta meta = cdrom_parse_sector(raw);
  cdrom_last_read_lba_ = cdrom_lba_;
  cdrom_last_mode_ = meta.mode;
  cdrom_last_file_ = meta.file;
  cdrom_last_channel_ = meta.channel;
  cdrom_last_submode_ = meta.submode;
  cdrom_last_coding_ = meta.coding;

  bool filter_enabled = (cdrom_mode_ & 0x08u) != 0;
  bool adpcm_enabled = (cdrom_mode_ & 0x40u) != 0;
  bool whole_sector = (cdrom_mode_ & 0x20u) != 0;
  bool filter_match = (meta.file == cdrom_filter_file_) && (meta.channel == cdrom_filter_channel_);

  if (meta.is_xa && meta.xa_audio) {
    bool queue_audio = adpcm_enabled && (!filter_enabled || filter_match);
    if (queue_audio) {
      uint32_t data_offset = meta.data_offset;
      uint32_t data_size = meta.data_size;
      if (raw.size() < data_offset) {
        data_offset = 0;
      }
      if (raw.size() < data_offset + data_size) {
        data_size = static_cast<uint32_t>(raw.size() - data_offset);
      }
      XaAudioSector sector;
      sector.lba = cdrom_lba_;
      sector.mode = meta.mode;
      sector.file = meta.file;
      sector.channel = meta.channel;
      sector.submode = meta.submode;
      sector.coding = meta.coding;
      sector.data.assign(raw.begin() + static_cast<long>(data_offset),
                         raw.begin() + static_cast<long>(data_offset + data_size));
      if (cdrom_xa_audio_queue_.size() >= kMaxXaQueue) {
        cdrom_xa_audio_queue_.pop_front();
      }
      cdrom_xa_audio_queue_.push_back(std::move(sector));
      cdrom_lba_ += 1;
      return CdromFillResult::Skipped;
    }
    if (filter_enabled) {
      cdrom_lba_ += 1;
      return CdromFillResult::Skipped;
    }
  }
  if (filter_enabled && meta.is_xa && !filter_match) {
    cdrom_lba_ += 1;
    return CdromFillResult::Skipped;
  }

  if (whole_sector) {
    if (raw.size() >= 12) {
      cdrom_data_fifo_.assign(raw.begin() + 12, raw.end());
    } else {
      cdrom_data_fifo_ = cdrom_build_whole_sector(raw, cdrom_lba_, meta.mode, meta.mode == 2);
    }
    if (raw.size() == 2048) {
      cdrom_data_fifo_ = cdrom_build_whole_sector(raw, cdrom_lba_, meta.mode, meta.mode == 2);
    }
  } else {
    uint32_t data_offset = meta.data_offset;
    uint32_t data_size = meta.data_size;
    if (raw.size() < data_offset) {
      data_offset = 0;
    }
    if (raw.size() < data_offset + data_size) {
      data_size = static_cast<uint32_t>(raw.size() - data_offset);
    }
    cdrom_data_fifo_.assign(raw.begin() + static_cast<long>(data_offset),
                            raw.begin() + static_cast<long>(data_offset + data_size));
  }

  cdrom_lba_ += 1;
  return cdrom_data_fifo_.empty() ? CdromFillResult::Skipped : CdromFillResult::Delivered;
}

uint8_t MmioBus::cdrom_status() const {
  bool data_ready = !cdrom_data_fifo_.empty() && (cdrom_request_ & 0x01u);
  bool response_ready = !cdrom_response_fifo_.empty();
  bool ready = data_ready || response_ready;
  return cdrom_status_byte(cdrom_image_.loaded(),
                           cdrom_reading_,
                           ready,
                           cdrom_error_,
                           cdrom_playing_,
                           cdrom_seeking_);
}

uint16_t MmioBus::joy_status() const {
  uint16_t status = static_cast<uint16_t>(kJoyStatTxReady | kJoyStatTxEmpty);
  if (joy_rx_ready_) {
    status |= kJoyStatRxReady;
  }
  if (joy_ack_) {
    status |= kJoyStatDsr;
  }
  return status;
}

uint16_t MmioBus::sio1_status() const {
  uint16_t status = static_cast<uint16_t>(kJoyStatTxReady | kJoyStatTxEmpty);
  if (sio1_rx_ready_) {
    status |= kJoyStatRxReady;
  }
  status |= static_cast<uint16_t>(1u << 7); // DSR/CTS high
  status |= static_cast<uint16_t>(1u << 8);
  return status;
}

uint16_t MmioBus::spu_status() const {
  uint16_t ctrl = spu_ctrl_;
  uint16_t status = static_cast<uint16_t>(ctrl & 0x3Fu);
  if (ctrl & (1u << 5)) {
    status |= static_cast<uint16_t>(1u << 7);
  }
  uint16_t transfer = (ctrl >> 4) & 0x3u;
  if (transfer == 2) {
    status |= static_cast<uint16_t>(1u << 8);
  } else if (transfer == 3) {
    status |= static_cast<uint16_t>(1u << 9);
  }
  return status;
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

  if (cdrom_log_enabled()) {
    std::ostringstream oss;
    oss << "[cdrom] cmd=0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(cmd);
    if (!params.empty()) {
      oss << " params=";
      for (size_t i = 0; i < params.size(); ++i) {
        if (i) {
          oss << ",";
        }
        oss << "0x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(params[i]);
      }
    }
    std::cerr << oss.str() << "\n";
  }

  auto queue_status = [&](uint8_t irq_flags, bool clear_seeking = false) {
    cdrom_queue_response(kCdromCmdDelayCycles, irq_flags, {cdrom_status()}, clear_seeking);
  };

  switch (cmd) {
    case 0x00: { // Sync
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x01: { // Getstat
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x02: { // Setloc
      if (params.size() >= 3) {
        cdrom_lba_ = bcd_to_lba(params[0], params[1], params[2]);
      } else {
        cdrom_error_ = true;
      }
      queue_status(0x01);
      break;
    }
    case 0x03: { // Play
      cdrom_playing_ = true;
      cdrom_reading_ = false;
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x04: // Forward
    case 0x05: { // Backward
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x06: // ReadN
    case 0x1B: { // ReadS
      cdrom_reading_ = true;
      cdrom_playing_ = false;
      cdrom_seeking_ = false;
      cdrom_request_ |= 0x01u;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_read_timer_ = std::max(1u, cdrom_read_period_);
      cdrom_data_fifo_.clear();
      if (!cdrom_image_.loaded()) {
        cdrom_error_ = true;
      }
      queue_status(0x04);
      break;
    }
    case 0x07: { // MotorOn
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x08: { // Stop
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_read_timer_ = 0;
      cdrom_request_ &= static_cast<uint8_t>(~0x01u);
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x09: { // Pause
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_read_timer_ = 0;
      cdrom_request_ &= static_cast<uint8_t>(~0x01u);
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0A: { // Init
      cdrom_mode_ = 0;
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_muted_ = false;
      cdrom_seeking_ = false;
      cdrom_request_ = 0;
      cdrom_filter_file_ = 0;
      cdrom_filter_channel_ = 0;
      cdrom_session_ = 1;
      cdrom_read_timer_ = 0;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_pending_.clear();
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0B: { // Mute
      cdrom_muted_ = true;
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0C: { // Demute
      cdrom_muted_ = false;
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0D: { // Setfilter
      if (params.size() >= 2) {
        cdrom_filter_file_ = params[0];
        cdrom_filter_channel_ = params[1];
      }
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0E: { // Setmode
      if (!params.empty()) {
        cdrom_mode_ = params[0];
      }
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x0F: { // Getparam
      cdrom_push_response(cdrom_status());
      cdrom_push_response(cdrom_mode_);
      cdrom_push_response(0x00);
      cdrom_push_response(cdrom_filter_file_);
      cdrom_push_response(cdrom_filter_channel_);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x10: { // GetlocL
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(cdrom_last_read_lba_, mm, ss, ff);
      cdrom_push_response(cdrom_status());
      cdrom_push_response(mm);
      cdrom_push_response(ss);
      cdrom_push_response(ff);
      cdrom_push_response(cdrom_last_mode_);
      cdrom_push_response(cdrom_last_file_);
      cdrom_push_response(cdrom_last_channel_);
      cdrom_push_response(cdrom_last_submode_);
      cdrom_push_response(cdrom_last_coding_);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x11: { // GetlocP
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(cdrom_last_read_lba_, mm, ss, ff);
      cdrom_push_response(cdrom_status());
      cdrom_push_response(0x01); // track
      cdrom_push_response(0x01); // index
      cdrom_push_response(mm);
      cdrom_push_response(ss);
      cdrom_push_response(ff);
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
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x13: { // GetTN
      cdrom_push_response(cdrom_status());
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
      cdrom_push_response(cdrom_status());
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
      cdrom_seeking_ = true;
      queue_status(0x04);
      uint32_t delay = (kCdromSeekDelayCycles > kCdromCmdDelayCycles)
                           ? (kCdromSeekDelayCycles - kCdromCmdDelayCycles)
                           : 1u;
      cdrom_queue_response(delay, 0x01, {cdrom_status()}, true);
      break;
    }
    case 0x17: { // SetClock
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x18: { // GetClock
      cdrom_push_response(cdrom_status());
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x19: { // Test
      uint8_t sub = params.empty() ? 0 : params[0];
      cdrom_push_response(cdrom_status());
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
      queue_status(0x04);
      uint8_t disc_type = cdrom_image_.loaded() ? 0x20 : 0x00;
      char region = cdrom_image_.loaded() ? cdrom_image_.region_code() : 'I';
      std::vector<uint8_t> response = {
          cdrom_status(),
          0x00,
          disc_type,
          0x00,
          static_cast<uint8_t>('S'),
          static_cast<uint8_t>('C'),
          static_cast<uint8_t>('E'),
          static_cast<uint8_t>(region),
      };
      uint32_t delay = (kCdromGetIdDelayCycles > kCdromCmdDelayCycles)
                           ? (kCdromGetIdDelayCycles - kCdromCmdDelayCycles)
                           : 1u;
      cdrom_queue_response(delay, 0x01, std::move(response));
      break;
    }
    case 0x1C: { // Reset
      cdrom_mode_ = 0;
      cdrom_reading_ = false;
      cdrom_playing_ = false;
      cdrom_muted_ = false;
      cdrom_seeking_ = false;
      cdrom_request_ = 0;
      cdrom_filter_file_ = 0;
      cdrom_filter_channel_ = 0;
      cdrom_read_timer_ = 0;
      cdrom_read_period_ = cdrom_read_period_cycles(cdrom_mode_);
      cdrom_pending_.clear();
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1D: { // GetQ
      cdrom_push_response(cdrom_status());
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_push_response(0x00);
      cdrom_raise_irq(0x01);
      break;
    }
    case 0x1E: { // ReadTOC
      cdrom_seeking_ = true;
      queue_status(0x04);
      uint8_t first_track = cdrom_image_.first_track();
      uint8_t last_track = cdrom_image_.last_track();
      uint8_t mm = 0, ss = 0, ff = 0;
      lba_to_bcd(cdrom_image_.leadout_lba(), mm, ss, ff);
      std::vector<uint8_t> response = {
          cdrom_status(),
          first_track,
          last_track,
          mm,
          ss,
          ff,
      };
      uint32_t delay = (kCdromTocDelayCycles > kCdromCmdDelayCycles)
                           ? (kCdromTocDelayCycles - kCdromCmdDelayCycles)
                           : 1u;
      cdrom_queue_response(delay, 0x01, std::move(response), true);
      break;
    }
    default: {
      cdrom_error_ = true;
      cdrom_push_response(cdrom_status());
      cdrom_raise_irq(0x10);
      break;
    }
  }
}

uint32_t MmioBus::offset(uint32_t addr) const {
  return addr - kBase;
}

uint8_t MmioBus::read8(uint32_t addr) {
  if (addr == 0x1F801070 || addr == 0x1F801071) { // I_STAT
    return (addr & 1) ? static_cast<uint8_t>((irq_stat_ >> 8) & 0xFFu)
                      : static_cast<uint8_t>(irq_stat_ & 0xFFu);
  }
  if (addr == 0x1F801074 || addr == 0x1F801075) { // I_MASK
    return (addr & 1) ? static_cast<uint8_t>((irq_mask_ >> 8) & 0xFFu)
                      : static_cast<uint8_t>(irq_mask_ & 0xFFu);
  }
  if (addr == kJoyData) {
    uint8_t value = 0xFF;
    if (!joy_response_queue_.empty()) {
      value = joy_response_queue_.front();
      joy_response_queue_.pop_front();
    } else if (joy_rx_ready_) {
      value = joy_rx_data_;
    }
    joy_rx_ready_ = !joy_response_queue_.empty();
    joy_ack_ = joy_rx_ready_;
    if (!joy_rx_ready_ && joy_tx_queue_.empty() && joy_tx_delay_cycles_ == 0) {
      joy_session_active_ = false;
      joy_phase_ = 0;
      joy_device_ = 0;
    }
    return value;
  }
  if (addr == kJoyStat || addr == kJoyStat + 1) {
    uint16_t status = joy_status();
    return (addr == kJoyStat) ? static_cast<uint8_t>(status & 0xFFu)
                              : static_cast<uint8_t>((status >> 8) & 0xFFu);
  }
  if (addr == kJoyMode || addr == kJoyMode + 1) {
    return (addr == kJoyMode) ? static_cast<uint8_t>(joy_mode_ & 0xFFu)
                              : static_cast<uint8_t>((joy_mode_ >> 8) & 0xFFu);
  }
  if (addr == kJoyCtrl || addr == kJoyCtrl + 1) {
    return (addr == kJoyCtrl) ? static_cast<uint8_t>(joy_ctrl_ & 0xFFu)
                              : static_cast<uint8_t>((joy_ctrl_ >> 8) & 0xFFu);
  }
  if (addr == kJoyBaud || addr == kJoyBaud + 1) {
    return (addr == kJoyBaud) ? static_cast<uint8_t>(joy_baud_ & 0xFFu)
                              : static_cast<uint8_t>((joy_baud_ >> 8) & 0xFFu);
  }
  if (addr == kSio1Data) {
    uint8_t value = sio1_rx_ready_ ? sio1_rx_data_ : 0xFF;
    sio1_rx_ready_ = false;
    return value;
  }
  if (addr == kSio1Stat || addr == kSio1Stat + 1) {
    uint16_t status = sio1_status();
    return (addr == kSio1Stat) ? static_cast<uint8_t>(status & 0xFFu)
                               : static_cast<uint8_t>((status >> 8) & 0xFFu);
  }
  if (addr == kSio1Mode || addr == kSio1Mode + 1) {
    return (addr == kSio1Mode) ? static_cast<uint8_t>(sio1_mode_ & 0xFFu)
                               : static_cast<uint8_t>((sio1_mode_ >> 8) & 0xFFu);
  }
  if (addr == kSio1Ctrl || addr == kSio1Ctrl + 1) {
    return (addr == kSio1Ctrl) ? static_cast<uint8_t>(sio1_ctrl_ & 0xFFu)
                               : static_cast<uint8_t>((sio1_ctrl_ >> 8) & 0xFFu);
  }
  if (addr == kSio1Misc || addr == kSio1Misc + 1) {
    return (addr == kSio1Misc) ? static_cast<uint8_t>(sio1_misc_ & 0xFFu)
                               : static_cast<uint8_t>((sio1_misc_ >> 8) & 0xFFu);
  }
  if (addr == kSio1Baud || addr == kSio1Baud + 1) {
    return (addr == kSio1Baud) ? static_cast<uint8_t>(sio1_baud_ & 0xFFu)
                               : static_cast<uint8_t>((sio1_baud_ >> 8) & 0xFFu);
  }
  if (addr == kSpuStatAddr || addr == kSpuStatAddr + 1) {
    uint16_t status = spu_status();
    return (addr == kSpuStatAddr) ? static_cast<uint8_t>(status & 0xFFu)
                                  : static_cast<uint8_t>((status >> 8) & 0xFFu);
  }
  if (addr == kSpuCtrlAddr || addr == kSpuCtrlAddr + 1) {
    return (addr == kSpuCtrlAddr) ? static_cast<uint8_t>(spu_ctrl_ & 0xFFu)
                                  : static_cast<uint8_t>((spu_ctrl_ >> 8) & 0xFFu);
  }
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint32_t reg = addr - 0x1F801800;
    if (reg == 0) {
      cdrom_status_ = cdrom_status();
      uint8_t value = static_cast<uint8_t>((cdrom_status_ & 0xFCu) | (cdrom_index_ & 0x03u));
      if (cdrom_log_enabled()) {
        std::cerr << "[cdrom] read reg0 value=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value) << "\n";
      }
      return value;
    }
    if (reg == 1) {
      uint8_t value = 0;
      switch (cdrom_index_ & 0x3u) {
        case 0: {
          if (!cdrom_response_fifo_.empty()) {
            value = cdrom_response_fifo_.front();
            cdrom_response_fifo_.erase(cdrom_response_fifo_.begin());
          }
          break;
        }
        case 1:
          value = cdrom_irq_enable_;
          break;
        case 2:
          value = cdrom_vol_ll_;
          break;
        case 3:
          value = cdrom_vol_rr_;
          break;
      }
      if (cdrom_log_enabled()) {
        std::cerr << "[cdrom] read reg1 idx=" << std::dec << static_cast<int>(cdrom_index_ & 0x3u)
                  << " value=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value) << "\n";
      }
      return value;
    }
    if (reg == 2) {
      uint8_t value = 0;
      switch (cdrom_index_ & 0x3u) {
        case 0: {
          if ((cdrom_request_ & 0x01u) != 0) {
            cdrom_maybe_fill_data();
          }
          if ((cdrom_request_ & 0x01u) != 0 && !cdrom_data_fifo_.empty()) {
            value = cdrom_data_fifo_.front();
            cdrom_data_fifo_.erase(cdrom_data_fifo_.begin());
          }
          break;
        }
        case 1:
          value = cdrom_irq_flags_;
          break;
        case 2:
          value = cdrom_vol_lr_;
          break;
        case 3:
          value = cdrom_vol_rl_;
          break;
      }
      if (cdrom_log_enabled()) {
        std::cerr << "[cdrom] read reg2 idx=" << std::dec << static_cast<int>(cdrom_index_ & 0x3u)
                  << " value=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value) << "\n";
      }
      return value;
    }
    if (reg == 3) {
      uint8_t value = (cdrom_index_ & 0x3u) < 2 ? cdrom_irq_flags_ : cdrom_vol_apply_;
      if (cdrom_log_enabled()) {
        std::cerr << "[cdrom] read reg3 idx=" << std::dec << static_cast<int>(cdrom_index_ & 0x3u)
                  << " value=0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value) << "\n";
      }
      return value;
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
  if (addr == 0x1F801070) { // I_STAT
    return irq_stat_;
  }
  if (addr == 0x1F801074) { // I_MASK
    return irq_mask_;
  }
  if (addr == kJoyStat) {
    return joy_status();
  }
  if (addr == kJoyMode) {
    return joy_mode_;
  }
  if (addr == kJoyCtrl) {
    return joy_ctrl_;
  }
  if (addr == kJoyBaud) {
    return joy_baud_;
  }
  if (addr == kSio1Stat) {
    return sio1_status();
  }
  if (addr == kSio1Mode) {
    return sio1_mode_;
  }
  if (addr == kSio1Ctrl) {
    return sio1_ctrl_;
  }
  if (addr == kSio1Misc) {
    return sio1_misc_;
  }
  if (addr == kSio1Baud) {
    return sio1_baud_;
  }
  if (addr == kSpuStatAddr) {
    return spu_status();
  }
  if (addr == kSpuCtrlAddr) {
    return spu_ctrl_;
  }
  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        uint16_t value = timer_count_[timer];
        if (irq_log_enabled()) {
          static uint16_t last_count[3] = {0xFFFFu, 0xFFFFu, 0xFFFFu};
          if (value != last_count[timer]) {
            last_count[timer] = value;
            std::cerr << "[timer] T" << timer << " count=0x" << std::hex << std::setw(4)
                      << std::setfill('0') << value << "\n";
          }
        }
        return value;
      }
      if (reg == 0x4) {
        uint16_t value = timer_mode_[timer];
        timer_mode_[timer] &= static_cast<uint16_t>(~((1u << 11) | (1u << 12)));
        if (!timer_irq_toggle_[timer]) {
          timer_mode_[timer] |= static_cast<uint16_t>(1u << 10);
        }
        return value;
      }
      if (reg == 0x8) {
        return timer_target_[timer];
      }
    }
  }
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

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        uint16_t value = timer_count_[timer];
        if (irq_log_enabled()) {
          static uint16_t last_count32[3] = {0xFFFFu, 0xFFFFu, 0xFFFFu};
          if (value != last_count32[timer]) {
            last_count32[timer] = value;
            std::cerr << "[timer] T" << timer << " count=0x" << std::hex << std::setw(4)
                      << std::setfill('0') << value << "\n";
          }
        }
        return value;
      }
      if (reg == 0x4) {
        uint16_t value = timer_mode_[timer];
        timer_mode_[timer] &= static_cast<uint16_t>(~((1u << 11) | (1u << 12)));
        if (!timer_irq_toggle_[timer]) {
          timer_mode_[timer] |= static_cast<uint16_t>(1u << 10);
        } else {
          timer_mode_[timer] &= static_cast<uint16_t>(~(1u << 10));
        }
        return value;
      }
      if (reg == 0x8) {
        return timer_target_[timer];
      }
    }
  }

  if (addr == kJoyStat) {
    return joy_status();
  }
  if (addr == kJoyMode) {
    return joy_mode_;
  }
  if (addr == kJoyCtrl) {
    return joy_ctrl_;
  }
  if (addr == kJoyBaud) {
    return joy_baud_;
  }
  if (addr == kSio1Stat) {
    return sio1_status();
  }
  if (addr == kSio1Mode) {
    return sio1_mode_;
  }
  if (addr == kSio1Ctrl) {
    return sio1_ctrl_;
  }
  if (addr == kSio1Misc) {
    return sio1_misc_;
  }
  if (addr == kSio1Baud) {
    return sio1_baud_;
  }
  if (addr == kSpuStatAddr) {
    return spu_status();
  }
  if (addr == kSpuCtrlAddr) {
    return spu_ctrl_;
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
    if (gpu_read_log_enabled()) {
      std::cerr << "[gpu] GPUREAD=0x" << std::hex << std::setw(8) << std::setfill('0')
                << gpu_read_latch_ << "\n";
    }
    return gpu_read_latch_;
  }
  if (addr == 0x1F801814) { // GPU GP1
    uint32_t stat = compute_gpustat();
    if (gpustat_log_enabled()) {
      static uint32_t last_stat = 0xFFFFFFFFu;
      if (stat != last_stat) {
        last_stat = stat;
        std::cerr << "[gpu] GPUSTAT=0x" << std::hex << std::setw(8) << std::setfill('0')
                  << stat << " dma_ready=" << ((stat & (1u << 28)) ? 1 : 0) << "\n";
      }
    }
    return stat;
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
        uint32_t value = dma_chcr_[index];
        if (dma_log_enabled()) {
          static uint32_t last_chcr[7] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
          if (value != last_chcr[index]) {
            last_chcr[index] = value;
            std::cerr << "[dma] CHCR" << std::dec << index << "=0x" << std::hex << std::setw(8)
                      << std::setfill('0') << value << "\n";
          }
        }
        return value;
      }
    }
  }
  if (addr == 0x1F8010F0) { // DPCR
    if (dma_log_enabled()) {
      static uint32_t last_dpcr = 0xFFFFFFFFu;
      if (dma_dpcr_ != last_dpcr) {
        last_dpcr = dma_dpcr_;
        std::cerr << "[dma] DPCR=0x" << std::hex << std::setw(8) << std::setfill('0')
                  << dma_dpcr_ << "\n";
      }
    }
    return dma_dpcr_;
  }
  if (addr == 0x1F8010F4) { // DICR
    if (dma_log_enabled()) {
      static uint32_t last_dicr = 0xFFFFFFFFu;
      if (dma_dicr_ != last_dicr) {
        last_dicr = dma_dicr_;
        std::cerr << "[dma] DICR=0x" << std::hex << std::setw(8) << std::setfill('0')
                  << dma_dicr_ << "\n";
      }
    }
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
        uint16_t value = timer_mode_[timer];
        timer_mode_[timer] &= static_cast<uint16_t>(~((1u << 11) | (1u << 12)));
        if (!timer_irq_toggle_[timer]) {
          timer_mode_[timer] |= static_cast<uint16_t>(1u << 10);
        }
        return value;
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
  if (addr == 0x1F801070 || addr == 0x1F801071) { // I_STAT
    uint16_t mask = (addr & 1) ? static_cast<uint16_t>(value) << 8
                               : static_cast<uint16_t>(value);
    irq_stat_ &= static_cast<uint16_t>(~mask);
    return;
  }
  if (addr == 0x1F801074 || addr == 0x1F801075) { // I_MASK
    if (addr & 1) {
      irq_mask_ = static_cast<uint16_t>((irq_mask_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
    } else {
      irq_mask_ = static_cast<uint16_t>((irq_mask_ & 0xFF00u) | value);
    }
    return;
  }
  if (addr == kJoyData) {
    if (!joy_session_active_) {
      if (value == 0x01) {
        joy_device_ = 1; // pad
      } else if (value == 0x81) {
        joy_device_ = 2; // memory card
      } else {
        joy_device_ = 0;
      }
      joy_session_active_ = true;
      joy_phase_ = 0;
    }

    uint8_t response = 0xFF;
    if (joy_device_ == 1) {
      switch (joy_phase_) {
        case 0:
          response = 0xFF;
          break;
        case 1:
          response = 0x41;
          break;
        case 2:
          response = 0x5A;
          break;
        case 3:
          response = 0xFF;
          break;
        case 4:
          response = 0xFF;
          break;
        default:
          response = 0xFF;
          break;
      }
    } else if (joy_device_ == 2) {
      if (joy_phase_ == 0) {
        response = 0xFF;
      } else if (joy_phase_ == 1) {
        response = 0x5A;
      } else {
        response = 0x00;
      }
    } else {
      response = 0xFF;
    }

    joy_tx_queue_.push_back(response);
    joy_phase_ = static_cast<uint8_t>(joy_phase_ + 1);
    if (joy_tx_delay_cycles_ == 0) {
      joy_tx_delay_cycles_ = joy_byte_delay_cycles(joy_baud_);
    }
    return;
  }
  if (addr == kJoyMode) {
    joy_mode_ = static_cast<uint16_t>((joy_mode_ & 0xFF00u) | value);
  } else if (addr == kJoyMode + 1) {
    joy_mode_ = static_cast<uint16_t>((joy_mode_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  } else if (addr == kJoyCtrl) {
    joy_ctrl_ = static_cast<uint16_t>((joy_ctrl_ & 0xFF00u) | value);
    if (joy_ctrl_ & 0x0010u) {
      joy_irq_pending_ = false;
      irq_stat_ &= static_cast<uint16_t>(~(1u << 7));
    }
    if (joy_ctrl_ & 0x0040u) {
      joy_rx_ready_ = false;
      joy_ack_ = false;
      joy_tx_queue_.clear();
      joy_tx_delay_cycles_ = 0;
      joy_response_queue_.clear();
      joy_session_active_ = false;
      joy_phase_ = 0;
      joy_device_ = 0;
    }
  } else if (addr == kJoyCtrl + 1) {
    joy_ctrl_ = static_cast<uint16_t>((joy_ctrl_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
    if (joy_ctrl_ & 0x0010u) {
      joy_irq_pending_ = false;
      irq_stat_ &= static_cast<uint16_t>(~(1u << 7));
    }
    if (joy_ctrl_ & 0x0040u) {
      joy_rx_ready_ = false;
      joy_ack_ = false;
      joy_tx_queue_.clear();
      joy_tx_delay_cycles_ = 0;
      joy_response_queue_.clear();
      joy_session_active_ = false;
      joy_phase_ = 0;
      joy_device_ = 0;
    }
  } else if (addr == kJoyBaud) {
    joy_baud_ = static_cast<uint16_t>((joy_baud_ & 0xFF00u) | value);
  } else if (addr == kJoyBaud + 1) {
    joy_baud_ = static_cast<uint16_t>((joy_baud_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  } else if (addr == kSio1Data) {
    sio1_rx_data_ = 0xFF;
    sio1_rx_ready_ = true;
    return;
  } else if (addr == kSio1Mode) {
    sio1_mode_ = static_cast<uint16_t>((sio1_mode_ & 0xFF00u) | value);
  } else if (addr == kSio1Mode + 1) {
    sio1_mode_ = static_cast<uint16_t>((sio1_mode_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  } else if (addr == kSio1Ctrl) {
    sio1_ctrl_ = static_cast<uint16_t>((sio1_ctrl_ & 0xFF00u) | value);
  } else if (addr == kSio1Ctrl + 1) {
    sio1_ctrl_ = static_cast<uint16_t>((sio1_ctrl_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  } else if (addr == kSio1Misc) {
    sio1_misc_ = static_cast<uint16_t>((sio1_misc_ & 0xFF00u) | value);
  } else if (addr == kSio1Misc + 1) {
    sio1_misc_ = static_cast<uint16_t>((sio1_misc_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  } else if (addr == kSio1Baud) {
    sio1_baud_ = static_cast<uint16_t>((sio1_baud_ & 0xFF00u) | value);
  } else if (addr == kSio1Baud + 1) {
    sio1_baud_ = static_cast<uint16_t>((sio1_baud_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  }
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    uint32_t reg = addr - 0x1F801800;
    if (cdrom_log_enabled()) {
      std::cerr << "[cdrom] write reg" << reg
                << " value=0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(value) << "\n";
    }
    if (reg == 0) {
      cdrom_index_ = value & 0x03u;
    } else if (reg == 1) {
      switch (cdrom_index_ & 0x3u) {
        case 0:
          cdrom_execute_command(value);
          break;
        case 1:
          cdrom_set_irq_enable(value);
          break;
        case 2:
          cdrom_vol_ll_ = value;
          break;
        case 3:
          cdrom_vol_rr_ = value;
          break;
      }
    } else if (reg == 2) {
      switch (cdrom_index_ & 0x3u) {
        case 0:
          cdrom_param_fifo_.push_back(value);
          break;
        case 1:
          cdrom_set_irq_enable(value);
          break;
        case 2:
          cdrom_vol_lr_ = value;
          break;
        case 3:
          cdrom_vol_rl_ = value;
          break;
      }
    } else if (reg == 3) {
      uint8_t bits = static_cast<uint8_t>(value & 0x1Fu);
      if ((value & 0x80u) == 0) {
        if ((cdrom_index_ & 0x3u) == 0) {
          cdrom_request_ = value;
          if (cdrom_request_ & 0x01u) {
            cdrom_maybe_fill_data();
          }
        } else if ((cdrom_index_ & 0x3u) >= 2) {
          cdrom_vol_apply_ = value;
        }
        if ((cdrom_index_ & 0x3u) <= 1) {
          cdrom_set_irq_enable(bits);
        }
      } else {
        uint8_t ack = bits;
        if (ack) {
          cdrom_irq_flags_ &= static_cast<uint8_t>(~ack);
          if (cdrom_irq_flags_ == 0 && !cdrom_irq_queue_.empty()) {
            cdrom_irq_flags_ = cdrom_irq_queue_.front();
            cdrom_irq_queue_.pop_front();
          }
          cdrom_update_irq_line();
        }
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
  if (addr == 0x1F801070) { // I_STAT
    if (irq_log_enabled() && value != 0) {
      std::cerr << "[irq] I_STAT clear=0x" << std::hex << std::setw(4) << std::setfill('0')
                << value << "\n";
    }
    irq_stat_ &= static_cast<uint16_t>(~value);
    return;
  }
  if (addr == 0x1F801074) { // I_MASK
    if (irq_log_enabled()) {
      std::cerr << "[irq] I_MASK=0x" << std::hex << std::setw(4) << std::setfill('0')
                << value << "\n";
    }
    irq_mask_ = value;
    return;
  }
  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    write8(addr, static_cast<uint8_t>(value & 0xFFu));
    write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFFu));
    return;
  }
  if (addr == kJoyMode) {
    joy_mode_ = value;
  } else if (addr == kJoyCtrl) {
    joy_ctrl_ = value;
    if (joy_ctrl_ & 0x0010u) {
      joy_irq_pending_ = false;
      irq_stat_ &= static_cast<uint16_t>(~(1u << 7));
    }
    if (joy_ctrl_ & 0x0040u) {
      joy_rx_ready_ = false;
      joy_ack_ = false;
      joy_tx_queue_.clear();
      joy_tx_delay_cycles_ = 0;
      joy_response_queue_.clear();
      joy_session_active_ = false;
      joy_phase_ = 0;
      joy_device_ = 0;
    }
  } else if (addr == kJoyBaud) {
    joy_baud_ = value;
  } else if (addr == kSio1Mode) {
    sio1_mode_ = value;
  } else if (addr == kSio1Ctrl) {
    sio1_ctrl_ = value;
  } else if (addr == kSio1Misc) {
    sio1_misc_ = value;
  } else if (addr == kSio1Baud) {
    sio1_baud_ = value;
  } else if (addr == kSpuCtrlAddr) {
    spu_ctrl_ = value;
  }
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
        timer_cycle_accum_[timer] = 0;
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " count=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << value << "\n";
        }
      } else if (reg == 0x4) {
        timer_irq_on_target_[timer] = (value & (1u << 4)) != 0;
        timer_irq_on_overflow_[timer] = (value & (1u << 5)) != 0;
        timer_irq_repeat_[timer] = (value & (1u << 6)) != 0;
        timer_irq_toggle_[timer] = (value & (1u << 7)) != 0;
        timer_irq_enable_[timer] = timer_irq_on_target_[timer] || timer_irq_on_overflow_[timer];
        timer_mode_[timer] = static_cast<uint16_t>(value & 0x03FFu);
        timer_mode_[timer] |= static_cast<uint16_t>(1u << 10);
        timer_mode_[timer] &= static_cast<uint16_t>(~((1u << 11) | (1u << 12)));
        timer_count_[timer] = 0;
        timer_cycle_accum_[timer] = 0;
        irq_stat_ &= static_cast<uint16_t>(~(1u << (4 + timer)));
        timer_sync_waiting_[timer] =
            ((timer_mode_[timer] & 0x1u) && (((timer_mode_[timer] >> 1) & 0x3u) == 3u));
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " mode=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << timer_mode_[timer] << "\n";
        }
      } else if (reg == 0x8) {
        timer_target_[timer] = value;
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " target=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << value << "\n";
        }
      }
    }
  }

  if (addr == 0x1F801070) { // I_STAT
    if (irq_log_enabled() && value != 0) {
      std::cerr << "[irq] I_STAT clear=0x" << std::hex << std::setw(4) << std::setfill('0')
                << value << "\n";
    }
    irq_stat_ &= static_cast<uint16_t>(~value);
  } else if (addr == 0x1F801074) { // I_MASK
    if (irq_log_enabled()) {
      std::cerr << "[irq] I_MASK=0x" << std::hex << std::setw(4) << std::setfill('0')
                << value << "\n";
    }
    irq_mask_ = value;
  }
}

void MmioBus::write32(uint32_t addr, uint32_t value) {
  uint32_t off = offset(addr);
  if (off + 3 >= kSize) {
    return;
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    write16(addr, static_cast<uint16_t>(value & 0xFFFFu));
    return;
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    write8(addr, static_cast<uint8_t>(value & 0xFFu));
    write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFFu));
    write8(addr + 2, static_cast<uint8_t>((value >> 16) & 0xFFu));
    write8(addr + 3, static_cast<uint8_t>((value >> 24) & 0xFFu));
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
    apply_gp0_state(value);
    uint32_t penalty = 1;
    gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + penalty, 100000);
  } else if (addr == 0x1F801814) { // GPU GP1
    gpu_gp1_ = value;
    gpu_gp1_fifo_.push_back(value);
    uint32_t penalty = 1;
    gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + penalty, 100000);
    uint8_t cmd = static_cast<uint8_t>(value >> 24);
    if (gpu_cmd_log_enabled()) {
      std::cerr << "[gpu] GP1=0x" << std::hex << std::setw(8) << std::setfill('0') << value
                << " cmd=0x" << std::setw(2) << static_cast<uint32_t>(cmd) << "\n";
    }
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
          dma_pending_mask_ |= (1u << index);
          if (irq_log_enabled()) {
            std::cerr << "[irq] DMA start ch=" << std::dec << index
                      << " madr=0x" << std::hex << std::setw(8) << std::setfill('0')
                      << dma_madr_[index]
                      << " bcr=0x" << std::setw(8) << dma_bcr_[index]
                      << " chcr=0x" << std::setw(8) << dma_chcr_[index] << "\n";
          }
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
    if (dma_dicr_ & (1u << 31)) {
      irq_stat_ |= (1u << 3);
    } else {
      irq_stat_ &= static_cast<uint16_t>(~(1u << 3));
    }
    if (irq_log_enabled()) {
      std::cerr << "[irq] DICR=0x" << std::hex << std::setw(8) << std::setfill('0')
                << dma_dicr_ << "\n";
    }
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        timer_count_[timer] = static_cast<uint16_t>(value);
        timer_cycle_accum_[timer] = 0;
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " count=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << timer_count_[timer] << "\n";
        }
      } else if (reg == 0x4) {
        uint16_t mode = static_cast<uint16_t>(value);
        timer_irq_on_target_[timer] = (mode & (1u << 4)) != 0;
        timer_irq_on_overflow_[timer] = (mode & (1u << 5)) != 0;
        timer_irq_repeat_[timer] = (mode & (1u << 6)) != 0;
        timer_irq_toggle_[timer] = (mode & (1u << 7)) != 0;
        timer_irq_enable_[timer] = timer_irq_on_target_[timer] || timer_irq_on_overflow_[timer];
        timer_mode_[timer] = static_cast<uint16_t>(mode & 0x03FFu);
        timer_mode_[timer] |= static_cast<uint16_t>(1u << 10);
        timer_mode_[timer] &= static_cast<uint16_t>(~((1u << 11) | (1u << 12)));
        timer_count_[timer] = 0;
        timer_cycle_accum_[timer] = 0;
        irq_stat_ &= static_cast<uint16_t>(~(1u << (4 + timer)));
        timer_sync_waiting_[timer] =
            ((timer_mode_[timer] & 0x1u) && (((timer_mode_[timer] >> 1) & 0x3u) == 3u));
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " mode=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << timer_mode_[timer] << "\n";
        }
      } else if (reg == 0x8) {
        timer_target_[timer] = static_cast<uint16_t>(value);
        if (irq_log_enabled()) {
          std::cerr << "[timer] T" << timer << " target=0x" << std::hex << std::setw(4)
                    << std::setfill('0') << timer_target_[timer] << "\n";
        }
      }
    }
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) {
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      spu_regs_[index] = static_cast<uint16_t>(value);
    }
    if (addr == kSpuCtrlAddr) {
      spu_ctrl_ = static_cast<uint16_t>(value);
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
  bool vblank_pulse = false;
  uint32_t hblank_pulses = 0;
  bool vblank_start_pulse = false;

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
  uint32_t lines_per_frame = gpu_vmode_pal_ ? 314u : 262u;
  uint32_t vblank_start_line = gpu_vmode_pal_ ? 256u : 240u;
  uint32_t line_period = period / std::max(1u, lines_per_frame);
  if (line_period == 0) {
    line_period = 1;
  }
  gpu_line_cycle_accum_ += cycles;
  while (gpu_line_cycle_accum_ >= line_period) {
    gpu_line_cycle_accum_ -= line_period;
    hblank_pulses++;
    gpu_line_++;
    if (gpu_line_ >= lines_per_frame) {
      gpu_line_ = 0;
    }
    if (gpu_line_ == vblank_start_line) {
      vblank_start_pulse = true;
    }
  }
  bool in_vblank = gpu_line_ >= vblank_start_line;
  bool in_hblank = hblank_pulses > 0;
  uint32_t field_period = gpu_interlace_ ? std::max(1u, period / 2) : period;
  if (gpu_field_cycle_accum_ >= field_period) {
    if (gpu_interlace_) {
      while (gpu_field_cycle_accum_ >= field_period) {
        gpu_field_cycle_accum_ -= field_period;
        gpu_field_ = !gpu_field_;
        vblank_pulse = true;
      }
    } else {
      gpu_field_cycle_accum_ %= field_period;
      gpu_field_ = false;
      vblank_pulse = true;
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
    uint32_t clock = (mode >> 8) & 0x3u;
    uint32_t ticks = 0;
    if (i == 2) {
      if (clock & 0x1u) {
        timer_cycle_accum_[i] += cycles;
        ticks = timer_cycle_accum_[i] / 8;
        timer_cycle_accum_[i] %= 8;
      } else {
        ticks = cycles;
      }
    } else if (i == 1) {
      switch (clock) {
        case 0:
          ticks = cycles;
          break;
        case 1:
          ticks = hblank_pulses;
          break;
        case 2:
          timer_cycle_accum_[i] += cycles;
          ticks = timer_cycle_accum_[i] / 8;
          timer_cycle_accum_[i] %= 8;
          break;
        case 3:
          timer_cycle_accum_[i] += hblank_pulses;
          ticks = timer_cycle_accum_[i] / 8;
          timer_cycle_accum_[i] %= 8;
          break;
        default:
          ticks = cycles;
          break;
      }
    } else { // i == 0
      switch (clock) {
        case 0:
          ticks = cycles;
          break;
        case 1:
          timer_cycle_accum_[i] += cycles;
          ticks = timer_cycle_accum_[i] / 8;
          timer_cycle_accum_[i] %= 8;
          break;
        case 2:
          ticks = cycles;
          break;
        case 3:
          timer_cycle_accum_[i] += cycles;
          ticks = timer_cycle_accum_[i] / 8;
          timer_cycle_accum_[i] %= 8;
          break;
        default:
          ticks = cycles;
          break;
      }
    }

    bool sync_enable = (mode & 0x1u) != 0;
    uint32_t sync_mode = (mode >> 1) & 0x3u;
    bool blank = (i == 0) ? in_hblank : (i == 1 ? in_vblank : false);
    bool blank_start = (i == 0) ? in_hblank : (i == 1 ? vblank_start_pulse : false);
    if (sync_enable) {
      if (sync_mode == 3) {
        if (timer_sync_waiting_[i]) {
          if (blank_start) {
            timer_sync_waiting_[i] = false;
            timer_count_[i] = 0;
            timer_cycle_accum_[i] = 0;
            ticks = 0;
          } else {
            ticks = 0;
          }
        }
      }
      if (sync_mode == 0) {
        if (blank) {
          ticks = 0;
        }
      } else if (sync_mode == 1) {
        if (blank_start) {
          before = 0;
          timer_count_[i] = 0;
          timer_cycle_accum_[i] = 0;
          ticks = 0;
        }
      } else if (sync_mode == 2) {
        if (blank_start) {
          before = 0;
          timer_count_[i] = 0;
          timer_cycle_accum_[i] = 0;
          ticks = 0;
        }
        if (!blank) {
          ticks = 0;
        }
      }
    }

    uint32_t full = before + ticks;
    uint32_t after = full & 0xFFFFu;
    timer_count_[i] = static_cast<uint16_t>(after);
    uint16_t target = timer_target_[i];
    if (ticks > 0 && target != 0 && before < target && full >= target && full <= 0x1FFFFu) {
      timer_mode_[i] |= static_cast<uint16_t>(1u << 11);
      if (timer_irq_enable_[i] && timer_irq_on_target_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
        if (timer_irq_toggle_[i]) {
          timer_mode_[i] ^= static_cast<uint16_t>(1u << 10);
        } else {
          timer_mode_[i] &= static_cast<uint16_t>(~(1u << 10));
        }
      }
      if (mode & (1u << 3)) { // reset on target
        timer_count_[i] = 0;
        timer_cycle_accum_[i] = 0;
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
    if (full > 0xFFFFu) {
      timer_mode_[i] |= static_cast<uint16_t>(1u << 12);
      if (timer_irq_on_overflow_[i] && timer_irq_enable_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
        if (timer_irq_toggle_[i]) {
          timer_mode_[i] ^= static_cast<uint16_t>(1u << 10);
        } else {
          timer_mode_[i] &= static_cast<uint16_t>(~(1u << 10));
        }
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
  }

  if (!cdrom_pending_.empty()) {
    uint32_t remaining = cycles;
    while (!cdrom_pending_.empty() && remaining > 0) {
      auto &pending = cdrom_pending_.front();
      if (pending.delay_cycles > remaining) {
        pending.delay_cycles -= remaining;
        break;
      }
      remaining -= pending.delay_cycles;
      pending.delay_cycles = 0;
      if (pending.clear_seeking) {
        cdrom_seeking_ = false;
      }
      if (!pending.response.empty()) {
        pending.response[0] = cdrom_status();
      }
      cdrom_push_response_block(pending.response);
      cdrom_raise_irq(pending.irq_flags);
      cdrom_pending_.pop_front();
    }
  }

  if (joy_tx_delay_cycles_ > 0) {
    if (joy_tx_delay_cycles_ > cycles) {
      joy_tx_delay_cycles_ -= cycles;
    } else {
      joy_tx_delay_cycles_ = 0;
    }
  }
  while (joy_tx_delay_cycles_ == 0 && !joy_tx_queue_.empty()) {
    uint8_t response = joy_tx_queue_.front();
    joy_tx_queue_.pop_front();
    joy_response_queue_.push_back(response);
    joy_rx_ready_ = true;
    joy_ack_ = true;
    if ((joy_ctrl_ & 0x1000u) && !joy_irq_pending_) {
      joy_irq_pending_ = true;
      irq_stat_ |= static_cast<uint16_t>(1u << 7);
    }
    if (!joy_tx_queue_.empty()) {
      joy_tx_delay_cycles_ = joy_byte_delay_cycles(joy_baud_);
    }
  }

  if (vblank_pulse) {
    irq_stat_ |= 1u << 0;
    if (irq_log_enabled()) {
      std::cerr << "[irq] VBLANK irq_stat=0x" << std::hex << std::setw(4) << std::setfill('0')
                << irq_stat_ << "\n";
    }
  }
}

uint32_t MmioBus::consume_dma_channel() {
  if (dma_pending_mask_ == 0) {
    return 0xFFFFFFFFu;
  }
  for (uint32_t channel = 0; channel < 7; ++channel) {
    if ((dma_pending_mask_ & (1u << channel)) == 0) {
      continue;
    }
    if (dma_dpcr_ != 0 && (dma_dpcr_ & (1u << (3 + channel * 4))) == 0) {
      continue;
    }
    if (channel == 2 && (compute_gpustat() & (1u << 28)) == 0) {
      continue;
    }
    if (channel == 3 && ((cdrom_request_ & 0x01u) == 0 || cdrom_data_fifo_.empty())) {
      continue;
    }
    dma_pending_mask_ &= ~(1u << channel);
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
  if ((cdrom_request_ & 0x01u) == 0) {
    return 0;
  }
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

bool MmioBus::pop_xa_audio(XaAudioSector &out) {
  if (cdrom_xa_audio_queue_.empty()) {
    return false;
  }
  out = std::move(cdrom_xa_audio_queue_.front());
  cdrom_xa_audio_queue_.pop_front();
  return true;
}

uint16_t MmioBus::spu_main_volume_left() const {
  constexpr uint32_t kSpuMainVolLeft = 0x1F801D80;
  uint32_t index = (kSpuMainVolLeft - 0x1F801C00) / 2;
  return index < spu_regs_.size() ? spu_regs_[index] : 0;
}

uint16_t MmioBus::spu_main_volume_right() const {
  constexpr uint32_t kSpuMainVolRight = 0x1F801D82;
  uint32_t index = (kSpuMainVolRight - 0x1F801C00) / 2;
  return index < spu_regs_.size() ? spu_regs_[index] : 0;
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
      gpu_mask_set_ = (mode & (1u << 11)) != 0;
      gpu_mask_eval_ = (mode & (1u << 12)) != 0;
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
  uint32_t scaled = std::max(1u, cycles / 32u);
  gpu_busy_cycles_ = std::min<uint32_t>(gpu_busy_cycles_ + scaled, 100000);
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
