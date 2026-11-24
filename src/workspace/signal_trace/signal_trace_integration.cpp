#include "signal_trace_integration.h"
#include "trace_dialog.h"

#include "../project/language_registry.h"
#include "../project/project_integration.h"
#include "../project/project_model.h"
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QDynamicPropertyChangeEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QStatusBar>
#include <QTimer>
#include <vd_ffi.h>

#include <array>
#include <cstdint>

namespace vedit::workspace {
namespace {
constexpr int kFileLimit = 2000;
constexpr auto kSlangProperty = "vedit.diagnostics.toolchainSlang";

struct Command {
    const char *id;
    const char *label;
    bool driver;
    bool full;
};

constexpr std::array<Command, 4> kCommands{{
    {"signal.trace.driver", QT_TRANSLATE_NOOP("VEdit", "Trace Driver"), true, false},
    {"signal.trace.load", QT_TRANSLATE_NOOP("VEdit", "Trace Load"), false, false},
    {"signal.trace.driver.chain", QT_TRANSLATE_NOOP("VEdit", "Show Driver Chain"), true, true},
    {"signal.trace.load.chain", QT_TRANSLATE_NOOP("VEdit", "Show Load Chain"), false, true},
}};

class Strings {
public:
    explicit Strings(const QStringList &values)
    {
        for (const auto &value : values) {
            storage_.append(value.toUtf8());
        }
        for (auto &value : storage_) {
            pointers_.append(value.data());
        }
    }

    VdStringList list() { return {static_cast<std::size_t>(pointers_.size()), pointers_.data()}; }

private:
    QList<QByteArray> storage_;
    QList<char *> pointers_;
};

class TraceLane final : public QObject {
public:
    TraceLane(vedit::app::ApplicationShell &shell, project::ProjectModel &model)
        : QObject(&shell), shell_(shell), model_(model)
    {
        for (std::size_t i = 0; i < kCommands.size(); ++i) {
            const auto command = kCommands[i];
            vedit::app::CommandSpec spec;
            spec.id = QString::fromLatin1(command.id);
            spec.text = QCoreApplication::translate("VEdit", command.label);
            spec.menu = QStringLiteral("project");
            spec.order = 74 + static_cast<int>(i);
            actions_[i] = shell_.commands()->add(spec);
            shell_.commands()->setHandler(spec.id, [this, command]() { request(command); });
        }
        gateTimer_.setInterval(180);
        connect(&gateTimer_, &QTimer::timeout, this, [this]() { updateGate(); });
        gateTimer_.start();
        pollTimer_.setInterval(35);
        connect(&pollTimer_, &QTimer::timeout, this, [this]() { poll(); });
        connect(&model_, &project::ProjectModel::stateChanged, this, [this]() { invalidate(); updateGate(); });
        connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this]() { invalidate(); });
        connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { invalidate(); });
        updateGate();
    }

    ~TraceLane() override
    {
        if (job_ != 0) {
            vd_hier_release(service_, job_);
        }
        vd_service_destroy(service_);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange) {
            const auto *change = static_cast<QDynamicPropertyChangeEvent *>(event);
            if (change->propertyName() == kSlangProperty) {
                invalidate();
                updateGate();
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    KTextEditor::View *view() const
    {
        const auto *pane = shell_.editorArea()->activePane();
        return pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget()) : nullptr;
    }

    QString unavailable(KTextEditor::View *editor) const
    {
        if (!model_.hasFilelist()) {
            return QCoreApplication::translate("VEdit", "Import a project file list to trace signals.");
        }
        if (model_.isPending() || model_.files().isEmpty()) {
            return QCoreApplication::translate("VEdit", "Signal tracing needs an expanded project file list.");
        }
        if (shell_.property(kSlangProperty).toString().isEmpty()) {
            return QCoreApplication::translate("VEdit", "Signal tracing requires the slang compiler. Check Preferences → Tools.");
        }
        if (model_.files().size() > kFileLimit) {
            return QCoreApplication::translate("VEdit", "Signal tracing supports up to %1 project files.").arg(kFileLimit);
        }
        const QString path = editor != nullptr ? editor->document()->url().toLocalFile() : QString();
        const auto *language = project::LanguageRegistry::instance().languageFor(path);
        if (language == nullptr || language->id != QStringLiteral("systemverilog")) {
            return QCoreApplication::translate("VEdit", "Open a Verilog or SystemVerilog file to trace signals.");
        }
        return editor->document()->isModified() ? QCoreApplication::translate("VEdit", "Save this file before tracing signals.") : QString();
    }

    void updateGate()
    {
        const QString reason = unavailable(view());
        for (auto *action : actions_) {
            action->setEnabled(reason.isEmpty());
            action->setToolTip(reason);
        }
    }

    void invalidate()
    {
        ++generation_;
        pollTimer_.stop();
        if (job_ != 0) {
            vd_hier_release(service_, job_);
            job_ = 0;
        }
        const auto files = watcher_.files();
        if (!files.isEmpty()) {
            watcher_.removePaths(files);
        }
        const auto directories = watcher_.directories();
        if (!directories.isEmpty()) {
            watcher_.removePaths(directories);
        }
        if (dialog_) {
            dialog_->close();
        }
        if (pending_) {
            pending_ = false;
            status(QCoreApplication::translate("VEdit", "Project inputs changed. Run the trace again."));
        }
    }

    void request(Command command);
    void poll();
    void present(const QJsonObject &answer);
    void status(const QString &message) { shell_.statusBar()->showMessage(message, 6000); }

    vedit::app::ApplicationShell &shell_;
    project::ProjectModel &model_;
    std::array<QAction *, kCommands.size()> actions_{};
    QTimer gateTimer_;
    QTimer pollTimer_;
    QFileSystemWatcher watcher_;
    VdService *service_ = nullptr;
    std::uint64_t job_ = 0;
    quint64 generation_ = 0;
    quint64 requestedGeneration_ = 0;
    bool pending_ = false;
    bool cacheable_ = true;
    QPointer<KTextEditor::View> origin_;
    KTextEditor::Cursor caret_;
    QString file_;
    std::uint32_t tokenColumn_ = 0;
    Command command_ = kCommands.front();
    QPointer<QDialog> dialog_;
};

