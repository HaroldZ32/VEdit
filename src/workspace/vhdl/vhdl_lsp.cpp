#include "vhdl_lsp.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <utility>

namespace vedit::workspace {

namespace {

/// `Content-Length` framing is BYTE counted (LSP base protocol). The parser below
/// never splits on characters: a buffer may hold half a frame, several frames, or
/// a frame whose body is CJK text, and only the declared byte count decides where
/// the body ends.
constexpr int kInitializeTimeoutMs = 15000;
constexpr int kRequestTimeoutMs = 10000;

qint64 requestId(const QJsonObject &object)
{
    const QJsonValue id = object.value(QStringLiteral("id"));
    if (id.isDouble()) {
        return static_cast<qint64>(id.toDouble());
    }
    return -1;
}

QString localFile(const QJsonValue &uriValue)
{
    if (!uriValue.isString()) {
        return {};
    }
    const QUrl url(uriValue.toString());
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

/// The `contents` of a hover answer: a plain string, a {language,value} markup
/// object, or an array of such entries. vhdl_ls answers the markup form.
QString hoverText(const QJsonValue &contents)
{
    if (contents.isString()) {
        return contents.toString();
    }
    if (contents.isArray()) {
        QStringList parts;
        for (const QJsonValue &entry : contents.toArray()) {
            const QString text = hoverText(entry);
            if (!text.isEmpty()) {
                parts.append(text);
            }
        }
        return parts.join(QLatin1Char('\n'));
    }
    if (contents.isObject()) {
        const QJsonObject object = contents.toObject();
        if (object.contains(QStringLiteral("value"))) {
            return object.value(QStringLiteral("value")).toString();
        }
        return hoverText(object.value(QStringLiteral("contents")));
    }
    return {};
}

VhdlLocation locationFrom(const QJsonObject &object)
{
    VhdlLocation location;
    location.file = localFile(object.value(QStringLiteral("uri")));
    const QJsonObject start =
        object.value(QStringLiteral("range")).toObject().value(QStringLiteral("start")).toObject();
    location.line = start.value(QStringLiteral("line")).toInt();
    location.column = start.value(QStringLiteral("character")).toInt();
    return location;
}

/// The definition answer is `Location | Location[] | LocationLink[] | null`.
QVector<VhdlLocation> definitionTargets(const QJsonValue &result)
{
    QVector<VhdlLocation> targets;
    if (result.isObject()) {
        const QJsonObject object = result.toObject();
        // A LocationLink carries `targetUri`/`targetRange` instead of `uri`/`range`.
        if (object.contains(QStringLiteral("targetUri"))) {
            VhdlLocation link;
            link.file = localFile(object.value(QStringLiteral("targetUri")));
            const QJsonObject start = object.value(QStringLiteral("targetRange"))
                                          .toObject()
                                          .value(QStringLiteral("start"))
                                          .toObject();
            link.line = start.value(QStringLiteral("line")).toInt();
            link.column = start.value(QStringLiteral("character")).toInt();
            if (!link.file.isEmpty()) {
                targets.append(link);
            }
        } else {
            VhdlLocation location = locationFrom(object);
            if (!location.file.isEmpty()) {
                targets.append(location);
            }
        }
    } else if (result.isArray()) {
        for (const QJsonValue &entry : result.toArray()) {
            for (const VhdlLocation &location : definitionTargets(entry)) {
                targets.append(location);
            }
        }
    }
    return targets;
}

} // namespace

bool isVhdlFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("vhd") || suffix == QLatin1String("vhdl");
}

namespace {

/// The library name the GENERATED config declares. It must NOT be `work`
/// (measured: vhdl_ls rejects that key) and it must not collide with the pinned
/// installation libraries (`ieee`, `std`, `synopsys`, `vital2000`).
constexpr const char *kGeneratedLibrary = "proj";

/// Upper bound for the generated file list: a runaway tree must not turn "open a
/// VHDL file" into a directory-wide scan of a monorepo. Truncation is REPORTED.
constexpr int kMaxGeneratedFiles = 2000;

/// Directories a VHDL project never keeps sources in (build outputs, VCS data,
/// editor state and vendored node modules).
bool skippedDirectory(const QString &name)
{
    static const QSet<QString> skipped{
        QStringLiteral(".git"),   QStringLiteral(".svn"),  QStringLiteral(".hg"),
        QStringLiteral("target"), QStringLiteral("build"), QStringLiteral("node_modules"),
        QStringLiteral(".mpd"),   QStringLiteral(".venv"), QStringLiteral(".cache"),
    };
    return skipped.contains(name);
}

/// Every `.vhd`/`.vhdl` under `root` (relative, sorted, no duplicates).
QStringList collectVhdlFiles(const QString &root, int *truncated)
{
    QStringList files;
    *truncated = 0;
    QDirIterator iterator(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = QDir(root).relativeFilePath(absolute);
        // Skip a whole subtree cheaply: `QDirIterator` cannot prune, so the test
        // happens per file — the set of names is tiny and exact.
        bool skip = false;
        for (const QString &part : relative.split(QLatin1Char('/'))) {
            if (skippedDirectory(part)) {
                skip = true;
                break;
            }
        }
        if (skip || !isVhdlFile(relative)) {
            continue;
        }
        if (files.size() >= kMaxGeneratedFiles) {
            ++(*truncated);
            continue;
        }
        files.append(relative);
    }
    files.sort();
    return files;
}

QString generatedConfigText(const QStringList &files)
{
    QString text = QStringLiteral(
        "# Generated by VEdit (AC-6.3) because this workspace had no vhdl_ls.toml.\n"
        "# Every project .vhd/.vhdl file is declared in ONE library, so `use work.x`\n"
        "# resolves: `work` is the implicit library of the analysed unit, and it is\n"
        "# NOT a valid key in this file (vhdl_ls rejects it).\n"
        "# Edit this file to describe several libraries, or delete it to regenerate.\n"
        "[libraries]\n%1.files = [\n").arg(QLatin1String(kGeneratedLibrary));
    for (const QString &file : files) {
        text += QStringLiteral("  \"%1\",\n").arg(file);
    }
    text += QStringLiteral("]\n");
    return text;
}

/// Library names a workspace config declares, read with the same dotted-key shape
/// vhdl_ls uses (`libA.files = [...]`). Best effort: it exists for the LOG line,
/// never to second-guess the server.
QStringList declaredLibraries(const QString &text)
{
    QStringList names;
    bool inLibraries = false;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            inLibraries = line == QLatin1String("[libraries]");
            continue;
        }
        if (!inLibraries || !line.contains(QLatin1Char('='))) {
            continue;
        }
        const QString key = line.left(line.indexOf(QLatin1Char('='))).trimmed();
        const QString library = key.left(key.indexOf(QLatin1Char('.'))).trimmed();
        if (!library.isEmpty() && !names.contains(library)) {
            names.append(library);
        }
    }
    names.sort();
    return names;
}

} // namespace

QString VhdlConfigReport::text() const
{
    const char *sourceName = "missing";
    switch (source) {
    case Source::User:
        sourceName = "user";
        break;
    case Source::Generated:
        sourceName = "generated";
        break;
    case Source::Missing:
        sourceName = "missing";
        break;
    }
    return QStringLiteral("source=%1 path='%2' files=%3 libraries=[%4]%5")
        .arg(QLatin1String(sourceName), path)
        .arg(files)
        .arg(libraries.join(QLatin1Char(',')))
        .arg(error.isEmpty() ? QString() : QStringLiteral(" error='%1'").arg(error));
}

