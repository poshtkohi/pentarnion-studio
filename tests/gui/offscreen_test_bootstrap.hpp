#pragma once
#include <QApplication>
#include <QTest>
#include <cstdio>

namespace penta::test {
inline bool requireOffscreenTestPlatform(int argc,char* argv[]) {
    bool safe=qEnvironmentVariable("QT_QPA_PLATFORM").section(':',0,0)==QStringLiteral("offscreen");
    for (int i=1;i<argc;++i) {
        const auto argument=QString::fromLocal8Bit(argv[i]);
        if (argument==QStringLiteral("-platform") || argument==QStringLiteral("--platform")) {
            if (i+1>=argc || QString::fromLocal8Bit(argv[++i]).section(':',0,0)!=QStringLiteral("offscreen")) safe=false;
        } else if (argument.startsWith(QStringLiteral("-platform=")) || argument.startsWith(QStringLiteral("--platform="))) {
            if (argument.section('=',1).section(':',0,0)!=QStringLiteral("offscreen")) safe=false;
        }
    }
    if (!safe) std::fputs("GUI tests require QT_QPA_PLATFORM=offscreen; desktop graphics execution is disabled.\n",stderr);
    return safe;
}
}

// Check before constructing QApplication: command-line environment overrides and
// historical native QA opt-ins must never select a desktop graphics platform.
#define PENTA_OFFSCREEN_TEST_MAIN(TestClass) \
int main(int argc,char* argv[]) { \
    if (!penta::test::requireOffscreenTestPlatform(argc,argv)) return 77; \
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); \
    QApplication application(argc,argv); \
    if (QApplication::platformName()!=QStringLiteral("offscreen")) return 77; \
    TestClass tests;return QTest::qExec(&tests,argc,argv); \
}
