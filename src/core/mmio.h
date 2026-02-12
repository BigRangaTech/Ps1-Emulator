#ifndef PS1EMU_MMIO_H
#define PS1EMU_MMIO_H

#include "core/cdrom_image.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ps1emu {

class MmioBus {
public:
  struct XaAudioSector {
    std::vector<uint8_t> data;
    uint32_t lba = 0;
    uint8_t mode = 0;
    uint8_t file = 0;
    uint8_t channel = 0;
    uint8_t submode = 0;
    uint8_t coding = 0;
  };

  void reset();

  uint8_t read8(uint32_t addr);
  uint16_t read16(uint32_t addr);
  uint32_t read32(uint32_t addr);

  void write8(uint32_t addr, uint8_t value);
  void write16(uint32_t addr, uint16_t value);
  void write32(uint32_t addr, uint32_t value);
  bool irq_pending() const;
  uint16_t irq_stat() const;
  uint16_t irq_mask() const;
  void tick(uint32_t cycles);
  bool has_gpu_commands() const;
  std::vector<uint32_t> take_gpu_commands();
  void restore_gpu_commands(std::vector<uint32_t> remainder);
  bool has_gpu_control() const;
  std::vector<uint32_t> take_gpu_control();
  void apply_gp0_state(uint32_t word);
  void queue_gpu_read_data(std::vector<uint32_t> words);
  void schedule_gpu_read_data(std::vector<uint32_t> words, uint32_t delay_cycles);
  void gpu_add_busy(uint32_t cycles);
  bool gpu_ready_for_commands() const;
  uint32_t gpu_dma_dir() const;
  uint32_t gpu_read_word();
  uint32_t consume_dma_channel();
  uint32_t dma_madr(uint32_t channel) const;
  uint32_t dma_bcr(uint32_t channel) const;
  uint32_t dma_chcr(uint32_t channel) const;
  void set_dma_madr(uint32_t channel, uint32_t value);
  bool load_cdrom_image(const std::string &path, std::string &error);
  size_t read_cdrom_data(uint8_t *dst, size_t len);
  bool pop_xa_audio(XaAudioSector &out);
  uint16_t spu_main_volume_left() const;
  uint16_t spu_main_volume_right() const;

private:
  enum class CdromFillResult {
    Error,
    Skipped,
    Delivered,
  };

  static constexpr uint32_t kBase = 0x1F801000;
  static constexpr uint32_t kSize = 0x2000;

  uint32_t offset(uint32_t addr) const;
  void reset_gpu_state();
  uint32_t compute_gpustat() const;
  struct CdromPendingResponse {
    uint32_t delay_cycles = 0;
    uint8_t irq_flags = 0;
    std::vector<uint8_t> response;
    bool clear_seeking = false;
  };

  uint8_t cdrom_status() const;
  uint16_t joy_status() const;
  uint16_t sio1_status() const;
  uint16_t spu_status() const;
  void cdrom_push_response(uint8_t value);
  void cdrom_push_response_block(const std::vector<uint8_t> &values);
  void cdrom_queue_response(uint32_t delay_cycles,
                            uint8_t irq_flags,
                            std::vector<uint8_t> response,
                            bool clear_seeking = false);
  void cdrom_raise_irq(uint8_t flags);
  void cdrom_update_irq_line();
  void cdrom_set_irq_enable(uint8_t enable);
  void cdrom_execute_command(uint8_t cmd);
  void cdrom_maybe_fill_data();
  CdromFillResult cdrom_fill_data_fifo();

  std::array<uint8_t, kSize> raw_ {};

  uint32_t gpu_gp0_ = 0;
  uint32_t gpu_gp1_ = 0;
  std::vector<uint32_t> gpu_gp0_fifo_;
  std::vector<uint32_t> gpu_gp1_fifo_;
  std::vector<uint32_t> gpu_read_fifo_;
  uint32_t gpu_read_latch_ = 0;
  std::vector<uint32_t> gpu_read_pending_;
  uint32_t gpu_read_pending_delay_ = 0;
  uint32_t gpu_texpage_x_ = 0;
  uint32_t gpu_texpage_y_ = 0;
  uint32_t gpu_semi_ = 0;
  uint32_t gpu_tex_depth_ = 0;
  bool gpu_dither_ = false;
  bool gpu_draw_to_display_ = false;
  bool gpu_mask_set_ = false;
  bool gpu_mask_eval_ = false;
  bool gpu_display_disabled_ = true;
  bool gpu_irq_ = false;
  bool gpu_interlace_ = false;
  bool gpu_flip_ = false;
  bool gpu_hres2_ = false;
  uint32_t gpu_hres1_ = 0;
  bool gpu_vres_ = false;
  bool gpu_vmode_pal_ = false;
  bool gpu_display_depth24_ = false;
  uint32_t gpu_dma_dir_ = 0;
  bool gpu_field_ = false;
  uint16_t gpu_display_x_ = 0;
  uint16_t gpu_display_y_ = 0;
  uint16_t gpu_h_range_start_ = 0;
  uint16_t gpu_h_range_end_ = 0;
  uint16_t gpu_v_range_start_ = 0;
  uint16_t gpu_v_range_end_ = 0;
  uint64_t gpu_field_cycle_accum_ = 0;
  uint32_t gpu_busy_cycles_ = 0;
  uint32_t gpu_tex_window_ = 0;
  uint32_t gpu_draw_area_tl_ = 0;
  uint32_t gpu_draw_area_br_ = 0;
  uint32_t gpu_draw_offset_ = 0;
  uint32_t gpu_line_cycle_accum_ = 0;
  uint32_t gpu_line_ = 0;
  uint32_t dma_active_channel_ = 0xFFFFFFFFu;