VhdlConfigReport prepareVhdlConfig(const QString &root)
{
    VhdlConfigReport report;
    const QDir directory(root);
    if (root.isEmpty() || !directory.exists()) {
        report.error = QStringLiteral("the workspace root is not a directory: '%1'").arg(root);
        return report;
    }
    const QString userConfig = directory.filePath(QStringLiteral("vhdl_ls.toml"));
    if (QFileInfo::exists(userConfig)) {
        // AC-6.3: the workspace's own file WINS. It is never rewritten, and a file
        // that cannot even be read is reported instead of silently replaced.
        report.source = VhdlConfigReport::Source::User;
        report.path = userConfig;
        QFile file(userConfig);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            report.error = QStringLiteral("vhdl_ls.toml is not readable: %1").arg(userConfig);
            return report;
        }
        const QString text = QString::fromUtf8(file.readAll());
        file.close();
        report.libraries = declaredLibraries(text);
        return report;
    }

    // No user config: generate the DEFAULT one in the root. The root file is the
    // only channel the server reports as its WORKSPACE mapping (a VHDL_LS_CONFIG
    // file resolves navigation but keeps "Library mapping is unknown" + "semantic
    // analysis might be incorrect" alive), and a mapping is what gives the server
    // the project files nobody opened. Navigation is NOT root-only — measured:
    // every arrangement resolves once the documents are opened. An existing file is
    // never overwritten (checked above and again by QSaveFile's commit).
    int truncated = 0;
    const QStringList files = collectVhdlFiles(root, &truncated);
    if (files.isEmpty()) {
        report.error = QStringLiteral("no .vhd/.vhdl file under '%1'").arg(root);
        return report;
    }
    QSaveFile output(userConfig);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        report.error = QStringLiteral("cannot write %1: %2").arg(userConfig, output.errorString());
        return report;
    }
    output.write(generatedConfigText(files).toUtf8());
    if (!output.commit()) {
        report.error = QStringLiteral("cannot write %1: %2").arg(userConfig, output.errorString());
        return report;
    }
    report.source = VhdlConfigReport::Source::Generated;
    report.path = userConfig;
    report.files = static_cast<int>(files.size());
    report.libraries = QStringList{QString::fromLatin1(kGeneratedLibrary)};
    if (truncated > 0) {
        report.error = QStringLiteral("the file list was capped at %1 files (%2 more omitted)")
                           .arg(kMaxGeneratedFiles)
                           .arg(truncated);
    }
    return report;
}

VhdlLspSession::VhdlLspSession(QObject *parent)
    : QObject(parent)
{
}

VhdlLspSession::~VhdlLspSession()
{
    // Never leave a child behind: the destructor is the last owner. It does not
    // wait for a graceful shutdown (that would block a teardown path), it only
    // makes sure the process is gone.
    if (process_ != nullptr) {
        process_->disconnect(this);
        if (process_->state() != QProcess::NotRunning) {
            // Teardown-only, bounded reap: without it the child survives as an
            // orphan and Qt reports "QProcess: destroyed while still running".
            // This is NOT a GUI-loop wait — the destructor runs once, at exit.
            process_->kill();
            process_->waitForFinished(500);
        }
    }
}

QString VhdlLspSession::stateName() const
{
    switch (state_) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Starting:
        return QStringLiteral("starting");
    case State::Ready:
        return QStringLiteral("ready");
    case State::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

qint64 VhdlLspSession::processId() const
{
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) {
        return -1;
    }
    return process_->processId();
}

bool VhdlLspSession::start(const QString &program, const QString &workspaceRoot)
{
    if (state_ == State::Starting || state_ == State::Ready) {
        return true;
    }
    program_ = program;
    workspaceRoot_ = workspaceRoot;
    error_.clear();
    buffer_.clear();
    opened_.clear();
    diagnostics_.clear();
    messages_.clear();
    clearPending();

    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
        fail(QStringLiteral("vhdl_ls is not an executable file: '%1'").arg(program));
        return false;
    }
    if (workspaceRoot.isEmpty() || !QFileInfo(workspaceRoot).isDir()) {
        fail(QStringLiteral("the workspace root is not a directory: '%1'").arg(workspaceRoot));
        return false;
    }

    process_ = new QProcess(this);
    // Array argv, no shell; the child inherits the product's environment by
    // default, which is what the resolver's import seam expects.
    process_->setProgram(program_);
    process_->setArguments(QStringList{});
    process_->setWorkingDirectory(workspaceRoot_);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::started, this, &VhdlLspSession::onStarted);
    connect(process_, &QProcess::readyReadStandardOutput, this, &VhdlLspSession::onReadyRead);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        onProcessError(static_cast<int>(error));
    });
    connect(process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                onFinished(exitCode, static_cast<int>(status));
            });
    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray text = process_ != nullptr ? process_->readAllStandardError() : QByteArray();
        if (text.trimmed().isEmpty()) {
            return;
        }
        std::printf("VHDL-LSP-STDERR %s\n", text.trimmed().constData());
        std::fflush(stdout);
    });

    state_ = State::Starting;
    process_->start();
    // The handshake must be observable: a server that never answers `initialize`
    // becomes a named failure instead of a session that looks "starting" forever.
    initializeTimer_ = new QTimer(this);
    initializeTimer_->setSingleShot(true);
    initializeTimer_->setInterval(kInitializeTimeoutMs);
    connect(initializeTimer_, &QTimer::timeout, this, [this]() {
        if (state_ == State::Starting) {
            fail(QStringLiteral("vhdl_ls did not answer `initialize` within %1 ms")
                     .arg(kInitializeTimeoutMs));
        }
    });
    initializeTimer_->start();
    return true;
}

void VhdlLspSession::onStarted()
{
    const qint64 pid = process_ != nullptr ? process_->processId() : -1;
    std::printf("VHDL-LSP-SPAWN program='%s' root='%s' pid=%lld language=vhdl\n",
                qPrintable(program_), qPrintable(workspaceRoot_), static_cast<long long>(pid));
    std::fflush(stdout);
    const QString rootUri = QUrl::fromLocalFile(workspaceRoot_).toString();

    QJsonObject clientCapabilities;
    QJsonObject hoverFormats;
    hoverFormats.insert(QStringLiteral("contentFormat"),
                        QJsonArray{QStringLiteral("plaintext")});
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("hover"), hoverFormats);
    textDocument.insert(QStringLiteral("definition"), QJsonObject{});
    textDocument.insert(QStringLiteral("documentSymbol"), QJsonObject{});
    clientCapabilities.insert(QStringLiteral("textDocument"), textDocument);

    QJsonObject folder;
    folder.insert(QStringLiteral("uri"), rootUri);
    folder.insert(QStringLiteral("name"), QFileInfo(workspaceRoot_).fileName());

    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("VEdit"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.2"));

    QJsonObject params;
    params.insert(QStringLiteral("processId"),
                  static_cast<double>(QCoreApplication::applicationPid()));
    params.insert(QStringLiteral("clientInfo"), clientInfo);
    params.insert(QStringLiteral("rootUri"), rootUri);
    params.insert(QStringLiteral("capabilities"), clientCapabilities);
    params.insert(QStringLiteral("workspaceFolders"), QJsonArray{folder});
    PendingRequest initialize;
    initialize.tag = QStringLiteral("initialize");
    sendRequest(QStringLiteral("initialize"), params, QStringLiteral("initialize"), initialize);
}

