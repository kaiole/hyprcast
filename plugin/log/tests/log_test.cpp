#include "hyprcast/log/log_sink.hpp"
#include "hyprcast/log/run_guarded.hpp"
#include "hyprcast/test_support/recording_log.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

namespace {

using namespace hyprcast;
using test_support::recording_log;

// A sink whose backend fails, as Hyprland's logger or notification call could.
class failing_log final : public log_sink {
private:
    void do_write(log_level /*level*/, std::string_view /*message*/) override {
        throw std::runtime_error("log backend failed");
    }

    void do_notify_failure(std::string_view /*message*/) override {
        throw std::runtime_error("notification backend failed");
    }
};

TEST(LogSink, FormatsTheMessageAndKeepsTheLevel) {
    recording_log log;

    log_message(log, log_level::trace, "plain");
    log_message(log, log_level::warning, "{} of {}", 1, "two");
    log_error(log, "failed: {}", "reason");

    ASSERT_EQ(log.entries().size(), 3u);
    EXPECT_EQ(log.entries()[0].level, log_level::trace);
    EXPECT_EQ(log.entries()[0].message, "plain");
    EXPECT_EQ(log.entries()[1].level, log_level::warning);
    EXPECT_EQ(log.entries()[1].message, "1 of two");
    EXPECT_EQ(log.entries()[2].level, log_level::error);
    EXPECT_EQ(log.entries()[2].message, "failed: reason");
    EXPECT_TRUE(log.failures().empty());
}

TEST(LogSink, KeepsFailureNotificationsApartFromTheLog) {
    recording_log log;

    log.notify_failure("reload the plugin");

    EXPECT_TRUE(log.entries().empty());
    ASSERT_EQ(log.failures().size(), 1u);
    EXPECT_EQ(log.failures()[0], "reload the plugin");
}

// Reporting happens on error paths and in noexcept code, so a backend that
// throws must not make things worse.
TEST(LogSink, SwallowsBackendFailures) {
    failing_log log;

    EXPECT_NO_THROW(log.write(log_level::error, "message"));
    EXPECT_NO_THROW(log.notify_failure("message"));
    EXPECT_NO_THROW(log_message(log, log_level::trace, "{}", 1));
    EXPECT_NO_THROW(log_error(log, "{}", 1));
}

TEST(RunGuarded, RunsTheFunctionWithoutRecovering) {
    recording_log log;
    bool ran = false;
    bool recovered = false;

    run_guarded(
        log,
        "context",
        [&ran] { ran = true; },
        [&recovered] noexcept { recovered = true; });

    EXPECT_TRUE(ran);
    EXPECT_FALSE(recovered);
    EXPECT_TRUE(log.entries().empty());
}

TEST(RunGuarded, LogsAnExceptionWithItsContextThenRecovers) {
    recording_log log;
    bool recovered = false;

    run_guarded(
        log,
        "Cannot do the thing",
        [] { throw std::runtime_error("it broke"); },
        [&recovered] noexcept { recovered = true; });

    EXPECT_TRUE(recovered);
    ASSERT_EQ(log.entries().size(), 1u);
    EXPECT_EQ(log.entries()[0].level, log_level::error);
    EXPECT_EQ(log.entries()[0].message, "Cannot do the thing: it broke");
    EXPECT_TRUE(log.failures().empty());
}

TEST(RunGuarded, LogsAnExceptionOfUnknownTypeThenRecovers) {
    recording_log log;
    bool recovered = false;

    run_guarded(
        log,
        "Cannot do the thing",
        // Deliberately not a std::exception.
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        [] { throw 42; },
        [&recovered] noexcept { recovered = true; });

    EXPECT_TRUE(recovered);
    ASSERT_EQ(log.entries().size(), 1u);
    EXPECT_EQ(log.entries()[0].message,
              "Cannot do the thing: unknown exception");
}

} // namespace
