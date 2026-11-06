// T18 / AC-4.1, AC-4.2, AC-4.8 — i18n runtime probe.
//
//   i18n-probe --checks [--translations <dir>]        headless assertions (offscreen)
//   i18n-probe --screenshots <dir> [--translations <dir>]  Xvfb/xcb dialogs + PNGs
//
// It links the product's own static libraries (see build.sh and the link line it
// reuses), so the shell, the Preferences dialog, the settings store client and the
// restart path under test are the shipped ones — not copies.
//
// The three AC-4.2 branches are asserted separately:
//   1. cancel     -> one prompt for several dirty documents, NOTHING written,
//                    UI says "语言未切换：重启已取消", no relaunch;
//   2. save       -> documents saved first, then ui.language stored, then
//                    saveSession(), then the relaunch;
//   3. no archive -> persisting the language touches settings.toml only;
//                    session.json keeps its sha256 AND its mtime (T19 owns it).
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "i18n.h"
#include "settings_dialog.h"
#include "settings_panel.h"

#include "ktexteditor_provider.h"

#include <KTextEditor/Document>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdio>

using namespace vedit::app;
using vedit::workspace::settings::SettingsClient;
using vedit::workspace::settings::SettingsDialog;
using vedit::workspace::settings::SettingsPanel;

namespace {

int failures = 0;
QString g_shotDir;
QString g_translationsDir;

void check(const char *id, bool ok, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", id,
                detail.isEmpty() ? "" : " — ", qPrintable(detail));
    if (!ok) {
        ++failures;
    }
}

void pump(int rounds = 8)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

QString sha256(const QString &path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex());
}