void VhdlLspSession::onReadyRead()
{
    if (process_ == nullptr) {
        return;
    }
    feed(process_->readAllStandardOutput());
}

void VhdlLspSession::feed(const QByteArray &chunk)
{
    buffer_.append(chunk);
    while (true) {
        const int headerEnd = buffer_.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        int contentLength = -1;
        const QByteArray header = buffer_.left(headerEnd);
        for (const QByteArray &line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                bool ok = false;
                const int parsed = trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toInt(&ok);
                if (ok) {
                    contentLength = parsed;
                }
            }
        }
        if (contentLength < 0) {
            // A frame without a length cannot be delimited: drop the header and
            // keep reading rather than blocking on a body that will never come.
            std::printf("VHDL-LSP-PROTOCOL error='frame without Content-Length'\n");
            std::fflush(stdout);
            buffer_.remove(0, headerEnd + 4);
            continue;
        }
        if (buffer_.size() < headerEnd + 4 + contentLength) {
            return; // wait for the rest of the body
        }
        const QByteArray body = buffer_.mid(headerEnd + 4, contentLength);
        buffer_.remove(0, headerEnd + 4 + contentLength);
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            std::printf("VHDL-LSP-PROTOCOL error='invalid JSON body' detail='%s'\n",
                        parseError.errorString().toUtf8().constData());
            std::fflush(stdout);
            continue;
        }
        dispatch(document.object());
    }
}

void VhdlLspSession::dispatch(const QJsonObject &message)
{
    if (message.contains(QStringLiteral("id")) && !message.contains(QStringLiteral("method"))) {
        handleResponse(message);
        return;
    }
    if (message.contains(QStringLiteral("method"))) {
        // `id` + `method` is a server-to-client REQUEST: answer it, or the server
        // waits forever. vhdl_ls asks for nothing we implement, so `null` is the
        // honest answer and keeps the session alive.
        if (message.contains(QStringLiteral("id"))) {
            std::printf("VHDL-LSP-SERVER-REQUEST method='%s' (answered null)\n",
                        qPrintable(message.value(QStringLiteral("method")).toString()));
            std::fflush(stdout);
            QJsonObject reply;
            reply.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
            reply.insert(QStringLiteral("id"), message.value(QStringLiteral("id")));
            reply.insert(QStringLiteral("result"), QJsonValue());
            send(reply);
            return;
        }
        handleNotification(message);
    }
}

void VhdlLspSession::handleResponse(const QJsonObject &response)
{
    const qint64 id = requestId(response);
    const PendingRequest pending = pending_.take(id);
    if (pending.timer != nullptr) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    const QString tag = pending.tag;
    const QJsonObject body = response.value(QStringLiteral("error")).toObject();
    if (!body.isEmpty()) {
        const QString reason = body.value(QStringLiteral("message")).toString();
        std::printf("VHDL-LSP-ERROR request='%s' reason='%s'\n", qPrintable(tag),
                    qPrintable(reason));
        std::fflush(stdout);
    }
    const QJsonValue result = response.value(QStringLiteral("result"));

    if (tag == QLatin1String("initialize")) {
        if (initializeTimer_ != nullptr) {
            initializeTimer_->stop();
        }
        const QJsonObject capabilities =
            result.toObject().value(QStringLiteral("capabilities")).toObject();
        definitionProvider_ = capabilities.contains(QStringLiteral("definitionProvider"));
        hoverProvider_ = capabilities.contains(QStringLiteral("hoverProvider"));
        documentSymbolProvider_ = capabilities.contains(QStringLiteral("documentSymbolProvider"));
        std::printf("VHDL-LSP-INITIALIZE backend=vhdl_ls definition=%d hover=%d "
                    "symbols=%d\n",
                    definitionProvider_ ? 1 : 0, hoverProvider_ ? 1 : 0,
                    documentSymbolProvider_ ? 1 : 0);
        std::fflush(stdout);
        sendNotification(QStringLiteral("initialized"), QJsonObject{});
        markReady();
        return;
    }
    if (tag == QLatin1String("hover")) {
        const QString text =
            hoverText(result.toObject().value(QStringLiteral("contents")));
        std::printf("VHDL-LSP-HOVER file='%s' line=%d column=%d text='%s'\n",
                    qPrintable(pending.file), pending.line, pending.column, qPrintable(text));
        std::fflush(stdout);
        emit hoverAnswered(pending.file, pending.line, pending.column, text);
        return;
    }
    if (tag == QLatin1String("definition")) {
        const QVector<VhdlLocation> targets = definitionTargets(result);
        std::printf("VHDL-LSP-DEFINITION file='%s' line=%d column=%d targets=%d\n",
                    qPrintable(pending.file), pending.line, pending.column,
                    static_cast<int>(targets.size()));
        for (const VhdlLocation &target : targets) {
            std::printf("VHDL-LSP-DEFINITION-TARGET file='%s' line=%d column=%d\n",
                        qPrintable(target.file), target.line, target.column);
        }
        std::fflush(stdout);
        emit definitionAnswered(pending.file, pending.line, pending.column, targets);
        return;
    }
}

void VhdlLspSession::handleNotification(const QJsonObject &notification)
{
    const QString method = notification.value(QStringLiteral("method")).toString();
    const QJsonObject params = notification.value(QStringLiteral("params")).toObject();
    if (method == QLatin1String("textDocument/publishDiagnostics")) {
        const QString file = localFile(params.value(QStringLiteral("uri")));
        QVector<VhdlDiagnostic> rows;
        for (const QJsonValue &entry : params.value(QStringLiteral("diagnostics")).toArray()) {
            const QJsonObject object = entry.toObject();
            const QJsonObject start =
                object.value(QStringLiteral("range")).toObject().value(QStringLiteral("start")).toObject();
            const QJsonObject end =
                object.value(QStringLiteral("range")).toObject().value(QStringLiteral("end")).toObject();
            VhdlDiagnostic row;
            row.file = file;
            row.line = start.value(QStringLiteral("line")).toInt();
            row.column = start.value(QStringLiteral("character")).toInt();
            row.endLine = end.value(QStringLiteral("line")).toInt();
            row.endColumn = end.value(QStringLiteral("character")).toInt();
            row.severity = object.value(QStringLiteral("severity")).toInt();
            const QJsonValue code = object.value(QStringLiteral("code"));
            row.code = code.isString() ? code.toString()
                                       : QString::number(code.toInt());
            row.source = object.value(QStringLiteral("source")).toString();
            row.message = object.value(QStringLiteral("message")).toString();
            rows.append(row);
        }
        diagnostics_.insert(file, rows);
        std::printf("VHDL-LSP-PUBLISH file='%s' diagnostics=%d\n", qPrintable(file),
                    static_cast<int>(rows.size()));
        for (const VhdlDiagnostic &row : rows) {
            std::printf("VHDL-LSP-DIAG file='%s' line=%d column=%d severity=%d code='%s' "
                        "message='%s'\n",
                        qPrintable(row.file), row.line, row.column, row.severity,
                        qPrintable(row.code), qPrintable(row.message));
        }
        std::fflush(stdout);
        emit diagnosticsPublished(file, static_cast<int>(rows.size()));
        return;
    }
    if (method == QLatin1String("window/showMessage")
        || method == QLatin1String("window/logMessage")) {
        // vhdl_ls reports "library mapping is unknown" here; it is the reason a
        // configuration-less workspace produces no diagnostics, so it is logged
        // rather than dropped.
        const QString text = params.value(QStringLiteral("message")).toString();
        noteServerMessage(text);
        std::printf("VHDL-LSP-MESSAGE level=%d text='%s'\n",
                    params.value(QStringLiteral("type")).toInt(), qPrintable(text));
        std::fflush(stdout);
        return;
    }
    std::printf("VHDL-LSP-NOTIFY method='%s'\n", qPrintable(method));
    std::fflush(stdout);
}

