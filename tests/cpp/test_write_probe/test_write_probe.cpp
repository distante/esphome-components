#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "../common/probe_fixture.h"
#include "sec_touch/data_message.h"
#include "sec_touch/protocol_ids.h"
#include "sec_touch_sniffer/property_slots.h"
#include "sec_touch_sniffer/sniffer_data.h"
#include "sec_touch_sniffer/write_probe.h"

using esphome::sec_touch::append_probe_step;
using esphome::sec_touch::is_probe_value;
using esphome::sec_touch::MAX_PROPERTY_ID;
using esphome::sec_touch::PROBE_READBACK_INTERVAL_MS;
using esphome::sec_touch::PROBE_READBACK_WINDOW_MS;
using esphome::sec_touch::PROBE_STEP_CAP;
using esphome::sec_touch::probe_request_from;
using esphome::sec_touch::probe_start_result;
using esphome::sec_touch::ProbeAction;
using esphome::sec_touch::ProbeCommand;
using esphome::sec_touch::ProbeKind;
using esphome::sec_touch::ProbeRequest;
using esphome::sec_touch::ProbeRequestError;
using esphome::sec_touch::ProbeRestore;
using esphome::sec_touch::ProbeResult;
using esphome::sec_touch::ProbeStep;
using esphome::sec_touch::ProbeStepKind;
using esphome::sec_touch::ProbeValue;
using esphome::sec_touch::ProbeVerdict;
using esphome::sec_touch::SCAN_RETRY_DELAY_MS;
using esphome::sec_touch::ScanOutcome;
using esphome::sec_touch::SET_VALUE_CAP;
using esphome::sec_touch::SNIFFER_PROBE_PATH;
using esphome::sec_touch::SUMMER_VENTILATION_PROPERTY_ID;
using esphome::sec_touch::WIRE_ETX;
using esphome::sec_touch::WIRE_STX;
using esphome::sec_touch::WIRE_TAB;
using esphome::sec_touch::WriteProbe;
using sec_touch_test::ORIGINAL;
using sec_touch_test::PROBE_ID;
using sec_touch_test::THIRD;
using sec_touch_test::VALUE;

namespace {

/// How the panel answers one GET or SET.
struct Answer {
  ScanOutcome outcome;
  std::string value;
};

Answer ok(const std::string &value = "") { return Answer{ScanOutcome::SCAN_OUTCOME_OK, value}; }
Answer nak() { return Answer{ScanOutcome::SCAN_OUTCOME_NAK, ""}; }
Answer timeout() { return Answer{ScanOutcome::SCAN_OUTCOME_TIMEOUT, ""}; }

std::vector<Answer> repeat(const Answer &answer, int times) { return std::vector<Answer>(times, answer); }

/// Read-backs at every PROBE_READBACK_INTERVAL_MS with instant answers: the first right after the SET, then one per
/// interval until the window has passed.
constexpr int READBACKS_PER_WINDOW = PROBE_READBACK_WINDOW_MS / PROBE_READBACK_INTERVAL_MS + 1;

/// Plays the panel for a WriteProbe: answers each GET and SET from a script, then with `otherwise`, and logs every
/// command ("GET", "SET <value>", "WAIT <ms>"). Answers take no time; waits advance the clock.
class Panel {
 public:
  WriteProbe probe;
  uint64_t now_ms = 1000;
  std::vector<std::string> log;
  std::deque<Answer> script;
  std::optional<Answer> otherwise;

  Panel &then(const Answer &answer) {
    this->script.push_back(answer);
    return *this;
  }
  Panel &then(const std::vector<Answer> &answers) {
    this->script.insert(this->script.end(), answers.begin(), answers.end());
    return *this;
  }

  void run_probe(const std::string &value) { this->drive(this->probe.start_probe(PROBE_ID, ProbeValue::of(value))); }
  void run_restore(const std::string &original) {
    this->drive(this->probe.start_restore(PROBE_ID, ProbeValue::of(original)));
  }

