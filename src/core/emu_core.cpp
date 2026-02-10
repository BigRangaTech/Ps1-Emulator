#include "core/emu_core.h"

#include "core/gpu_packets.h"
#include "core/xa_adpcm.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ps1emu {

EmulatorCore::EmulatorCore() : cpu_(memory_, scheduler_) {}

static int16_t clamp_sample(int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return static_cast<int16_t>(value);
}

static std::vector<int16_t> resample_linear(const std::vector<int16_t> &in, uint32_t out_count) {
  if (out_count == 0 || in.empty()) {
    return {};
  }
  if (in.size() == 1) {
    return std::vector<int16_t>(out_count, in[0]);
  }
  if (out_count == in.size()) {
    return in;
  }
  std::vector<int16_t> out(out_count);
  double scale = static_cast<double>(in.size() - 1) / static_cast<double>(out_count - 1);
  for (uint32_t i = 0; i < out_count; ++i) {
    double pos = static_cast<double>(i) * scale;
    size_t idx = static_cast<size_t>(pos);
    double frac = pos - static_cast<double>(idx);
    int32_t a = in[idx];
    int32_t b = in[std::min(idx + 1, in.size() - 1)];
    int32_t interp = static_cast<int32_t>(a + (b - a) * frac);
    out[i] = clamp_sample(interp);
  }
  return out;
}

bool EmulatorCore::initialize(const std::string &config_path) {
  if (!load_and_apply_config(config_path)) {
    return false;
  }

  if (!plugin_host_.launch_plugin(PluginType::Gpu, config_.plugin_gpu, config_.sandbox)) {
    std::cerr << "Failed to launch GPU plugin\n";
    return false;
  }
  if (!plugin_host_.launch_plugin(PluginType::Spu, config_.plugin_spu, config_.sandbox)) {
    std::cerr << "Failed to launch SPU plugin\n";
    return false;
  }
  if (!plugin_host_.launch_plugin(PluginType::Input, config_.plugin_input, config_.sandbox)) {
    std::cerr << "Failed to launch INPUT plugin\n";
    return false;
  }
  if (!plugin_host_.launch_plugin(PluginType::Cdrom, config_.plugin_cdrom, config_.sandbox)) {
    std::cerr << "Failed to launch CDROM plugin\n";
    return false;
  }

  if (!plugin_host_.handshake(PluginType::Gpu) ||
      !plugin_host_.handshake(PluginType::Spu) ||
      !plugin_host_.handshake(PluginType::Input) ||
      !plugin_host_.handshake(PluginType::Cdrom)) {
    std::cerr << "Plugin handshake failed\n";
    return false;
  }

  if (!plugin_host_.enter_frame_mode(PluginType::Gpu)) {
    std::cerr << "GPU plugin failed to enter frame mode\n";
    return false;
  }
  if (!plugin_host_.enter_frame_mode(PluginType::Spu)) {
    std::cerr << "SPU plugin failed to enter frame mode (XA audio disabled)\n";
  }

  total_cycles_ = 0;
  next_trace_cycle_ = 0;
  watchdog_cycle_accum_ = 0;
  watchdog_same_pc_samples_ = 0;
  watchdog_alt_pc_samples_ = 0;
  watchdog_reported_ = false;

  return true;
}

void EmulatorCore::run_for_cycles(uint32_t cycles) {
  uint32_t remaining = cycles;
  while (remaining > 0) {
    uint32_t step_cycles = cpu_.step();
    if (step_cycles > remaining) {
      remaining = 0;
    } else {
      remaining -= step_cycles;
    }

    mmio_.tick(step_cycles);
    process_dma();
    flush_spu_controls();
    flush_xa_audio();
    flush_gpu_dma_pending();
    flush_gpu_commands();
    flush_gpu_control();

    total_cycles_ += step_cycles;

    if (trace_enabled_) {
      CpuExceptionInfo ex;
      if (cpu_.consume_exception(ex)) {
        log_exception_event(ex);
      }
      if (total_cycles_ >= next_trace_cycle_) {
        log_trace_state("tick");
        next_trace_cycle_ = total_cycles_ + trace_period_cycles_;
      }
    }

    if (watchdog_enabled_) {
      watchdog_cycle_accum_ += step_cycles;
      if (watchdog_cycle_accum_ >= watchdog_sample_cycles_) {
        watchdog_cycle_accum_ = 0;
        watchdog_sample();
      }
    }
  }
}