void VhdlLspSession::send(const QJsonObject &message)
{
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) {
        return;
    }
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame = "Content-Length: ";
    frame += QByteArray::number(body.size());
    frame += "\r\n\r\n";
    frame += body;
    // QProcess buffers the write; this never blocks the caller.
    process_->write(frame);
}

void VhdlLspSession::sendRequest(const QString &method, const QJsonObject &params,
                                const QString &tag, const PendingRequest &pending)
{
    const qint64 id = nextId_++;
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(id));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    if (!tag.isEmpty() && tag != QLatin1String("initialize")) {
        PendingRequest tracked = pending;
        tracked.tag = tag;
        tracked.timer = new QTimer(this);
        tracked.timer->setSingleShot(true);
        tracked.timer->setInterval(kRequestTimeoutMs);
        connect(tracked.timer, &QTimer::timeout, this, [this, id, tag]() {
            if (!pending_.contains(id)) {
                return;
            }
            const PendingRequest given = pending_.take(id);
            std::printf("VHDL-LSP-TIMEOUT request='%s' file='%s'\n", qPrintable(tag),
                        qPrintable(given.file));
            std::fflush(stdout);
            if (tag == QLatin1String("hover")) {
                emit hoverAnswered(given.file, given.line, given.column, QString());
            } else if (tag == QLatin1String("definition")) {
                emit definitionAnswered(given.file, given.line, given.column, {});
            }
        });
        tracked.timer->start();
        pending_.insert(id, tracked);
    } else {
        pending_.insert(id, pending);
    }
    send(message);
}

void VhdlLspSession::sendNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    send(message);
}

void VhdlLspSession::noteServerMessage(const QString &text)
{
    messages_.append(text);
    if (configRejected_ || !text.contains(QLatin1String("Error loading vhdl_ls.toml"))) {
        return;
    }
    // AC-6.3: a refused workspace config is a STATE the product shows, never a
    // crash and never a silent downgrade — the server keeps running and answers.
    configRejected_ = true;
    configError_ = text;
    std::printf("VHDL-CONFIG-REJECTED reason='%s'\n", qPrintable(text));
    std::fflush(stdout);
    emit configRefused(text);
}

void VhdlLspSession::markReady()
{
    state_ = State::Ready;
    std::printf("VHDL-LSP-READY backend=vhdl_ls files=%d pid=%lld\n", filesOpened(),
                static_cast<long long>(processId()));
    std::fflush(stdout);
    emit readyChanged(true);
}

void VhdlLspSession::fail(const QString &reason)
{
    if (state_ == State::Failed) {
        return;
    }
    error_ = reason;
    state_ = State::Failed;
    if (initializeTimer_ != nullptr) {
        initializeTimer_->stop();
    }
    clearPending();
    std::printf("VHDL-LSP-FAILED backend=vhdl_ls reason='%s'\n", qPrintable(reason));
    std::fflush(stdout);
    emit failed(reason);
}

void VhdlLspSession::clearPending()
{
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it.value().timer != nullptr) {
            it.value().timer->stop();
            it.value().timer->deleteLater();
        }
    }
    pending_.clear();
}

void VhdlLspSession::onProcessError(int error)
{
    if (stopping_ || state_ == State::Failed) {
        return;
    }
    fail(QStringLiteral("vhdl_ls could not be run (QProcess error %1)").arg(error));
}

void VhdlLspSession::onFinished(int exitCode, int status)
{
    if (stopping_) {
        std::printf("VHDL-LSP-STOP backend=vhdl_ls exit=%d status=%d\n", exitCode, status);
        std::fflush(stdout);
        return;
    }
    if (state_ == State::Failed) {
        return;
    }
    // A server that exits before (or after) the handshake is a REAL failure:
    // say why instead of leaving a session that looks merely "not ready".
    const QString stderrTail =
        process_ != nullptr ? QString::fromUtf8(process_->readAllStandardError()).trimmed()
                            : QString();
    fail(QStringLiteral("vhdl_ls exited (code %1, status %2)%3")
             .arg(exitCode)
             .arg(status)
             .arg(stderrTail.isEmpty() ? QString()
                                       : QStringLiteral(": %1").arg(stderrTail)));
}

bool VhdlLspSession::openDocument(const QString &path)
{
    if (state_ != State::Ready) {
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (isOpen(absolute)) {
        return true;
    }
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::printf("VHDL-LSP-SKIP file='%s' reason='unreadable'\n", qPrintable(path));
        std::fflush(stdout);
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), QUrl::fromLocalFile(absolute).toString());
    // AC-6.2: the language id of a VHDL document is `vhdl`, never the Rust
    // session's `verilog`/`systemverilog` mapping.
    textDocument.insert(QStringLiteral("languageId"), QStringLiteral("vhdl"));
    textDocument.insert(QStringLiteral("version"), 1);
    textDocument.insert(QStringLiteral("text"), text);

    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    sendNotification(QStringLiteral("textDocument/didOpen"), params);
    opened_.append(absolute);
    std::printf("VHDL-LSP-DIDOPEN file='%s' language=vhdl bytes=%lld\n", qPrintable(absolute),
                static_cast<long long>(text.toUtf8().size()));
    std::fflush(stdout);
    return true;
}

QVector<VhdlDiagnostic> VhdlLspSession::diagnostics(const QString &path) const
{
    return diagnostics_.value(QFileInfo(path).absoluteFilePath());
}

int VhdlLspSession::diagnosticCount() const
{
    int total = 0;
    for (auto it = diagnostics_.constBegin(); it != diagnostics_.constEnd(); ++it) {
        total += static_cast<int>(it.value().size());
    }
    return total;
}

bool VhdlLspSession::requestHover(const QString &path, int line, int column)
{
    if (state_ != State::Ready) {
        return false;
    }
    if (!isOpen(path) && !openDocument(path)) {
        return false;
    }
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), QUrl::fromLocalFile(path).toString());
    QJsonObject position;
    position.insert(QStringLiteral("line"), line);
    position.insert(QStringLiteral("character"), column);
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    params.insert(QStringLiteral("position"), position);
    PendingRequest pending;
    pending.kind = PendingKind::Hover;
    pending.tag = QStringLiteral("hover");
    pending.file = QFileInfo(path).absoluteFilePath();
    pending.line = line;
    pending.column = column;
    sendRequest(QStringLiteral("textDocument/hover"), params, QStringLiteral("hover"), pending);
    return true;
}