  const ProbeResult &result() const { return this->probe.result(); }

  /// How many commands of the log start with `prefix`.
  int count(const std::string &prefix) const {
    int n = 0;
    for (const std::string &entry : this->log) {
      n += entry.rfind(prefix, 0) == 0 ? 1 : 0;
    }
    return n;
  }

 protected:
  Answer next_answer() {
    if (!this->script.empty()) {
      const Answer answer = this->script.front();
      this->script.pop_front();
      return answer;
    }
    if (this->otherwise.has_value()) {
      return *this->otherwise;
    }
    ADD_FAILURE() << "the probe sent more than the panel's script answers";
    return timeout();
  }

  void drive(ProbeCommand command) {
    for (int guard = 0; guard < 200; guard++) {
      switch (command.action) {
        case ProbeAction::PROBE_ACTION_FINISHED:
          return;
        case ProbeAction::PROBE_ACTION_GET: {
          this->log.push_back("GET");
          const Answer answer = this->next_answer();
          command = this->probe.on_answer(answer.outcome, answer.value, this->now_ms);
          break;
        }
        case ProbeAction::PROBE_ACTION_SET: {
          this->log.push_back("SET " + std::string(this->probe.set_value()));
          const Answer answer = this->next_answer();
          command = this->probe.on_answer(answer.outcome, "", this->now_ms);
          break;
        }
        case ProbeAction::PROBE_ACTION_WAIT:
          this->log.push_back("WAIT " + std::to_string(command.wait_ms));
          this->now_ms += command.wait_ms;
          command = this->probe.on_wait_over();
          break;
      }
    }
    ADD_FAILURE() << "the probe never finished";
  }
};

std::string step_text(const ProbeStep &step) {
  std::string text = step.kind == ProbeStepKind::PROBE_STEP_GET ? "get " : "set ";
  switch (step.outcome) {
    case ScanOutcome::SCAN_OUTCOME_OK:
      text += "ok";
      break;
    case ScanOutcome::SCAN_OUTCOME_NAK:
      text += "nak";
      break;
    case ScanOutcome::SCAN_OUTCOME_TIMEOUT:
      text += "timeout";
      break;
  }
  return text + (step.has_value ? " " + std::string(step.value.view()) : " null");
}

/// Every recorded step as "<get|set> <outcome> <value|null>".
std::vector<std::string> steps(const ProbeResult &result) {
  std::vector<std::string> out;
  for (uint8_t i = 0; i < result.step_count; i++) {
    out.push_back(step_text(result.steps[i]));
  }
  return out;
}

ProbeRequestError request_error(const std::string &uri, const std::string &body) {
  return probe_request_from(uri, body).error;
}

std::string probe_uri(int id) { return std::string(SNIFFER_PROBE_PATH) + "?id=" + std::to_string(id); }

}  // namespace

// --- The request's 400 rules ---

TEST(WriteProbe, ARequestTakesTheIdAndTheBodyAsTheValue) {
  const ProbeRequest request = probe_request_from(probe_uri(PROBE_ID), VALUE);
  ASSERT_EQ(request.error, ProbeRequestError::PROBE_REQUEST_OK);
  EXPECT_EQ(request.id, PROBE_ID);
  EXPECT_EQ(request.value.view(), VALUE);
}

TEST(WriteProbe, AnEmptyOrPaddedValueIsKeptByteExact) {
  EXPECT_EQ(probe_request_from(probe_uri(PROBE_ID), "").value.view(), "");
  const std::string padded = " " + VALUE + " ";
  EXPECT_EQ(probe_request_from(probe_uri(PROBE_ID), padded).value.view(), padded);
}

TEST(WriteProbe, AValueUpToTheSetCapIsAccepted) {
  EXPECT_EQ(request_error(probe_uri(PROBE_ID), std::string(SET_VALUE_CAP, 'x')), ProbeRequestError::PROBE_REQUEST_OK);
  EXPECT_EQ(request_error(probe_uri(PROBE_ID), std::string(SET_VALUE_CAP + 1, 'x')),
            ProbeRequestError::PROBE_REQUEST_BAD_VALUE);
}

