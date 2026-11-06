#include "project_lint_trigger.h"

#include <QTimer>

namespace vedit::workspace::diagnostics {

ProjectLintTrigger::ProjectLintTrigger(DiagnosticsController *controller, QObject *parent)
    : QObject(parent)
    , controller_(controller)
    , debounce_(new QTimer(this))
{
    debounce_->setSingleShot(true);
    debounce_->setInterval(700);
    connect(debounce_, &QTimer::timeout, this, [this]() { fire(); });
}

void ProjectLintTrigger::setRequestBuilder(RequestBuilder builder)
{
    requestBuilder_ = std::move(builder);
}

void ProjectLintTrigger::setOpenFilesProvider(OpenFilesProvider provider)
{
    openFiles_ = std::move(provider);
}

void ProjectLintTrigger::setResultSink(ResultSink sink)
{
    sink_ = std::move(sink);
}

void ProjectLintTrigger::setDebounceInterval(int milliseconds)
{
    debounce_->setInterval(milliseconds > 0 ? milliseconds : 0);
}

int ProjectLintTrigger::debounceInterval() const
{
    return debounce_->interval();
}

bool ProjectLintTrigger::pending() const
{
    return debounce_->isActive();
}

void ProjectLintTrigger::noteSaved(const QString &file)
{
    lastSavedFile_ = file;
    ++saveCount_;
    // Restarting the single-shot timer IS the debounce: three quick saves leave
    // ONE pending pass, and nothing here touches the disk or the GUI thread.
    debounce_->start();
}

void ProjectLintTrigger::flushPending()
{
    if (!debounce_->isActive()) {
        return;
    }
    debounce_->stop();
    fire();
}

void ProjectLintTrigger::fire()
{
    if (controller_ == nullptr || !requestBuilder_) {
        return;
    }
    LintPassRequest request = requestBuilder_();
    if (request.files.isEmpty()) {
        if (sink_) {
            sink_(false, QStringLiteral("no project file set"));
        }
        return;
    }
    // Single-flight: a running pass is cancelled, never queued behind. The
    // newest request wins; the cancelled pass's rows are not appended.
    if (controller_->projectPassRunning()) {
        controller_->cancelProjectPass();
        ++cancelledRunning_;
    }
    // F6 ordering fix: the open-file snapshot must be recorded BEFORE the pass
    // starts, or the pass lints files the LSP already covers (first run: every
    // file, because the snapshot was still empty).
    if (openFiles_) {
        controller_->setOpenFiles(openFiles_());
    }
    controller_->setNextPassAutomatic(true);
    QString error;
    const bool started = controller_->startProjectPass(request, &error);
    if (started) {
        ++passStarts_;
    }
    if (sink_) {
        sink_(started, error);
    }
}

} // namespace vedit::workspace::diagnostics
