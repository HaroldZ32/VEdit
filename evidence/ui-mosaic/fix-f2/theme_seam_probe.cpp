// UI-Mosaic F2 evidence probe — NOT part of the product build.
//
// It exercises the REAL wiring, not a mock of it:
//   * vedit::app::ThemeManager hands the applied syntax theme to its registered
//     applier (and stays silent on a failed apply);
//   * vedit::editor::EditCoreProvider receives that name through the shell's
//     EditorProvider seam, renders it in a real KTextEditor view, and every view
//     created later starts with the current theme.
//
// The rendering half is a pixel assertion, not a state assertion: the same view
// is grabbed once with "Breeze Dark" and once with "Breeze Light", and the
// difference must cover the view's text area (the theme's own background colour
// must dominate both renders). That is the user-visible effect the review found
// missing (evidence/ui-mosaic/review/logs/06-syntax-theme-effect.log).
//
// Build + run: evidence/ui-mosaic/fix-f2/run-probe.sh (inside the pinned image).
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "theme_manager.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QPlainTextEdit>
#include <QStringList>
#include <QWidget>

#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    std::printf("CHECK %-42s %s", name, ok ? "PASS" : "FAIL");
    if (!detail.isEmpty()) {
        std::printf(" (%s)", qPrintable(detail));
    }
    std::printf("\n");
    if (!ok) {
        ++g_failures;
    }
}

// The shell-facing seam is exercised with a recording provider: this is the
// "applyTheme() 后 provider 收到正确的主题名" assertion, independent of whether a
// KTextEditor renderer is available in the build.
class RecordingProvider final : public vedit::app::EditorProvider
{
public:
    QString id() const override { return QStringLiteral("recording"); }
    QString displayName() const override { return QStringLiteral("Recording provider"); }
    QWidget *createEditor(QWidget *parent) override { return new QPlainTextEdit(parent); }
    void applySyntaxTheme(const QString &themeName) override { calls.append(themeName); }

    QStringList calls;
};

int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) {
        return -1;
    }
    int count = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

int countPixelsNear(const QImage &image, const QColor &target, int tolerance)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - target.red()) <= tolerance
                && qAbs(pixel.green() - target.green()) <= tolerance
                && qAbs(pixel.blue() - target.blue()) <= tolerance) {
                ++count;
            }
        }
    }
    return count;
}

