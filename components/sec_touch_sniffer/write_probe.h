#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_write_probe.
//
// The write probe and the restore request (.specs/sniffer.md, "Write probe"): the request's 400 rules, the step
// sequence and its result, the `probe` object of /sniffer/data. The sniffer runs the steps on its main loop in scan
// mode: it sends what each ProbeCommand asks and feeds each answer back.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../sec_touch/data_message.h"
#include "../sec_touch/known_property_meaning.h"
#include "../sec_touch/scan_outcome.h"
#include "property_slots.h"
#include "uri_query.h"

namespace esphome {
namespace sec_touch {

/// Wait after a scan query's first timeout before its one retry. A probe's timed-out SET waits as long.
constexpr uint32_t SCAN_RETRY_DELAY_MS = 5000;
/// How long a read-back (or a restore's verification) polls for the value it waits for.
constexpr uint32_t PROBE_READBACK_WINDOW_MS = 5000;
/// Gap between one read-back's answer and the next read-back.
constexpr uint32_t PROBE_READBACK_INTERVAL_MS = 1000;
/// Steps a probe result records. Later steps are not recorded; the sequence still runs to its end.
constexpr size_t PROBE_STEP_CAP = 32;

/// A raw value of a probe: the value written, the original, or a GET's answer (up to RAW_VALUE_CAP bytes).
struct ProbeValue {
  uint8_t len{0};
  char bytes[RAW_VALUE_CAP]{};

