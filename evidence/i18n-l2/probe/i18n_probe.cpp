// T38 / G7 i18n L2 acceptance probe: the REAL product shell in zh_CN.
//
// It answers the three observable acceptance questions with TEXT evidence (not a
// screenshot-only claim):
//
//   AC-9.1  panel titles / toolbar buttons / the search placeholder /
//           diagnostic text are Chinese in the zh_CN interface.
//   T40     the surfaces T38's probe did not assert: the diagnostics-panel
//           STATUS LINE, the project dock title, the project panel header (on a
//           real ProjectModel), and every F1/F2 string T40 wrapped.
//   AC-9.3  Qt's OWN standard-dialog translations are shipped and LOADED:
//           `installTranslations` reports qtbaseLoaded and a real
//           QDialogButtonBox paints 确定/取消/保存/打开.
//   AC-9.2  (the discovery/reconciliation tables are evidence files, not this
//           program) - the corner of it verified here is that the .ts/.qm the
//           build produced actually carries the B-class strings.
//
// The probe installs the translator BEFORE constructing ApplicationShell, exactly
// like src/app/main.cpp does at startup, then reads the widgets the user sees.
//
// argv[1] = build translations directory (<build>/src/app/translations),
// argv[2] = screenshot directory. Exit 0 = every check passed.
#include "app_shell.h"
#include "diagnostics_panel.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "file_tree_widget.h"
#include "i18n.h"
#include "integration.h"
#include "module_tree/module_tree_panel.h"
#include "panel_registry.h"
#include "project_panel.h"
#include "project_model.h"
#include "search_panel.h"
#include "terminal_panel.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>

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

