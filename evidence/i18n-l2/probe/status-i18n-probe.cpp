// T27 i18n probe: the F8 diagnostic status line in the REAL product shell, zh_CN.
//
// The G7 report (§7.7) left one user-visible surface English: the F8/Shift+F8
// status-bar message composed by `describeDiagnostic()` in
// `src/editor/kernel/lsp_actions.cpp`, plus the words `diagnosticsSeverityName()`
// embeds (`error` / `warning` / `information` / `hint` / `problem`). T27 wrapped
// both, so this probe drives the ACTUAL product path instead of asserting a
// translation table entry:
//
//   ApplicationShell + real EditCoreProvider -> open a file (a real
//   KTextEditor::View, whose construction installs DiagnosticsAnnotations and
//   DiagnosticNavigator) -> push two annotations -> DiagnosticNavigator::navigate()
//   -> read QMainWindow::statusBar()->currentMessage()
//
// The message it reads is therefore the exact text a user sees in zh_CN after F8.
// Two screenshots are written: the shell window and the status-bar strip.
//
// argv[1] = build translations directory (<build>/src/app/translations),
// argv[2] = screenshot directory. Exit 0 = every check passed.
#include "app_shell.h"
#include "diagnostics_annotations.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "i18n.h"
#include "integration.h"
#include "ktexteditor_provider.h"
#include "lsp_actions.h"

#include <KTextEditor/View>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStatusBar>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