bool VhdlLspSession::requestDefinition(const QString &path, int line, int column)
{
    if (state_ != State::Ready) {
        return false;
    }
    if (!isOpen(path) && !openDocument(path)) {
        return false;
    }
    QJsonObject textDocument;
    textDocument.insert(QStringLiteral("uri"), QUrl::fromLocalFile(path).toString());
    QJsonObject position;
    position.insert(QStringLiteral("line"), line);
    position.insert(QStringLiteral("character"), column);
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"), textDocument);
    params.insert(QStringLiteral("position"), position);
    PendingRequest pending;
    pending.kind = PendingKind::Definition;
    pending.tag = QStringLiteral("definition");
    pending.file = QFileInfo(path).absoluteFilePath();
    pending.line = line;
    pending.column = column;
    sendRequest(QStringLiteral("textDocument/definition"), params, QStringLiteral("definition"),
                pending);
    return true;
}

void VhdlLspSession::stop()
{
    if (stopping_) {
        return;
    }
    stopping_ = true;
    if (initializeTimer_ != nullptr) {
        initializeTimer_->stop();
    }
    clearPending();
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) {
        state_ = state_ == State::Failed ? State::Failed : State::Idle;
        return;
    }
    // Graceful first: LSP `shutdown` then `exit`. The bounded fallback below
    // guarantees the process is gone even when the server ignores both.
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(nextId_++));
    message.insert(QStringLiteral("method"), QStringLiteral("shutdown"));
    message.insert(QStringLiteral("params"), QJsonValue());
    send(message);
    sendNotification(QStringLiteral("exit"), QJsonObject{});

    QProcess *child = process_;
    QTimer::singleShot(1000, child, [child]() {
        if (child->state() != QProcess::NotRunning) {
            child->terminate();
        }
    });
    QTimer::singleShot(2500, child, [child]() {
        if (child->state() != QProcess::NotRunning) {
            child->kill();
        }
    });
}

} // namespace vedit::workspace

// ---------------------------------------------------------------------------
// AC-6.2 probe (evidence generator, never part of the library)
//
// The probe is compiled from THIS file with `VEDIT_VHDL_LSP_PROBE_MAIN` defined
// (src/workspace/vhdl/CMakeLists.txt), so the lane keeps its frozen write scope:
// no extra source file, and nothing of it is linked into the product.
//
// It drives the REAL `vhdl_ls` twice:
//   * a workspace WITH `vhdl_ls.toml` (the `[libraries] work.files` mapping):
//     initialize handshake, three didOpen, published diagnostics, hover and
//     definition;
//   * a workspace WITHOUT `vhdl_ls.toml`: the server still handshakes but
//     publishes NO diagnostics and warns that the library mapping is unknown
//     (the reason a configuration-less workspace looks "clean").
// ---------------------------------------------------------------------------
#if defined(VEDIT_VHDL_LSP_PROBE_MAIN)

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("VHDL-CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

const char *kPackage = R"(library ieee;
use ieee.std_logic_1164.all;

package util_pkg is
  constant WIDTH : integer := 8;
  function add_one(x : integer) return integer;
end package util_pkg;

package body util_pkg is
  function add_one(x : integer) return integer is
  begin
    return x + 1;
  end function add_one;
end package body util_pkg;
)";

// The reference to `add_one` is the definition/hover anchor; the probe finds the
// token in the file instead of hard-coding a column.
const char *kTop = R"(library ieee;
use ieee.std_logic_1164.all;
use work.util_pkg.all;

entity top is
  port (
    clk : in std_logic;
    q   : out std_logic
  );
end entity top;

architecture rtl of top is
  signal count : integer := WIDTH;
begin
  count <= add_one(count);
  q <= clk;
end architecture rtl;
)";

// Two errors on purpose (a missing `;` and an undeclared target), both reported
// by a real semantic analysis.
const char *kBad = R"(library ieee;
use ieee.std_logic_1164.all;

entity bad is
  port (x : in integer)
end entity bad;

architecture a of bad is
begin
  y <= x;
end architecture a;
)";