QStringList widgetTexts(const QWidget *root)
{
    QStringList texts;
    if (root == nullptr) {
        return texts;
    }
    for (QToolButton *button : root->findChildren<QToolButton *>()) {
        if (!button->text().isEmpty()) {
            texts.append(button->text());
        }
    }
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (!button->text().isEmpty()) {
            texts.append(button->text());
        }
    }
    return texts;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-i18n-l2-probe"));

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
    // The Rust settings store writes dotted keys as quoted TOML keys; the i18n
    // reader accepts the quoted spelling (and a bare one) - use the real form.
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
    std::printf("     language: setting=%s tag=%s vedit=%s qtbase=%s\n",
                qPrintable(language.setting), qPrintable(language.tag),
                qPrintable(language.qmPath), qPrintable(language.qtbasePath));
    check("i18n.selection", language.setting == QLatin1String("zh_CN"));
    check("i18n.vedit-loaded", language.translated,
          QFileInfo(language.qmPath).fileName());
    check("i18n.vedit-file",
          QFileInfo(language.qmPath).fileName() == QLatin1String("vedit_zh_CN.qm"),
          language.qmPath);
    check("i18n.qtbase-loaded", language.qtbaseLoaded,
          language.qtbasePath.isEmpty() ? QStringLiteral("no qtbase_zh_CN.qm")
                                        : QFileInfo(language.qtbasePath).fileName());
    check("i18n.qtbase-file",
          QFileInfo(language.qtbasePath).fileName() == QLatin1String("qtbase_zh_CN.qm"),
          language.qtbasePath);

    // --- AC-9.1: the strings the panels/buttons/placeholders actually use ------
    const auto tr = [](const char *source) {
        return QCoreApplication::translate("VEdit", source);
    };
    check("text.panel-title.files", tr("Files") == QString::fromUtf8("文件"), tr("Files"));
    check("text.panel-title.search", tr("Search") == QString::fromUtf8("搜索"), tr("Search"));
    check("text.panel-title.terminal", tr("Terminal") == QString::fromUtf8("终端"),
          tr("Terminal"));
    check("text.panel-title.outline", tr("Outline") == QString::fromUtf8("大纲"), tr("Outline"));
    check("text.button.new-file", tr("New File") == QString::fromUtf8("新建文件"),
          tr("New File"));
    check("text.placeholder.module-tree",
          tr("Filter instance / net / file…") == QString::fromUtf8("过滤实例 / 网络 / 文件…"),
          tr("Filter instance / net / file…"));
    check("text.diagnostic.counts",
          tr("Errors: %1 · Warnings: %2 · Information: %3")
              .arg(0).arg(1).arg(2)
              .contains(QString::fromUtf8("错误")),
          tr("Errors: %1 · Warnings: %2 · Information: %3").arg(0).arg(1).arg(2));

    // --- the REAL shell (AC-9.1 on the widgets the user sees) ------------------
    vedit::app::ShellOptions options;
    options.stateDir = stateDir.path();
    options.projectRoot = QDir::currentPath();
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(options);
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = options.projectRoot;
    integrationOptions.stateDir = stateDir.path();
    vedit::workspace::installPanels(shell, integrationOptions);
    // main.cpp installs the terminal factory itself (it is not a workspace lane
    // with an install entry): mirror that line so the real panel is created.
    shell.panels()->setFactory(QStringLiteral("terminal"),
                               vedit::terminal::terminalPanelFactory());
    shell.resize(1500, 950);
    shell.show();
    for (const char *id : {"file-tree", "search", "outline", "terminal", "module-tree"}) {
        shell.panels()->showPanel(QString::fromLatin1(id), &shell);
    }
    QCoreApplication::processEvents();

    const auto dockTitle = [&shell](const char *id) {
        QDockWidget *dock = shell.panels()->dock(QString::fromLatin1(id));
        return dock != nullptr ? dock->windowTitle() : QString();
    };
    check("shell.dock.files", dockTitle("file-tree") == QString::fromUtf8("文件"),
          dockTitle("file-tree"));
    check("shell.dock.search", dockTitle("search") == QString::fromUtf8("搜索"),
          dockTitle("search"));
    check("shell.dock.terminal", dockTitle("terminal") == QString::fromUtf8("终端"),
          dockTitle("terminal"));
    check("shell.dock.outline", dockTitle("outline") == QString::fromUtf8("大纲"),
          dockTitle("outline"));
    check("shell.dock.module-tree", dockTitle("module-tree") == QString::fromUtf8("模块树"),
          dockTitle("module-tree"));

    auto *searchPanel = shell.panels()->dock(QStringLiteral("search")) != nullptr
        ? qobject_cast<vedit::workspace::search::SearchPanel *>(
              shell.panels()->dock(QStringLiteral("search"))->widget())
        : nullptr;
    check("shell.search-panel", searchPanel != nullptr);
    if (searchPanel != nullptr && searchPanel->queryEdit() != nullptr) {
        check("shell.search-placeholder",
              searchPanel->queryEdit()->placeholderText() == QString::fromUtf8("在工程中搜索文本…"),
              searchPanel->queryEdit()->placeholderText());
    } else {
        check("shell.search-placeholder", false);
    }

    auto *fileTree = shell.panels()->dock(QStringLiteral("file-tree")) != nullptr
        ? shell.panels()->dock(QStringLiteral("file-tree"))->widget()
        : nullptr;
    const QStringList fileTreeTexts = widgetTexts(fileTree);
    check("shell.filetree-buttons",
          fileTreeTexts.contains(QString::fromUtf8("打开…"))
              && fileTreeTexts.contains(QString::fromUtf8("新建文件"))
              && fileTreeTexts.contains(QString::fromUtf8("新建文件夹"))
              && fileTreeTexts.contains(QString::fromUtf8("重命名"))
              && fileTreeTexts.contains(QString::fromUtf8("删除"))
              && fileTreeTexts.contains(QString::fromUtf8("刷新"))
              && fileTreeTexts.contains(QString::fromUtf8("折叠")),
          fileTreeTexts.join(QStringLiteral(", ")));

    auto *terminal = shell.panels()->dock(QStringLiteral("terminal")) != nullptr
        ? shell.panels()->dock(QStringLiteral("terminal"))->widget()
        : nullptr;
    const QStringList terminalTexts = widgetTexts(terminal);
    check("shell.terminal-buttons",
          terminalTexts.contains(QString::fromUtf8("重启"))
              && terminalTexts.contains(QString::fromUtf8("清空"))
              && terminalTexts.contains(QString::fromUtf8("关闭")),
          terminalTexts.join(QStringLiteral(", ")));

    auto *moduleTree = shell.panels()->dock(QStringLiteral("module-tree")) != nullptr
        ? shell.panels()->dock(QStringLiteral("module-tree"))->widget()
        : nullptr;
    const QStringList moduleTreeTexts = widgetTexts(moduleTree);
    check("shell.module-tree-buttons",
          moduleTreeTexts.contains(QString::fromUtf8("跳转到源"))
              && moduleTreeTexts.contains(QString::fromUtf8("跳转到目标"))
              && moduleTreeTexts.contains(QString::fromUtf8("刷新")),
          moduleTreeTexts.join(QStringLiteral(", ")));
    if (moduleTree != nullptr) {
        auto *filter = moduleTree->findChild<QLineEdit *>(QStringLiteral("module-tree-filter"));
        check("shell.module-tree-placeholder",
              filter != nullptr
                  && filter->placeholderText() == QString::fromUtf8("过滤实例 / 网络 / 文件…"),
              filter != nullptr ? filter->placeholderText() : QStringLiteral("no filter widget"));
    } else {
        check("shell.module-tree-placeholder", false);
    }

    auto *diagnostics = shell.panels()->dock(QStringLiteral("outline")) != nullptr
        ? qobject_cast<vedit::workspace::diagnostics::DiagnosticsPanel *>(
              shell.panels()->dock(QStringLiteral("outline"))->widget())
        : nullptr;
    check("shell.diagnostics-panel", diagnostics != nullptr);
    if (diagnostics != nullptr) {
        diagnostics->refreshFromController();
        check("shell.diagnostics-counts", diagnostics->countsText().contains(QString::fromUtf8("错误")),
              diagnostics->countsText());
    }

    // --- T40 repair: the two surfaces T38's probe did not reach --------------
    // F1: the diagnostics-panel STATUS LINE. With no per-file analysis yet the
    // panel composes "<session message> — <N> diagnostic(s)"; T40 wrapped that
    // literal, so the rendered text must be Chinese under the same translator.
    if (diagnostics != nullptr) {
        diagnostics->refreshFromController();
        const QString status = diagnostics->statusText();
        // Two legitimate branches: the counts line (F1's string) or the
        // one-line toolchain message when the image lacks verible/slang. Both
        // must be free of the old English literal; the counts branch must be
        // Chinese — the template check below pins the Chinese rendering itself.
        check("text.diagnostic.status-line",
              !status.contains(QLatin1String("diagnostic(s)"))
                  && (!status.contains(QLatin1String("—")) || hasCjk(status)),
              status);
    } else {
        check("text.diagnostic.status-line", false);
    }
    check("text.diagnostic.status-template",
          tr("%1 — %2 diagnostic(s)").arg(QStringLiteral("x")).arg(0)
              == QString::fromUtf8("x — 0 条诊断"),
          tr("%1 — %2 diagnostic(s)").arg(QStringLiteral("x")).arg(0));
    check("text.diagnostic.pending", tr("pending") == QString::fromUtf8("待分析"), tr("pending"));
    check("text.diagnostic.saved-file",
          tr("saved file") == QString::fromUtf8("已保存的文件"), tr("saved file"));

    // F2: the project dock title and the panel header composition.
    const auto *projectDock = shell.findChild<QDockWidget *>(QStringLiteral("dock-project"));
    check("text.project-dock-title",
          projectDock != nullptr && projectDock->windowTitle() == QString::fromUtf8("工程"),
          projectDock != nullptr ? projectDock->windowTitle() : QStringLiteral("no dock-project"));
    check("text.project-title-template", tr("Project") == QString::fromUtf8("工程"), tr("Project"));
    check("text.project-file-nouns",
          tr("file") == QString::fromUtf8("个文件") && tr("files") == QString::fromUtf8("个文件"),
          tr("file") + QLatin1Char('/') + tr("files"));
    check("text.project-header-template",
          tr("Project: %1 — %2 %3").arg(QStringLiteral("demo")).arg(2).arg(tr("files"))
              == QString::fromUtf8("工程：demo — 2 个文件"),
          tr("Project: %1 — %2 %3").arg(QStringLiteral("demo")).arg(2).arg(tr("files")));
    const QStringList suffixMessages = {
        tr(" · %1 filelist"),     tr(" · %1 filelists"), tr(" · %1 variable"),
        tr(" · %1 variables"),    tr(" · %1 line skipped"), tr(" · %1 lines skipped"),
        tr(" · %1 unresolved"),
    };
    check("text.project-header-suffixes",
          suffixMessages
              == QStringList{QString::fromUtf8(" · %1 个文件列表"),
                             QString::fromUtf8(" · %1 个文件列表"),
                             QString::fromUtf8(" · %1 个变量"),
                             QString::fromUtf8(" · %1 个变量"),
                             QString::fromUtf8(" · 跳过 %1 行"),
                             QString::fromUtf8(" · 跳过 %1 行"),
                             QString::fromUtf8(" · %1 个未解析变量")},
          suffixMessages.join(QLatin1Char('|')));

    // F2 on a REAL model: a controlled fixture makes the header deterministic
    // (2 files, 1 filelist); the expansion is the same asynchronous Rust job the
    // product uses, so the probe waits for the state to settle.
    QTemporaryDir projectState;
    const QString projectRoot = projectState.path() + QStringLiteral("/demo-project");
    QDir().mkpath(projectRoot + QStringLiteral("/rtl"));
    {
        QFile list(projectRoot + QStringLiteral("/project.f"));
        if (list.open(QIODevice::WriteOnly | QIODevice::Text)) {
            list.write("rtl/a.sv\nrtl/b.sv\n");
        }
        for (const char *name : {"rtl/a.sv", "rtl/b.sv"}) {
            QFile source(projectRoot + QLatin1Char('/') + QLatin1String(name));
            if (source.open(QIODevice::WriteOnly | QIODevice::Text)) {
                source.write("module m; endmodule\n");
            }
        }
    }
    vedit::workspace::project::ProjectModel projectModel(
        projectState.path() + QStringLiteral("/state"));
    projectModel.open(projectRoot);
    for (int tick = 0; tick < 400 && (projectModel.isPending() || projectModel.files().isEmpty());
         ++tick) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    const QString header = projectModel.summary();
    check("text.project-header",
          header.startsWith(QString::fromUtf8("工程：demo-project — 2 个文件")) && hasCjk(header)
              && !header.contains(QLatin1String("Project:")),
          header);

    // F3-audit closures: the three user-visible strings the claim verification
    // turned red in the T38 table (see 03-b-reconciliation.md §3.2).
    check("text.t40-audit-closures",
          tr("Opened %1") == QString::fromUtf8("已打开 %1")
              && hasCjk(tr("Settings loaded from %1 with %2 warning(s); the built-in "
                           "defaults were kept where the file is invalid."))
              && tr("%1 configuration problem(s) — the built-in defaults were kept "
                    "where needed:")
                     .contains(QString::fromUtf8("个配置问题")),
          tr("Opened %1") + QLatin1Char('|')
              + tr("Settings loaded from %1 with %2 warning(s); the built-in "
                   "defaults were kept where the file is invalid."));

    // --- AC-9.3: Qt's own standard buttons are Chinese -------------------------
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                             | QDialogButtonBox::Save | QDialogButtonBox::Open);
    const QString okText = buttons.button(QDialogButtonBox::Ok)->text();
    const QString cancelText = buttons.button(QDialogButtonBox::Cancel)->text();
    const QString saveText = buttons.button(QDialogButtonBox::Save)->text();
    const QString openText = buttons.button(QDialogButtonBox::Open)->text();
    std::printf("     qt standard buttons: Ok='%s' Cancel='%s' Save='%s' Open='%s'\n",
                qPrintable(okText), qPrintable(cancelText), qPrintable(saveText),
                qPrintable(openText));
    check("std.ok", okText != QLatin1String("OK") && hasCjk(okText), okText);
    check("std.cancel", cancelText != QLatin1String("Cancel") && hasCjk(cancelText), cancelText);
    check("std.save", saveText != QLatin1String("Save") && hasCjk(saveText), saveText);
    check("std.open", openText != QLatin1String("Open") && hasCjk(openText), openText);

    // --- screenshots (visual companion to the text checks) ---------------------
    check("shot.shell", shell.grab().save(shotsDir + QStringLiteral("/01-shell-zh_CN.png")));
    QWidget buttonsHost;
    auto *layout = new QVBoxLayout(&buttonsHost);
    layout->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                           | QDialogButtonBox::Save | QDialogButtonBox::Open,
                                           &buttonsHost));
    buttonsHost.resize(420, 80);
    buttonsHost.show();
    QCoreApplication::processEvents();
    check("shot.std-buttons",
          buttonsHost.grab().save(shotsDir + QStringLiteral("/02-qt-standard-buttons-zh_CN.png")));

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
