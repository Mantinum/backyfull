#include "core/Scheduler.h" // Adjust path as necessary
#include "gtest/gtest.h"
#include <QCoreApplication> // Required by Qt classes, even in tests
#include <QFile>            // For QFile::remove
#include <QSignalSpy>       // For testing signals
#include <QTemporaryFile>   // To create a temporary settings file

// Test fixture for Scheduler tests
class SchedulerTest : public ::testing::Test {
protected:
  QTemporaryFile tempSettingsFile_; // Will provide a unique temporary file name
  Scheduler *scheduler_;

  // We need a QCoreApplication instance for QSettings and QTimer to work.
  // GTest doesn't run a Qt event loop by default.
  // For tests involving QTimer, more advanced setup (QEventLoop) might be
  // needed. For now, focus on QSettings and property logic.
  static QCoreApplication *appInstance;
  static int argc_;     // Dummy argc
  static char *argv_[]; // Dummy argv

public:
  static void SetUpTestSuite() {
    // Create a QCoreApplication instance once for all tests in this suite
    // This is necessary for QSettings to function correctly, especially if not
    // using a specific file path initially. And for QObject-based classes like
    // QTimer.
    if (!QCoreApplication::instance()) {
      // For tests, ensure argc_ is at least 1 and argv_[0] is a valid string if
      // QCoreApplication uses them. Simplified for GTest: often just 0 and
      // nullptr are fine if not parsing actual command line. However, some Qt
      // versions might require a valid argv[0].
      static char app_name[] = "SchedulerTest"; // Dummy app name
      argc_ = 1;
      argv_[0] = app_name; // Provide a dummy app name
      argv_[1] = nullptr;  // Null-terminate argv

      appInstance = new QCoreApplication(argc_, argv_);
      // Use specific names for testing to avoid interfering with user's actual
      // app settings
      QCoreApplication::setOrganizationName("BackyFullTestOrg");
      QCoreApplication::setApplicationName("BackyFullTestApp");
    }
  }

  static void TearDownTestSuite() {
    delete appInstance;
    appInstance = nullptr;
  }

protected:
  void SetUp() override {
    // Open the temporary file to get its name, then close it.
    // QSettings will create/overwrite it.
    ASSERT_TRUE(tempSettingsFile_.open());
    QString settingsFilePath = tempSettingsFile_.fileName();
    tempSettingsFile_.close(); // QSettings will manage the file itself.

    scheduler_ = new Scheduler(settingsFilePath); // Pass temp file path
  }

  void TearDown() override {
    delete scheduler_;
    scheduler_ = nullptr;
    // QTemporaryFile will delete the file when it goes out of scope if it still
    // exists. To be absolutely sure it's gone for the next test (if it was
    // recreated by QSettings):
    QFile::remove(tempSettingsFile_.fileName());
  }
};

// Initialize static members
QCoreApplication *SchedulerTest::appInstance = nullptr;
// Ensure argc_ and argv_ are defined.
// For safety, initialize argv_ to point to a char array that can hold at least
// one string + nullptr. Or, if QCoreApplication only needs argc=0,
// argv=nullptr, that's simpler. Let's refine based on common practice: minimal
// argc/argv for QCoreApplication if not parsing.
int SchedulerTest::argc_ = 0;
char *SchedulerTest::argv_[] = {nullptr};

TEST_F(SchedulerTest, InitialState) {
  // Scheduler loads task on construction.
  // Default task (if settings file is new/empty) should be:
  // - Empty source/destination paths
  // - Default time (e.g., 23:00)
  // - Disabled
  EXPECT_EQ(scheduler_->sourcePath(), QString(""));
  EXPECT_EQ(scheduler_->destinationPath(), QString(""));
  ASSERT_EQ(scheduler_->scheduledTimes().size(), 1);
  EXPECT_EQ(scheduler_->scheduledTimes().first(), QTime(23, 0));
  EXPECT_FALSE(scheduler_->isEnabled());
}

TEST_F(SchedulerTest, SetAndGetTaskProperties) {
  QString src = "/test/source";
  QString dst = "/test/destination";
  QTime time(10, 30);
  ScheduleEntry se{time, {}};
  bool enabled = true;

  QSignalSpy taskChangedSpy(scheduler_, &Scheduler::taskChanged);

  scheduler_->setDailyBackupTask(src, dst, QList<ScheduleEntry>{se}, enabled,
                                 false, QString(), 0, QString(), QString());

  EXPECT_EQ(scheduler_->sourcePath(), src);
  EXPECT_EQ(scheduler_->destinationPath(), dst);
  ASSERT_EQ(scheduler_->scheduleEntries().size(), 1);
  EXPECT_EQ(scheduler_->scheduleEntries().first().time, time);
  EXPECT_EQ(scheduler_->isEnabled(), enabled);
  ASSERT_GE(taskChangedSpy.count(),
            1); // taskChanged should be emitted at least once
                // (could be twice if loadTask also emits and changes state)
                // Let's assume setDailyBackupTask is the primary emitter here.
                // If loadTask always emits, this might need adjustment or
                // specific spy setup. For this test, 1 emission directly from
                // setDailyBackupTask is key.
  // To be more precise if loadTask always emits:
  // taskChangedSpy.clear(); // Clear any emissions from loadTask during setup
  // scheduler_->setDailyBackupTask(src, dst, time, enabled);
  // EXPECT_EQ(taskChangedSpy.count(), 1);
  // For simplicity, assuming current Scheduler logic where loadTask might emit,
  // and setDailyBackupTask also emits. The test as is checks if *a* change
  // happened. If setDailyBackupTask itself guarantees one emission, this is
  // fine.
}

