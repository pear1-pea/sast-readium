#include <QtTest/QtTest>

#include <QFileInfo>
#include <QTemporaryDir>

#include "../../app/utils/Logger.h"

class TestLogger : public QObject {
    Q_OBJECT

private slots:
    void absoluteLogFilePathIsNotPrefixedWithDefaultLogDir();
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

QTEST_MAIN(TestLogger)
#include "test_logger.moc"