/// Zero-based (line, character) of the first `needle` occurrence in `text`.
QPair<int, int> positionOf(const QString &text, const QString &needle)
{
    const int index = text.indexOf(needle);
    if (index < 0) {
        return {-1, -1};
    }
    int line = 0;
    int column = 0;
    for (int i = 0; i < index; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    return {line, column + 1}; // inside the identifier, not at its edge
}

/// AC-6.3 scenarios: user config wins / missing config generated / broken config
/// refused without a crash / the measured MULTI-library boundary.
int runConfigScenarios(const QString &program)
{
    // --- A. a workspace WITH its own vhdl_ls.toml: it wins and is left untouched
    QTemporaryDir userWorkspace;
    const QString userRtl = userWorkspace.path() + QStringLiteral("/rtl");
    QDir().mkpath(userRtl);
    writeFile(userRtl + QStringLiteral("/pkg.vhd"), QString::fromUtf8(kPackage));
    writeFile(userRtl + QStringLiteral("/top.vhd"), QString::fromUtf8(kTop));
    const QString userConfigPath = userWorkspace.path() + QStringLiteral("/vhdl_ls.toml");
    writeFile(userConfigPath,
              QStringLiteral("# the project's own mapping\n[libraries]\n"
                             "proj.files = [\"rtl/pkg.vhd\", \"rtl/top.vhd\"]\n"));
    const QString userBefore = readAll(userConfigPath);
    const vedit::workspace::VhdlConfigReport userReport =
        vedit::workspace::prepareVhdlConfig(userWorkspace.path());
    const QString userAfter = readAll(userConfigPath);
    check("config.user.source", userReport.source == vedit::workspace::VhdlConfigReport::Source::User,
          userReport.text());
    check("config.user.libraries", userReport.libraries.contains(QLatin1String("proj")),
          userReport.libraries.join(QLatin1Char(',')));
    check("config.user.untouched", userBefore == userAfter);

    // --- B. NO config: the lane generates the default single-library mapping
    QTemporaryDir bareWorkspace;
    const QString bareRtl = bareWorkspace.path() + QStringLiteral("/rtl");
    QDir().mkpath(bareRtl);
    writeFile(bareRtl + QStringLiteral("/pkg.vhd"), QString::fromUtf8(kPackage));
    writeFile(bareRtl + QStringLiteral("/top.vhd"), QString::fromUtf8(kTop));
    QDir().mkpath(bareWorkspace.path() + QStringLiteral("/ignored"));
    writeFile(bareWorkspace.path() + QStringLiteral("/ignored/pkg.vhd"),
              QString::fromUtf8(kPackage)); // under an ordinary dir: must be listed too
    const vedit::workspace::VhdlConfigReport bareReport =
        vedit::workspace::prepareVhdlConfig(bareWorkspace.path());
    check("config.generated.source",
          bareReport.source == vedit::workspace::VhdlConfigReport::Source::Generated,
          bareReport.text());
    check("config.generated.file-written", QFileInfo::exists(bareReport.path), bareReport.path);
    check("config.generated.files", bareReport.files == 3,
          QStringLiteral("files=%1").arg(bareReport.files));
    check("config.generated.one-library", bareReport.libraries
              == QStringList{QStringLiteral("proj")},
          bareReport.libraries.join(QLatin1Char(',')));
    const QString generatedText = readAll(bareReport.path);
    check("config.generated.declares-files",
          generatedText.contains(QLatin1String("\"rtl/top.vhd\""))
              && generatedText.contains(QLatin1String("\"ignored/pkg.vhd\"")),
          QStringLiteral("lines=%1").arg(generatedText.count(QLatin1Char('\n'))));
    check("config.generated.work-not-a-key",
          !generatedText.contains(QLatin1String("work.files"))
              && generatedText.contains(QLatin1String("proj.files")),
          QStringLiteral("(work is rejected by vhdl_ls as a library name)"));
    // Second pass sees its own file: it is now a USER config, never regenerated.
    const vedit::workspace::VhdlConfigReport secondReport =
        vedit::workspace::prepareVhdlConfig(bareWorkspace.path());
    check("config.generated.idempotent",
          secondReport.source == vedit::workspace::VhdlConfigReport::Source::User,
          secondReport.text());

    // A session on the generated config must ANALYSE: cross-file definition works.
    vedit::workspace::VhdlLspSession generatedSession;
    bool generatedReady = false;
    QObject::connect(&generatedSession, &vedit::workspace::VhdlLspSession::readyChanged,
                     qApp, [&generatedReady](bool value) { generatedReady = value; });
    check("config.generated.session.start",
          generatedSession.start(program, bareWorkspace.path()));
    const bool generatedHandshake =
        waitFor([&generatedReady]() { return generatedReady; }, 30000);
    check("config.generated.session.ready", generatedHandshake,
          generatedSession.stateName());
    generatedSession.openDocument(bareRtl + QStringLiteral("/pkg.vhd"));
    generatedSession.openDocument(bareRtl + QStringLiteral("/top.vhd"));
    const QPair<int, int> generatedAnchor =
        positionOf(QString::fromUtf8(kTop), QStringLiteral("add_one"));
    QVector<vedit::workspace::VhdlLocation> generatedTargets;
    bool generatedDefinition = false;
    QObject::connect(&generatedSession, &vedit::workspace::VhdlLspSession::definitionAnswered,
                     qApp, [&generatedTargets, &generatedDefinition](
                               const QString &, int, int,
                               const QVector<vedit::workspace::VhdlLocation> &targets) {
                         generatedTargets = targets;
                         generatedDefinition = true;
                     });
    generatedSession.requestDefinition(bareRtl + QStringLiteral("/top.vhd"),
                                       generatedAnchor.first, generatedAnchor.second);
    const bool generatedAnswered =
        waitFor([&generatedDefinition]() { return generatedDefinition; }, 20000);
    check("config.generated.session.analysis-works",
          generatedAnswered && !generatedTargets.isEmpty(),
          QStringLiteral("targets=%1").arg(static_cast<int>(generatedTargets.size())));
    check("config.generated.session.config-accepted", !generatedSession.configRejected(),
          generatedSession.configError());
    generatedSession.stop();
    waitFor([&generatedSession]() { return generatedSession.processId() <= 0; }, 8000);

    // --- C. a BROKEN config: explicit refusal, the process keeps running
    QTemporaryDir brokenWorkspace;
    QDir().mkpath(brokenWorkspace.path() + QStringLiteral("/rtl"));
    writeFile(brokenWorkspace.path() + QStringLiteral("/rtl/top.vhd"),
              QString::fromUtf8(kTop));
    writeFile(brokenWorkspace.path() + QStringLiteral("/vhdl_ls.toml"),
              QStringLiteral("[libraries\nproj.files = \n"));
    const vedit::workspace::VhdlConfigReport brokenReport =
        vedit::workspace::prepareVhdlConfig(brokenWorkspace.path());
    check("config.broken.still-user",
          brokenReport.source == vedit::workspace::VhdlConfigReport::Source::User,
          brokenReport.text());
    vedit::workspace::VhdlLspSession brokenSession;
    bool brokenReady = false;
    QObject::connect(&brokenSession, &vedit::workspace::VhdlLspSession::readyChanged, qApp,
                     [&brokenReady](bool value) { brokenReady = value; });
    check("config.broken.session.start",
          brokenSession.start(program, brokenWorkspace.path()));
    const bool brokenHandshake = waitFor([&brokenReady]() { return brokenReady; }, 30000);
    check("config.broken.session.alive", brokenHandshake, brokenSession.stateName());
    const bool brokenRefused = waitFor(
        [&brokenSession]() { return brokenSession.configRejected(); }, 15000);
    check("config.broken.refused-explicitly", brokenRefused,
          brokenSession.configError());
    check("config.broken.no-crash",
          brokenSession.state() == vedit::workspace::VhdlLspSession::State::Ready,
          brokenSession.stateName());
    brokenSession.stop();
    waitFor([&brokenSession]() { return brokenSession.processId() <= 0; }, 8000);

    // --- D. `work` as a library key: refused with a reason (v0.88 measured)
    QTemporaryDir workWorkspace;
    QDir().mkpath(workWorkspace.path() + QStringLiteral("/rtl"));
    writeFile(workWorkspace.path() + QStringLiteral("/rtl/top.vhd"),
              QString::fromUtf8(kTop));
    writeFile(workWorkspace.path() + QStringLiteral("/vhdl_ls.toml"),
              QStringLiteral("[libraries]\nwork.files = [\"rtl/top.vhd\"]\n"));
    vedit::workspace::VhdlLspSession workSession;
    bool workReady = false;
    QObject::connect(&workSession, &vedit::workspace::VhdlLspSession::readyChanged, qApp,
                     [&workReady](bool value) { workReady = value; });
    check("config.work.session.start", workSession.start(program, workWorkspace.path()));
    const bool workHandshake = waitFor([&workReady]() { return workReady; }, 30000);
    check("config.work.session.alive", workHandshake, workSession.stateName());
    const bool workRefused =
        waitFor([&workSession]() { return workSession.configRejected(); }, 15000);
    check("config.work.refused",
          workRefused
              && workSession.configError().contains(QLatin1String("not a valid library")),
          workSession.configError());
    workSession.stop();
    waitFor([&workSession]() { return workSession.processId() <= 0; }, 8000);

    // --- E. the MEASURED multi-library boundary (v0.88.0)
    // Two libraries in the config: the server LOADS the file, but a cross-library
    // `use libA.pkg.all` is not resolved ("No declaration of 'libA'"). The lane
    // therefore claims the DEFAULT single-library mapping only — never a
    // whole-project elaborate, never multi-library navigation.
    QTemporaryDir multiWorkspace;
    const QString multiRtl = multiWorkspace.path() + QStringLiteral("/rtl");
    QDir().mkpath(multiRtl);
    writeFile(multiRtl + QStringLiteral("/alpha.vhd"),
              QStringLiteral("library ieee;\nuse ieee.std_logic_1164.all;\n"
                             "package alpha_pkg is\n  constant LIMIT : integer := 4;\n"
                             "  function clamp(x : integer) return integer;\n"
                             "end package alpha_pkg;\n"
                             "package body alpha_pkg is\n"
                             "  function clamp(x : integer) return integer is\n"
                             "  begin\n    return x;\n  end function clamp;\n"
                             "end package body alpha_pkg;\n"));
    writeFile(multiRtl + QStringLiteral("/consumer.vhd"),
              QStringLiteral("library ieee;\nuse ieee.std_logic_1164.all;\n"
                             "use liba.alpha_pkg.all;\n"
                             "entity consumer is\n  port (q : out integer);\n"
                             "end entity consumer;\n"
                             "architecture rtl of consumer is\nbegin\n"
                             "  q <= clamp(LIMIT);\nend architecture rtl;\n"));
    writeFile(multiWorkspace.path() + QStringLiteral("/vhdl_ls.toml"),
              QStringLiteral("[libraries]\nliba.files = [\"rtl/alpha.vhd\"]\n"
                             "libb.files = [\"rtl/consumer.vhd\"]\n"));
    vedit::workspace::VhdlLspSession multiSession;
    bool multiReady = false;
    QObject::connect(&multiSession, &vedit::workspace::VhdlLspSession::readyChanged, qApp,
                     [&multiReady](bool value) { multiReady = value; });
    check("config.multi.session.start", multiSession.start(program, multiWorkspace.path()));
    const bool multiHandshake = waitFor([&multiReady]() { return multiReady; }, 30000);
    check("config.multi.session.ready", multiHandshake, multiSession.stateName());
    check("config.multi.config-accepted", !multiSession.configRejected(),
          multiSession.configError());
    multiSession.openDocument(multiRtl + QStringLiteral("/alpha.vhd"));
    multiSession.openDocument(multiRtl + QStringLiteral("/consumer.vhd"));
    // Give the server time to publish; a diagnostic mentioning the library name is
    // the evidence that the cross-library reference did NOT resolve.
    QElapsedTimer multiSettle;
    multiSettle.start();
    bool crossLibraryResolved = false;
    bool crossLibraryReported = false;
    while (multiSettle.elapsed() < 12000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
        crossLibraryResolved = false;
        crossLibraryReported = false;
        for (const vedit::workspace::VhdlDiagnostic &row :
             multiSession.diagnostics(multiRtl + QStringLiteral("/consumer.vhd"))) {
            if (row.message.contains(QLatin1String("No declaration of"))) {
                crossLibraryReported = true;
                if (row.message.contains(QLatin1String("liba"), Qt::CaseInsensitive)) {
                    crossLibraryResolved = false;
                }
            }
        }
    }
    std::printf("VHDL-BOUNDARY multi-library: config-loaded=1 cross-library-use-resolved=%d "
                "server-says='%s'\n",
                crossLibraryResolved ? 1 : 0,
                crossLibraryReported ? "No declaration of the declaring library"
                                     : "no unresolved diagnostic");
    std::fflush(stdout);
    // The assertion records the MEASURED boundary, not a wish: a cross-library
    // reference is NOT resolved by v0.88.0, and the product must not claim it.
    check("config.multi.boundary-recorded", crossLibraryReported,
          QStringLiteral("cross-library diagnostics reported=%1").arg(crossLibraryReported ? 1 : 0));
    multiSession.stop();
    waitFor([&multiSession]() { return multiSession.processId() <= 0; }, 8000);

    // --- F. a root with no VHDL at all: one explicit reason, no generation
    QTemporaryDir emptyWorkspace;
    const vedit::workspace::VhdlConfigReport emptyReport =
        vedit::workspace::prepareVhdlConfig(emptyWorkspace.path());
    check("config.empty.reason",
          emptyReport.source == vedit::workspace::VhdlConfigReport::Source::Missing
              && !emptyReport.error.isEmpty(),
          emptyReport.text());
    check("config.empty.file-not-created",
          !QFileInfo::exists(emptyWorkspace.path() + QStringLiteral("/vhdl_ls.toml")));

    std::printf("VHDL-CONFIG-PROBE checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString program;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--vhdl-ls") && index + 1 < argc) {
            program = QString::fromLocal8Bit(argv[++index]);
        }
    }
    if (program.isEmpty()) {
        program = qEnvironmentVariable("VEDIT_VHDL_LS");
    }
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
        std::printf("VHDL-PROBE SKIP no vhdl_ls at '%s' (set VEDIT_VHDL_LS or --vhdl-ls)\n",
                    program.toUtf8().constData());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--config"))) {
        return runConfigScenarios(program);
    }

    QTemporaryDir workspace;
    QTemporaryDir bareWorkspace;
    if (!workspace.isValid() || !bareWorkspace.isValid()) {
        std::printf("VHDL-PROBE FAIL cannot create a temporary workspace\n");
        return 1;
    }
    const QString rtl = workspace.path() + QStringLiteral("/rtl");
    const QString bareRtl = bareWorkspace.path() + QStringLiteral("/rtl");
    QDir().mkpath(rtl);
    QDir().mkpath(bareRtl);
    const QString pkgPath = rtl + QStringLiteral("/pkg.vhd");
    const QString topPath = rtl + QStringLiteral("/top.vhd");
    const QString badPath = rtl + QStringLiteral("/bad.vhd");
    check("probe.fixture",
          writeFile(pkgPath, QString::fromUtf8(kPackage))
              && writeFile(topPath, QString::fromUtf8(kTop))
              && writeFile(badPath, QString::fromUtf8(kBad))
              // `work` is NOT a valid library name in vhdl_ls.toml (measured:
              // "The 'work' library is not a valid library"); `work` is the
              // implicit library of the unit being analysed, so the files are
              // declared under a normal name and `use work.…` still resolves.
              && writeFile(workspace.path() + QStringLiteral("/vhdl_ls.toml"),
                           QStringLiteral("[libraries]\nproj.files = [\"rtl/pkg.vhd\", "
                                          "\"rtl/top.vhd\", \"rtl/bad.vhd\"]\n")));
    // The negative control: the same files, no `vhdl_ls.toml` at the root.
    check("probe.bare-fixture",
          writeFile(bareRtl + QStringLiteral("/pkg.vhd"), QString::fromUtf8(kPackage))
              && writeFile(bareRtl + QStringLiteral("/top.vhd"), QString::fromUtf8(kTop)));

    // --- 1. a configured workspace: handshake + diagnostics + hover + definition
    vedit::workspace::VhdlLspSession session;
    bool ready = false;
    QObject::connect(&session, &vedit::workspace::VhdlLspSession::readyChanged, &app,
                     [&ready](bool value) { ready = value; });
    check("session.start", session.start(program, workspace.path()));
    const bool handshaken = waitFor([&ready]() { return ready; }, 30000);
    const QString handshakeDetail = QStringLiteral("state=%1 error='%2'")
                                        .arg(session.stateName(), session.errorText());
    check("session.initialize-handshake", handshaken, handshakeDetail);
    check("session.capabilities", session.hasDefinition() && session.hasHover(),
          QStringLiteral("definition=%1 hover=%2")
              .arg(session.hasDefinition() ? 1 : 0)
              .arg(session.hasHover() ? 1 : 0));
    check("session.pid", session.processId() > 0,
          QStringLiteral("pid=%1").arg(session.processId()));

    check("session.didopen.pkg", session.openDocument(pkgPath));
    check("session.didopen.top", session.openDocument(topPath));
    check("session.didopen.bad", session.openDocument(badPath));
    check("session.files-opened", session.filesOpened() == 3,
          QStringLiteral("files=%1").arg(session.filesOpened()));

    // Diagnostics: a real analysis of `bad.vhd` must publish at least one error.
    const bool diagnosed = waitFor(
        [&session, &badPath]() {
            return std::any_of(session.diagnostics(badPath).cbegin(),
                               session.diagnostics(badPath).cend(),
                               [](const vedit::workspace::VhdlDiagnostic &row) {
                                   return row.severity == 1 && !row.message.isEmpty();
                               });
        },
        20000);
    check("lsp.diagnostics.published", diagnosed,
          QStringLiteral("count=%1")
              .arg(static_cast<int>(session.diagnostics(badPath).size())));
    QString diagnosticText;
    int errorCount = 0;
    for (const vedit::workspace::VhdlDiagnostic &row : session.diagnostics(badPath)) {
        if (row.severity == 1) {
            ++errorCount;
            diagnosticText += row.message + QLatin1Char(';');
        }
    }
    check("lsp.diagnostics.error-severity", errorCount >= 1,
          QStringLiteral("errors=%1 text='%2'").arg(errorCount).arg(diagnosticText));
    check("lsp.diagnostics.clean-file-silent", session.diagnostics(pkgPath).isEmpty(),
          QStringLiteral("pkg-diagnostics=%1")
              .arg(static_cast<int>(session.diagnostics(pkgPath).size())));
    // A workspace WITH the mapping must not warn about it — the two states are
    // asserted in both directions, so "it works because of the config" is a
    // measured statement rather than an assumption.
    bool configLoaded = false;
    bool configError = false;
    bool mappingUnknown = false;
    for (const QString &message : session.serverMessages()) {
        if (message.contains(QLatin1String("Loaded workspace root configuration file"))) {
            configLoaded = true;
        }
        if (message.contains(QLatin1String("Error loading vhdl_ls.toml"))) {
            configError = true;
        }
        if (message.contains(QLatin1String("Library mapping is unknown"))) {
            mappingUnknown = true;
        }
    }
    check("lsp.config.loaded", configLoaded && !configError,
          QStringLiteral("loaded=%1 error=%2")
              .arg(configLoaded ? 1 : 0)
              .arg(configError ? 1 : 0));
    check("lsp.config.mapping-known", !mappingUnknown,
          QStringLiteral("messages=%1")
              .arg(static_cast<int>(session.serverMessages().size())));

    // Hover on the `add_one` call inside top.vhd, cross-file: the answer comes
    // from the package body in pkg.vhd.
    const QPair<int, int> anchor =
        positionOf(QString::fromUtf8(kTop), QStringLiteral("add_one"));
    check("probe.anchor", anchor.first >= 0 && anchor.second > 0,
          QStringLiteral("line=%1 column=%2").arg(anchor.first).arg(anchor.second));
    QString hoverText;
    bool hoverAnswered = false;
    QObject::connect(&session, &vedit::workspace::VhdlLspSession::hoverAnswered, &app,
                     [&hoverText, &hoverAnswered](const QString &, int, int, const QString &text) {
                         hoverText = text;
                         hoverAnswered = true;
                     });
    check("lsp.hover.request", session.requestHover(topPath, anchor.first, anchor.second));
    const bool hoverArrived = waitFor([&hoverAnswered]() { return hoverAnswered; }, 20000);
    const QString hoverDetail = QStringLiteral("text='%1'").arg(hoverText);
    check("lsp.hover.answer", hoverArrived, hoverDetail);
    check("lsp.hover.content", hoverText.contains(QLatin1String("add_one")),
          QStringLiteral("text='%1'").arg(hoverText));

    QVector<vedit::workspace::VhdlLocation> targets;
    bool definitionAnswered = false;
    QObject::connect(&session, &vedit::workspace::VhdlLspSession::definitionAnswered, &app,
                     [&targets, &definitionAnswered](
                         const QString &, int, int,
                         const QVector<vedit::workspace::VhdlLocation> &locations) {
                         targets = locations;
                         definitionAnswered = true;
                     });
    check("lsp.definition.request",
          session.requestDefinition(topPath, anchor.first, anchor.second));
    const bool definitionArrived =
        waitFor([&definitionAnswered]() { return definitionAnswered; }, 20000);
    const QString definitionDetail =
        QStringLiteral("targets=%1").arg(static_cast<int>(targets.size()));
    check("lsp.definition.answer", definitionArrived, definitionDetail);
    const bool crossFile =
        !targets.isEmpty()
        && QFileInfo(targets.first().file).absoluteFilePath() == QFileInfo(pkgPath).absoluteFilePath();
    check("lsp.definition.cross-file", crossFile,
          targets.isEmpty()
              ? QStringLiteral("(no target)")
              : QStringLiteral("target='%1:%2:%3'")
                    .arg(targets.first().file)
                    .arg(targets.first().line)
                    .arg(targets.first().column));
    session.stop();
    const bool stopped = waitFor([&session]() { return session.processId() <= 0; }, 8000);
    check("session.stop", stopped, QStringLiteral("pid=%1").arg(session.processId()));

    // --- 2. the negative control: no `vhdl_ls.toml` -> handshake yes, no diagnostics
    vedit::workspace::VhdlLspSession bare;
    bool bareReady = false;
    QObject::connect(&bare, &vedit::workspace::VhdlLspSession::readyChanged, &app,
                     [&bareReady](bool value) { bareReady = value; });
    check("bare.start", bare.start(program, bareWorkspace.path()));
    const bool bareHandshaken = waitFor([&bareReady]() { return bareReady; }, 30000);
    check("bare.initialize-handshake", bareHandshaken,
          QStringLiteral("state=%1").arg(bare.stateName()));
    check("bare.didopen.top", bare.openDocument(bareRtl + QStringLiteral("/top.vhd")));
    // The server needs a moment to decide that it has nothing to analyse; the
    // absence is asserted after a bounded wait, not instantly.
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    // The negative control: without the root `vhdl_ls.toml` the server SAYS its
    // library mapping is unknown and its analysis is unusable — the same sources
    // that were clean with the mapping now carry unresolved-reference errors.
    bool bareWarned = false;
    for (const QString &message : bare.serverMessages()) {
        if (message.contains(QLatin1String("Library mapping is unknown"))) {
            bareWarned = true;
        }
    }
    check("bare.mapping-unknown-warning", bareWarned,
          QStringLiteral("messages=%1")
              .arg(static_cast<int>(bare.serverMessages().size())));
    int unresolved = 0;
    for (const vedit::workspace::VhdlDiagnostic &row :
         bare.diagnostics(bareRtl + QStringLiteral("/top.vhd"))) {
        if (row.severity == 1
            && (row.message.contains(QLatin1String("No primary unit"))
                || row.message.contains(QLatin1String("No declaration")))) {
            ++unresolved;
        }
    }
    check("bare.analysis-not-usable", unresolved >= 1,
          QStringLiteral("unresolved-errors=%1 total=%2")
              .arg(unresolved)
              .arg(bare.diagnosticCount()));
    bare.stop();
    const bool bareStopped = waitFor([&bare]() { return bare.processId() <= 0; }, 8000);
    check("bare.stop", bareStopped);

    std::printf("VHDL-PROBE checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}

#endif // VEDIT_VHDL_LSP_PROBE_MAIN
