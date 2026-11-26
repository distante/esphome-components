#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common.h"
#include "esphome/core/base_automation.h"
#include "esphome/components/sec_touch/time_program.h"
#include "esphome/components/sec_touch/time_program_action.h"
#include "esphome/components/sec_touch/time_program_bulk.h"
#include "esphome/components/sec_touch/time_program_codec.h"
#include "esphome/components/sec_touch/time_program_ids.h"
#include "esphome/components/sec_touch/time_program_write.h"

namespace esphome::sec_touch::testing {

namespace {

/// The listed pair; switch time SWITCH_TIME has entities, the others none (the bulk write ignores switch_times).
constexpr int PAIR = 2;
constexpr int UNLISTED_PAIR = 5;
constexpr int SWITCH_TIME = 3;

const int MASK_ID = time_program_enabled_id(PAIR);

/// The spec's HA call.
const std::vector<std::string> TIMES = {"06:00", "08:30", "17:00", "22:00", "00:00"};
const std::vector<std::string> TIMES_AS_WRITTEN = {"06:00:00", "08:30:00", "17:00:00", "22:00:00", "00:00:00"};
const std::vector<int32_t> LEVELS = {4, 2, 4, 1, 0};
const std::vector<bool> ENABLED = {true, true, true, true, false};
/// What the panel's mask holds before the call: switch time 5 enabled.
const uint8_t MASK_BEFORE = switch_time_bit(5);
/// ENABLED: switch times 1-4 on, 5 off.
const uint8_t MASK_AFTER = switch_time_bit(1) | switch_time_bit(2) | switch_time_bit(3) | switch_time_bit(4);

/// How the `error` names a failed write of switch time `switch_time`'s `field`.
std::string failed_write(int switch_time, TimeProgramField field, WriteFailure failure) {
  return bulk_detail::switch_time_field(switch_time, field) + " " + write_failure_name(failure);
}

/// How the `error` names a failed mask write.
std::string failed_mask_write(WriteFailure failure) {
  return std::string(time_program_field_name(TimeProgramField::ENABLED)) + " " + write_failure_name(failure);
}

/// How the `error` of a bulk write with `count` failed writes starts.
std::string failed_prefix(size_t count) {
  return std::to_string(count) + " of " + std::to_string(BULK_WRITE_COUNT) + " writes failed: ";
}

/// A request the hub sent, read back from its bytes.
struct Request {
  bool is_get;
  int id;
  std::string value;

  bool operator==(const Request &other) const {
    return this->is_get == other.is_get && this->id == other.id && this->value == other.value;
  }
};

void PrintTo(const Request &request, std::ostream *os) {
  *os << (request.is_get ? "GET " : "SET ") << request.id << " '" << request.value << "'";
}

Request set_of(int id, const std::string &value) { return Request{false, id, value}; }

/// Splits `[STX]<command>[TAB]<id>[TAB](<value>[TAB])<crc>[ETX]` into its request.
Request parse_request(const std::vector<uint8_t> &bytes) {
  std::vector<std::string> fields{""};
  for (size_t i = 1; i + 1 < bytes.size(); i++) {
    if (bytes[i] == TAB) {
      fields.emplace_back();
      continue;
    }
    fields.back().push_back(static_cast<char>(bytes[i]));
  }
  const bool is_get = fields[0] == std::to_string(COMMANDID_GET);
  return Request{is_get, std::stoi(fields[1]), is_get ? "" : fields[2]};
}

class SetTimeProgramTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->hub_.set_uart_parent(&this->uart_);
    this->hub_.set_inter_task_delay(0);
    this->hub_.last_time_programs_update_ms_ = millis();
    this->hub_.last_global_settings_update_ms_ = millis();
    this->action_.set_parent(&this->hub_);
    this->action_.set_fan_number([]() -> int { return PAIR; });
    this->action_.set_times(TIMES);
    this->action_.set_levels(LEVELS);
    this->action_.set_enabled(ENABLED);
    this->write_failed_.add_on_event_callback(
        [this](StringRef type) { this->events_.push_back(std::string(type.c_str())); });
    this->panel_[MASK_ID] = mask_text(MASK_BEFORE);
    this->on_success_.add_action(&this->count_success_);
    this->on_error_.add_action(&this->keep_error_);
  }