TEST(WriteProbe, AValueWithAFramingByteIsBad) {
  for (char bad : {'\0', static_cast<char>(WIRE_TAB), static_cast<char>(WIRE_STX), static_cast<char>(WIRE_ETX)}) {
    EXPECT_FALSE(is_probe_value(std::string("0") + bad + "1")) << int(bad);
    EXPECT_EQ(request_error(probe_uri(PROBE_ID), std::string("0") + bad + "1"),
              ProbeRequestError::PROBE_REQUEST_BAD_VALUE)
        << int(bad);
  }
}

TEST(WriteProbe, AMissingOrBadIdIsBad) {
  EXPECT_EQ(request_error(std::string(SNIFFER_PROBE_PATH), VALUE), ProbeRequestError::PROBE_REQUEST_BAD_ID);
  EXPECT_EQ(request_error(std::string(SNIFFER_PROBE_PATH) + "?id=", VALUE), ProbeRequestError::PROBE_REQUEST_BAD_ID);
  EXPECT_EQ(request_error(std::string(SNIFFER_PROBE_PATH) + "?id=x", VALUE), ProbeRequestError::PROBE_REQUEST_BAD_ID);
  EXPECT_EQ(request_error(std::string(SNIFFER_PROBE_PATH) + "?id=-1", VALUE), ProbeRequestError::PROBE_REQUEST_BAD_ID);
  EXPECT_EQ(request_error(probe_uri(MAX_PROPERTY_ID + 1), VALUE), ProbeRequestError::PROBE_REQUEST_BAD_ID);
  EXPECT_EQ(request_error(probe_uri(MAX_PROPERTY_ID), VALUE), ProbeRequestError::PROBE_REQUEST_OK);
}

TEST(WriteProbe, AKnownIdIsBad) {
  EXPECT_EQ(request_error(probe_uri(SUMMER_VENTILATION_PROPERTY_ID), VALUE), ProbeRequestError::PROBE_REQUEST_KNOWN_ID);
}

// --- Verdicts ---

TEST(WriteProbe, AValueReadBackIsWritableAndTheOriginalIsRestored) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(ok(VALUE)).then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, (std::vector<std::string>{"GET", "SET " + VALUE, "GET", "SET " + ORIGINAL, "GET"}));
  EXPECT_EQ(steps(panel.result()), (std::vector<std::string>{"get ok " + ORIGINAL, "set ok " + VALUE, "get ok " + VALUE,
                                                             "set ok " + ORIGINAL, "get ok " + ORIGINAL}));
  EXPECT_FALSE(panel.result().active);
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_WRITABLE);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
  EXPECT_TRUE(panel.result().has_original);
  EXPECT_EQ(panel.result().original.view(), ORIGINAL);
}

TEST(WriteProbe, AnAckedSetThatOnlyReadsBackTheOriginalIsNotWritableAndNeedsNoRestore) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
  EXPECT_EQ(panel.count("SET"), 1);
}

TEST(WriteProbe, ANakedSetThatOnlyReadsBackTheOriginalIsNotWritable) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(nak());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
}

TEST(WriteProbe, TheReadBackPollsEveryIntervalUntilTheWindowHasPassed) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.count("GET"), 1 + READBACKS_PER_WINDOW);
  EXPECT_EQ(panel.count("WAIT " + std::to_string(PROBE_READBACK_INTERVAL_MS)), READBACKS_PER_WINDOW - 1);
}

// Failed read-backs are skipped: the ones that answered all show the original.
TEST(WriteProbe, AFailedReadBackAmongOriginalsIsStillNotWritable) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(timeout()).then(nak());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOT_WRITABLE);
}

