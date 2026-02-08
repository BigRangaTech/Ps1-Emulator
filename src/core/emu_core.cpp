#include "core/emu_core.h"

#include "core/gpu_packets.h"

#include <iostream>

namespace ps1emu {

EmulatorCore::EmulatorCore() : cpu_(memory_, scheduler_) {}

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

  return true;
}

void EmulatorCore::run_for_cycles(uint32_t cycles) {
  for (uint32_t i = 0; i < cycles; ++i) {
    uint32_t step_cycles = cpu_.step();
    mmio_.tick(step_cycles);
    process_dma();
    flush_gpu_commands();
    flush_gpu_control();
  }
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

  auto send_packet = [&](const GpuPacket &packet) {
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
  };

  for (const auto &packet : packets) {
    if (!send_packet(packet)) {
      return;
    }
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

    auto send_packet = [&](const GpuPacket &packet) {
      std::vector<uint8_t> payload;
      payload.reserve(packet.words.size() * sizeof(uint32_t));
      for (uint32_t word : packet.words) {
        payload.push_back(static_cast<uint8_t>(word & 0xFF));
        payload.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
      }

      if (!plugin_host_.send_frame(PluginType::Gpu, 0x0001, payload)) {
        std::cerr << "DMA GPU frame send failed\n";
        return false;
      }

      uint16_t reply_type = 0;
      std::vector<uint8_t> reply_payload;
      if (!plugin_host_.recv_frame(PluginType::Gpu, reply_type, reply_payload) || reply_type != 0x0002) {
        std::cerr << "DMA GPU frame not acknowledged\n";
        return false;
      }
      return true;
    };

    for (const auto &packet : packets) {
      if (!send_packet(packet)) {
        break;
      }
    }
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