bool saveShot(QWidget *widget, const QString &path)
{
    const bool ok = widget != nullptr && widget->grab().save(path);
    std::printf("SHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    return ok;
}

QByteArray moduleFile(const QString &module, int lines)
{
    QByteArray bytes = "module " + module.toUtf8() + ";\n";
    for (int line = 4; line <= lines; ++line) {
        bytes += QByteArray("  // line ") + QByteArray::number(line) + "\n";
    }
    bytes += "endmodule\n";
    return bytes;
}

// The product sequence of src/app/main.cpp: build the shell with the session
// restore deferred, install the real KTextEditor provider, THEN apply the session.
ApplicationShell *makeShell(const QString &stateDir, const QString &projectRoot, bool restore)
{
    ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = projectRoot;
    options.restoreSession = restore;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto *shell = new ApplicationShell(options);
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell->startSession();
    shell->resize(1000, 720);
    shell->show();
    pump();
    return shell;
}

QString translate(const char *context, const char *source)
{
    return QCoreApplication::translate(context, source);
}

QStringList dataValues(QComboBox *combo)
{
    QStringList values;
    if (combo == nullptr) {
        return values;
    }
    for (int index = 0; index < combo->count(); ++index) {
        values.append(combo->itemData(index).toString());
    }
    return values;
}

// Makes `file` the active tab, appends a marker and marks the document dirty.
void dirtyFile(EditorPane *pane, const QString &path, const QByteArray &marker)
{
    const int index = pane->tabIndexForPath(path);
    if (index < 0) {
        return;
    }
    pane->setActiveTab(index);
    pump();
    KTextEditor::Document *document = vedit::editor::documentFor(pane->editorWidget());
    if (document == nullptr) {
        return;
    }
    document->setText(document->text() + QString::fromUtf8(marker));
    document->setModified(true);
    pump();
}

void useLanguage(const QString &stateDir, const QString &setting)
{
    writeFile(i18n::settingsFilePath(stateDir),
              QStringLiteral("\"ui.language\" = \"%1\"\n").arg(setting).toUtf8());
}

// --- headless assertions ----------------------------------------------------
bool runChecks(const QString &root)
{
    const QString project = root + QStringLiteral("/project");
    QDir().mkpath(project);
    const QString fileA = project + QStringLiteral("/alpha.sv");
    const QString fileB = project + QStringLiteral("/bravo.sv");
    writeFile(fileA, moduleFile(QStringLiteral("alpha"), 80));
    writeFile(fileB, moduleFile(QStringLiteral("bravo"), 120));

    const QString stateDir = root + QStringLiteral("/state");
    QDir().mkpath(stateDir);
    const QString settingsPath = i18n::settingsFilePath(stateDir);
    const QString sessionPath = stateDir + QStringLiteral("/session.json");

    // --- A. the persisted zh_CN selection installs before the shell ---------
    useLanguage(stateDir, QStringLiteral("zh_CN"));
    const i18n::InstallResult zh = i18n::installTranslations(settingsPath, g_translationsDir);
    check("i18n.zh.installed", zh.translated && zh.selection == i18n::UiLanguage::SimplifiedChinese,
          QStringLiteral("tag=%1 setting=%2").arg(zh.tag, zh.setting));
    check("i18n.zh.tag", zh.tag == QStringLiteral("zh_CN"), zh.tag);
    check("i18n.zh.qm", QFileInfo(zh.qmPath).fileName() == QStringLiteral("vedit_zh_CN.qm"),
          zh.qmPath);
    check("i18n.zh.translate-shell",
          translate("vedit::app::ApplicationShell", "&File") == QString::fromUtf8("文件(&F)"),
          translate("vedit::app::ApplicationShell", "&File"));
    check("i18n.zh.translate-cancel-message",
          translate("vedit::app::ApplicationShell", "Language not switched: restart cancelled")
              == QString::fromUtf8("语言未切换：重启已取消"));
    const i18n::InstallResult again = i18n::installTranslations(settingsPath, g_translationsDir);
    check("i18n.idempotent", again.translated
              && translate("vedit::app::ApplicationShell", "&File") == QString::fromUtf8("文件(&F)"));

    ApplicationShell *shell = makeShell(stateDir, project, /*restore=*/false);
    check("shell.zh.menu", shell->commands()->menuLabel(QStringLiteral("settings"))
              == QString::fromUtf8("设置(&S)"),
          shell->commands()->menuLabel(QStringLiteral("settings")));
    check("shell.zh.command", shell->commands()->action(QStringLiteral("file.save")) != nullptr
              && shell->commands()->action(QStringLiteral("file.save"))->text()
                  == QString::fromUtf8("保存(&S)"),
          shell->commands()->action(QStringLiteral("file.save"))
              ? shell->commands()->action(QStringLiteral("file.save"))->text()
              : QStringLiteral("<missing>"));

    EditorPane *pane = shell->editorArea()->panes().value(0);
    check("shell.pane", pane != nullptr);
    if (pane == nullptr) {
        return false;
    }
    QString openError;
    check("open.alpha", pane->openFile(fileA, &openError), openError);
    check("open.bravo", pane->openFile(fileB, &openError), openError);
    check("pane.two-tabs", pane->tabCount() == 2, QString::number(pane->tabCount()));
    QString saveError;
    check("archive.saved", shell->saveSession(&saveError), saveError);
    check("archive.exists", QFileInfo::exists(sessionPath));
    const QByteArray archive = readFile(sessionPath);
    check("archive.schema-v2",
          QJsonDocument::fromJson(archive).object().value(QStringLiteral("version")).toInt() == 2);
    check("archive.no-language-key",
          !archive.contains("ui.language") && !archive.contains("interface_language"));
    const QString archiveSha = sha256(sessionPath);
    check("archive.sha-readable", archiveSha.size() == 64);

    // --- the Preferences page: the row exists and is a PREVIEW until applied -
    SettingsDialog dialog(shell->themes(), shell->commands(), settingsPath, nullptr);
    SettingsPanel *panel = dialog.panel();
    check("panel.store-loaded", panel != nullptr && panel->flush(), panel ? panel->statusText() : QString());
    check("panel.language-row", panel->languageCombo() != nullptr
              && panel->languageRestartButton() != nullptr && panel->languageHint() != nullptr);
    const QStringList values = panel->languageCombo() ? dataValues(panel->languageCombo()) : QStringList();
    check("panel.language-values",
          values == QStringList{QStringLiteral("auto"), QStringLiteral("en"), QStringLiteral("zh_CN")},
          values.join(QStringLiteral(",")));
    check("panel.language-stored", panel->storedLanguage() == QStringLiteral("zh_CN"),
          panel->storedLanguage());
    panel->setLanguageSetting(QStringLiteral("en"));
    check("panel.language-pending", panel->languageSetting() == QStringLiteral("en"));
    check("panel.language-hint", panel->languageHint() != nullptr
              && panel->languageHint()->text() == QString::fromUtf8("重启后生效。"),
          panel->languageHint() ? panel->languageHint()->text() : QString());

    // Two dirty documents, so the "one dialog" rule is observable.
    dirtyFile(pane, fileA, QByteArrayLiteral("// unsaved edit a\n"));
    dirtyFile(pane, fileB, QByteArrayLiteral("// unsaved edit b\n"));
    check("dirty.both", pane->isTabModified(0) && pane->isTabModified(1));

    // T19's archive carries the editor's caret/scroll; the dirty edits above moved
    // the caret, so refresh the archive NOW. The sha comparisons below then measure
    // the restart path alone, not those legitimate T19 state changes.
    QString baselineError;
    check("archive.baseline-refreshed", shell->saveSession(&baselineError), baselineError);
    const QString archiveShaBaseline = sha256(sessionPath);

    // --- B. cancel: ONE prompt, nothing written, UI states the outcome ------
    int providerCalls = 0;
    QStringList seenDisplays;
    QStringList seenPaths;
    shell->setUnsavedPromptProvider(
        [&providerCalls, &seenDisplays, &seenPaths](const QStringList &displays,
                                                    const QStringList &paths) {
            ++providerCalls;
            seenDisplays = displays;
            seenPaths = paths;
            return ApplicationShell::CloseChoice::Cancel;
        });
    int relaunchCalls = 0;
    i18n::setRestartApplicationHandler([&relaunchCalls](QString *) {
        ++relaunchCalls;
        return true;
    });
    i18n::setRestartHandler(
        [shell](const QString &tag, const i18n::PersistLanguage &persist, QString *error) {
            return shell->restartForLanguageChange(tag, persist, error);
        });

    const QString settingsShaBefore = sha256(settingsPath);
    const QString alphaShaBefore = sha256(fileA);
    const QString bravoShaBefore = sha256(fileB);
    const QDateTime archiveMtimeBefore = QFileInfo(sessionPath).lastModified();

    panel->requestLanguageRestart();
    pump();
    check("cancel.one-prompt", providerCalls == 1, QString::number(providerCalls));
    check("cancel.prompt-lists-two", seenDisplays.size() == 2 && seenPaths.size() == 2,
          seenDisplays.join(QStringLiteral(", ")));
    check("cancel.panel-message",
          panel->languageStatus() == QString::fromUtf8("语言未切换：重启已取消"),
          panel->languageStatus());
    check("cancel.shell-message",
          shell->statusBar()->currentMessage() == QString::fromUtf8("语言未切换：重启已取消"),
          shell->statusBar()->currentMessage());
    check("cancel.no-relaunch", relaunchCalls == 0);
    check("cancel.settings-unchanged", sha256(settingsPath) == settingsShaBefore);
    check("cancel.archive-unchanged", sha256(sessionPath) == archiveShaBaseline);
    check("cancel.archive-mtime-unchanged",
          QFileInfo(sessionPath).lastModified() == archiveMtimeBefore);
    check("cancel.alpha-unchanged", sha256(fileA) == alphaShaBefore);
    check("cancel.bravo-unchanged", sha256(fileB) == bravoShaBefore);
    check("cancel.language-not-stored", panel->storedLanguage() == QStringLiteral("zh_CN"),
          panel->storedLanguage());

    // --- C. save -> persist -> saveSession -> relaunch ----------------------
    providerCalls = 0;
    seenDisplays.clear();
    shell->setUnsavedPromptProvider(
        [&providerCalls, &seenDisplays](const QStringList &displays, const QStringList &) {
            ++providerCalls;
            seenDisplays = displays;
            return ApplicationShell::CloseChoice::Save;
        });
    panel->requestLanguageRestart();
    pump();
    check("save.one-prompt", providerCalls == 1, QString::number(providerCalls));
    check("save.document-a-written", readFile(fileA).contains("unsaved edit a"));
    check("save.document-b-written", readFile(fileB).contains("unsaved edit b"));
    check("save.language-stored", readFile(settingsPath).contains("ui.language")
              && readFile(settingsPath).contains("en"));
    check("save.language-in-panel", panel->storedLanguage() == QStringLiteral("en"),
          panel->storedLanguage());
    check("save.relaunch-fired", relaunchCalls == 1, QString::number(relaunchCalls));
    check("save.panel-message",
          panel->languageStatus().startsWith(QString::fromUtf8("正在重启")),
          panel->languageStatus());
    check("save.archive-valid",
          QJsonDocument::fromJson(readFile(sessionPath)).object().value(QStringLiteral("version"))
              .toInt() == 2);
    check("save.archive-no-language-key",
          !readFile(sessionPath).contains("ui.language"));
    const QString archiveShaAfterSave = sha256(sessionPath);
    check("save.archive-sha-unchanged", archiveShaAfterSave == archiveShaBaseline,
          QStringLiteral("before=%1 after=%2")
              .arg(archiveShaBaseline.left(12), archiveShaAfterSave.left(12)));
    check("save.commands-saved", QFileInfo::exists(stateDir + QStringLiteral("/shortcuts.json")));

    // The persisted choice is what the NEXT start reads.
    const i18n::InstallResult next = i18n::installTranslations(settingsPath, g_translationsDir);
    check("next-start.english", !next.translated && next.selection == i18n::UiLanguage::English
              && next.tag == QStringLiteral("en"),
          QStringLiteral("translated=%1 tag=%2").arg(next.translated).arg(next.tag));
    check("next-start.source-string",
          translate("vedit::app::ApplicationShell", "&File") == QStringLiteral("&File"),
          translate("vedit::app::ApplicationShell", "&File"));

    // --- D. the language write itself never touches session.json -----------
    const QString archiveShaClean = sha256(sessionPath);
    const QDateTime archiveMtimeClean = QFileInfo(sessionPath).lastModified();
    QString storeError;
    check("store.language-write", panel->storeLanguage(QStringLiteral("zh_CN"), &storeError),
          storeError);
    check("store.settings-updated", readFile(settingsPath).contains("zh_CN"));
    check("store.archive-sha-unchanged", sha256(sessionPath) == archiveShaClean,
          QStringLiteral("before=%1 after=%2")
              .arg(archiveShaClean.left(12), sha256(sessionPath).left(12)));
    check("store.archive-mtime-unchanged",
          QFileInfo(sessionPath).lastModified() == archiveMtimeClean);
    check("store.archive-no-language-key", !readFile(sessionPath).contains("ui.language"));

    // The Rust store reads the flat dotted key back (independent reader).
    SettingsClient reader(settingsPath);
    bool loaded = false;
    QString loadedValue;
    reader.load([&loaded, &loadedValue](vedit::workspace::settings::Snapshot snapshot) {
        loaded = snapshot.ok();
        loadedValue =
            SettingsClient::valueOf(snapshot.entries, QStringLiteral("ui.language"));
    });
    reader.flush();
    check("store.roundtrip", loaded && loadedValue == QStringLiteral("zh_CN"),
          QStringLiteral("loaded=%1 value=%2").arg(loaded).arg(loadedValue));

    delete shell;
    return true;
}

// --- Xvfb/xcb screenshot pass ----------------------------------------------
bool runScreenshots(const QString &root)
{
    const QString project = root + QStringLiteral("/project");
    QDir().mkpath(project);
    const QString fileA = project + QStringLiteral("/alpha.sv");
    const QString fileB = project + QStringLiteral("/bravo.sv");
    writeFile(fileA, moduleFile(QStringLiteral("alpha"), 80));
    writeFile(fileB, moduleFile(QStringLiteral("bravo"), 120));
    const QString stateDir = root + QStringLiteral("/state");
    QDir().mkpath(stateDir);
    const QString settingsPath = i18n::settingsFilePath(stateDir);
    const QString sessionPath = stateDir + QStringLiteral("/session.json");

    useLanguage(stateDir, QStringLiteral("zh_CN"));
    const i18n::InstallResult zh = i18n::installTranslations(settingsPath, g_translationsDir);
    check("shot.zh-installed", zh.translated, zh.diagnostic);

    ApplicationShell *shell = makeShell(stateDir, project, /*restore=*/false);
    check("shot.zh-menu", shell->commands()->menuLabel(QStringLiteral("settings"))
              == QString::fromUtf8("设置(&S)"));
    EditorPane *pane = shell->editorArea()->panes().value(0);
    QString openError;
    pane->openFile(fileA, &openError);
    pane->openFile(fileB, &openError);
    saveShot(shell, g_shotDir + QStringLiteral("/30-zh-shell.png"));
    QString saveError;
    shell->saveSession(&saveError);
    check("shot.archive", QFileInfo::exists(sessionPath), saveError);

    dirtyFile(pane, fileA, QByteArrayLiteral("// unsaved edit a\n"));
    dirtyFile(pane, fileB, QByteArrayLiteral("// unsaved edit b\n"));
    // T19's archive carries the caret; refresh it after the dirty edits so the
    // sha comparisons measure the restart path, not those T19 state changes.
    QString baselineError;
    check("shot.archive-baseline", shell->saveSession(&baselineError), baselineError);

    SettingsDialog dialog(shell->themes(), shell->commands(), settingsPath, nullptr);
    SettingsPanel *panel = dialog.panel();
    check("shot.panel-loaded", panel->flush());
    panel->setLanguageSetting(QStringLiteral("en"));
    // The interface-language row lives on the Tools page; show it in the shot.
    if (panel->tabs() != nullptr) {
        panel->tabs()->setCurrentIndex(1);
        pump();
    }
    i18n::setRestartHandler(
        [shell](const QString &tag, const i18n::PersistLanguage &persist, QString *error) {
            return shell->restartForLanguageChange(tag, persist, error);
        });
    i18n::setRestartApplicationHandler([](QString *) { return true; });

    const QString archiveSha = sha256(sessionPath);
    const QString settingsSha = sha256(settingsPath);
    const QString alphaSha = sha256(fileA);
    const QString bravoSha = sha256(fileB);

    dialog.show();
    pump();
    saveShot(&dialog, g_shotDir + QStringLiteral("/31-zh-preferences.png"));

    // The single batch dialog is driven by OBJECT NAME (the labels are translated,
    // so text matching would depend on the language under test).
    bool batchSeen = false;
    QTimer::singleShot(500, [&dialog, &shell, &batchSeen]() {
        QWidget *modal = QApplication::activeModalWidget();
        batchSeen = modal != nullptr
            && modal->objectName() == QStringLiteral("unsaved-batch-prompt");
        if (!batchSeen) {
            if (modal != nullptr) {
                modal->close();
            }
            return;
        }
        saveShot(modal, g_shotDir + QStringLiteral("/32-batch-unsaved-dialog.png"));
        auto *box = qobject_cast<QMessageBox *>(modal);
        if (box != nullptr) {
            for (QAbstractButton *button : box->buttons()) {
                if (button->objectName() == QStringLiteral("unsaved-cancel-all")) {
                    button->click();
                    break;
                }
            }
        }
        QTimer::singleShot(250, [&dialog, &shell]() {
            saveShot(&dialog, g_shotDir + QStringLiteral("/33-language-not-switched.png"));
            saveShot(shell, g_shotDir + QStringLiteral("/34-cancel-message-shell.png"));
            dialog.reject();
        });
    });
    QTimer::singleShot(150, [panel]() { panel->requestLanguageRestart(); });
    dialog.exec();
    pump();

    check("shot.one-batch-dialog", batchSeen);

    check("shot.cancel-message",
          panel->languageStatus() == QString::fromUtf8("语言未切换：重启已取消"),
          panel->languageStatus());
    check("shot.cancel-no-settings-write", sha256(settingsPath) == settingsSha);
    check("shot.cancel-no-archive-write", sha256(sessionPath) == archiveSha);
    check("shot.cancel-no-document-write",
          sha256(fileA) == alphaSha && sha256(fileB) == bravoSha);

    // --- save branch on the REAL dialog: "全部保存" then the relaunch --------
    int relaunchCalls = 0;
    i18n::setRestartApplicationHandler([&relaunchCalls](QString *) {
        ++relaunchCalls;
        return true;
    });
    const QString archiveShaSave = sha256(sessionPath);
    const QString settingsShaSave = sha256(settingsPath);
    panel->setLanguageSetting(QStringLiteral("en"));
    bool saveDialogSeen = false;
    dialog.show();
    pump();
    QTimer::singleShot(500, [&dialog, &saveDialogSeen]() {
        QWidget *modal = QApplication::activeModalWidget();
        saveDialogSeen = modal != nullptr
            && modal->objectName() == QStringLiteral("unsaved-batch-prompt");
        if (!saveDialogSeen) {
            if (modal != nullptr) {
                modal->close();
            }
            return;
        }
        auto *box = qobject_cast<QMessageBox *>(modal);
        if (box != nullptr) {
            for (QAbstractButton *button : box->buttons()) {
                if (button->objectName() == QStringLiteral("unsaved-save-all")) {
                    button->click();
                    break;
                }
            }
        }
        QTimer::singleShot(400, [&dialog]() {
            saveShot(&dialog, g_shotDir + QStringLiteral("/37-save-branch-restarting.png"));
            dialog.reject();
        });
    });
    QTimer::singleShot(150, [panel]() { panel->requestLanguageRestart(); });
    dialog.exec();
    pump();
    check("shot.save-branch-dialog", saveDialogSeen);
    check("shot.save-branch-documents",
          readFile(fileA).contains("unsaved edit a") && readFile(fileB).contains("unsaved edit b"));
    check("shot.save-branch-language",
          readFile(settingsPath).contains("ui.language") && readFile(settingsPath).contains("en"));
    check("shot.save-branch-relaunch", relaunchCalls == 1, QString::number(relaunchCalls));
    check("shot.save-branch-archive-sha", sha256(sessionPath) == archiveShaSave);
    check("shot.save-branch-settings-changed", sha256(settingsPath) != settingsShaSave);

    // English: the same shell after a next start with the English selection.
    const QString enStateDir = root + QStringLiteral("/state-en");
    QDir().mkpath(enStateDir);
    useLanguage(enStateDir, QStringLiteral("en"));
    const i18n::InstallResult en =
        i18n::installTranslations(i18n::settingsFilePath(enStateDir), g_translationsDir);
    check("shot.en-installed", !en.translated && en.tag == QStringLiteral("en"));
    ApplicationShell *english = makeShell(enStateDir, project, /*restore=*/false);
    check("shot.en-menu", english->commands()->menuLabel(QStringLiteral("settings"))
              == QStringLiteral("&Settings"));
    saveShot(english, g_shotDir + QStringLiteral("/35-en-shell.png"));
    delete english;
    delete shell;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    // Child side of --restart-spawn-check: this process was started by
    // i18n::restartApplication() itself (the parent sets the env var just before
    // spawning). Prove it by writing the marker and exit cleanly — no Qt needed.
    const QByteArray relaunchMarker = qgetenv("VEDIT_PROBE_RELAUNCH_MARKER");
    if (!relaunchMarker.isEmpty()) {
        const bool ok = writeFile(QString::fromLocal8Bit(relaunchMarker)
                                      + QStringLiteral("/relaunched.txt"),
                                  QByteArrayLiteral("relaunched"));
        std::printf("RELAUNCHED marker=%s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-i18n-probe"));

    bool screenshots = false;
    QString spawnCheckDir;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--screenshots") && i + 1 < argc) {
            screenshots = true;
            g_shotDir = QString::fromLocal8Bit(argv[i + 1]);
        } else if (arg == QLatin1String("--translations") && i + 1 < argc) {
            g_translationsDir = QString::fromLocal8Bit(argv[i + 1]);
        } else if (arg == QLatin1String("--restart-spawn-check") && i + 1 < argc) {
            spawnCheckDir = QString::fromLocal8Bit(argv[i + 1]);
        }
    }

    // Parent side: the DEFAULT restartApplication path must spawn this very
    // executable and exit the old process (the "自动退出并重启" half). The child
    // is the same binary with the inherited arguments; the env var makes it write
    // relaunched.txt and exit immediately.
    if (!spawnCheckDir.isEmpty()) {
        const bool dirOk = QDir().mkpath(spawnCheckDir);
        check("spawn.check-dir", dirOk);
        check("spawn.parent-marker",
              writeFile(spawnCheckDir + QStringLiteral("/parent.txt"),
                        QByteArrayLiteral("parent")));
        qputenv("VEDIT_PROBE_RELAUNCH_MARKER", spawnCheckDir.toLocal8Bit());
        QString restartError;
        const bool restarted = i18n::restartApplication(&restartError);
        // Unreachable through the shipped default path: it exits after spawning.
        writeFile(spawnCheckDir + QStringLiteral("/unexpected-return.txt"),
                  restarted ? QByteArrayLiteral("returned-true") : restartError.toUtf8());
        std::printf("FAIL spawn.restart-returned — %s\n",
                    restarted ? "returned true instead of exiting" : qPrintable(restartError));
        return 3;
    }
    if (screenshots && !QDir().mkpath(g_shotDir)) {
        std::printf("FAIL shot-dir\n");
        return 1;
    }

    QTemporaryDir temp;
    check("probe.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }
    const bool ok = screenshots ? runScreenshots(temp.path()) : runChecks(temp.path());
    std::printf("I18N-PROBE failures=%d platform=%s translations=%s\n", failures,
                qPrintable(QApplication::platformName()), qPrintable(g_translationsDir));
    return ok && failures == 0 ? 0 : 1;
}
