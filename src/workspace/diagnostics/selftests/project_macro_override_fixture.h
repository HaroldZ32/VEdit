#pragma once

#include "app_shell.h"
#include "diagnostics_controller.h"
#include "editor_area.h"
#include "project_model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>

#include <cstdio>

namespace project_macro_fixture {
using namespace Qt::StringLiterals;

inline bool run(vedit::app::ApplicationShell &shell,
                vedit::workspace::project::ProjectModel &model,
                vedit::workspace::diagnostics::DiagnosticsController &controller,
                const QString &state, const auto &writeFile, const auto &waitFor,
                const auto &addDefine, const auto &removeDefine)
{
    const QString root = model.projectRoot();
    const QString source = root + u"/rtl/macro_override.sv"_s;
    const QString filelist = root + u"/macro_override.f"_s;
    const QString nativeConfig = root + u"/.slang/server.json"_s;
    const QString nativeFlags = root + u"/config/compiler.f"_s;
    const QByteArray configText =
        "{\"indexGlobs\":[\"rtl/original.sv\"],\"flags\":\"-DDOCK_CONFIG_PRESENT -F config/compiler.f\"}\n";
    const QByteArray flagsText = "-D DOCK_OVERRIDE_WIDTH=5\n-D DOCK_NESTED_PRESENT\n";
    const QString macro = u"DOCK_OVERRIDE_WIDTH"_s;
    const auto onlyWidth = [&](QStringList entries) {
        entries.removeIf([&](const QString &entry) {
            return entry != macro && !entry.startsWith(macro + u'=');
        });
        return entries;
    };
    QString error;
    auto *pane = shell.editorArea()->activePane();
    auto *list = shell.findChild<QListWidget *>(u"project-defines"_s);
    if (pane == nullptr || list == nullptr
        || !writeFile(source,
               "module macro_override;\n"
               "  genuinely_missing u_missing ();\n"
               "  logic sink;\n"
               "  logic [`DOCK_OVERRIDE_WIDTH-1:0] bits;\n"
               "  assign sink = bits[8];\n"
               "`ifndef DOCK_CONFIG_PRESENT\n"
               "  missing_configured_macro u_config ();\n"
               "`endif\n"
               "`ifndef DOCK_NESTED_PRESENT\n"
               "  missing_nested_macro u_nested ();\n"
               "`endif\n"
               "endmodule\n")
        || !writeFile(filelist, "+define+DOCK_OVERRIDE_WIDTH=2\nrtl/macro_override.sv\n")
        || !writeFile(state + u"/settings.toml"_s,
               "\"tools.lsp_backend\" = \"slang\"\n"
               "\"tools.defines\" = \"DOCK_OVERRIDE_WIDTH=7\\nDOCK_SETTINGS_PRESENT\"\n")
        || !writeFile(nativeConfig, configText) || !writeFile(nativeFlags, flagsText)
        || !model.importFilelists({filelist}, &error) || !pane->openFile(source, &error)) {
        std::printf("MACRO-OVERRIDE setup failed: %s\n", qPrintable(error));
        return false;
    }

    const auto diagnosticsText = [&] {
        QStringList messages;
        for (const auto &row : controller.lowerRowsForFile(source)) {
            messages.append(row.message);
        }
        return messages.join(u'|');
    };
    const auto displayed = [&] {
        QStringList entries;
        for (int row = 0; row < list->count(); ++row) {
            entries.append(list->item(row)->text());
        }
        return onlyWidth(entries);
    };
    const auto expect = [&](const char *phase, const QString &entry, bool authored, int highBit) {
        const QStringList effective{entry};
        const QStringList own = authored ? effective : QStringList{};
        const QJsonArray persisted = QJsonArray::fromStringList(own);
        const QString expected = u"element 8 of 'logic[%1:0]'"_s.arg(highBit);
        const bool pass = waitFor([&] {
            const QString rows = diagnosticsText();
            QFile stored(model.statePath());
            return !model.isPending() && model.hasFilelist() && model.files().contains(source)
                && displayed() == effective && onlyWidth(model.defines()) == effective
                && model.defines().contains(u"DOCK_SETTINGS_PRESENT"_s)
                && onlyWidth(model.projectDefines()) == own && stored.open(QIODevice::ReadOnly)
                && QJsonDocument::fromJson(stored.readAll()).object()
                       .value(u"projectDefines"_s).toArray() == persisted
                && controller.session().backendName == u"slang" && rows.contains(expected)
                && rows.contains(u"genuinely_missing") && !rows.contains(u"missing_configured_macro")
                && !rows.contains(u"missing_nested_macro");
        });
        std::printf("MACRO-OVERRIDE %s pass=%d authored=[%s] resolved=[%s] displayed=[%s] rows=[%s]\n",
                    phase, pass, qPrintable(model.projectDefines().join(u',')),
                    qPrintable(model.defines().join(u',')), qPrintable(displayed().join(u',')),
                    qPrintable(diagnosticsText()));
        return pass;
    };

    const bool baseline = expect("NATIVE-FALLBACK", macro + u"=2"_s, false, 4);
    const bool added = addDefine(shell, macro + u"=2"_s);
    const bool sameValue = expect("ADDED-SAME-AS-FILELIST", macro + u"=2"_s, true, 1);
    const bool replaced = addDefine(shell, macro + u"=3"_s);
    const bool override = expect("REPLACED", macro + u"=3"_s, true, 2);
    const bool updated = addDefine(shell, macro);
    const bool nameOnly = expect("UPDATED-NAME-ONLY", macro, true, 0);
    const bool removed = removeDefine(shell, macro);
    const bool fallback = expect("REMOVED-TO-NATIVE", macro + u"=2"_s, false, 4);
    QFile storedConfig(nativeConfig);
    QFile storedFlags(nativeFlags);
    const bool unchanged = storedConfig.open(QIODevice::ReadOnly) && storedConfig.readAll() == configText
        && storedFlags.open(QIODevice::ReadOnly) && storedFlags.readAll() == flagsText;
    std::printf("MACRO-OVERRIDE NATIVE-FILES-UNCHANGED pass=%d\n", unchanged);
    return baseline && added && sameValue && replaced && override && updated && nameOnly
        && removed && fallback && unchanged;
}
} // namespace project_macro_fixture
