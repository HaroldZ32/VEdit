#pragma once

// i18n runtime (T18 / AC-4.1, AC-4.2, AC-4.8): the UI-language selection, the
// ONE QTranslator installation point, and the relaunch entry point the shell's
// language action uses.
//
// Persistence is the flat Rust settings store (`settings.toml`, the same store
// the Preferences dialog writes): one string key `ui.language` with the values
// `auto`, `en`, `zh_CN` (the engine value, not a display name). The language is
// deliberately NOT part of T19's session archive (`session.json`): that file
// belongs to the session lane, this module never opens it, and the restart path
// only calls `ApplicationShell::saveSession()`.
//
// Startup order matters: `installTranslations()` runs BEFORE the shell is built,
// so menus, panel titles and dialog strings are created in the chosen language.
// A language switch therefore needs a restart — the reason the Preferences page
// offers exactly one action, "restart to apply".
#include <QString>
#include <QStringList>

#include <functional>

namespace vedit::app::i18n {

enum class UiLanguage { Auto, English, SimplifiedChinese };

// The only spellings written to / read from `ui.language`.
QString languageSetting(UiLanguage language);
UiLanguage languageFromSetting(const QString &value);
// The flat settings-store key the selection is persisted under ("ui.language").
QString languageKey();
// Resolves `auto` against the system locale. The product ships zh_CN; every
// other system locale keeps the English sources (no translation file needed).
QString languageTag(UiLanguage language);

// `<stateDir>/settings.toml` with the same fallback chain as the settings lane.
QString settingsFilePath(const QString &stateDir);

struct InstallResult {
    UiLanguage selection = UiLanguage::Auto;
    QString setting;         // "auto" | "en" | "zh_CN"
    QString tag;             // resolved language tag, e.g. "zh_CN"
    QString qmPath;          // file actually loaded (empty when none is needed)
    bool translated = false; // the VEdit QTranslator is installed
    // T38/C-class: Qt's OWN standard dialogs (OK/Cancel/Open/Save in
    // QFileDialog, QMessageBox, QDialogButtonBox) need `qtbase_<tag>.qm`; the
    // vendored copy ships beside the VEdit .qm (translations/). Its absence is
    // not fatal (the product still runs, standard buttons stay English), so it
    // has its own path/loaded pair instead of reusing `diagnostic`.
    QString qtbasePath;          // qtbase_<tag>.qm actually loaded (empty when none)
    bool qtbaseLoaded = false;   // a QTranslator for Qt's standard strings is installed
    QString diagnostic;      // why no qm could be loaded; empty on success
};

// Idempotent: the translators installed by a previous call are removed first, so
// a second call can never stack them. Both the VEdit .qm (vedit_<tag>.qm) and the
// vendored Qt standard-dialog .qm (qtbase_<tag>.qm, T38/C-class) are loaded from
// the same directory. A missing VEdit .qm is reported in `diagnostic`, never
// fatal; a missing qtbase .qm only leaves `qtbaseLoaded=false`.
// `translationsDir` is mainly for probes; the product leaves it empty and uses
// translationsDirectory().
InstallResult installTranslations(const QString &settingsPath,
                                  const QString &translationsDir = QString());

// Search order: $VEDIT_TRANSLATIONS_DIR, <applicationDirPath>/bin/translations
// (packaged AppDir: the loader makes applicationDirPath the AppDir ROOT while
// the .qm are installed under bin/), <applicationDirPath>/translations (build
// tree / tarball), <applicationDirPath>/../share/vedit/translations (prefix).
QString translationsDirectory();

// The ordered candidates and the first existing one, both pure so a headless
// test can exercise the packaged, build-tree and prefix layouts. When none
// exists the first candidate (the packaged location) is returned.
QStringList translationCandidates(const QString &applicationDirPath);
QString translationsDirectoryFor(const QString &applicationDirPath);

// The relaunch target the restart action must use, resolved without spawning:
// $APPIMAGE (AppImage runtime) -> <applicationDirPath>/AppRun (extracted AppDir,
// executable) -> applicationFilePath(). `arguments` are the process arguments
// INCLUDING argv[0]; the result carries arguments.mid(1), so the same user
// arguments are forwarded to every launcher.
struct RestartCommand {
    QString program;
    QStringList arguments;
};
RestartCommand restartCommand(const QString &appImage, const QString &applicationDirPath,
                              const QString &applicationFilePath,
                              const QStringList &arguments);

// Relaunches the application with the same arguments (detached) and returns true
// once the child started. The launcher is $APPIMAGE when the AppImage runtime set
// it, else <applicationDirPath>/AppRun in an extracted AppDir (where
// applicationFilePath() is the bundled loader, not the program), else
// applicationFilePath(). One stderr line names the chosen program. On the
// success path the CURRENT process exits immediately (the restart is triggered
// from a modal dialog, where QCoreApplication::quit() is a no-op) — that is the
// "auto-exit and relaunch" half of the one-click restart. A handler installed
// with setRestartApplicationHandler() replaces the spawn and does NOT exit; that
// is the testing seam a probe uses to observe the restart contract.
bool restartApplication(QString *error = nullptr);

// --- The restart contract seam (AC-4.2) ------------------------------------
// ApplicationShell implements the strict ordering
//   confirmUnsavedDocuments() -> false: abort, write nothing
//                             -> true:  persist language -> saveSession() -> relaunch
// and main.cpp registers it here, so the Preferences page can invoke the
// "restart to apply" action without depending on the shell type (the settings
// dialog is created inside the settings lane).
enum class RestartOutcome { Restarted, Cancelled, Failed };

// Writes the language key. Called only AFTER the unsaved-document guard passed,
// which is what makes "cancel writes nothing" true.
using PersistLanguage = std::function<bool(QString *error)>;
using RestartHandler =
    std::function<RestartOutcome(const QString &tag, const PersistLanguage &persist,
                                 QString *error)>;

void setRestartHandler(RestartHandler handler);
void clearRestartHandler();
RestartOutcome requestRestart(const QString &tag, const PersistLanguage &persist,
                              QString *error);

// Testing seams (not used by the product entry point).
void setRestartApplicationHandler(std::function<bool(QString *)> handler);
void clearRestartApplicationHandler();

} // namespace vedit::app::i18n