  /// Runs the queue against a panel that keeps what is SET and answers each GET with what it holds. A SET of an ID in
  /// `refuse` gets a NAK; the first GET of an ID in `ignore_first_get` gets no answer. Returns each request in order.
  std::vector<Request> run_panel(const std::set<int> &refuse = {}, const std::set<int> &ignore_first_get = {}) {
    std::vector<Request> sent;
    std::set<int> ignored;
    this->uart_.tx.clear();
    this->hub_.loop();
    std::vector<uint8_t> out = this->uart_.tx;
    while (true) {
      const std::vector<uint8_t> ack = ack_frame();
      if (out.size() >= ack.size() && std::equal(ack.begin(), ack.end(), out.begin())) {
        out.erase(out.begin(), out.begin() + ack.size());
      }
      if (out.empty()) {
        this->uart_.tx.clear();
        this->hub_.loop();
        out = this->uart_.tx;
        if (out.empty()) {
          return sent;
        }
        continue;
      }
      const Request request = parse_request(out);
      sent.push_back(request);
      this->uart_.tx.clear();
      if (request.is_get && ignore_first_get.count(request.id) > 0 && ignored.insert(request.id).second) {
        this->hub_.age_task_past_watchdog();
        this->hub_.loop();
        this->hub_.loop();
      } else if (request.is_get) {
        this->uart_.push_rx(concat(ack_frame(), data_frame(request.id, this->panel_[request.id])));
        this->hub_.loop_times(3);
      } else if (refuse.count(request.id) > 0) {
        this->uart_.push_rx(nak_frame());
        this->hub_.loop_times(3);
      } else {
        this->panel_[request.id] = request.value;
        this->uart_.push_rx(ack_frame());
        this->hub_.loop_times(3);
      }
      out = this->uart_.tx;
    }
  }

  static std::vector<Request> sets(const std::vector<Request> &requests) {
    std::vector<Request> out;
    for (const Request &request : requests) {
      if (!request.is_get) {
        out.push_back(request);
      }
    }
    return out;
  }

  /// The 11 SETs of the spec's HA call, in the order they must go out.
  static std::vector<Request> expected_sets(uint8_t mask) {
    std::vector<Request> out;
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      out.push_back(set_of(time_program_time_id(PAIR, n), TIMES_AS_WRITTEN[n - 1]));
    }
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      out.push_back(set_of(time_program_level_id(PAIR, n), std::to_string(LEVELS[n - 1])));
    }
    out.push_back(set_of(MASK_ID, mask_text(mask)));
    return out;
  }

  FakeUART uart_;
  TestableSECTouch hub_;
  TimeProgram program_{&this->hub_, PAIR};
  TimeProgramTime time_{&this->program_, SWITCH_TIME};
  TimeProgramLevelSelect level_{&this->program_, SWITCH_TIME};
  TimeProgramEnabledSwitch enabled_{&this->program_, SWITCH_TIME};
  TimeProgramWriteFailedEvent write_failed_{&this->program_};
  std::vector<std::string> events_;
  std::map<int, std::string> panel_;

  SetTimeProgramAction<> action_;
  std::vector<std::string> errors_;
  int successes_{0};
  Automation<> on_success_{this->action_.get_success_trigger()};
  Automation<std::string> on_error_{this->action_.get_error_trigger()};
  /// The panel's mask when on_success fired.
  std::string mask_at_success_;
  LambdaAction<> count_success_{[this]() {
    this->successes_++;
    this->mask_at_success_ = this->panel_[MASK_ID];
  }};
  LambdaAction<std::string> keep_error_{[this](std::string error) { this->errors_.push_back(error); }};
};

}  // namespace

