#include "core/emu_core.h"

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
    flush_gpu_commands();
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

  std::vector<uint8_t> payload;
  payload.reserve(commands.size() * sizeof(uint32_t));
  for (uint32_t word : commands) {
    payload.push_back(static_cast<uint8_t>(word & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
  }

  if (!plugin_host_.send_frame(PluginType::Gpu, 0x0001, payload)) {
    std::cerr << "Failed to send GPU command frame\n";
    return;
  }

  uint16_t reply_type = 0;
  std::vector<uint8_t> reply_payload;
  if (!plugin_host_.recv_frame(PluginType::Gpu, reply_type, reply_payload) || reply_type != 0x0002) {
    std::cerr << "GPU command frame not acknowledged\n";
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
  // TODO: send shutdown messages and wait for plugin exit.
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
