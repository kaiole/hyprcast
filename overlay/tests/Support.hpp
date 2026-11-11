// What every overlay test executable shares: a main that keeps the test away
// from the live session, fatal assertions that end the test from anywhere, and
// waiting on the event loop.
#pragma once

#include <gtest/gtest.h>

// QTest::qWaitFor and QTest::qWait are part of QtCore. <QTest> is the QtTest
// module, which these tests do not otherwise need.
#include <QtCore/qtestsupport_core.h>

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <iostream>
#include <ostream>
#include <utility>

// GoogleTest finds these through the Qt types, which live in the global
// namespace. Without them a failed comparison prints the bytes of the object.
inline void PrintTo(const QString& value, std::ostream* out) {
    *out << testing::PrintToString(value.toStdString());
}
inline void PrintTo(const QByteArray& value, std::ostream* out) {
    *out << testing::PrintToString(value.toStdString());
}

namespace hyprcast::overlay::tests {

// Long enough for a sanitizer build on a busy machine. A wait that succeeds
// returns at once, so only a failing test pays for it.
constexpr int WaitTimeoutMs = 15'000;

// Runs the event loop until `predicate` holds or the time is up, and returns
// whether it holds.
template <typename Predicate>
[[nodiscard]] bool waitFor(Predicate predicate, int timeoutMs = WaitTimeoutMs) {
    return QTest::qWaitFor(std::move(predicate), timeoutMs);
}

// Runs the event loop for `ms`, for a test that has to show that something
// does not happen.
inline void runEventLoopFor(int ms) {
    QTest::qWait(ms);
}

// ASSERT_* only returns from the function it is in. This ends the whole test
// instead, so a helper can assert and its caller never runs on with a state
// the helper has rejected. GoogleTest catches the exception and has recorded
// the failure by then.
//
// An assertion in a slot that the event loop calls must stay EXPECT_*: Qt does
// not let an exception pass through it.
class ThrowOnFatalFailure : public testing::EmptyTestEventListener {
public:
    void OnTestPartResult(const testing::TestPartResult& result) override {
        if (result.type() == testing::TestPartResult::kFatalFailure) {
            throw testing::AssertionException(result);
        }
    }
};

// Points the session directories at `root`, for this process and those it
// starts: the runtime directory is where the plugin's socket lives, and the
// overlay reads its config and themes from the others. Also drops what would
// let Qt or the overlay find the running compositor.
inline void isolateFromSession(const QString& root) {
    qputenv("XDG_RUNTIME_DIR", root.toUtf8());
    qputenv("XDG_CONFIG_HOME", (root + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (root + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (root + QStringLiteral("/cache")).toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    for (const char* variable : {
             "HYPRLAND_INSTANCE_SIGNATURE",
             "WAYLAND_DISPLAY",
             "WAYLAND_SOCKET",
             "DISPLAY",
         }) {
        qunsetenv(variable);
    }
}

// The body of main: runs the tests of this executable under `Application`,
// which is QCoreApplication or QGuiApplication.
template <typename Application>
int run(int argc, char** argv) {
    // First, so that Qt never sees the GoogleTest flags.
    testing::InitGoogleTest(&argc, argv);
    const QTemporaryDir session;
    if (!session.isValid()) {
        std::cerr << "cannot create a temporary session directory\n";
        return 1;
    }
    isolateFromSession(session.path());
    const Application application(argc, argv);
    // Last, so that the printer reports the failure before this throws.
    testing::UnitTest::GetInstance()->listeners().Append(
        new ThrowOnFatalFailure);
    return RUN_ALL_TESTS();
}

} // namespace hyprcast::overlay::tests