bool EmulatorCore::send_gpu_packet(const GpuPacket &packet) {
  std::vector<uint8_t> payload;
  payload.reserve(packet.words.size() * sizeof(uint32_t));
  for (uint32_t word : packet.words) {
    payload.push_back(static_cast<uint8_t>(word & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
  }

  if (!plugin_host_.send_frame(PluginType::Gpu, 0x0001, payload)) {
    std::cerr << "Failed to send GPU command frame\n";
    return false;
  }

  uint16_t reply_type = 0;
  std::vector<uint8_t> reply_payload;
  if (!plugin_host_.recv_frame(PluginType::Gpu, reply_type, reply_payload) || reply_type != 0x0002) {
    std::cerr << "GPU command frame not acknowledged\n";
    return false;
  }
  return true;
}

void EmulatorCore::set_trace_enabled(bool enabled) {
  trace_enabled_ = enabled;
  next_trace_cycle_ = total_cycles_;
}

void EmulatorCore::set_trace_period_cycles(uint32_t cycles) {
  trace_period_cycles_ = std::max<uint32_t>(cycles, 1);
  next_trace_cycle_ = total_cycles_;
}

void EmulatorCore::set_watchdog_enabled(bool enabled) {
  watchdog_enabled_ = enabled;
  watchdog_reported_ = false;
  watchdog_same_pc_samples_ = 0;
  watchdog_alt_pc_samples_ = 0;
}

void EmulatorCore::set_watchdog_sample_cycles(uint32_t cycles) {
  watchdog_sample_cycles_ = std::max<uint32_t>(cycles, 1);
}

void EmulatorCore::set_watchdog_stall_cycles(uint32_t cycles) {
  watchdog_stall_cycles_ = std::max<uint32_t>(cycles, 1);
}

void EmulatorCore::log_trace_state(const char *label) {
  std::ostringstream oss;
  const auto &st = cpu_.state();
  uint32_t instr = memory_.read32(st.pc);
  uint32_t prev_instr = memory_.read32(st.pc - 4);
  uint32_t next_instr = memory_.read32(st.next_pc);
  oss << "[trace] " << label << " cycles=" << total_cycles_;
  oss << " pc=0x" << std::hex << std::setw(8) << std::setfill('0') << st.pc;
  oss << " npc=0x" << std::hex << std::setw(8) << std::setfill('0') << st.next_pc;
  oss << " instr=0x" << std::hex << std::setw(8) << std::setfill('0') << instr;
  oss << " prev=0x" << std::hex << std::setw(8) << std::setfill('0') << prev_instr;
  oss << " npc_instr=0x" << std::hex << std::setw(8) << std::setfill('0') << next_instr;
  oss << " sr=0x" << std::hex << std::setw(8) << std::setfill('0') << st.cop0.sr;
  oss << " cause=0x" << std::hex << std::setw(8) << std::setfill('0') << st.cop0.cause;
  oss << " irq=0x" << std::hex << std::setw(4) << std::setfill('0') << mmio_.irq_stat();
  oss << "/0x" << std::hex << std::setw(4) << std::setfill('0') << mmio_.irq_mask();
  std::cout << oss.str() << "\n";
}

void EmulatorCore::log_exception_event(const CpuExceptionInfo &info) {
  std::ostringstream oss;
  const auto &st = cpu_.state();
  oss << "[trace] exception code=" << info.code;
  oss << " pc=0x" << std::hex << std::setw(8) << std::setfill('0') << info.pc;
  oss << " badv=0x" << std::hex << std::setw(8) << std::setfill('0') << info.badvaddr;
  oss << " in_delay=" << (info.in_delay ? "1" : "0");
  oss << " sr=0x" << std::hex << std::setw(8) << std::setfill('0') << st.cop0.sr;
  oss << " cause=0x" << std::hex << std::setw(8) << std::setfill('0') << info.cause;
  std::cout << oss.str() << "\n";
}

void EmulatorCore::watchdog_sample() {
  uint32_t pc = cpu_.state().pc;
  if (pc == watchdog_last_pc_) {
    watchdog_same_pc_samples_++;
  } else {
    watchdog_same_pc_samples_ = 0;
    watchdog_reported_ = false;
  }

  if (pc == watchdog_prev2_pc_) {
    watchdog_alt_pc_samples_++;
  } else {
    watchdog_alt_pc_samples_ = 0;
  }

  uint32_t threshold_samples =
      (watchdog_stall_cycles_ + watchdog_sample_cycles_ - 1) / watchdog_sample_cycles_;

  if (!watchdog_reported_ &&
      (watchdog_same_pc_samples_ >= threshold_samples ||
       watchdog_alt_pc_samples_ >= threshold_samples)) {
    std::ostringstream oss;
    const auto &st = cpu_.state();
    oss << "[watchdog] possible tight loop";
    if (watchdog_same_pc_samples_ >= threshold_samples) {
      oss << " (same PC)";
    } else {
      oss << " (2-PC alternation)";
    }
    oss << " cycles=" << total_cycles_;
    oss << " pc=0x" << std::hex << std::setw(8) << std::setfill('0') << st.pc;
    oss << " sr=0x" << std::hex << std::setw(8) << std::setfill('0') << st.cop0.sr;
    oss << " cause=0x" << std::hex << std::setw(8) << std::setfill('0') << st.cop0.cause;
    std::cout << oss.str() << "\n";
    watchdog_reported_ = true;
  }

  watchdog_prev2_pc_ = watchdog_prev_pc_;
  watchdog_prev_pc_ = pc;
  watchdog_last_pc_ = pc;
}

bool EmulatorCore::request_vram_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint64_t pixel_count = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
  uint64_t word_count = (pixel_count + 1) / 2;

  std::vector<uint8_t> payload(8);
  payload[0] = static_cast<uint8_t>(x & 0xFF);
  payload[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
  payload[2] = static_cast<uint8_t>(y & 0xFF);
  payload[3] = static_cast<uint8_t>((y >> 8) & 0xFF);
  payload[4] = static_cast<uint8_t>(w & 0xFF);
  payload[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
  payload[6] = static_cast<uint8_t>(h & 0xFF);
  payload[7] = static_cast<uint8_t>((h >> 8) & 0xFF);

  if (!plugin_host_.send_frame(PluginType::Gpu, 0x0004, payload)) {
    std::cerr << "Failed to send GPU VRAM read request\n";
    return false;
  }

  uint16_t reply_type = 0;
  std::vector<uint8_t> reply_payload;
  if (!plugin_host_.recv_frame(PluginType::Gpu, reply_type, reply_payload) || reply_type != 0x0005) {
    std::cerr << "GPU VRAM read response not received\n";
    return false;
  }

  size_t payload_bytes = reply_payload.size() & ~static_cast<size_t>(1);
  std::vector<uint32_t> words;
  words.reserve((payload_bytes + 3) / 4);
  uint32_t current = 0;
  bool low = true;
  for (size_t i = 0; i + 1 < payload_bytes; i += 2) {
    uint16_t pix = static_cast<uint16_t>(reply_payload[i]) |
                   static_cast<uint16_t>(reply_payload[i + 1] << 8);
    if (low) {
      current = pix;
      low = false;
    } else {
      current |= (static_cast<uint32_t>(pix) << 16);
      words.push_back(current);
      low = true;
    }
  }
  if (!low) {
    words.push_back(current);
  }
  uint32_t delay = static_cast<uint32_t>(std::min<uint64_t>(word_count, 100000));
  mmio_.schedule_gpu_read_data(std::move(words), delay);
  mmio_.gpu_add_busy(delay);
  return true;
}

void EmulatorCore::flush_gpu_commands() {
  if (!mmio_.has_gpu_commands()) {
    return;
  }
  std::vector<uint32_t> commands = mmio_.take_gpu_commands();
  if (commands.empty()) {
    return;
  }

  std::vector<uint32_t> remainder;
  std::vector<GpuPacket> packets = parse_gp0_packets(commands, remainder);
  if (!remainder.empty()) {
    mmio_.restore_gpu_commands(std::move(remainder));
  }

  for (const auto &packet : packets) {
    if (!packet.words.empty()) {
      mmio_.apply_gp0_state(packet.words[0]);
    }
    if (packet.command == 0xC0 && packet.words.size() >= 3) {
      uint16_t x = static_cast<uint16_t>(packet.words[1] & 0xFFFFu);
      uint16_t y = static_cast<uint16_t>((packet.words[1] >> 16) & 0xFFFFu);
      uint16_t w = static_cast<uint16_t>(packet.words[2] & 0xFFFFu);
      uint16_t h = static_cast<uint16_t>((packet.words[2] >> 16) & 0xFFFFu);
      if (!request_vram_read(x, y, w, h)) {
        return;
      }
      continue;
    }
    if (!send_gpu_packet(packet)) {
      return;
    }
  }
}

void EmulatorCore::flush_gpu_dma_pending() {
  if (gpu_dma_pending_packets_.empty()) {
    return;
  }
  constexpr size_t kMaxPacketsPerTick = 32;
  size_t sent = 0;
  while (!gpu_dma_pending_packets_.empty() && sent < kMaxPacketsPerTick) {
    if (!mmio_.gpu_ready_for_commands()) {
      break;
    }
    const auto &packet = gpu_dma_pending_packets_.front();
    if (!packet.words.empty()) {
      mmio_.apply_gp0_state(packet.words[0]);
    }
    if (packet.command == 0xC0 && packet.words.size() >= 3) {
      uint16_t x = static_cast<uint16_t>(packet.words[1] & 0xFFFFu);
      uint16_t y = static_cast<uint16_t>((packet.words[1] >> 16) & 0xFFFFu);
      uint16_t w = static_cast<uint16_t>(packet.words[2] & 0xFFFFu);
      uint16_t h = static_cast<uint16_t>((packet.words[2] >> 16) & 0xFFFFu);
      if (!request_vram_read(x, y, w, h)) {
        break;
      }
    } else if (!send_gpu_packet(packet)) {
      break;
    }
    gpu_dma_pending_packets_.pop_front();
    sent++;
  }
}

void EmulatorCore::flush_gpu_control() {
  if (!mmio_.has_gpu_control()) {
    return;
  }
  std::vector<uint32_t> commands = mmio_.take_gpu_control();
  if (commands.empty()) {
    return;
  }

  std::vector<uint8_t> payload;
  payload.reserve(commands.size() * sizeof(uint32_t));
  for (uint32_t word : commands) {
    payload.push_back(static_cast<uint8_t>(word & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
  }

  if (!plugin_host_.send_frame(PluginType::Gpu, 0x0003, payload)) {
    std::cerr << "Failed to send GPU control frame\n";
    return;
  }

  uint16_t reply_type = 0;
  std::vector<uint8_t> reply_payload;
  if (!plugin_host_.recv_frame(PluginType::Gpu, reply_type, reply_payload) || reply_type != 0x0002) {
    std::cerr << "GPU control frame not acknowledged\n";
    return;
  }
}

void EmulatorCore::process_dma() {
  uint32_t channel = mmio_.consume_dma_channel();
  if (channel == 0xFFFFFFFFu) {
    return;
  }

  // Basic DMA behavior: channel 2 (GPU) forwards words from RAM to GP0 FIFO.
  if (channel == 2) {
    uint32_t madr = mmio_.dma_madr(channel) & 0x1FFFFC;
    uint32_t bcr = mmio_.dma_bcr(channel);
    uint32_t chcr = mmio_.dma_chcr(channel);
    uint32_t block_size = bcr & 0xFFFF;
    uint32_t block_count = (bcr >> 16) & 0xFFFF;
    uint32_t total_words = block_size * (block_count ? block_count : 1);
    if (total_words == 0) {
      total_words = block_size;
    }
    if (total_words == 0) {
      total_words = 1;
    }
    bool decrement = (chcr & (1u << 1)) != 0;
    uint32_t sync_mode = (chcr >> 8) & 0x3u;

    if (sync_mode == 2) { // linked list
      std::vector<uint32_t> words;
      size_t block_words = 0;
      size_t blocks = 0;
      uint32_t addr = madr;
      bool end = false;
      while (!end && blocks < 1024) {
        uint32_t header = memory_.read32(addr);
        uint32_t count = header >> 24;
        uint32_t next = header & 0x00FFFFFFu;
        addr = (addr + 4) & 0x1FFFFC;
        for (uint32_t i = 0; i < count; ++i) {
          words.push_back(memory_.read32(addr));
          addr = (addr + 4) & 0x1FFFFC;
        }
        block_words += count;
        blocks++;
        if (next & 0x800000u) {
          end = true;
        } else {
          addr = next & 0x1FFFFC;
        }
      }

      mmio_.set_dma_madr(channel, addr);
      uint64_t dma_busy = static_cast<uint64_t>(block_words) * 2;
      dma_busy += static_cast<uint64_t>(blocks) * 8;
      mmio_.gpu_add_busy(static_cast<uint32_t>(std::min<uint64_t>(dma_busy, 100000)));

      if (!words.empty() || !gpu_dma_remainder_.empty()) {
        std::vector<uint32_t> merged;
        merged.reserve(words.size() + gpu_dma_remainder_.size());
        if (!gpu_dma_remainder_.empty()) {
          merged.insert(merged.end(), gpu_dma_remainder_.begin(), gpu_dma_remainder_.end());
          gpu_dma_remainder_.clear();
        }
        merged.insert(merged.end(), words.begin(), words.end());
        std::vector<uint32_t> remainder;
        std::vector<GpuPacket> packets = parse_gp0_packets(merged, remainder);
        if (!remainder.empty()) {
          gpu_dma_remainder_ = std::move(remainder);
        }
        for (auto &packet : packets) {
          gpu_dma_pending_packets_.push_back(std::move(packet));
        }
        flush_gpu_dma_pending();
      }
      return;
    }

    uint64_t dma_busy = static_cast<uint64_t>(total_words) * 2;
    dma_busy += static_cast<uint64_t>(block_count ? block_count : 1) * 8;
    mmio_.gpu_add_busy(static_cast<uint32_t>(std::min<uint64_t>(dma_busy, 100000)));

    if (mmio_.gpu_dma_dir() == 2) { // GPU -> CPU (VRAM read DMA)
      for (uint32_t i = 0; i < total_words; ++i) {
        uint32_t addr = decrement ? (madr - i * 4) : (madr + i * 4);
        uint32_t word = mmio_.gpu_read_word();
        memory_.write32(addr, word);
      }

      if (decrement) {
        mmio_.set_dma_madr(channel, madr - total_words * 4);
      } else {
        mmio_.set_dma_madr(channel, madr + total_words * 4);
      }
      return;
    }

    std::vector<uint32_t> words;
    words.reserve(total_words + gpu_dma_remainder_.size());
    if (!gpu_dma_remainder_.empty()) {
      words.insert(words.end(), gpu_dma_remainder_.begin(), gpu_dma_remainder_.end());
      gpu_dma_remainder_.clear();
    }
    for (uint32_t i = 0; i < total_words; ++i) {
      uint32_t addr = decrement ? (madr - i * 4) : (madr + i * 4);
      uint32_t word = memory_.read32(addr);
      words.push_back(word);
    }

    if (decrement) {
      mmio_.set_dma_madr(channel, madr - total_words * 4);
    } else {
      mmio_.set_dma_madr(channel, madr + total_words * 4);
    }

    std::vector<uint32_t> remainder;
    std::vector<GpuPacket> packets = parse_gp0_packets(words, remainder);
    if (!remainder.empty()) {
      gpu_dma_remainder_ = std::move(remainder);
    }

    for (auto &packet : packets) {
      gpu_dma_pending_packets_.push_back(std::move(packet));
    }
    flush_gpu_dma_pending();
  } else if (channel == 3) {
    uint32_t madr = mmio_.dma_madr(channel) & 0x1FFFFC;
    uint32_t bcr = mmio_.dma_bcr(channel);
    uint32_t block_size = bcr & 0xFFFF;
    uint32_t block_count = (bcr >> 16) & 0xFFFF;
    uint32_t total_words = block_size * (block_count ? block_count : 1);
    if (total_words == 0) {
      total_words = block_size;
    }
    if (total_words == 0) {
      total_words = 1;
    }

    std::vector<uint8_t> payload(total_words * 4);
    size_t read = mmio_.read_cdrom_data(payload.data(), payload.size());
    for (size_t i = read; i < payload.size(); ++i) {
      payload[i] = 0;
    }

    for (uint32_t i = 0; i < total_words; ++i) {
      size_t base = static_cast<size_t>(i) * 4;
      uint32_t word = static_cast<uint32_t>(payload[base]) |
                      (static_cast<uint32_t>(payload[base + 1]) << 8) |
                      (static_cast<uint32_t>(payload[base + 2]) << 16) |
                      (static_cast<uint32_t>(payload[base + 3]) << 24);
      memory_.write32(madr + i * 4, word);
    }

    mmio_.set_dma_madr(channel, madr + total_words * 4);
  }
}

void EmulatorCore::flush_spu_controls() {
  if (!plugin_host_.is_frame_mode(PluginType::Spu)) {
    return;
  }
  uint16_t left = mmio_.spu_main_volume_left();
  uint16_t right = mmio_.spu_main_volume_right();
  if (left == spu_main_vol_l_ && right == spu_main_vol_r_) {
    return;
  }
  spu_main_vol_l_ = left;
  spu_main_vol_r_ = right;

  std::vector<uint8_t> payload(4);
  payload[0] = static_cast<uint8_t>(left & 0xFF);
  payload[1] = static_cast<uint8_t>((left >> 8) & 0xFF);
  payload[2] = static_cast<uint8_t>(right & 0xFF);
  payload[3] = static_cast<uint8_t>((right >> 8) & 0xFF);
  plugin_host_.send_frame(PluginType::Spu, 0x0102, payload);
}

void EmulatorCore::flush_xa_audio() {
  ps1emu::MmioBus::XaAudioSector sector;
  while (mmio_.pop_xa_audio(sector)) {
    if (!plugin_host_.is_frame_mode(PluginType::Spu)) {
      continue;
    }

    uint16_t key = static_cast<uint16_t>((sector.file << 8) | sector.channel);
    XaDecodeState &state = xa_decode_states_[key];
    XaDecodeInfo info;
    std::vector<int16_t> left;
    std::vector<int16_t> right;
    if (!decode_xa_adpcm(sector.data.data(), sector.data.size(), sector.coding, state, info, left, right)) {
      continue;
    }

    uint16_t sample_rate = info.sample_rate;
    uint8_t channels = info.channels;
    uint32_t expected = sample_rate / 75;
    if (expected > 0 && !left.empty()) {
      left = resample_linear(left, expected);
      if (channels == 2) {
        if (right.empty()) {
          right = left;
        } else {
          right = resample_linear(right, expected);
        }
      }
    }

    if (left.empty()) {
      continue;
    }

    uint32_t sample_count = static_cast<uint32_t>(left.size());

    std::vector<uint8_t> payload;
    payload.reserve(12 + sample_count * channels * 2);
    payload.push_back(static_cast<uint8_t>(sector.lba & 0xFF));
    payload.push_back(static_cast<uint8_t>((sector.lba >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((sector.lba >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((sector.lba >> 24) & 0xFF));
    payload.push_back(static_cast<uint8_t>(sample_rate & 0xFF));
    payload.push_back(static_cast<uint8_t>((sample_rate >> 8) & 0xFF));
    payload.push_back(channels);
    payload.push_back(0x00);
    payload.push_back(static_cast<uint8_t>(sample_count & 0xFF));
    payload.push_back(static_cast<uint8_t>((sample_count >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((sample_count >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((sample_count >> 24) & 0xFF));

    for (uint32_t i = 0; i < sample_count; ++i) {
      int16_t l = left[i];
      payload.push_back(static_cast<uint8_t>(l & 0xFF));
      payload.push_back(static_cast<uint8_t>((l >> 8) & 0xFF));
      if (channels == 2) {
        int16_t r = (i < right.size()) ? right[i] : l;
        payload.push_back(static_cast<uint8_t>(r & 0xFF));
        payload.push_back(static_cast<uint8_t>((r >> 8) & 0xFF));
      }
    }

    if (!plugin_host_.send_frame(PluginType::Spu, 0x0101, payload)) {
      break;
    }
  }
}

void EmulatorCore::dump_dynarec_profile() const {
  auto blocks = cpu_.dynarec_blocks();
  std::cout << "Dynarec blocks: " << blocks.size() << "\n";
  for (const auto &block : blocks) {
    std::cout << "PC=0x" << std::hex << block.pc << std::dec
              << " size=" << block.size
              << " opcodes=" << block.opcodes.size() << "\n";
    size_t count = 0;
    for (uint32_t op : block.opcodes) {
      std::cout << "  0x" << std::hex << op << std::dec;
      if (++count >= 8) {
        break;
      }
    }
    if (!block.opcodes.empty()) {
      std::cout << "\n";
    }
  }
}

void EmulatorCore::shutdown() {
  plugin_host_.shutdown_all();
}

const Config &EmulatorCore::config() const {
  return config_;
}

bool EmulatorCore::bios_is_hle() const {
  return bios_.is_hle();
}

bool EmulatorCore::load_and_apply_config(const std::string &config_path) {
  std::string error;
  if (!load_config_file(config_path, config_, error)) {
    std::cerr << "Config error: " << error << "\n";
    return false;
  }

  if (config_.plugin_gpu.empty() || config_.plugin_spu.empty() ||
      config_.plugin_input.empty() || config_.plugin_cdrom.empty()) {
    std::cerr << "Config error: plugin paths must be set for GPU/SPU/Input/CD-ROM\n";
    return false;
  }

  memory_.reset();
  mmio_.reset();
  memory_.attach_mmio(mmio_);
  scheduler_.reset();

  if (!config_.bios_path.empty()) {
    if (!bios_.load_from_file(config_.bios_path, error)) {
      std::cerr << "BIOS error: " << error << "\n";
      return false;
    }
    memory_.load_bios(bios_);
  } else {
    bios_.load_hle_stub();
    memory_.load_bios(bios_);
    std::cerr << "Using HLE BIOS stub (no BIOS file configured)\n";
  }

  if (!config_.cdrom_image.empty()) {
    std::string error;
    if (!mmio_.load_cdrom_image(config_.cdrom_image, error)) {
      std::cerr << "CD-ROM image error: " << error << "\n";
    }
  }

  cpu_.set_mode(resolve_cpu_mode());
  cpu_.reset();
  return true;
}

CpuCore::Mode EmulatorCore::resolve_cpu_mode() const {
  switch (config_.cpu_mode) {
    case CpuMode::Interpreter:
      return CpuCore::Mode::Interpreter;
    case CpuMode::Dynarec:
      return CpuCore::Mode::Dynarec;
    case CpuMode::Auto:
      return CpuCore::dynarec_available() ? CpuCore::Mode::Dynarec : CpuCore::Mode::Interpreter;
  }
  return CpuCore::Mode::Interpreter;
}

} // namespace ps1emu
