#include "plugins/ipc.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#ifdef PS1EMU_SPU_SDL
#include <SDL2/SDL.h>
#endif

int main() {
  ps1emu::IpcChannel channel(STDIN_FILENO, STDOUT_FILENO);
  if (!channel.valid()) {
    return 1;
  }

  const char *mix_rate_env = std::getenv("PS1EMU_SPU_MIX_RATE");
  uint32_t mix_rate = 44100;
  if (mix_rate_env && *mix_rate_env) {
    int rate = std::atoi(mix_rate_env);
    if (rate > 0) {
      mix_rate = static_cast<uint32_t>(rate);
    }
  }

  bool audio_enabled = false;
#ifdef PS1EMU_SPU_SDL
  const char *audio_disable = std::getenv("PS1EMU_SPU_DISABLE_AUDIO");
  SDL_AudioDeviceID audio_dev = 0;
  bool sdl_inited = false;
  if (!audio_disable || *audio_disable == '\0' || *audio_disable == '0') {
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
      sdl_inited = true;
      SDL_AudioSpec desired {};
      SDL_AudioSpec obtained {};
      desired.freq = static_cast<int>(mix_rate);
      desired.format = AUDIO_S16;
      desired.channels = 2;
      desired.samples = 1024;
      desired.callback = nullptr;
      audio_dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
      if (audio_dev != 0) {
        mix_rate = static_cast<uint32_t>(obtained.freq);
        SDL_PauseAudioDevice(audio_dev, 0);
        audio_enabled = true;
      }
    }
  }
#endif

  const char *wav_path = std::getenv("PS1EMU_SPU_DUMP_WAV");
  std::vector<int16_t> mix_buffer;

  std::string line;
  if (!channel.recv_line(line)) {
    return 1;
  }
  if (line != "HELLO SPU 1") {
    channel.send_line("ERROR");
    return 1;
  }
  channel.send_line("READY SPU 1");

  auto clamp_sample = [](int32_t value) -> int16_t {
    if (value > 32767) {
      return 32767;
    }
    if (value < -32768) {
      return -32768;
    }
    return static_cast<int16_t>(value);
  };

  auto resample_channel = [&](const std::vector<int16_t> &in, uint32_t out_count) -> std::vector<int16_t> {
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
  };

  auto write_wav = [&](const std::string &path) {
    if (mix_buffer.empty()) {
      return;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
      return;
    }
    uint16_t channels = 2;
    uint32_t sample_rate = mix_rate;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align = channels * (bits_per_sample / 8);
    uint32_t data_size = static_cast<uint32_t>(mix_buffer.size() * sizeof(int16_t));
    uint32_t riff_size = 36 + data_size;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char *>(&riff_size), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    out.write(reinterpret_cast<const char *>(&fmt_size), 4);
    out.write(reinterpret_cast<const char *>(&audio_format), 2);
    out.write(reinterpret_cast<const char *>(&channels), 2);
    out.write(reinterpret_cast<const char *>(&sample_rate), 4);
    out.write(reinterpret_cast<const char *>(&byte_rate), 4);
    out.write(reinterpret_cast<const char *>(&block_align), 2);
    out.write(reinterpret_cast<const char *>(&bits_per_sample), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char *>(&data_size), 4);
    out.write(reinterpret_cast<const char *>(mix_buffer.data()),
              static_cast<std::streamsize>(mix_buffer.size() * sizeof(int16_t)));
  };

  bool frame_mode = false;
  while (true) {
    if (!frame_mode) {
      if (!channel.recv_line(line)) {
        break;
      }
      if (line == "PING") {
        channel.send_line("PONG");
        continue;
      }
      if (line == "FRAME_MODE") {
        frame_mode = true;
        channel.send_line("FRAME_READY");
        continue;
      }
      if (line == "SHUTDOWN") {
        break;
      }
      channel.send_line("ERROR");
      continue;
    }

    uint16_t type = 0;
    std::vector<uint8_t> payload;
    if (!channel.recv_frame(type, payload)) {
      break;
    }
    if (type == 0x0101 && payload.size() >= 12) {
      uint32_t sample_rate = static_cast<uint32_t>(payload[4]) |
                             (static_cast<uint32_t>(payload[5]) << 8);
      uint8_t channels = payload[6];
      uint32_t sample_count = static_cast<uint32_t>(payload[8]) |
                              (static_cast<uint32_t>(payload[9]) << 8) |
                              (static_cast<uint32_t>(payload[10]) << 16) |
                              (static_cast<uint32_t>(payload[11]) << 24);
      size_t expected_bytes = static_cast<size_t>(sample_count) * channels * sizeof(int16_t);
      if (payload.size() >= 12 + expected_bytes && channels >= 1 && channels <= 2 && sample_rate > 0) {
        std::vector<int16_t> left;
        std::vector<int16_t> right;
        left.reserve(sample_count);
        right.reserve(sample_count);
        const uint8_t *pcm = payload.data() + 12;
        for (uint32_t i = 0; i < sample_count; ++i) {
          int16_t l = static_cast<int16_t>(pcm[i * channels * 2] |
                                           (static_cast<int16_t>(pcm[i * channels * 2 + 1]) << 8));
          left.push_back(l);
          if (channels == 2) {
            int16_t r = static_cast<int16_t>(pcm[i * channels * 2 + 2] |
                                             (static_cast<int16_t>(pcm[i * channels * 2 + 3]) << 8));
            right.push_back(r);
          }
        }
        if (channels == 1) {
          right = left;
        }

        uint32_t out_count = static_cast<uint32_t>(
            static_cast<double>(sample_count) * static_cast<double>(mix_rate) / static_cast<double>(sample_rate));
        if (out_count == 0) {
          continue;
        }
        left = resample_channel(left, out_count);
        right = resample_channel(right, out_count);

        std::vector<int16_t> interleaved(out_count * 2);
        for (uint32_t i = 0; i < out_count; ++i) {
          interleaved[i * 2] = left[i];
          interleaved[i * 2 + 1] = right[i];
        }

#ifdef PS1EMU_SPU_SDL
        if (audio_enabled && audio_dev != 0) {
          uint32_t queued = SDL_GetQueuedAudioSize(audio_dev);
          uint32_t max_queue = mix_rate * 2 * 2 * 2;
          if (queued < max_queue) {
            SDL_QueueAudio(audio_dev,
                           interleaved.data(),
                           static_cast<uint32_t>(interleaved.size() * sizeof(int16_t)));
          }
        }
#endif

        if (wav_path && *wav_path) {
          mix_buffer.insert(mix_buffer.end(), interleaved.begin(), interleaved.end());
        }
      }
    }
  }

  if (wav_path && *wav_path) {
    write_wav(wav_path);
  }

#ifdef PS1EMU_SPU_SDL
  if (audio_enabled && audio_dev != 0) {
    SDL_CloseAudioDevice(audio_dev);
  }
  if (sdl_inited) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
#endif

  return 0;
}
