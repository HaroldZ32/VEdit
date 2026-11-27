#include "app_shell.h"
#include "command_registry.h"
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"
#include "panel_registry.h"
#include "project_integration.h"
#include "project_model.h"
#include "project_macro_override_fixture.h"
#include "runtime_init.h"
#include "toolchain.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <cstdio>

namespace {
using namespace Qt::StringLiterals;
namespace workspace = vedit::workspace;
namespace diagnostics = workspace::diagnostics;

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly)
        && file.write(content) == content.size();
}

bool waitFor(auto predicate, int timeoutMs = 7000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        QApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    } while (elapsed.elapsed() < timeoutMs);
    return false;
}

bool addDefine(QWidget &shell, const QString &define)
{
    auto *button = shell.findChild<QPushButton *>(u"project-add-define"_s);
    if (button == nullptr) {
        return false;
    }
    bool submitted = false;
    QTimer::singleShot(0, &shell, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (dialog != nullptr) {
            dialog->setTextValue(define);
            dialog->accept();
            submitted = true;
        }
    });
    button->click();
    return submitted;
}

bool removeDefine(QWidget &shell, const QString &define)
{
    auto *list = shell.findChild<QListWidget *>(u"project-defines"_s);
    auto *button = shell.findChild<QPushButton *>(u"project-remove-define"_s);
    if (list == nullptr || button == nullptr) {
        return false;
    }
    const auto items = list->findItems(define, Qt::MatchExactly);
    if (items.size() != 1) {
        return false;
    }
    list->setCurrentItem(items.constFirst());
    button->click();
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication qt(argc, argv);
    const auto tools = diagnostics::resolveToolchain(diagnostics::applicationToolchainContext());
    if (!tools.slang.found() || !tools.slangServer.found()) {
        std::puts("SKIP: requires slang and slang-server");
        return 77;
    }
    QTemporaryDir temp;
    const QString root = temp.filePath(u"project"_s);
    const QString state = temp.filePath(u"state"_s);
    const QString top = root + u"/rtl/top.sv"_s;
    const QString original = root + u"/rtl/original.sv"_s;
    const QString alternate = root + u"/include/alternate.svh"_s;
    const QString alternateOwner = root + u"/lib/alternate.sv"_s;
    const QString macroParent = root + u"/rtl/macro_parent.sv"_s;
    const QString scopedParent = root + u"/rtl/scoped_parent.sv"_s;
    const QString scopedBridge = root + u"/rtl/scoped_bridge.sv"_s;
    const QString variant = root + u"/rtl/variant.sv"_s;
    const QString driver = root + u"/rtl/variant_driver.sv"_s;
    const QString filelist = root + u"/sources.f"_s;
    const QByteArray literalInstance = "  alternate via_literal (.correct(1'b0));\n";
    const QByteArray macroParentText =
        QByteArray("`define KEEP_CHILD alternate via_macro (.macro_bad_port(1'b0));\n"
                   "module macro_parent;\n") + literalInstance
        + "  `KEEP_CHILD\n  genuinely_missing u_missing ();\nendmodule\n";
    const QByteArray topText =
        "module top;\n"
        "  original u_child (.correct(1'b0));\n"
        "  genuinely_missing u_missing ();\n"
        "  `include \"options.svh\"\n"
        "  `ifdef PROJECT_WIDTH\n"
        "    if (`PROJECT_WIDTH == 2) begin : width_two\n"
        "      assign probe = only_width_two;\n"
        "    end else begin : changed_width\n"
        "      assign probe = only_changed_width;\n"
        "    end\n"
        "  `else\n"
        "    assign probe = only_undefined_width;\n"
        "  `endif\n"
        "endmodule\n";
    if (!temp.isValid() || !writeFile(top, topText)
        || !writeFile(original, "module original(input logic correct); endmodule\n")
        || !writeFile(alternate, "`ifdef ENABLE_ALTERNATE\n"
                                 "module alternate(input logic correct); endmodule\n`endif\n")
        || !writeFile(alternateOwner, "`define ENABLE_ALTERNATE\n`include \"alternate.svh\"\n")
        || !writeFile(root + u"/rtl/options.svh"_s, "logic probe;\n")
        || !writeFile(macroParent, macroParentText)
        || !writeFile(scopedParent, "module scoped_top; scoped_bridge u_bridge (); endmodule\n"
                                    "module scoped_sibling;\n"
                                    "  alternate u_child (.sibling_bad_port(1'b0));\n"
                                    "  genuinely_missing u_missing ();\nendmodule\n")
        || !writeFile(scopedBridge, "module scoped_bridge; alternate u_child (.correct(1'b0)); endmodule\n")
        || !writeFile(variant, "module variant #(parameter bit USE_ALTERNATE = 1);\n"
                               "  if (USE_ALTERNATE) begin : use_alternate\n"
                               "    alternate u_child (.variant_bad_port(1'b0));\n"
                               "    genuinely_missing u_missing ();\n"
                               "  end else begin : use_original\n"
                               "    original u_child (.correct(1'b0));\n"
                               "  end\nendmodule\n")
        || !writeFile(driver, "module variant_driver; variant #(.USE_ALTERNATE(0)) u_variant (); endmodule\n")
        || !writeFile(filelist, "rtl/top.sv\nrtl/original.sv\nlib/alternate.sv\nrtl/macro_parent.sv\n"
                              "rtl/scoped_parent.sv\nrtl/scoped_bridge.sv\nrtl/variant.sv\nrtl/variant_driver.sv\n"
                              "+incdir+rtl+include\n")
        || !writeFile(state + u"/settings.toml"_s, "\"tools.lsp_backend\" = \"slang\"\n")) {
        std::puts("FAIL: fixture creation");
        return 1;
    }

    auto editor = vedit::editor::makeEditCoreProvider();
    vedit::app::ShellOptions options;
    options.projectRoot = root;
    options.stateDir = state;
    options.restoreSession = false;
    vedit::app::ApplicationShell shell(options);
    shell.editorArea()->setProvider(editor.get());
    shell.editorArea()->ensurePane();
    workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = state;
    workspace::installProject(shell, integration);
    workspace::installModuleTree(shell, integration);
    workspace::installDiagnostics(shell, integration);
    shell.panels()->showPanel(u"outline"_s, &shell);
    shell.resize(1050, 700);
    shell.show();

    auto *model = workspace::projectModelForShell(shell);
    auto *tree = shell.findChild<workspace::module_tree::ModuleTreePanel *>();
    auto *controller = workspace::diagnosticsControllerForShell(shell);
    auto *pane = shell.editorArea()->activePane();
    QString error;
    if (model == nullptr || tree == nullptr || controller == nullptr || pane == nullptr
        || !pane->openFile(top, &error)) {
        std::printf("FAIL: open: %s\n", qPrintable(error));
        return 1;
    }
    auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    if (view == nullptr) {
        std::puts("FAIL: missing editor view");
        return 1;
    }
    const auto diagnosticsText = [&] {
        QStringList messages;
        for (const auto &row : controller->lowerRowsForFile(top)) {
            messages.append(row.message);
        }
        return messages.join(u'|');
    };
    const auto expectWidth = [&](const char *label, const QString &expected) {
        const bool pass = waitFor([&] {
            const QString rows = diagnosticsText();
            return controller->session().backendName == u"slang"
                && rows.contains(expected) && rows.contains(u"genuinely_missing")
                && (!rows.contains(u"only_width_two") || expected == u"only_width_two")
                && (!rows.contains(u"only_changed_width") || expected == u"only_changed_width")
                && (!rows.contains(u"only_undefined_width") || expected == u"only_undefined_width");
        });
        std::printf("%s pass=%d authored=[%s] resolved=[%s] rows=[%s]\n", label, pass,
                    qPrintable(model->projectDefines().join(u',')), qPrintable(model->defines().join(u',')),
                    qPrintable(diagnosticsText()));
        return pass;
    };
    if (!model->importFilelists({filelist}, &error)) {
        std::printf("FAIL: import: %s\n", qPrintable(error));
        return 1;
    }
    const bool catalogReady = waitFor([&] {
        const auto catalog = tree->catalog();
        const auto *definition = catalog.find(u"alternate"_s);
        return definition != nullptr && QFileInfo(definition->file).canonicalFilePath() == alternate
            && QFileInfo(definition->compilationUnit).canonicalFilePath() == alternateOwner
            && controller->fileSetReport().files.contains(original);
    });
    const bool initial = expectWidth("AFTER-IMPORT", u"only_undefined_width"_s);
    const bool added = addDefine(shell, u"PROJECT_WIDTH=2"_s);
    const bool importedMacro = expectWidth("ADDED", u"only_width_two"_s);
    const bool changed = removeDefine(shell, u"PROJECT_WIDTH=2"_s) && addDefine(shell, u"PROJECT_WIDTH=3"_s);
    const bool changedMacro = expectWidth("REPLACED", u"only_changed_width"_s);
    const bool removed = removeDefine(shell, u"PROJECT_WIDTH=3"_s);
    const bool removedMacro = expectWidth("REMOVED", u"only_undefined_width"_s);

    QString edited = QString::fromUtf8(topText);
    edited.replace(u"original u_child"_s, u"alternate u_child"_s);
    edited.replace(u".correct"_s, u".intentional_bad_port"_s);
    view->document()->setText(edited);
    const bool childResolved = waitFor([&] {
        const QString rows = diagnosticsText();
        return rows.contains(u"genuinely_missing") && rows.contains(u"intentional_bad_port")
            && !rows.contains(u"unknown module 'alternate'")
            && controller->fileSetReport().files.contains(alternateOwner)
            && !controller->fileSetReport().files.contains(alternate)
            && !controller->fileSetReport().files.contains(original);
    });
    std::printf("DIRECT-CHILD tree=%d pass=%d inputs=[%s] rows=[%s]\n", catalogReady, childResolved,
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(diagnosticsText()));
    const auto knownInstanceResolved = [&] {
        const QString rows = diagnosticsText();
        return rows.contains(u"genuinely_missing") && !rows.contains(u"unknown module 'alternate'")
            && !rows.contains(u"intentional_bad_port") && controller->session().state == diagnostics::BackendState::Ready;
    };
    edited.replace(u".intentional_bad_port"_s, u".correct"_s);
    view->document()->setText(edited);
    const bool corrected = waitFor(knownInstanceResolved);
    QElapsedTimer settle;
    settle.start();
    const bool settled = waitFor([&] { return settle.elapsed() >= 900; }, 1400);
    QFile savedSource(top);
    const bool diskUnchanged = savedSource.open(QIODevice::ReadOnly) && savedSource.readAll() == topText;
    const bool fresh = corrected && settled && knownInstanceResolved() && diskUnchanged;
    std::printf("DIRECT-CHILD-CORRECTED fresh=%d disk-unchanged=%d rows=[%s]\n", fresh, diskUnchanged,
                qPrintable(diagnosticsText()));
    auto *save = shell.commands()->action(u"file.save"_s);
    if (save == nullptr) {
        std::puts("FAIL: missing Save action");
        return 1;
    }
    save->trigger();
    QFile reopenedSource(top);
    const bool saved = reopenedSource.open(QIODevice::ReadOnly) && reopenedSource.readAll() == edited.toUtf8();
    const bool closed = saved && pane->closeTab(pane->activeTabIndex()) && pane->openFile(original, &error)
        && waitFor([&] { return controller->analyzeFileName() == original; });
    const quint64 beforeReopen = controller->analysisCount();
    const bool reopened = closed && pane->closeTab(pane->activeTabIndex()) && pane->openFile(top, &error)
        && waitFor([&] {
            return controller->analysisCount() > beforeReopen && controller->fileSetReport().files.contains(alternateOwner)
                && knownInstanceResolved() && !controller->analyzeWasDirty();
        });
    std::printf("SAVED-REOPEN pass=%d saved=%d inputs=[%s] rows=[%s]\n", reopened, saved,
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(diagnosticsText()));

    const auto *moduleTree = tree->findChild<QTreeWidget *>(u"module-tree"_s);
    auto *outline = shell.findChild<QTreeWidget *>(u"diagnosticsIssues"_s);
    const auto outlineRows = [&] {
        QStringList messages;
        if (outline != nullptr) {
            for (QTreeWidgetItemIterator item(outline); *item != nullptr; ++item) {
                if ((*item)->parent() != nullptr) {
                    messages.append((*item)->text(0));
                }
            }
        }
        return messages.join(u'|');
    };
    model->setTop(u"scoped_top"_s);
    const bool scopedTreeReady = waitFor([&] {
        const auto catalog = tree->catalog();
        return catalog.valid && catalog.top == u"scoped_top" && catalog.reachableFiles.contains(scopedParent)
            && moduleTree != nullptr
            && !moduleTree->findItems(u"alternate"_s, Qt::MatchExactly | Qt::MatchRecursive, 0).isEmpty();
    });
    const bool openedScopedParent = pane->closeTab(pane->activeTabIndex()) && pane->openFile(scopedParent, &error);
    const bool siblingResolved = openedScopedParent && waitFor([&] {
        const QString rows = outlineRows();
        return controller->analyzeFileName() == scopedParent && !controller->analyzeWasDirty()
            && controller->fileSetReport().files == QStringList{alternateOwner, scopedBridge, scopedParent}
            && rows.contains(u"sibling_bad_port") && rows.contains(u"genuinely_missing")
            && !rows.contains(u"unknown module 'alternate'");
    });
    std::printf("SCOPED-SIBLING tree=%d pass=%d inputs=[%s] outline=[%s]\n", scopedTreeReady, siblingResolved,
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(outlineRows()));
    model->setTop({});
    const bool restoredAuto = waitFor([&] { return tree->catalog().valid && tree->catalog().top.isEmpty(); });
    const bool openedVariant = pane->closeTab(pane->activeTabIndex()) && pane->openFile(variant, &error);
    const bool variantResolved = openedVariant && waitFor([&] {
        const QString rows = outlineRows();
        return controller->analyzeFileName() == variant && !controller->analyzeWasDirty()
            && controller->fileSetReport().files == QStringList{alternateOwner, original, variant}
            && rows.contains(u"variant_bad_port") && rows.contains(u"genuinely_missing")
            && !rows.contains(u"unknown module 'alternate'");
    });
    std::printf("AUTO-VARIANT pass=%d inputs=[%s] outline=[%s]\n", variantResolved,
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(outlineRows()));
    const bool openedMacroParent = pane->closeTab(pane->activeTabIndex()) && pane->openFile(macroParent, &error);
    const auto macroRows = [&] {
        QStringList messages;
        for (const auto &row : controller->lowerRowsForFile(macroParent)) {
            messages.append(row.message);
        }
        return messages.join(u'|');
    };
    const bool macroReady = openedMacroParent && waitFor([&] {
        return controller->analyzeFileName() == macroParent && macroRows().contains(u"macro_bad_port")
            && controller->fileSetReport().files == QStringList{alternateOwner, macroParent};
    });
    auto *macroView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    bool keptMacroOwner = false;
    if (macroReady && macroView != nullptr) {
        QByteArray macroEdit = macroParentText;
        macroEdit.replace(literalInstance, "");
        const quint64 beforeRemoval = controller->analysisCount();
        macroView->document()->setText(QString::fromUtf8(macroEdit));
        keptMacroOwner = waitFor([&] {
            const auto rows = controller->lowerRowsForFile(macroParent);
            const bool freshPort = std::any_of(rows.cbegin(), rows.cend(), [](const auto &row) {
                return row.line == 2 && row.message.contains(u"macro_bad_port");
            });
            return controller->analysisCount() > beforeRemoval && freshPort
                && controller->fileSetReport().files == QStringList{alternateOwner, macroParent}
                && macroRows().contains(u"genuinely_missing") && !macroRows().contains(u"unknown module 'alternate'");
        });
    }
    std::printf("MACRO-CHILD pass=%d before=%d inputs=[%s] rows=[%s]\n", keptMacroOwner, macroReady,
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(macroRows()));

    const bool manualOnly = controller->projectPassSerial() == 0 && !controller->projectPassRunning();
    std::printf("PROJECT-MANUAL-ONLY pass=%d\n", manualOnly);
    const bool macroOverride = project_macro_fixture::run(
        shell, *model, *controller, state, writeFile,
        [](auto predicate) { return waitFor(predicate); }, addDefine, removeDefine);
    return initial && added && catalogReady && importedMacro && changed && changedMacro
            && removed && removedMacro && childResolved && fresh && reopened && scopedTreeReady
            && siblingResolved && restoredAuto && variantResolved && keptMacroOwner && manualOnly
            && macroOverride ? 0 : 1;
}
