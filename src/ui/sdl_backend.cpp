#include "ui/sdl_backend.h"

#include <SDL.h>

#include <cstdlib>

namespace ps1emu {

bool init_sdl_video_with_fallback() {
  auto try_init = [&](const char *driver) {
    SDL_Quit();
    if (driver && driver[0] != '\0') {
      setenv("SDL_VIDEODRIVER", driver, 1);
    }
    return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0;
  };

  const char *forced = getenv("SDL_VIDEODRIVER");
  if (forced && forced[0] != '\0') {
    return try_init(forced);
  }

  const char *drivers[] = {"wayland", "x11"};
  for (const char *driver : drivers) {
    if (try_init(driver)) {
      return true;
    }
  }
  return false;
}

} // namespace ps1emu