  std::string_view view() const { return std::string_view(this->bytes, this->len); }
  /// `value`, cut at RAW_VALUE_CAP.
  static ProbeValue of(std::string_view value) {
    ProbeValue out;
    out.len = static_cast<uint8_t>(value.size() < RAW_VALUE_CAP ? value.size() : RAW_VALUE_CAP);
    std::memcpy(out.bytes, value.data(), out.len);
    return out;
  }
};

enum class ProbeKind : uint8_t {
  /// GET the original, SET the value, read back, then restore.
  PROBE_KIND_PROBE,
  /// Only the restore: SET the requested original and verify it.
  PROBE_KIND_RESTORE,
};

enum class ProbeStepKind : uint8_t { PROBE_STEP_GET, PROBE_STEP_SET };

enum class ProbeVerdict : uint8_t {
  /// Not decided yet, or a restore, which has none.
  PROBE_VERDICT_NONE,
  PROBE_VERDICT_WRITABLE,
  PROBE_VERDICT_NOT_WRITABLE,
  PROBE_VERDICT_UNCLEAR,
  PROBE_VERDICT_NOTHING_WRITTEN,
};

enum class ProbeRestore : uint8_t {
  /// Not decided yet.
  PROBE_RESTORE_NONE,
  PROBE_RESTORE_OK,
  PROBE_RESTORE_FAILED,
  /// No restore was needed, nothing written included.
  PROBE_RESTORE_SKIPPED,
};

/// One GET or SET of a probe.
struct ProbeStep {
  ProbeStepKind kind;
  ScanOutcome outcome;
  /// False for a failed GET: its value is null.
  bool has_value;
  /// The GET's answer or the SET's value.
  ProbeValue value;
  uint64_t at_ms;
};

/// The last probe's or restore's result: the `probe` object of /sniffer/data.
struct ProbeResult {
  bool active;
  ProbeKind kind;
  uint8_t id;
  /// The value to write (a probe) or the requested original (a restore).
  ProbeValue value;
  /// The original GET answered. A restore has its requested original from the start.
  bool has_original;
  ProbeValue original;
  ProbeStep steps[PROBE_STEP_CAP];
  uint8_t step_count;
  /// PROBE_VERDICT_NONE while the probe runs, and always for a restore.
  ProbeVerdict verdict;
  /// PROBE_RESTORE_NONE until the run ends.
  ProbeRestore restore;
};

/// Why a probe or restore request is a 400. Nothing starts.
enum class ProbeRequestError : uint8_t {
  PROBE_REQUEST_OK,
  /// `id` missing, not a number, or outside 0..MAX_PROPERTY_ID.
  PROBE_REQUEST_BAD_ID,
  /// `id` is a known property: the component's own entities write those.
  PROBE_REQUEST_KNOWN_ID,
  /// The value is over SET_VALUE_CAP bytes or holds NUL, TAB, STX or ETX.
  PROBE_REQUEST_BAD_VALUE,
};

/// A parsed `POST /sniffer/probe` or `/sniffer/restore`. `id` and `value` are meaningful only without an error.
struct ProbeRequest {
  ProbeRequestError error;
  uint8_t id;
  ProbeValue value;
};

/// Whether `value` can be a SET's value: at most SET_VALUE_CAP bytes (SetDataTask holds no more), and no NUL, TAB,
/// STX or ETX (the SET frames it with `%s` between TABs). An empty value is allowed.
inline bool is_probe_value(std::string_view value) {
  if (value.size() > static_cast<size_t>(SET_VALUE_CAP)) {
    return false;
  }
  for (char c : value) {
    const auto b = static_cast<uint8_t>(c);
    if (b == 0 || b == WIRE_TAB || b == WIRE_STX || b == WIRE_ETX) {
      return false;
    }
  }
  return true;
}

/// The request in `uri`'s `id` and the exact bytes of its `body`. A body longer than SET_VALUE_CAP is a 400 however
/// long it is, so the caller may pass only its first SET_VALUE_CAP + 1 bytes.
inline ProbeRequest probe_request_from(std::string_view uri, std::string_view body) {
  ProbeRequest request{ProbeRequestError::PROBE_REQUEST_OK, 0, ProbeValue{}};
  const auto id_text = query_value(uri, "id");
  const auto id = id_text.has_value() ? parse_bounded_uint(*id_text, MAX_PROPERTY_ID) : std::nullopt;
  if (!id.has_value()) {
    request.error = ProbeRequestError::PROBE_REQUEST_BAD_ID;
    return request;
  }
  if (is_known_property(static_cast<int>(*id))) {
    request.error = ProbeRequestError::PROBE_REQUEST_KNOWN_ID;
    return request;
  }
  if (!is_probe_value(body)) {
    request.error = ProbeRequestError::PROBE_REQUEST_BAD_VALUE;
    return request;
  }
  request.id = static_cast<uint8_t>(*id);
  request.value = ProbeValue::of(body);
  return request;
}

/// Sets `result` to what a probe or restore starts with, before any step: what its start replies with. In place,
/// because a ProbeResult is about 2.4 KB: a temporary one overflows the httpd task's stack.
inline void reset_probe_result(ProbeResult &result, ProbeKind kind, uint8_t id, const ProbeValue &value) {
  result.active = true;
  result.kind = kind;
  result.id = id;
  result.value = value;
  result.has_original = kind == ProbeKind::PROBE_KIND_RESTORE;
  result.original = kind == ProbeKind::PROBE_KIND_RESTORE ? value : ProbeValue{};
  result.step_count = 0;
  result.verdict = ProbeVerdict::PROBE_VERDICT_NONE;
  result.restore = ProbeRestore::PROBE_RESTORE_NONE;
}

/// The result a probe or restore starts with (reset_probe_result()), by value: for the host tests only.
inline ProbeResult probe_start_result(ProbeKind kind, uint8_t id, const ProbeValue &value) {
  ProbeResult result{};
  reset_probe_result(result, kind, id, value);
  return result;
}

/// Records `step` in `result`, unless PROBE_STEP_CAP steps are recorded already.
inline void append_probe_step(ProbeResult &result, const ProbeStep &step) {
  if (result.step_count >= PROBE_STEP_CAP) {
    return;
  }
  result.steps[result.step_count++] = step;
}

/// What the sniffer does next for a running probe.
enum class ProbeAction : uint8_t {
  /// GET the probe's ID, then call on_answer() with its outcome and value.
  PROBE_ACTION_GET,
  /// SET set_value() on the probe's ID, then call on_answer() with its outcome.
  PROBE_ACTION_SET,
  /// Wait `wait_ms`, then call on_wait_over().
  PROBE_ACTION_WAIT,
  /// The run has ended: result() holds its verdict and restore.
  PROBE_ACTION_FINISHED,
};

struct ProbeCommand {
  ProbeAction action;
  uint32_t wait_ms;
};

/// The step sequence of one probe or restore (.specs/sniffer.md, "Write probe", steps 1-6). Each call takes the time
/// in uptime ms and returns what to do next; the caller sends it and reports back.
///
/// The verdict: writable when a read-back shows the value; not writable when the SET got ACK or NAK, a read-back
/// showed the original and none showed another value (a failed read-back is skipped); unclear otherwise. An original
/// longer than SET_VALUE_CAP could never be restored, so such a probe writes nothing.
class WriteProbe {
 public:
  /// Starts a probe of `value` on `id`: GETs the original first.
  ProbeCommand start_probe(uint8_t id, const ProbeValue &value) {
    this->begin_(ProbeKind::PROBE_KIND_PROBE, id, value);
    this->phase_ = Phase::ORIGINAL;
    return get_();
  }

  /// Starts a restore of `original` on `id`: SETs it and verifies.
  ProbeCommand start_restore(uint8_t id, const ProbeValue &original) {
    this->begin_(ProbeKind::PROBE_KIND_RESTORE, id, original);
    return this->begin_restore_();
  }