TEST(WriteProbe, AThirdValueIsUnclearAndRestored) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(repeat(ok(THIRD), READBACKS_PER_WINDOW)).then(ok());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_UNCLEAR);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
  EXPECT_EQ(panel.count("SET " + ORIGINAL), 1);
}

TEST(WriteProbe, EveryReadBackTimedOutIsUnclearAndRestored) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(repeat(timeout(), READBACKS_PER_WINDOW)).then(ok());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_UNCLEAR);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
}

// The retried SET timed out too: unclear. The last read-back shows the original, so there is nothing to restore.
TEST(WriteProbe, ASetThatTimedOutTwiceIsUnclearAndSkipsARestoreAtTheOriginal) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(timeout()).then(timeout());
  panel.otherwise = ok(ORIGINAL);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_UNCLEAR);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
  EXPECT_EQ(panel.count("SET " + VALUE), 2);
}

TEST(WriteProbe, AnUnclearRunEndingOffTheOriginalIsRestored) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(repeat(ok(ORIGINAL), READBACKS_PER_WINDOW - 1)).then(ok(THIRD));
  panel.then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_UNCLEAR);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
}

// --- Nothing written ---

TEST(WriteProbe, ANakedOriginalWritesNothing) {
  Panel panel;
  panel.then(nak());

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, std::vector<std::string>{"GET"});
  EXPECT_EQ(steps(panel.result()), std::vector<std::string>{"get nak null"});
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
  EXPECT_FALSE(panel.result().has_original);
}

TEST(WriteProbe, AnOriginalThatTimesOutTwiceWritesNothing) {
  Panel panel;
  panel.then(timeout()).then(timeout());

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, (std::vector<std::string>{"GET", "WAIT " + std::to_string(SCAN_RETRY_DELAY_MS), "GET"}));
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
}

TEST(WriteProbe, AnOriginalAnsweredOnItsRetryGoesOn) {
  Panel panel;
  panel.then(timeout()).then(ok(ORIGINAL)).then(ok()).then(ok(VALUE)).then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_WRITABLE);
  EXPECT_EQ(steps(panel.result()).front(), "get timeout null");
}

TEST(WriteProbe, AnOriginalEqualToTheValueWritesNothing) {
  Panel panel;
  panel.then(ok(VALUE));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, std::vector<std::string>{"GET"});
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN);
  EXPECT_EQ(panel.result().original.view(), VALUE);
}

// A SET could never restore an original longer than SET_VALUE_CAP, so nothing is written.
TEST(WriteProbe, AnOriginalTooLongToRestoreWritesNothing) {
  const std::string padded = "00" + std::string(SET_VALUE_CAP, ' ');
  Panel panel;
  panel.then(ok(padded));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, std::vector<std::string>{"GET"});
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NOTHING_WRITTEN);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_SKIPPED);
  EXPECT_EQ(panel.result().original.view(), padded);
}

// --- The SET and its retry ---

TEST(WriteProbe, ATimedOutSetIsRetriedOnceAfterTheScanRetryWait) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(timeout()).then(ok()).then(ok(VALUE)).then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.log, (std::vector<std::string>{"GET", "SET " + VALUE, "WAIT " + std::to_string(SCAN_RETRY_DELAY_MS),
                                                 "SET " + VALUE, "GET", "SET " + ORIGINAL, "GET"}));
  EXPECT_EQ(steps(panel.result())[1], "set timeout " + VALUE);
  EXPECT_EQ(steps(panel.result())[2], "set ok " + VALUE);
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_WRITABLE);
}

TEST(WriteProbe, NoVerdictWhileTheProbeRuns) {
  WriteProbe probe;
  probe.start_probe(PROBE_ID, ProbeValue::of(VALUE));
  probe.on_answer(ScanOutcome::SCAN_OUTCOME_OK, ORIGINAL, 0);
  probe.on_answer(ScanOutcome::SCAN_OUTCOME_OK, "", 0);
  probe.on_answer(ScanOutcome::SCAN_OUTCOME_OK, VALUE, 0);

  EXPECT_TRUE(probe.result().active);
  EXPECT_EQ(probe.result().verdict, ProbeVerdict::PROBE_VERDICT_NONE);
  EXPECT_EQ(probe.result().restore, ProbeRestore::PROBE_RESTORE_NONE);
}

