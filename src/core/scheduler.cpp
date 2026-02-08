#include "core/scheduler.h"

#include <algorithm>

namespace ps1emu {

void Scheduler::reset() {
  now_ = 0;
  events_.clear();
}

void Scheduler::advance(uint32_t cycles) {
  now_ += cycles;
}

void Scheduler::schedule(uint64_t cycles_from_now, int id) {
  ScheduledEvent evt;
  evt.when = now_ + cycles_from_now;
  evt.id = id;
  events_.push_back(evt);
  std::sort(events_.begin(), events_.end(),
            [](const ScheduledEvent &a, const ScheduledEvent &b) { return a.when < b.when; });
}

bool Scheduler::pop_next(ScheduledEvent &out) {
  if (events_.empty()) {
    return false;
  }
  out = events_.front();
  events_.erase(events_.begin());
  return true;
}

uint64_t Scheduler::now() const {
  return now_;
}

} // namespace ps1emu
