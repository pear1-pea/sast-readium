#include <QtTest/QtTest>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "../../app/utils/Logger.h"

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    void absoluteLogFilePathIsNotPrefixedWithDefaultLogDir();
    void bareLogFileNameUsesAppDataLogDirectory();
};

void TestLogger::absoluteLogFilePathIsNotPrefixedWithDefaultLogDir() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString logFilePath = tempDir.path() + "/logs/sast-readium.log";

    Logger::LoggerConfig config;
    config.enableConsole = false;
    config.enableRotatingFile = true;
    config.enableQtWidget = false;
    config.logFileName = logFilePath;

    Logger& logger = Logger::instance();
    logger.initialize(config);
    logger.info(QStringLiteral("logger path resolution test"));
    QVERIFY(logger.getSpdlogLogger() != nullptr);
    logger.getSpdlogLogger()->flush();

    QVERIFY2(QFileInfo::exists(logFilePath), qPrintable(logFilePath));
}

void TestLogger::bareLogFileNameUsesAppDataLogDirectory() {
    QStandardPaths::setTestModeEnabled(true);

    const QString resolved =
        Logger::resolveLogFilePath(QStringLiteral("unit-test.log"));
    const QString expected =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/logs/unit-test.log");

    QCOMPARE(QDir::cleanPath(resolved), QDir::cleanPath(expected));
}

QTEST_MAIN(TestLogger)
#include "test_logger.moc"