QImage grab(QWidget *widget)
{
    QCoreApplication::processEvents();
    return widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);

    // --- 1. ThemeManager -> provider seam -----------------------------------
    {
        RecordingProvider recorder;
        vedit::app::ThemeManager manager;
        manager.setSyntaxThemeApplier(
            [&recorder](const QString &name) { recorder.applySyntaxTheme(name); });

        QString error;
        const bool applied = manager.applyTheme(QStringLiteral("Breeze Dark"), &error);
        check("theme.apply", applied, error);
        check("theme.callback.name",
              recorder.calls.size() == 1 && recorder.calls.at(0) == QStringLiteral("Breeze Dark"),
              recorder.calls.join(QStringLiteral(",")));
        check("theme.current", manager.currentTheme() == QStringLiteral("Breeze Dark"),
              manager.currentTheme());

        const int beforeInvalid = recorder.calls.size();
        const bool invalid = manager.applyTheme(QStringLiteral("__vedit-f2-no-such-theme__"), &error);
        check("theme.invalid.refused", !invalid, error);
        check("theme.invalid.silent", recorder.calls.size() == beforeInvalid,
              QStringLiteral("calls=%1").arg(recorder.calls.size()));

        const bool second = manager.applyTheme(QStringLiteral("Breeze Light"), &error);
        check("theme.second.apply", second && recorder.calls.size() == beforeInvalid + 1
                                        && recorder.calls.last() == QStringLiteral("Breeze Light"),
              recorder.calls.join(QStringLiteral(",")));

        // A manager with no consumer must keep working (demo provider / no
        // editor lane): the seam is optional, not a new required wiring.
        vedit::app::ThemeManager plain;
        QString plainError;
        check("theme.no-applier.noop", plain.applyTheme(QStringLiteral("Breeze Light"), &plainError),
              plainError);
    }

    // --- 2. real EditCoreProvider + real KTextEditor rendering --------------
    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (fixture.isEmpty() || !QFileInfo::exists(fixture)) {
        check("probe.fixture.argument", false, QStringLiteral("usage: theme_seam_probe <file.v>"));
        std::printf("F2-PROBE FAIL failures=%d\n", g_failures);
        return 1;
    }

    // Status-bar consistency: the shell's label is painted from
    // ThemeManager::currentTheme() (app_shell.cpp updateStatusBar). The chain
    // below is the same wiring the shell installs, so "the label names the theme
    // the editor really renders" is asserted end to end.
    vedit::editor::EditCoreProvider provider;
    vedit::app::ThemeManager manager;
    manager.setSyntaxThemeApplier(
        [&provider](const QString &name) { provider.applySyntaxTheme(name); });

    // The catalogue the status label / Preferences list comes from must be
    // renderable by KTextEditor's OWN repository (a separate object, T127).
    // Every name being valid is what makes the fallback path unreachable in the
    // product and keeps the label honest.
    {
        const QStringList names = manager.availableThemes();
        const KSyntaxHighlighting::Repository &repository =
            KTextEditor::Editor::instance()->repository();
        QStringList missing;
        for (const QString &name : names) {
            if (!repository.theme(name).isValid()) {
                missing.append(name);
            }
        }
        check("themes.kteditor-repository-parity",
              !names.isEmpty() && missing.isEmpty(),
              missing.isEmpty() ? QStringLiteral("%1/%1 names valid").arg(names.size())
                                : QStringLiteral("missing: %1").arg(missing.join(QStringLiteral(", "))));
        // The pixel assertions below only mean something if the fixture gets a
        // syntax definition (text rendered as plain text would give a weaker,
        // single-colour render).
        const KSyntaxHighlighting::Definition definition =
            repository.definitionForFileName(fixture);
        check("fixture.syntax-definition-found", definition.isValid(), definition.name());
        std::printf("INFO ktexteditor-themes=%lld fixture-definition=%s\n",
                    static_cast<long long>(repository.themes().size()),
                    qPrintable(definition.name()));
    }

    QString chainError;
    check("chain.apply", manager.applyTheme(QStringLiteral("Breeze Dark"), &chainError), chainError);

    QWidget *widget = provider.createEditor(nullptr);
    KTextEditor::View *view = vedit::editor::viewFor(widget);
    check("view.created", view != nullptr);
    if (view == nullptr) {
        std::printf("F2-PROBE FAIL failures=%d\n", g_failures);
        return 1;
    }

    widget->setObjectName(QStringLiteral("f2-probe-view"));
    widget->resize(960, 620);
    widget->show();
    QCoreApplication::processEvents();

    check("view.theme.config-value",
          view->configValue(QStringLiteral("theme")).toString() == QStringLiteral("Breeze Dark"),
          view->configValue(QStringLiteral("theme")).toString());
    check("view.theme.object", view->theme().name() == QStringLiteral("Breeze Dark"),
          view->theme().name());
    // The status label (ThemeManager::currentTheme()) and the rendered theme are
    // ONE value: the view was created after the apply, through the same seam.
    check("status.label-matches-render",
          manager.currentTheme() == view->theme().name(),
          QStringLiteral("label=%1 render=%2").arg(manager.currentTheme(), view->theme().name()));

    QString openError;
    const bool opened = provider.openFile(widget, fixture, &openError);
    check("view.fixture.open", opened, openError);
    if (!opened) {
        std::printf("F2-PROBE FAIL failures=%d\n", g_failures);
        return 1;
    }

    const QImage darkImage = grab(widget);
    check("view.dark.render-size", darkImage.width() > 100 && darkImage.height() > 100,
          QStringLiteral("%1x%2").arg(darkImage.width()).arg(darkImage.height()));

    // The switch the user makes in Preferences: ThemeManager::applyTheme() ->
    // applier -> provider -> every open view.
    const bool switched = manager.applyTheme(QStringLiteral("Breeze Light"), &chainError);
    check("chain.switch", switched, chainError);
    check("view.theme.switched",
          view->configValue(QStringLiteral("theme")).toString() == QStringLiteral("Breeze Light"),
          view->configValue(QStringLiteral("theme")).toString());
    check("view.theme.object.switched", view->theme().name() == QStringLiteral("Breeze Light"),
          view->theme().name());
    check("status.label-matches-render.switched",
          manager.currentTheme() == view->theme().name(),
          QStringLiteral("label=%1 render=%2").arg(manager.currentTheme(), view->theme().name()));

    const QImage lightImage = grab(widget);
    const int differing = differingPixels(lightImage, darkImage);
    check("view.render.differs", differing > 0,
          QStringLiteral("differing=%1").arg(differing));

    // The theme's OWN background colour must dominate each render. This is the
    // user-visible "the whole editor changed" assertion, independent of which
    // exact token colours the definition assigns.
    const KSyntaxHighlighting::Repository &repository =
        KTextEditor::Editor::instance()->repository();
    using KSEditorColor = KSyntaxHighlighting::Theme::EditorColorRole;
    const QColor darkBackground =
        repository.theme(QStringLiteral("Breeze Dark")).editorColor(KSEditorColor::BackgroundColor);
    const QColor lightBackground =
        repository.theme(QStringLiteral("Breeze Light")).editorColor(KSEditorColor::BackgroundColor);
    const qint64 pixels = qint64(darkImage.width()) * darkImage.height();
    const int darkHits = countPixelsNear(darkImage, darkBackground, 2);
    const int lightHits = countPixelsNear(lightImage, lightBackground, 2);
    check("view.background.dark-theme", darkHits * 100 >= pixels * 20,
          QStringLiteral("hits=%1/%2 (%3%)").arg(darkHits).arg(pixels).arg(darkHits * 100.0 / pixels, 0, 'f', 1));
    check("view.background.light-theme", lightHits * 100 >= pixels * 20,
          QStringLiteral("hits=%1/%2 (%3%)").arg(lightHits).arg(pixels).arg(lightHits * 100.0 / pixels, 0, 'f', 1));

    // A view created AFTER the switch must start with the current theme: the
    // product opens new panes/tabs long after startup (split panes included,
    // every pane view goes through the same createEditor()).
    QWidget *secondWidget = provider.createEditor(nullptr);
    KTextEditor::View *secondView = vedit::editor::viewFor(secondWidget);
    check("view.second.uses-current-theme",
          secondView != nullptr
              && secondView->configValue(QStringLiteral("theme")).toString() == QStringLiteral("Breeze Light"),
          secondView != nullptr ? secondView->configValue(QStringLiteral("theme")).toString() : QStringLiteral("no view"));

    // Documented fallback, asserted rather than assumed: a name KTextEditor's
    // repository does NOT know is never set as a per-view override. The provider
    // falls back to the editor's CURRENT GLOBAL theme (KTextEditor's own
    // default) and only if that is invalid to the repository default, so the
    // view can never keep a half-applied/undefined state. In the product this
    // path is unreachable — the parity check above proves every catalogue name
    // is valid in this repository — which is also why the status label stays
    // honest without a second reporting channel.
    {
        KTextEditor::Editor *editorInstance = KTextEditor::Editor::instance();
        const QString globalTheme = editorInstance->theme().name();
        QString expected = globalTheme;
        if (expected.isEmpty()) {
            expected = editorInstance->repository()
                           .defaultTheme(KSyntaxHighlighting::Repository::LightTheme)
                           .name();
        }
        std::printf("INFO editor-global-theme=%s (per-view theme settings do not change it)\n",
                    qPrintable(globalTheme));
        provider.applySyntaxTheme(QStringLiteral("__vedit-f2-no-such-theme__"));
        check("view.invalid-name.falls-back", view->theme().name() == expected,
              QStringLiteral("render=%1 expected=%2").arg(view->theme().name(), expected));
    }

    delete secondWidget;
    delete widget;

    std::printf("%s failures=%d\n", g_failures == 0 ? "F2-PROBE PASS" : "F2-PROBE FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
