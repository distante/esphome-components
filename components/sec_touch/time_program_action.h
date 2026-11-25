#pragma once

// `sec_touch.set_time_program` (.specs/time-programs.md, "Bulk-set action"). It needs the time programs, so it
// compiles only when they do (time_program.h).

#include "esphome/core/defines.h"

#if defined(USE_DATETIME_TIME) && defined(USE_SELECT) && defined(USE_SWITCH)

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include "sec_touch.h"
#include "time_program.h"
#include "time_program_bulk.h"
#include "time_program_ids.h"

namespace esphome {
namespace sec_touch {

/**
 * Sets a fan pair's whole time program. Fire and forget: it checks the arguments, queues the writes and lets the chain
 * go on at once. `on_success` or `on_error` fires once every write has ended, with this call's arguments. A call that
 * writes nothing (a bad argument, `busy`) fires `on_error` right away.
 */
template<typename... Ts>
class SetTimeProgramAction : public Action<Ts...>, public Parented<SECTouchComponent>, public BulkWriteListener {
 public:
  TEMPLATABLE_VALUE(int, fan_number)
  TEMPLATABLE_VALUE(std::vector<std::string>, times)
  TEMPLATABLE_VALUE(std::vector<int32_t>, levels)
  TEMPLATABLE_VALUE(std::vector<bool>, enabled)

  Trigger<Ts...> *get_success_trigger() { return &this->success_trigger_; }
  /// `error` comes after the call's arguments: api.respond reads call_id from the first one.
  Trigger<Ts..., std::string> *get_error_trigger() { return &this->error_trigger_; }

  void play(const Ts &...x) override {
    const int pair = this->fan_number_.value(x...);
    TimeProgram *program = this->parent_->get_time_program(pair);
    if (program == nullptr) {
      this->refuse_(pair, bulk_unlisted_pair_error(pair), x...);
      return;
    }
    BulkProgram bulk;
    if (auto error =
            read_bulk_program(this->times_.value(x...), this->levels_.value(x...), this->enabled_.value(x...), bulk)) {
      this->refuse_(pair, *error, x...);
      return;
    }
    if (program->is_bulk_write_running()) {
      this->refuse_(pair, BULK_BUSY, x...);
      return;
    }
    // The triggers fire after play() returns: keep this call's arguments until then.
    this->calls_[pair - 1].emplace(x...);
    program->write_program(bulk, this);
  }

  void on_bulk_write_done(int pair, const std::optional<std::string> &error) override {
    if (!is_time_program_pair(pair) || !this->calls_[pair - 1].has_value()) {
      return;
    }
    // Free the slot first: an automation may call this action again for the same pair.
    Call call = std::move(*this->calls_[pair - 1]);
    this->calls_[pair - 1].reset();
    if (error.has_value()) {
      std::apply([this, &error](const auto &...args) { this->error_trigger_.trigger(args..., *error); }, call);
      return;
    }
    std::apply([this](const auto &...args) { this->success_trigger_.trigger(args...); }, call);
  }

 protected:
  using Call = std::tuple<stored_trigger_arg_t<Ts>...>;

  void refuse_(int pair, const std::string &error, const Ts &...x) {
    log_bulk_write_refused(pair, error);
    this->error_trigger_.trigger(x..., error);
  }

  /// Per fan pair, by pair - 1: the arguments of the call whose bulk write runs.
  std::array<std::optional<Call>, TIME_PROGRAM_PAIR_COUNT> calls_{};
  Trigger<Ts...> success_trigger_;
  Trigger<Ts..., std::string> error_trigger_;
};

}  // namespace sec_touch
}  // namespace esphome

#endif