TEST_F(SchedulerTest, SaveAndLoadTask) {
  QString src = "/another/source";
  QString dst = "/another/destination";
  QTime time(14, 45);
  ScheduleEntry se{time, {}};
  bool enabled = true;

  scheduler_->setDailyBackupTask(src, dst, QList<ScheduleEntry>{se}, enabled,
                                 false, QString(), 0, QString(), QString());
  // Task is saved by setDailyBackupTask.

  // Create a new Scheduler instance using the same settings file
  // to verify persistence.
  Scheduler scheduler2(tempSettingsFile_.fileName());

  EXPECT_EQ(scheduler2.sourcePath(), src);
  EXPECT_EQ(scheduler2.destinationPath(), dst);
  ASSERT_EQ(scheduler2.scheduleEntries().size(), 1);
  EXPECT_EQ(scheduler2.scheduleEntries().first().time, time);
  EXPECT_EQ(scheduler2.isEnabled(), enabled);
}

TEST_F(SchedulerTest, DisableTask) {
  QString src = "/path/src";
  QString dst = "/path/dst";
  QTime time(12, 00);
  ScheduleEntry se{time, {}};

  scheduler_->setDailyBackupTask(src, dst, QList<ScheduleEntry>{se}, true,
                                 false, QString(), 0, QString(),
                                 QString()); // Enable first
  ASSERT_TRUE(scheduler_->isEnabled());

  scheduler_->setDailyBackupTask(src, dst, QList<ScheduleEntry>{se}, false,
                                 false, QString(), 0, QString(),
                                 QString()); // Then disable
  EXPECT_FALSE(scheduler_->isEnabled());

  // Verify persistence of disabled state
  Scheduler scheduler2(tempSettingsFile_.fileName());
  EXPECT_FALSE(scheduler2.isEnabled());
  EXPECT_EQ(scheduler2.sourcePath(), src); // Other params should still be there
}

TEST_F(SchedulerTest, HandlesEmptyPathsOnSet) {
  // Setting empty paths should be stored as such
  ScheduleEntry se{QTime(1, 1), {}};
  scheduler_->setDailyBackupTask("", "", QList<ScheduleEntry>{se}, true, false,
                                 QString(), 0, QString(), QString());
  EXPECT_EQ(scheduler_->sourcePath(), QString(""));
  EXPECT_EQ(scheduler_->destinationPath(), QString(""));

  Scheduler scheduler2(tempSettingsFile_.fileName());
  EXPECT_EQ(scheduler2.sourcePath(), QString(""));
  EXPECT_EQ(scheduler2.destinationPath(), QString(""));
}

// Note: Testing QTimer-based signal emission (backupTaskTriggered) is more
// complex as it requires an active event loop and control over time. This might
// involve QTestLib or more intricate QEventLoop spinning in tests. For M1,
// focusing on state/QSettings is a good start.

// Example of a placeholder for timer test (would likely fail or hang without
// event loop)
/*
#include <QtTest/QTest> // For QTest::qWait, if using QTestLib features

TEST_F(SchedulerTest, DISABLED_BackupSignalEmission) {
    if (!qgetenv("CI").isEmpty()) { // Or some other way to detect CI
        GTEST_SKIP() << "Skipping timer test in CI environment without full Qt
event loop setup.";
    }

    QSignalSpy spy(scheduler_, &Scheduler::backupTaskTriggered);
    QString src = "/test/source_signal";
    QString dst = "/test/dest_signal";
    // Schedule for a very short time in the future
    // QTime scheduledTime = QTime::currentTime().addMSecs(200); // 200 ms
    // For more reliability, schedule for a slightly longer, fixed time from a
known point
    // or mock QDateTime::currentDateTime() if possible (hard without a
dedicated mocking framework for Qt types).
    // Let's assume test runs reasonably fast.

    // Need to ensure the event loop runs. This is tricky with GTest alone.
    // If QCoreApplication is running, events might be processed, but timers
need active processing.

    // This part is highly dependent on the test runner and Qt integration.
    // A simple QTest::qWait might work if QTEST_MAIN is used or similar context
is set up.
    // QTest::qWait(300);

    // Alternative using QEventLoop (more GTest-idiomatic if not using
QTestLib's runner): QEventLoop loop; QTimer checkTimer; // Timer to periodically
check spy or exit loop int maxWaitLoops = 10; // Max 1 second wait (10 * 100ms)

    scheduler_->setDailyBackupTask(src, dst, QTime::currentTime().addMSecs(50),
true); // Schedule very soon

    QObject::connect(&checkTimer, &QTimer::timeout, [&]() {
        if (spy.count() > 0 || maxWaitLoops-- <= 0) {
            loop.quit();
        }
    });
    checkTimer.start(100); // Check every 100ms
    loop.exec(); // This will block until loop.quit() is called
    checkTimer.stop();


    ASSERT_GE(spy.count(), 1) << "Backup signal was not emitted.";
    if (spy.count() > 0) {
        QList<QVariant> arguments = spy.takeFirst();
        EXPECT_EQ(arguments.at(0).toString(), src);
        EXPECT_EQ(arguments.at(1).toString(), dst);
    }
}
*/

// It's good practice to have a main for test executables if not using a global
// test runner from CMake. However, CMake with GTest usually handles this. For
// Qt tests, especially those needing an event loop, QTEST_MAIN or a custom main
// with QApplication might be needed.
// For now, this structure assumes basic GTest execution. If timer tests are
// added, the test execution setup might need QTestLib integration.