bool identifier(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

void TraceLane::request(Command command)
{
    auto *editor = view();
    const QString reason = unavailable(editor);
    if (!reason.isEmpty()) {
        status(reason);
        return;
    }
    caret_ = editor->cursorPosition();
    const QString line = editor->document()->line(caret_.line());
    int start = caret_.column();
    if (start >= line.size() || !identifier(line[start])) {
        status(QCoreApplication::translate("VEdit", "Place the cursor on a signal identifier."));
        return;
    }
    while (start > 0 && identifier(line[start - 1])) {
        --start;
    }
    const QString path = editor->document()->url().toLocalFile();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    file_ = canonical.isEmpty() ? path : canonical;
    origin_ = editor;
    tokenColumn_ = static_cast<std::uint32_t>(start);
    command_ = command;
    if (job_ == 0) {
        if (service_ == nullptr) {
            service_ = vd_service_create();
        }
        const auto files = model_.files();
        QStringList inputs;
        for (const auto &file : files) {
            const auto *language = project::LanguageRegistry::instance().languageFor(file);
            if (language != nullptr && language->id == QStringLiteral("systemverilog")) {
                inputs.append(file);
            }
        }
        Strings source(inputs);
        Strings includes(model_.includeDirs());
        Strings defines(model_.defines());
        const QByteArray tool = shell_.property(kSlangProperty).toString().toUtf8();
        const QByteArray top = model_.top().toUtf8();
        const QByteArray root = model_.projectRoot().toUtf8();
        VdHierOptions options{};
        options.slang_binary = tool.constData();
        options.top = top.isEmpty() ? nullptr : top.constData();
        options.include_dirs = includes.list();
        options.defines = defines.list();
        options.max_files = kFileLimit;
        auto watched = inputs + model_.filelists() + model_.includeDirs();
        watched.removeDuplicates();
        cacheable_ = watched.isEmpty() || watcher_.addPaths(watched).isEmpty();
        const auto list = source.list();
        job_ = vd_hier_trace_start(service_, &list, &options, root.constData());
        if (job_ == 0) {
            VdError error{};
            vd_service_error(service_, &error);
            invalidate();
            status(QCoreApplication::translate("VEdit", "Could not start signal tracing. %1").arg(QString::fromUtf8(error.message)));
            return;
        }
    }
    requestedGeneration_ = generation_;
    pending_ = true;
    poll();
    if (pending_) {
        status(QCoreApplication::translate("VEdit", "Analyzing project signals with slang…"));
        pollTimer_.start();
    }
}

void TraceLane::poll()
{
    const QByteArray file = file_.toUtf8();
    char *json = nullptr;
    const int result = vd_hier_trace_poll(service_, job_, file.constData(),
        static_cast<std::uint32_t>(caret_.line()), tokenColumn_, (command_.driver ? 0u : 1u) + (command_.full ? 2u : 0u), &json);
    if (result == VD_STATUS_PENDING) {
        return;
    }
    pending_ = false;
    pollTimer_.stop();
    if (result != VD_STATUS_READY) {
        VdError error{};
        vd_service_error(service_, &error);
        invalidate();
        status(QCoreApplication::translate("VEdit", "Signal tracing failed. %1").arg(QString::fromUtf8(error.message)));
        return;
    }
    const auto answer = QJsonDocument::fromJson(QByteArray(json)).object();
    vd_string_free(json);
    if (answer.contains(QStringLiteral("dependencies"))) {
        const auto dependencies = answer.value(QStringLiteral("dependencies")).toArray();
        if (dependencies.isEmpty() || dependencies.size() > 4000) {
            cacheable_ = false;
        } else {
            QStringList additional;
            const auto currentFiles = watcher_.files();
            const QSet<QString> watched(currentFiles.cbegin(), currentFiles.cend());
            for (const auto &dependency : dependencies) {
                const QString path = dependency.toString();
                if (!path.isEmpty() && !watched.contains(path)) {
                    additional.append(path);
                }
            }
            additional.removeDuplicates();
            if (!additional.isEmpty() && !watcher_.addPaths(additional).isEmpty()) {
                cacheable_ = false;
            }
        }
    }
    const bool current = requestedGeneration_ == generation_ && origin_ && view() == origin_
                         && origin_->cursorPosition() == caret_ && !origin_->document()->isModified();
    if (!cacheable_) {
        invalidate();
    }
    if (!current) {
        status(QCoreApplication::translate("VEdit", "Signal trace discarded because the editor position changed."));
        return;
    }
    present(answer);
}

void TraceLane::present(const QJsonObject &answer)
{
    if (dialog_) {
        dialog_->close();
    }
    using namespace signal_trace;
    const auto roots = answer.value(QStringLiteral("roots")).toArray();
    const auto children = roots.isEmpty() ? QJsonArray{} : roots.at(0).toObject()
                                           .value(QStringLiteral("children")).toArray();
    if (roots.isEmpty() || (roots.size() == 1 && children.isEmpty())) {
        status(roots.isEmpty() ? QCoreApplication::translate("VEdit", "No elaborated signal at the cursor.")
                              : command_.driver ? QCoreApplication::translate("VEdit", "No drivers found.") : QCoreApplication::translate("VEdit", "No loads found."));
        return;
    }
    if (!command_.full && roots.size() == 1 && children.size() == 1) {
        shell_.statusBar()->clearMessage();
        jumpTo(&shell_, locationOf(children.at(0).toObject()
                                      .value(QStringLiteral("location")).toObject()));
        return;
    }
    dialog_ = showResults(&shell_, QCoreApplication::translate("VEdit", command_.label), answer, command_.full);
    shell_.statusBar()->clearMessage();
}
} // namespace

void installSignalTrace(vedit::app::ApplicationShell &shell, const IntegrationOptions &)
{
    if (auto *model = projectModelForShell(shell)) {
        shell.installEventFilter(new TraceLane(shell, *model));
    }
}
} // namespace vedit::workspace