bool hasCjk(const QString &text)
{
    for (const QChar ch : text) {
        if (ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fff) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-i18n-status-probe"));

    const QString translationsDir = argc > 1 && *argv[1] != '\0'
                                        ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
                                        : QString();
    const QString shotsDir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (translationsDir.isEmpty() || !QFileInfo(translationsDir).isDir()) {
        std::printf("FATAL: pass the build translations directory as argv[1]\n");
        return 2;
    }
    QDir().mkpath(shotsDir);

    QTemporaryDir stateDir;
    check("probe.temp-state", stateDir.isValid());
    const QString settingsPath = stateDir.path() + QStringLiteral("/settings.toml");
    {
        QFile file(settingsPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("FATAL: cannot write %s\n", qPrintable(settingsPath));
            return 2;
        }
        file.write("\"ui.language\" = \"zh_CN\"\n");
    }

    // Startup order of the product: install the translators, THEN build the shell.
    const vedit::app::i18n::InstallResult language =
        vedit::app::i18n::installTranslations(settingsPath, translationsDir);
    std::printf("     language: setting=%s tag=%s vedit=%s\n",
                qPrintable(language.setting), qPrintable(language.tag), qPrintable(language.qmPath));
    check("i18n.selection", language.setting == QLatin1String("zh_CN")
                                && language.translated
                                && language.qtbaseLoaded,
          language.setting + QStringLiteral("/") + QFileInfo(language.qmPath).fileName());

    const auto tr = [](const char *source) {
        return QCoreApplication::translate("VEdit", source);
    };
    // The five words diagnosticsSeverityName() can embed in the status line.
    check("text.severity-words",
          tr("error") == QString::fromUtf8("错误") && tr("warning") == QString::fromUtf8("警告")
              && tr("information") == QString::fromUtf8("信息")
              && tr("hint") == QString::fromUtf8("提示") && tr("problem") == QString::fromUtf8("问题"),
          tr("error") + QLatin1Char('/') + tr("warning") + QLatin1Char('/') + tr("information")
              + QLatin1Char('/') + tr("hint") + QLatin1Char('/') + tr("problem"));
    check("text.status-template",
          tr("Diagnostic %1/%2 - %3: %4").arg(1).arg(2).arg(tr("error"), QStringLiteral("x"))
              == QString::fromUtf8("诊断 1/2 - 错误：x"),
          tr("Diagnostic %1/%2 - %3: %4").arg(1).arg(2).arg(tr("error"), QStringLiteral("x")));

    // --- the REAL shell, with the REAL provider -------------------------------
    vedit::app::ShellOptions options;
    options.stateDir = stateDir.path();
    options.projectRoot = QDir::currentPath();
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(options);
    // main.cpp installs the edit-core provider itself; mirror that line so the
    // pane holds a real KTextEditor::View (and therefore the real navigator).
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = options.projectRoot;
    integrationOptions.stateDir = stateDir.path();
    vedit::workspace::installPanels(shell, integrationOptions);
    shell.resize(1500, 950);
    shell.show();
    QCoreApplication::processEvents();

    const QString sourcePath = stateDir.path() + QStringLiteral("/status_probe.sv");
    {
        QFile source(sourcePath);
        if (!source.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("FATAL: cannot write %s\n", qPrintable(sourcePath));
            return 2;
        }
        source.write("module m;\n  wire unused;\n  assign bad = 1'bx;\nendmodule\n");
    }
    vedit::app::EditorPane *pane = shell.editorArea()->ensurePane();
    bool opened = false;
    if (pane != nullptr) {
        opened = pane->openFile(sourcePath);
    }
    for (int tick = 0; tick < 100 && !opened; ++tick) {
        QCoreApplication::processEvents();
        opened = pane != nullptr && pane->currentFilePath() == sourcePath;
    }
    QCoreApplication::processEvents();
    check("status.editor-opened", opened,
          pane != nullptr ? pane->currentFilePath() : QStringLiteral("no pane"));

    KTextEditor::View *view =
        pane != nullptr ? vedit::editor::viewFor(pane->editorWidget()) : nullptr;
    check("status.real-view", view != nullptr);

    vedit::editor::DiagnosticNavigator *navigator =
        view != nullptr ? vedit::editor::DiagnosticNavigator::instanceFor(view) : nullptr;
    check("status.navigator-installed", navigator != nullptr);

    QList<vedit::editor::DiagnosticAnnotation> entries;
    {
        vedit::editor::DiagnosticAnnotation error;
        error.line = 1;
        error.severity = 1;
        error.message = QStringLiteral("unused signal 'unused'");
        error.rule = QStringLiteral("unused-signal");
        entries.append(error);
        vedit::editor::DiagnosticAnnotation warning;
        warning.line = 2;
        warning.severity = 2;
        warning.message = QStringLiteral("constant select");
        entries.append(warning);
    }
    if (view != nullptr) {
        vedit::editor::DiagnosticsAnnotations::setForView(view, entries);
    }
    QCoreApplication::processEvents();

    // F8 once: the FIRST diagnostic. The status bar text is describeDiagnostic()'s.
    if (navigator != nullptr) {
        navigator->navigate(true);
    }
    QCoreApplication::processEvents();
    const QString first =
        shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString();
    check("text.status-bar.error",
          first.contains(QString::fromUtf8("诊断 1/2"))
              && first.contains(QString::fromUtf8("错误"))
              && first.contains(QStringLiteral("unused signal 'unused'")) && hasCjk(first)
              && !first.contains(QLatin1String("Diagnostic"))
              && !first.contains(QLatin1String("error")),
          first);

    if (shell.statusBar() != nullptr) {
        shell.statusBar()->grab().save(shotsDir + QStringLiteral("/11-status-bar-zh_CN.png"));
    }
    shell.grab().save(shotsDir + QStringLiteral("/10-shell-status-zh_CN.png"));
    check("shot.status-bar",
          QFileInfo::exists(shotsDir + QStringLiteral("/11-status-bar-zh_CN.png"))
              && QFileInfo::exists(shotsDir + QStringLiteral("/10-shell-status-zh_CN.png")),
          shotsDir);

    // F8 again: the SECOND diagnostic, a warning — the severity word changes.
    if (navigator != nullptr) {
        navigator->navigate(true);
    }
    QCoreApplication::processEvents();
    const QString second =
        shell.statusBar() != nullptr ? shell.statusBar()->currentMessage() : QString();
    check("text.status-bar.warning",
          second.contains(QString::fromUtf8("诊断 2/2"))
              && second.contains(QString::fromUtf8("警告")) && hasCjk(second)
              && !second.contains(QLatin1String("warning")),
          second);

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
