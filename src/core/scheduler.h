#ifndef PS1EMU_SCHEDULER_H
#define PS1EMU_SCHEDULER_H

#include <cstdint>
#include <vector>

namespace ps1emu {

struct ScheduledEvent {
  uint64_t when = 0;
  int id = 0;
};

class Scheduler {
public:
  void reset();
  void advance(uint32_t cycles);
  void schedule(uint64_t cycles_from_now, int id);
  bool pop_next(ScheduledEvent &out);

  uint64_t now() const;

private:
  uint64_t now_ = 0;
  std::vector<ScheduledEvent> events_;
};

} // namespace ps1emu

#endif