// AC 10: the 5 times, the 5 levels, then the mask from a fresh GET; on_success once every read-back matches.
TEST_F(SetTimeProgramTest, WritesTimesThenLevelsThenTheMask) {
  this->action_.play_complex();
  EXPECT_EQ(this->successes_, 0);  // fire and forget: nothing has been written yet

  const std::vector<Request> sent = this->run_panel();

  EXPECT_EQ(this->sets(sent), this->expected_sets(MASK_AFTER));
  EXPECT_EQ(this->successes_, 1);
  EXPECT_TRUE(this->errors_.empty());
  EXPECT_TRUE(this->events_.empty());
}

// Spec: every value is written, even one the panel already holds.
TEST_F(SetTimeProgramTest, WritesValuesThePanelAlreadyHolds) {
  this->action_.play_complex();
  this->run_panel();

  this->action_.play_complex();

  EXPECT_EQ(this->sets(this->run_panel()), this->expected_sets(MASK_AFTER));
  EXPECT_EQ(this->successes_, 2);
}

// AC 10: a second call for the pair while one runs is busy and writes nothing.
TEST_F(SetTimeProgramTest, ASecondCallWhileOneRunsIsBusy) {
  this->action_.play_complex();

  this->action_.play_complex();

  EXPECT_EQ(this->errors_, std::vector<std::string>{BULK_BUSY});
  EXPECT_EQ(this->sets(this->run_panel()), this->expected_sets(MASK_AFTER));
  EXPECT_EQ(this->successes_, 1);
}

// AC 10: one bad argument writes nothing.
TEST_F(SetTimeProgramTest, ABadArgumentWritesNothing) {
  this->action_.set_levels(std::vector<int32_t>{4, 2, 4, 12, 0});

  this->action_.play_complex();

  ASSERT_EQ(this->errors_.size(), 1u);
  EXPECT_EQ(this->errors_[0].rfind(BULK_INVALID_ARGUMENT, 0), 0u);
  EXPECT_TRUE(this->run_panel().empty());
}

// Spec: a lambda fan_number is checked at run time.
TEST_F(SetTimeProgramTest, AnUnlistedPairWritesNothing) {
  this->action_.set_fan_number([]() -> int { return UNLISTED_PAIR; });

  this->action_.play_complex();

  EXPECT_EQ(this->errors_, std::vector<std::string>{bulk_unlisted_pair_error(UNLISTED_PAIR)});
  EXPECT_TRUE(this->run_panel().empty());
}

