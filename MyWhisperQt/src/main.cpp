#include "app_controller.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLoggingCategory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("MyWhisperQt");
    QApplication::setApplicationVersion("1.0");
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt port of MyWhisper with tray UI, audio transcription, and X11/macOS integrations.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption showDoneScreenOption(
        QStringList() << "show-done-screen",
        "Show the temporary Done overlay after a successful transcription instead of hiding the status overlay immediately."
    );
    QCommandLineOption verboseOption(
        QStringList() << "verbose",
        "Enable verbose debug logging, including Deepgram request/response details."
    );
    parser.addOption(showDoneScreenOption);
    parser.addOption(verboseOption);
    parser.process(app);

    QString loggingRules = QStringLiteral(
        "*.debug=false\n"
        "qt.multimedia=false\n"
        "qt.core.qfuture.continuations.warning=false"
    );
    if (parser.isSet(verboseOption)) {
        loggingRules += QStringLiteral(
            "mywhisper.transcriber.debug=true\n"
            "mywhisper.transcriber.info=true\n"
            "mywhisper.llm_refiner.debug=true\n"
        );
    }
    QLoggingCategory::setFilterRules(loggingRules);

    std::optional<bool> overrideShowDoneScreen;
    if (parser.isSet(showDoneScreenOption)) {
        overrideShowDoneScreen = true;
    }

    if (parser.isSet(verboseOption)) {
        qInfo("Verbose logging enabled.");
    }

    AppController controller(overrideShowDoneScreen);
    return app.exec();
}