  uint16_t irq_stat_ = 0;
  uint16_t irq_mask_ = 0;

  uint32_t dma_madr_[7] = {};
  uint32_t dma_bcr_[7] = {};
  uint32_t dma_chcr_[7] = {};
  uint32_t dma_dpcr_ = 0;
  uint32_t dma_dicr_ = 0;

  uint16_t timer_count_[3] = {};
  uint16_t timer_mode_[3] = {};
  uint16_t timer_target_[3] = {};
  uint32_t timer_cycle_accum_[3] = {};
  bool timer_sync_waiting_[3] = {};

  std::array<uint16_t, 0x200 / 2> spu_regs_ {};
  std::array<uint8_t, 4> cdrom_regs_ {};
  CdromImage cdrom_image_;
  std::vector<uint8_t> cdrom_param_fifo_;
  std::vector<uint8_t> cdrom_response_fifo_;
  std::vector<uint8_t> cdrom_data_fifo_;
  std::deque<XaAudioSector> cdrom_xa_audio_queue_;
  uint8_t cdrom_index_ = 0;
  uint8_t cdrom_status_ = 0;
  uint8_t cdrom_irq_flags_ = 0;
  uint8_t cdrom_irq_enable_ = 0;
  uint8_t cdrom_request_ = 0;
  uint8_t cdrom_vol_ll_ = 0;
  uint8_t cdrom_vol_lr_ = 0;
  uint8_t cdrom_vol_rl_ = 0;
  uint8_t cdrom_vol_rr_ = 0;
  uint8_t cdrom_vol_apply_ = 0;
  uint8_t cdrom_mode_ = 0;
  uint8_t cdrom_filter_file_ = 0;
  uint8_t cdrom_filter_channel_ = 0;
  uint8_t cdrom_session_ = 1;
  bool cdrom_error_ = false;
  bool cdrom_reading_ = false;
  bool cdrom_playing_ = false;
  bool cdrom_muted_ = false;
  bool cdrom_seeking_ = false;
  uint32_t cdrom_read_timer_ = 0;
  uint32_t cdrom_read_period_ = 0;
  uint32_t cdrom_last_read_lba_ = 0;
  uint32_t cdrom_lba_ = 0;
  uint8_t cdrom_last_mode_ = 0;
  uint8_t cdrom_last_file_ = 0;
  uint8_t cdrom_last_channel_ = 0;
  uint8_t cdrom_last_submode_ = 0;
  uint8_t cdrom_last_coding_ = 0;
  std::deque<CdromPendingResponse> cdrom_pending_;

  bool timer_irq_enable_[3] = {};
  bool timer_irq_repeat_[3] = {};
  bool timer_irq_on_overflow_[3] = {};
  bool timer_irq_on_target_[3] = {};
  bool timer_irq_toggle_[3] = {};

  uint16_t joy_mode_ = 0;
  uint16_t joy_ctrl_ = 0;
  uint16_t joy_baud_ = 0;
  uint8_t joy_rx_data_ = 0xFF;
  bool joy_rx_ready_ = false;
  bool joy_ack_ = false;
  bool joy_irq_pending_ = false;
  std::deque<uint8_t> joy_tx_queue_;
  uint32_t joy_tx_delay_cycles_ = 0;
  std::deque<uint8_t> joy_response_queue_;
  bool joy_session_active_ = false;
  uint8_t joy_phase_ = 0;
  uint8_t joy_device_ = 0;

  uint16_t sio1_mode_ = 0;
  uint16_t sio1_ctrl_ = 0;
  uint16_t sio1_baud_ = 0;
  uint16_t sio1_misc_ = 0;
  uint8_t sio1_rx_data_ = 0xFF;
  bool sio1_rx_ready_ = false;

  uint16_t spu_ctrl_ = 0;
};

} // namespace ps1emu

#endif