// AC 10: writing goes on past a failed field; the report names each failure; write_failed fires per failure.
TEST_F(SetTimeProgramTest, WritingGoesOnPastFailuresAndReportsThem) {
  const int failed_level_id = time_program_level_id(PAIR, SWITCH_TIME);
  this->action_.play_complex();

  const std::vector<Request> sent = this->run_panel({failed_level_id}, {MASK_ID});

  std::vector<Request> expected = this->expected_sets(MASK_AFTER);
  expected.pop_back();  // the mask GET failed: no mask SET
  EXPECT_EQ(this->sets(sent), expected);
  EXPECT_EQ(this->errors_,
            std::vector<std::string>{failed_prefix(2) +
                                     failed_write(SWITCH_TIME, TimeProgramField::LEVEL, WriteFailure::REFUSED) +
                                     BULK_FAILURE_SEPARATOR + failed_mask_write(WriteFailure::MASK_UNREADABLE)});
  EXPECT_EQ(this->successes_, 0);
  // One event per failed write: the refused level's read-back differs too, but is no second report.
  EXPECT_EQ(this->events_, (std::vector<std::string>{write_failure_name(WriteFailure::REFUSED),
                                                     write_failure_name(WriteFailure::MASK_UNREADABLE)}));
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

// A SET that can't be queued fails its write as a timeout, and the call still ends, freeing the pair.
TEST_F(SetTimeProgramTest, ASetThatCantBeQueuedFailsItsWriteAndTheCallEnds) {
  this->hub_.unqueueable_sets = {time_program_time_id(PAIR, 1), MASK_ID};
  this->action_.play_complex();

  this->run_panel();

  EXPECT_EQ(this->errors_,
            std::vector<std::string>{failed_prefix(2) + failed_write(1, TimeProgramField::TIME, WriteFailure::TIMEOUT) +
                                     BULK_FAILURE_SEPARATOR + failed_mask_write(WriteFailure::TIMEOUT)});
  EXPECT_EQ(this->events_, (std::vector<std::string>{write_failure_name(WriteFailure::TIMEOUT),
                                                     write_failure_name(WriteFailure::TIMEOUT)}));
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

// Spec: an entity write that replaces a queued bulk value shows up as not_kept in the bulk report.
TEST_F(SetTimeProgramTest, AnEntityWriteThatReplacesABulkValueIsNotKept) {
  constexpr size_t ENTITY_LEVEL = 7;
  this->action_.play_complex();
  this->level_.make_call().set_index(ENTITY_LEVEL).perform();

  this->run_panel();

  EXPECT_EQ(this->errors_,
            std::vector<std::string>{failed_prefix(1) +
                                     failed_write(SWITCH_TIME, TimeProgramField::LEVEL, WriteFailure::NOT_KEPT)});
  EXPECT_TRUE(this->events_.empty());  // the entity's own write landed
}

// An entity write's mask SET still in flight when the call starts is not the call's mask SET: the call waits for its
// own and confirms it.
TEST_F(SetTimeProgramTest, AnEntityMaskSetInFlightIsNotTheCallsMaskSet) {
  this->enabled_.turn_on();
  this->hub_.loop();  // the fresh mask GET goes out
  this->uart_.tx.clear();
  this->uart_.push_rx(concat(ack_frame(), data_frame(MASK_ID, this->panel_[MASK_ID])));
  this->hub_.loop_times(3);  // its answer queues the entity's mask SET, which goes out
  std::vector<uint8_t> out = this->uart_.tx;
  const std::vector<uint8_t> ack = ack_frame();
  ASSERT_TRUE(out.size() > ack.size() && std::equal(ack.begin(), ack.end(), out.begin()));
  out.erase(out.begin(), out.begin() + ack.size());
  ASSERT_EQ(parse_request(out), set_of(MASK_ID, mask_text(MASK_BEFORE | switch_time_bit(SWITCH_TIME))));
  this->panel_[MASK_ID] = mask_text(MASK_BEFORE | switch_time_bit(SWITCH_TIME));

  this->action_.play_complex();
  this->uart_.push_rx(ack_frame());  // the entity's mask SET ends after the call started
  this->run_panel();

  EXPECT_EQ(this->successes_, 1);
  EXPECT_EQ(this->mask_at_success_, mask_text(MASK_AFTER));
  EXPECT_TRUE(this->errors_.empty());
}

// Decisions: a scan drops the queue; the running call ends with on_error naming the unwritten fields, and frees
// the pair.
TEST_F(SetTimeProgramTest, AScanEndsTheRunningCall) {
  this->action_.play_complex();

  this->hub_.enter_scan_mode();

  ASSERT_EQ(this->errors_.size(), 1u);
  EXPECT_EQ(this->errors_[0].rfind(failed_prefix(BULK_WRITE_COUNT) +
                                       failed_write(1, TimeProgramField::TIME, WriteFailure::TIMEOUT) +
                                       BULK_FAILURE_SEPARATOR,
                                   0),
            0u);
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

// Leaving scan mode drops the SET in flight: it ends as a timeout and the call still ends, freeing the pair. The rest
// goes out once the watchdog frees the line.
TEST_F(SetTimeProgramTest, AScanExitEndsTheSetInFlight) {
  this->hub_.enter_scan_mode();
  this->action_.play_complex();
  this->hub_.loop();  // the first time SET goes out

  this->hub_.exit_scan_mode();
  this->hub_.age_task_past_watchdog();
  this->run_panel();

  EXPECT_EQ(this->errors_, std::vector<std::string>{failed_prefix(1) +
                                                    failed_write(1, TimeProgramField::TIME, WriteFailure::TIMEOUT)});
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

// AC 10: writes stay allowed during a scan, and each ACKed SET still gets its read-back GET. The scan takes the
// answers, so nothing is compared: no on_success.
TEST_F(SetTimeProgramTest, ACallDuringAScanIsNotConfirmed) {
  this->hub_.enter_scan_mode();
  this->action_.play_complex();

  const std::vector<Request> sent = this->run_panel();

  size_t read_backs = 0;
  for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
    read_backs += std::count(sent.begin(), sent.end(), Request{true, time_program_time_id(PAIR, n), ""});
    read_backs += std::count(sent.begin(), sent.end(), Request{true, time_program_level_id(PAIR, n), ""});
  }
  EXPECT_EQ(read_backs, 2u * SWITCH_TIME_COUNT);
  std::string expected = failed_prefix(BULK_WRITE_COUNT);
  for (TimeProgramField field : {TimeProgramField::TIME, TimeProgramField::LEVEL}) {
    for (int n = 1; n <= SWITCH_TIME_COUNT; n++) {
      expected += failed_write(n, field, WriteFailure::NOT_KEPT) + BULK_FAILURE_SEPARATOR;
    }
  }
  expected += failed_mask_write(WriteFailure::MASK_UNREADABLE);
  EXPECT_EQ(this->errors_, std::vector<std::string>{expected});
  EXPECT_EQ(this->successes_, 0);
}

// Only the call's own SET of a field counts for it: an entity SET of that field still in flight when the call starts
// that fails fires its event but is no failure of the call.
TEST_F(SetTimeProgramTest, ARefusedEntitySetInFlightIsNotTheCallsWrite) {
  constexpr size_t ENTITY_LEVEL = 7;
  this->level_.make_call().set_index(ENTITY_LEVEL).perform();
  this->hub_.loop();  // the entity's level SET goes out

  this->action_.play_complex();
  this->uart_.push_rx(nak_frame());  // the panel refuses it after the call started
  this->run_panel();

  EXPECT_EQ(this->successes_, 1);
  EXPECT_TRUE(this->errors_.empty());
  EXPECT_EQ(this->events_, std::vector<std::string>{write_failure_name(WriteFailure::REFUSED)});
}

// A call whose fresh mask GET can't be queued still ends: its mask write fails once, and the pair is free again.
TEST_F(SetTimeProgramTest, AMaskGetThatCantBeQueuedEndsTheCall) {
  this->hub_.unqueueable_priority_gets.insert(MASK_ID);
  this->action_.play_complex();

  this->run_panel();

  EXPECT_EQ(this->errors_,
            std::vector<std::string>{failed_prefix(1) + failed_mask_write(WriteFailure::MASK_UNREADABLE)});
  EXPECT_EQ(this->events_, std::vector<std::string>{write_failure_name(WriteFailure::MASK_UNREADABLE)});
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

// AC 10: a read-back GET that can't be queued fails its write as a timeout: nothing was compared, so no on_success.
TEST_F(SetTimeProgramTest, AReadBackGetThatCantBeQueuedFailsItsWrite) {
  this->hub_.unqueueable_priority_gets.insert(time_program_time_id(PAIR, 1));
  this->action_.play_complex();

  this->run_panel();

  EXPECT_EQ(this->errors_, std::vector<std::string>{failed_prefix(1) +
                                                    failed_write(1, TimeProgramField::TIME, WriteFailure::TIMEOUT)});
  EXPECT_EQ(this->successes_, 0);
  EXPECT_FALSE(this->program_.is_bulk_write_running());
}

}  // namespace esphome::sec_touch::testing