  /// The outcome of the GET or SET the last command asked for. `value` is a GET's answer, read only when `outcome`
  /// is ok.
  ProbeCommand on_answer(ScanOutcome outcome, std::string_view value, uint64_t now_ms) {
    switch (this->phase_) {
      case Phase::ORIGINAL:
        this->record_get_(outcome, value, now_ms);
        return this->on_original_(outcome, value);
      case Phase::SET:
        this->record_set_(outcome, now_ms);
        return this->on_set_(outcome, now_ms);
      case Phase::READBACK:
        this->record_get_(outcome, value, now_ms);
        return this->on_read_back_(outcome, value, now_ms);
      case Phase::RESTORE_SET:
        this->record_set_(outcome, now_ms);
        return this->on_restore_set_(outcome, now_ms);
      case Phase::RESTORE_VERIFY:
        this->record_get_(outcome, value, now_ms);
        return this->on_restore_verify_(outcome, value, now_ms);
      case Phase::DONE:
        break;
    }
    return finished_();
  }

  /// The wait the last command asked for is over: sends what it waited for.
  ProbeCommand on_wait_over() {
    switch (this->phase_) {
      case Phase::ORIGINAL:
      case Phase::READBACK:
      case Phase::RESTORE_VERIFY:
        return get_();
      case Phase::SET:
      case Phase::RESTORE_SET:
        return set_();
      case Phase::DONE:
        break;
    }
    return finished_();
  }

  /// The value a PROBE_ACTION_SET writes: the probe's value, or the original while restoring.
  std::string_view set_value() const {
    return this->restoring_() ? this->result_.original.view() : this->result_.value.view();
  }

  /// The last GET's answer, null (false) when it failed. Kept past PROBE_STEP_CAP, for the failed-restore log.
  bool last_read(ProbeValue &out) const {
    out = this->last_read_;
    return this->has_last_read_;
  }

  const ProbeResult &result() const { return this->result_; }

 protected:
  enum class Phase : uint8_t {
    /// Step 1: the GET of the original.
    ORIGINAL,
    /// Step 2: the SET of the value.
    SET,
    /// Step 3: the read-back.
    READBACK,
    /// Steps 5-6: the SET of the original.
    RESTORE_SET,
    /// Steps 5-6: the read-back that verifies the restore.
    RESTORE_VERIFY,
    DONE,
  };

  static ProbeCommand get_() { return ProbeCommand{ProbeAction::PROBE_ACTION_GET, 0}; }
  static ProbeCommand set_() { return ProbeCommand{ProbeAction::PROBE_ACTION_SET, 0}; }
  static ProbeCommand wait_(uint32_t ms) { return ProbeCommand{ProbeAction::PROBE_ACTION_WAIT, ms}; }
  static ProbeCommand finished_() { return ProbeCommand{ProbeAction::PROBE_ACTION_FINISHED, 0}; }

  bool restoring_() const { return this->phase_ == Phase::RESTORE_SET || this->phase_ == Phase::RESTORE_VERIFY; }

  void begin_(ProbeKind kind, uint8_t id, const ProbeValue &value) {
    reset_probe_result(this->result_, kind, id, value);
    this->retried_ = false;
    this->has_last_read_ = false;
    this->verdict_ = ProbeVerdict::PROBE_VERDICT_NONE;
  }

  void record_get_(ScanOutcome outcome, std::string_view value, uint64_t now_ms) {
    const bool answered = outcome == ScanOutcome::SCAN_OUTCOME_OK;
    this->has_last_read_ = answered;
    this->last_read_ = answered ? ProbeValue::of(value) : ProbeValue{};
    append_probe_step(this->result_,
                      ProbeStep{ProbeStepKind::PROBE_STEP_GET, outcome, answered, this->last_read_, now_ms});
  }

  void record_set_(ScanOutcome outcome, uint64_t now_ms) {
    append_probe_step(this->result_, ProbeStep{ProbeStepKind::PROBE_STEP_SET, outcome, true,
                                               ProbeValue::of(this->set_value()), now_ms});
  }

  /// Whether a timed-out request gets its one retry: true the first time, after which the caller waits.
  bool take_retry_(ScanOutcome outcome) {
    if (outcome != ScanOutcome::SCAN_OUTCOME_TIMEOUT || this->retried_) {
      return false;
    }
    this->retried_ = true;
    return true;
  }

  ProbeCommand on_original_(ScanOutcome outcome, std::string_view value) {
    if (this->take_retry_(outcome)) {
      return wait_(SCAN_RETRY_DELAY_MS);
    }
    if (outcome != ScanOutcome::SCAN_OUTCOME_OK) {
      return this->finish_(ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN, ProbeRestore::PROBE_RESTORE_SKIPPED);
    }
    this->result_.has_original = true;
    this->result_.original = ProbeValue::of(value);
    if (value == this->result_.value.view() || value.size() > static_cast<size_t>(SET_VALUE_CAP)) {
      return this->finish_(ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN, ProbeRestore::PROBE_RESTORE_SKIPPED);
    }
    this->phase_ = Phase::SET;
    this->retried_ = false;
    return set_();
  }

