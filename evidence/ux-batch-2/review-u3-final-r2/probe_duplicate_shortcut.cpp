// Controlled probe for the U3-F1 duplicate-shortcut detector (read-only review).
// Registers two commands with the SAME default sequence and checks that
//   (a) CommandRegistry::add emits the duplicate warning, and
//   (b) CommandRegistry::duplicateShortcuts() reports the pair.
// A detector that cannot fire would print duplicates=0 / warnings=0 and exit 1.
#include <QApplication>
#include <QAction>
#include <QStringList>
#include <cstdio>

#include "command_registry.h"

static QStringList g_warnings;
static void handler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_warnings.append(message);
}

int main(int argc, char **argv)
{
    qInstallMessageHandler(handler);
    QApplication app(argc, argv);
    QObject owner;

    vedit::app::CommandRegistry registry(&owner);
    auto spec = [](const char *id) {
        vedit::app::CommandSpec s;
        s.id = QString::fromLatin1(id);
        s.text = s.id;
        s.menu = QStringLiteral("probe");
        s.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Shift+F"));
        return s;
    };
    registry.add(spec("probe.first"));
    registry.add(spec("probe.second"));

    const QStringList duplicates = registry.duplicateShortcuts();
    const QStringList bindings = registry.shortcutBindings();
    std::printf("BINDINGS=%d %s\n", static_cast<int>(bindings.size()),
                qPrintable(bindings.join(QStringLiteral(" ; "))));
    std::printf("DUPLICATES=%d %s\n", static_cast<int>(duplicates.size()),
                qPrintable(duplicates.join(QStringLiteral(" | "))));
    std::printf("WARNINGS=%d %s\n", static_cast<int>(g_warnings.size()),
                qPrintable(g_warnings.join(QStringLiteral(" | "))));
    const bool detected = !duplicates.isEmpty();
    const bool warned = !g_warnings.isEmpty();
    std::printf("DETECTOR %s\n", detected && warned ? "FIRES" : "DID-NOT-FIRE");
    qInstallMessageHandler(nullptr);
    return detected && warned ? 0 : 1;
}
