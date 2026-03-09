#include "app_controller.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLoggingCategory>

int main(int argc, char *argv[]) {
    QLoggingCategory::setFilterRules(
        "qt.multimedia.ffmpeg.info=false\n"
        "qt.multimedia.ffmpeg.warning=false\n"
        "qt.core.qfuture.continuations.warning=false"
    );

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
    parser.addOption(showDoneScreenOption);
    parser.process(app);

    std::optional<bool> overrideShowDoneScreen;
    if (parser.isSet(showDoneScreenOption)) {
        overrideShowDoneScreen = true;
    }

    AppController controller(overrideShowDoneScreen);
    return app.exec();
}