  ProbeCommand on_set_(ScanOutcome outcome, uint64_t now_ms) {
    if (this->take_retry_(outcome)) {
      return wait_(SCAN_RETRY_DELAY_MS);
    }
    this->set_answered_ = outcome != ScanOutcome::SCAN_OUTCOME_TIMEOUT;
    this->saw_original_ = false;
    this->saw_other_ = false;
    this->begin_polling_(Phase::READBACK, now_ms);
    return get_();
  }

  ProbeCommand on_read_back_(ScanOutcome outcome, std::string_view value, uint64_t now_ms) {
    const bool answered = outcome == ScanOutcome::SCAN_OUTCOME_OK;
    if (answered && value == this->result_.value.view()) {
      return this->decide_(ProbeVerdict::PROBE_VERDICT_WRITABLE);
    }
    if (answered && value == this->result_.original.view()) {
      this->saw_original_ = true;
    } else if (answered) {
      this->saw_other_ = true;
    }
    if (!this->polling_over_(now_ms)) {
      return wait_(PROBE_READBACK_INTERVAL_MS);
    }
    const bool not_writable = this->set_answered_ && this->saw_original_ && !this->saw_other_;
    return this->decide_(not_writable ? ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE : ProbeVerdict::PROBE_VERDICT_UNCLEAR);
  }

  /// Step 5: a not-writable property never needs a restore, nor does one whose last read-back shows the original.
  ProbeCommand decide_(ProbeVerdict verdict) {
    this->verdict_ = verdict;
    const bool at_original = this->has_last_read_ && this->last_read_.view() == this->result_.original.view();
    if (verdict == ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE || at_original) {
      return this->finish_(verdict, ProbeRestore::PROBE_RESTORE_SKIPPED);
    }
    return this->begin_restore_();
  }

  ProbeCommand begin_restore_() {
    this->phase_ = Phase::RESTORE_SET;
    this->retried_ = false;
    this->restore_resent_ = false;
    return set_();
  }

  ProbeCommand on_restore_set_(ScanOutcome outcome, uint64_t now_ms) {
    if (this->take_retry_(outcome)) {
      return wait_(SCAN_RETRY_DELAY_MS);
    }
    this->begin_polling_(Phase::RESTORE_VERIFY, now_ms);
    return get_();
  }

  /// Step 6: a restore not verified is sent once more, then fails.
  ProbeCommand on_restore_verify_(ScanOutcome outcome, std::string_view value, uint64_t now_ms) {
    if (outcome == ScanOutcome::SCAN_OUTCOME_OK && value == this->result_.original.view()) {
      return this->finish_(this->verdict_, ProbeRestore::PROBE_RESTORE_OK);
    }
    if (!this->polling_over_(now_ms)) {
      return wait_(PROBE_READBACK_INTERVAL_MS);
    }
    if (this->restore_resent_) {
      return this->finish_(this->verdict_, ProbeRestore::PROBE_RESTORE_FAILED);
    }
    this->phase_ = Phase::RESTORE_SET;
    this->retried_ = false;
    this->restore_resent_ = true;
    return set_();
  }

  void begin_polling_(Phase phase, uint64_t now_ms) {
    this->phase_ = phase;
    this->polling_since_ms_ = now_ms;
  }

  bool polling_over_(uint64_t now_ms) const { return now_ms - this->polling_since_ms_ >= PROBE_READBACK_WINDOW_MS; }

  ProbeCommand finish_(ProbeVerdict verdict, ProbeRestore restore) {
    this->phase_ = Phase::DONE;
    this->result_.active = false;
    this->result_.verdict = verdict;
    this->result_.restore = restore;
    return finished_();
  }

  ProbeResult result_{};
  Phase phase_{Phase::DONE};
  /// The timed-out GET or SET in hand already had its one retry.
  bool retried_{false};
  /// The probe's SET got ACK or NAK.
  bool set_answered_{false};
  /// A read-back showed the original, or another value than the original and the value.
  bool saw_original_{false};
  bool saw_other_{false};
  /// The restoring SET was sent once more after a verification that failed.
  bool restore_resent_{false};
  /// Held until the run ends: `result_.verdict` stays none while it runs.
  ProbeVerdict verdict_{ProbeVerdict::PROBE_VERDICT_NONE};
  uint64_t polling_since_ms_{0};
  ProbeValue last_read_{};
  bool has_last_read_{false};
};

}  // namespace sec_touch
}  // namespace esphome