// --- The restore ---

TEST(WriteProbe, ATimedOutRestoreSetIsRetriedOnce) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(ok(VALUE)).then(timeout()).then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.count("SET " + ORIGINAL), 2);
  EXPECT_EQ(panel.count("WAIT " + std::to_string(SCAN_RETRY_DELAY_MS)), 1);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
}

TEST(WriteProbe, ARestoreNotVerifiedIsSentOnceMoreAndCanStillSucceed) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok()).then(ok(VALUE));
  panel.then(ok()).then(repeat(ok(VALUE), READBACKS_PER_WINDOW));
  panel.then(ok()).then(ok(ORIGINAL));

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.count("SET " + ORIGINAL), 2);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
}

TEST(WriteProbe, ARestoreNotVerifiedTwiceFailsAndKeepsTheValueItIsAt) {
  Panel panel;
  panel.then(ok(ORIGINAL)).then(ok());
  panel.otherwise = ok(VALUE);

  panel.run_probe(VALUE);

  EXPECT_EQ(panel.count("SET " + ORIGINAL), 2);
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_WRITABLE);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_FAILED);
  ProbeValue now_at;
  ASSERT_TRUE(panel.probe.last_read(now_at));
  EXPECT_EQ(now_at.view(), VALUE);
}

TEST(WriteProbe, ARestoreRequestSetsAndVerifiesTheRequestedOriginal) {
  Panel panel;
  panel.then(ok()).then(ok(ORIGINAL));

  panel.run_restore(ORIGINAL);

  EXPECT_EQ(panel.log, (std::vector<std::string>{"SET " + ORIGINAL, "GET"}));
  EXPECT_EQ(panel.result().kind, ProbeKind::PROBE_KIND_RESTORE);
  EXPECT_EQ(panel.result().value.view(), ORIGINAL);
  EXPECT_EQ(panel.result().original.view(), ORIGINAL);
  EXPECT_EQ(panel.result().verdict, ProbeVerdict::PROBE_VERDICT_NONE);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_OK);
}

TEST(WriteProbe, ARestoreRequestCanFail) {
  Panel panel;
  panel.otherwise = ok(THIRD);

  panel.run_restore(ORIGINAL);

  EXPECT_EQ(panel.count("SET " + ORIGINAL), 2);
  EXPECT_EQ(panel.result().restore, ProbeRestore::PROBE_RESTORE_FAILED);
}

// --- The result ---

TEST(WriteProbe, AStartResultHasNoStepsAndAnOriginalOnlyForARestore) {
  const ProbeResult probe = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of(VALUE));
  EXPECT_TRUE(probe.active);
  EXPECT_EQ(probe.step_count, 0);
  EXPECT_FALSE(probe.has_original);

  const ProbeResult restore = probe_start_result(ProbeKind::PROBE_KIND_RESTORE, PROBE_ID, ProbeValue::of(ORIGINAL));
  EXPECT_TRUE(restore.has_original);
  EXPECT_EQ(restore.original.view(), ORIGINAL);
}

TEST(WriteProbe, StepsPastTheCapAreNotRecorded) {
  ProbeResult result = probe_start_result(ProbeKind::PROBE_KIND_PROBE, PROBE_ID, ProbeValue::of(VALUE));
  const ProbeStep step{ProbeStepKind::PROBE_STEP_GET, ScanOutcome::SCAN_OUTCOME_OK, true, ProbeValue::of(ORIGINAL), 0};
  for (size_t i = 0; i < PROBE_STEP_CAP + 3; i++) {
    append_probe_step(result, step);
  }
  EXPECT_EQ(result.step_count, PROBE_STEP_CAP);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
