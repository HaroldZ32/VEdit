#include "catalog_client.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QVector>

#include <cstdio>

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
#include "language_registry.h"
#include "project_state.h"
#endif

#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
#include <vd_ffi.h>
#endif

#include <utility>

namespace vedit::workspace::module_tree {

#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
namespace {

/// Borrowed C string list for one call. The strings live in `storage`, which the
/// caller keeps alive for the duration of the call (the ABI borrows, it does not
/// own).
struct BorrowedList {
    QVector<QByteArray> storage;
    QVector<char *> pointers;

    void fill(const QStringList &values)
    {
        storage.reserve(values.size());
        pointers.reserve(values.size());
        for (const QString &value : values) {
            storage.append(value.toUtf8());
            pointers.append(storage.last().data());
        }
    }

    VdStringList view()
    {
        VdStringList list{};
        list.count = static_cast<size_t>(pointers.size());
        list.items = pointers.isEmpty() ? nullptr : pointers.data();
        return list;
    }
};

QStringList toStringList(const VdStringList &list)
{
    QStringList values;
    for (size_t index = 0; index < list.count; ++index) {
        if (list.items[index] != nullptr) {
            values.append(QString::fromUtf8(list.items[index]));
        }
    }
    return values;
}

QString fromC(const char *text)
{
    return text != nullptr ? QString::fromUtf8(text) : QString();
}

ModulePort toPort(const VdModulePort &port)
{
    ModulePort out;
    out.name = fromC(port.name);
    out.direction = fromC(port.direction);
    out.type = fromC(port.type_name);
    out.file = fromC(port.file);
    out.line = static_cast<int>(port.line);
    out.column = static_cast<int>(port.column);
    return out;
}

QVector<ModulePort> toPorts(const VdModulePortList &list)
{
    QVector<ModulePort> out;
    out.reserve(static_cast<int>(list.count));
    for (size_t index = 0; index < list.count; ++index) {
        out.append(toPort(list.items[index]));
    }
    return out;
}

ModuleInstance toInstance(const VdModuleInstance &instance)
{
    ModuleInstance out;
    out.module = fromC(instance.module);
    out.path = fromC(instance.path);
    out.file = fromC(instance.file);
    out.line = static_cast<int>(instance.line);
    out.column = static_cast<int>(instance.column);
    return out;
}

QVector<ModuleInstance> toInstances(const VdModuleInstanceList &list)
{
    QVector<ModuleInstance> out;
    out.reserve(static_cast<int>(list.count));
    for (size_t index = 0; index < list.count; ++index) {
        out.append(toInstance(list.items[index]));
    }
    return out;
}

/// The ABI reports source positions as the tool printed them: an absolute path
/// for an absolute file argument, and a path relative to the service's cwd (the
/// project root) otherwise. Every consumer of a jump target needs an absolute
/// path, so the client normalises here - the ONE place a catalog path becomes a
/// file path.
QString absoluteSourcePath(const QString &file, const QString &root)
{
    if (file.isEmpty()) {
        return file;
    }
    const QFileInfo info(file);
    if (info.isAbsolute() || root.isEmpty()) {
        return QDir::cleanPath(file);
    }
    return QDir::cleanPath(QDir(root).absoluteFilePath(file));
}

ModuleCatalog toCatalog(const VdModuleCatalog &catalog, const VdStringList &units,
                        const QString &root, int fileCount)
{
    ModuleCatalog snapshot;
    snapshot.projectRoot = root;
    snapshot.tool = fromC(catalog.tool);
    snapshot.elapsedMs = catalog.elapsed_ms;
    snapshot.peakRssKb = catalog.peak_rss_kb;
    snapshot.warning = fromC(catalog.warning);
    snapshot.top = fromC(catalog.top);
    snapshot.reachableFiles = toStringList(catalog.reachable_files);
    snapshot.outsideModules = toStringList(catalog.outside_modules);
    // G1-c: the files whose every module declaration lost the duplicate pick, and
    // the sentence saying they leave the compiler input. Same normalisation as a
    // module's own file (`absoluteSourcePath`): consumers compare these paths with
    // document paths, so a relative spelling from the service would never match.
    snapshot.shadowedFiles = toStringList(catalog.shadowed_files);
    for (QString &file : snapshot.shadowedFiles) {
        file = absoluteSourcePath(file, root);
    }
    snapshot.shadowedWarning = fromC(catalog.shadowed_warning);
    snapshot.fileSetSize = fileCount;
    snapshot.valid = true;
    snapshot.modules.reserve(static_cast<int>(catalog.modules.count));
    for (size_t index = 0; index < catalog.modules.count; ++index) {
        const VdModule &module = catalog.modules.items[index];
        ModuleDefinition out;
        out.name = fromC(module.name);
        out.file = fromC(module.file);
        out.line = static_cast<int>(module.line);
        out.column = static_cast<int>(module.column);
        out.isTop = module.is_top != 0;
        out.ports = toPorts(module.ports);
        out.children = toInstances(module.children);
        out.file = absoluteSourcePath(out.file, root);
        if (index < units.count) {
            out.compilationUnit = absoluteSourcePath(fromC(units.items[index]), root);
        }
        for (ModulePort &port : out.ports) {
            port.file = absoluteSourcePath(port.file, root);
        }
        for (ModuleInstance &child : out.children) {
            child.file = absoluteSourcePath(child.file, root);
        }
        snapshot.modules.append(out);
    }
    return snapshot;
}

} // namespace
#endif // VEDIT_MODULE_TREE_HAVE_FFI

CatalogClient::CatalogClient(QObject *parent)
    : QObject(parent)
    , timer_(new QTimer(this))
{
    timer_->setInterval(50);
    QObject::connect(timer_, &QTimer::timeout, this, [this]() { poll(); });
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    service_ = vd_service_create();
#endif
}

CatalogClient::~CatalogClient()
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    if (service_ != nullptr) {
        // Running jobs finish into memory nobody reads (vd-ffi contract), and the
        // service handle owns that memory until it is destroyed.
        vd_service_destroy(static_cast<VdService *>(service_));
    }
#endif
    service_ = nullptr;
}

void CatalogClient::setTop(const QString &top)
{
    top_ = top.trimmed();
}

QString CatalogClient::top() const
{
    return top_;
}

QStringList CatalogClient::lastFileSet() const
{
    return lastFileSet_;
}

void CatalogClient::setStateDirectory(const QString &stateDirectory)
{
    stateDirectory_ = stateDirectory;
}

void CatalogClient::setSlangBinary(const QString &binary)
{
    slangBinary_ = binary;
}

bool CatalogClient::isRunning() const
{
    return job_ != 0;
}

QString CatalogClient::projectRoot() const
{
    return projectRoot_;
}

QString CatalogClient::sourceDescription() const
{
    return source_;
}

void CatalogClient::resetJobs()
{
    timer_->stop();
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    if (job_ != 0 && catalogPhase_ && service_ != nullptr) {
        // AC M1.4: CANCEL, then release. Release alone left the `slang` child
        // running for its whole (now 120 s) budget into a slot nobody reads, so
        // one superseded run and the next one overlapped: 2-3 concurrent slang
        // processes per user gesture.
        vd_catalog_cancel(static_cast<VdService *>(service_), job_);
        vd_catalog_release(static_cast<VdService *>(service_), job_);
    }
#endif
    job_ = 0;
    ticks_ = 0;
    catalogPhase_ = false;
}

void CatalogClient::cancel()
{
    resetJobs();
}

void CatalogClient::setLargeProjectThreshold(int sources)
{
    largeProjectThreshold_ = sources;
}

void CatalogClient::setAllowLargeCatalog(bool allow)
{
    allowLargeCatalog_ = allow;
}

void CatalogClient::setTimeoutMs(int milliseconds)
{
    timeoutMs_ = milliseconds;
}

void CatalogClient::start(const QString &projectRoot)
{
    resetJobs();
    projectRoot_ = projectRoot;
#if !defined(VEDIT_MODULE_TREE_HAVE_FFI)
    Q_UNUSED(projectRoot);
    fail(QCoreApplication::translate(
             "VEdit",
             "this build has no Rust service layer (vd-ffi), so the module catalog cannot run"),
         CatalogFailureKind::ToolError);
    return;
#else
    if (service_ == nullptr) {
        fail(QCoreApplication::translate("VEdit", "the Rust service handle could not be created"),
             CatalogFailureKind::ToolError);
        return;
    }
    // 1) The project model's persisted state is the single source of truth for
    //    the root and the effective file set: the expansion of the DECLARED
    //    filelists (imported `.f` ∪ `vedit.toml [project] filelists`), with the
    //    tools page's -I/-D. The legacy folder scan and root-level `.f`
    //    discovery are opt-in per project (`[project] auto_discover = true`,
    //    default `false`; docs/USER_GUIDE.md §2.8).
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    {
        project::ProjectStateStore store(stateDirectory_);
        bool ok = false;
        QString loadError;
        const project::ProjectState state = store.load(&ok, &loadError);
        const bool rootMatches =
            !state.root.isEmpty() && (projectRoot.isEmpty() || state.root == projectRoot);
        // G1-e: `filesStale` is set when the file's `files` cannot be trusted -
        // a version-5-and-older file (that list may be an `auto_discover`
        // folder scan this build never asked for) OR a current-version file
        // whose authored-inputs fingerprint no longer matches the `.f` bytes /
        // `vedit.toml` / -I/-D / variables on disk (an externally edited
        // filelist). That was the exact way a stale file set used to become
        // authoritative ("root matches + files non-empty"). Never adopt it:
        // fall through to the live expansion below, which runs with the SAME
        // authored inputs the panel has.
        if (ok && rootMatches && !state.filesStale && !state.files.isEmpty()) {
            beginCatalog(state.files, state.includeDirs, state.defines, state.root,
                         static_cast<int>(state.files.size()),
                         QStringLiteral("project model (%1)")
                             .arg(QFileInfo(store.path()).fileName()));
            return;
        }
    }
#endif
    // 2) No usable persisted model yet: expand the root with the same service the
    //    project panel uses, so the catalog still covers the whole file set.
    if (!projectRoot.isEmpty()) {
        beginProjectFallback();
        return;
    }
    fail(QCoreApplication::translate(
             "VEdit", "no project root: open a folder or start vedit-app with --project <dir>"),
         CatalogFailureKind::ToolError);
#endif
}

void CatalogClient::startFromFileSet(const QString &root, const QStringList &files,
                                     const QStringList &includeDirs, const QStringList &defines)
{
    resetJobs();
    projectRoot_ = root;
    if (service_ == nullptr) {
        fail(QCoreApplication::translate("VEdit", "the Rust service handle could not be created"),
             CatalogFailureKind::ToolError);
        return;
    }
    beginCatalog(files, includeDirs, defines, root, static_cast<int>(files.size()),
                 QStringLiteral("live project file set"));
}

void CatalogClient::beginProjectFallback()
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    const QByteArray root = projectRoot_.toUtf8();
    // RM-2: expand with the PANEL's inputs, not with a bare root. The previous
    // version passed `nullptr, nullptr`, so right after a project switch (the
    // persisted model still describes the OLD root) the catalog was built from
    // the folder scan alone: a project with `[project]` / imported `.f`
    // filelists catalogued a different file set than the panel showed, and the
    // tools page's `-I`/`-D` never reached the elaboration.
    //
    // The inputs come from the SAME persisted model this run is reading:
    //   * the settings store beside `project-model.json` (the path the project
    //     panel itself passes), always;
    //   * the imported `.f` lists and the variables table ONLY when the
    //     persisted model belongs to THIS root - a previous project's lists
    //     must never be expanded into this one.
    QStringList filelists;
    QStringList variables;
    QString settingsPath;
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    project::ProjectStateStore store(stateDirectory_);
    // Same rule as ProjectModel::settingsPath(): `<state dir>/settings.toml`,
    // where an empty directory resolves to the platform app-config location.
    const QString storePath = store.path();
    if (!storePath.isEmpty()) {
        settingsPath = QFileInfo(storePath).absolutePath()
            + QStringLiteral("/settings.toml");
    }
    bool stateOk = false;
    const project::ProjectState state = store.load(&stateOk, nullptr);
    if (stateOk && state.root == projectRoot_) {
        filelists = state.filelists;
        variables = state.variables;
    }
#endif
    BorrowedList filelistList;
    filelistList.fill(filelists);
    BorrowedList variableList;
    variableList.fill(variables);
    VdStringList filelistView = filelistList.view();
    VdStringList variableView = variableList.view();
    const QByteArray settings = settingsPath.toUtf8();
    job_ = vd_project_expand_v2(static_cast<VdService *>(service_),
                                root.isEmpty() ? nullptr : root.constData(),
                                filelists.isEmpty() ? nullptr : &filelistView,
                                settings.isEmpty() ? nullptr : settings.constData(),
                                variables.isEmpty() ? nullptr : &variableView);
    catalogPhase_ = false;
    ticks_ = 0;
    if (job_ == 0) {
        fail(QCoreApplication::translate("VEdit", "project expansion was rejected: %1")
                 .arg(serviceError()),
             CatalogFailureKind::ToolError);
        return;
    }
    source_ = QStringLiteral("vd_project_expand_v2(%1, filelists=%2, settings=%3)")
                  .arg(projectRoot_)
                  .arg(filelists.size())
                  .arg(settingsPath.isEmpty() ? QStringLiteral("<none>") : settingsPath);
    if (onProgress_ != nullptr) {
        onProgress_(QCoreApplication::translate("VEdit", "expanding the project file set…"));
    }
    timer_->start();
#endif
}

void CatalogClient::beginCatalog(const QStringList &files, const QStringList &includeDirs,
                                 const QStringList &defines, const QString &root, int fileCount,
                                 const QString &source)
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    // RM-11: the catalog ingests only files whose language has the CATALOG
    // capability in this build. The persisted project model is already
    // filtered, and this is the same rule at the consumer boundary, so a state
    // written by an older build (or a future input path) can never feed a
    // manifest, note or build script to the elaborator.
    QStringList sources = files;
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    sources.clear();
    sources.reserve(files.size());
    for (const QString &file : files) {
        if (project::LanguageRegistry::instance().supports(file,
                                                           project::Capability::Catalog)) {
            sources.append(file);
        }
    }
#endif
    if (sources.isEmpty()) {
        fail(QCoreApplication::translate(
                 "VEdit",
                 "the project has no source files, so there is no module catalog to build"),
             CatalogFailureKind::ToolError);
        return;
    }
    // AC M1.5: an AUTOMATIC run over a very large file set is withheld. The old
    // behaviour catalogued everything, timed out after 120 s and left the panel
    // empty while the next gesture started ANOTHER whole-project elaboration.
    // A named top (a scoped `--top` run) or an explicit Refresh still starts it.
    const int sourceCount = static_cast<int>(sources.size());
    const bool userRequested = allowLargeCatalog_;
    allowLargeCatalog_ = false;
    lastFileSet_ = files;
    source_ = source;
    fileCount_ = sourceCount;
    projectRoot_ = root.isEmpty() ? projectRoot_ : root;
    if (!shouldAutoCatalog(sourceCount, !top_.isEmpty(), largeProjectThreshold_,
                           userRequested)) {
        job_ = 0;
        catalogPhase_ = false;
        ticks_ = 0;
        timer_->stop();
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            std::fprintf(stderr,
                         "module-catalog: withheld (large project) files=%d threshold=%d top='%s'\n",
                         sourceCount, largeProjectThreshold_, qPrintable(top_));
        }
        if (onLargeProject_ != nullptr) {
            onLargeProject_(sourceCount);
        }
        return;
    }
    BorrowedList fileList;
    fileList.fill(sources);
    BorrowedList includeList;
    includeList.fill(includeDirs);
    BorrowedList defineList;
    defineList.fill(defines);
    VdStringList fileView = fileList.view();
    VdStringList includeView = includeList.view();
    VdStringList defineView = defineList.view();

    // The service spawns the tool with cwd = the project root, so a relative
    // override (VEDIT_SLANG=build/slang/out/slang) would not resolve.
    const QString slangPath =
        slangBinary_.isEmpty() ? QString() : QFileInfo(slangBinary_).absoluteFilePath();
    const QByteArray slang = slangPath.toUtf8();
    VdCatalogOptions options{};
    options.slang_binary = slang.isEmpty() ? nullptr : slang.constData();
    options.include_dirs = includeView;
    options.defines = defineView;
    // 0 keeps the catalog default (120 s for the slang child). The explicit
    // setter and the `VEDIT_CATALOG_TIMEOUT_MS` probe are the test seams that make
    // the timeout rules (AC M1.1/M1.2) assertable without a 120 s test.
    int budgetMs = timeoutMs_;
    if (budgetMs <= 0) {
        budgetMs = qEnvironmentVariableIntValue("VEDIT_CATALOG_TIMEOUT_MS");
    }
    options.timeout_ms = static_cast<uint64_t>(std::max(0, budgetMs));
    options.max_json_bytes = 0;  // the service default (a 256 MiB warning threshold; the document is streamed)
    options.max_files = 0;       // the service default (oversize warning)
    // RM-17: the named top ("" = Auto) — the SAME run feeds the tree and the
    // reachable file set the lint scope consumes.
    const QByteArray topName = top_.toUtf8();
    options.top = topName.isEmpty() ? nullptr : topName.constData();

    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        std::fprintf(stderr, "module-catalog: run source='%s' files=%d kept=%d root='%s'\n",
                     qPrintable(source), fileCount, fileCount_, qPrintable(projectRoot_));
    }
    job_ = vd_catalog_start(static_cast<VdService *>(service_), &fileView, &options);
    catalogPhase_ = true;
    ticks_ = 0;
    if (job_ == 0) {
        fail(QCoreApplication::translate("VEdit", "module catalog was rejected: %1")
                 .arg(serviceError()),
             CatalogFailureKind::ToolError);
        return;
    }
    if (onProgress_ != nullptr) {
        onProgress_(QCoreApplication::translate("VEdit", "cataloguing %1 project file(s)…")
                        .arg(fileCount_));
    }
    timer_->start();
#else
    Q_UNUSED(files);
    Q_UNUSED(includeDirs);
    Q_UNUSED(defines);
    Q_UNUSED(root);
    Q_UNUSED(fileCount);
    Q_UNUSED(source);
#endif
}

void CatalogClient::poll()
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    if (job_ == 0 || service_ == nullptr) {
        timer_->stop();
        return;
    }
    // 300 s at 50 ms; a stuck worker must not poll forever. The budget covers a
    // whole-project elaboration PLUS the streamed parse, so the old 30 s (a
    // single-file-lint budget) turned a large but healthy project into
    // "did not answer" after the 256 MiB AST cap stopped being the limit.
    if (++ticks_ > 6000) {
        // The watchdog KILLS the child (AC M1.4): the old path only dropped the
        // handle, so a wedged slang kept running for minutes.
        resetJobs();
        fail(QCoreApplication::translate(
                 "VEdit", "the module catalog service did not answer within 300 s"),
             CatalogFailureKind::Timeout);
        return;
    }
    if (!catalogPhase_) {
        VdProjectInfo info{};
        const int status = vd_project_poll(static_cast<VdService *>(service_), job_, &info);
        if (status == VD_STATUS_PENDING) {
            return;
        }
        if (status != VD_STATUS_READY) {
            job_ = 0;
            timer_->stop();
            fail(QCoreApplication::translate("VEdit", "project expansion failed: %1")
                     .arg(serviceError()),
                 CatalogFailureKind::ToolError);
            return;
        }
        const QStringList files = toStringList(info.files);
        const QStringList includeDirs = toStringList(info.include_dirs);
        const QStringList defines = toStringList(info.defines);
        vd_project_info_free(&info);
        job_ = 0;
        beginCatalog(files, includeDirs, defines, projectRoot_, static_cast<int>(files.size()),
                     QStringLiteral("vd_project_expand_v2(%1)").arg(projectRoot_));
        return;
    }
    VdModuleCatalog raw{};
    VdStringList units{};
    const int status = vd_catalog_poll_v2(static_cast<VdService *>(service_), job_, &raw, &units);
    if (status == VD_STATUS_PENDING) {
        return;
    }
    if (status != VD_STATUS_READY) {
        job_ = 0;
        timer_->stop();
        int code = 0;
        const QString message = serviceError(&code);
        fail(QCoreApplication::translate("VEdit", "module catalog failed: %1").arg(message),
             catalogFailureKind(code));
        return;
    }
    const ModuleCatalog catalog = toCatalog(raw, units, projectRoot_, fileCount_);
    vd_catalog_compilation_units_free(&units);
    vd_module_catalog_free(&raw);
    job_ = 0;
    timer_->stop();
    finish(catalog);
#endif
}

void CatalogClient::finish(const ModuleCatalog &catalog)
{
    if (onReady_ != nullptr) {
        onReady_(catalog);
    }
}

void CatalogClient::fail(const QString &message, CatalogFailureKind kind)
{
    if (onFailed_ != nullptr) {
        onFailed_(message, kind);
    }
}

QString CatalogClient::serviceError(int *code) const
{
    if (code != nullptr) {
        *code = 0;
    }
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    if (service_ != nullptr) {
        VdError error{};
        if (vd_service_error(static_cast<VdService *>(service_), &error) != 0) {
            if (code != nullptr) {
                *code = error.code;
            }
            return QString::fromUtf8(error.message);
        }
    }
#endif
    return QCoreApplication::translate("VEdit", "unknown error");
}

void CatalogClient::setOnReady(std::function<void(const ModuleCatalog &)> callback)
{
    onReady_ = std::move(callback);
}

void CatalogClient::setOnFailed(
    std::function<void(const QString &, CatalogFailureKind)> callback)
{
    onFailed_ = std::move(callback);
}

void CatalogClient::setOnLargeProject(std::function<void(int)> callback)
{
    onLargeProject_ = std::move(callback);
}

void CatalogClient::setOnProgress(std::function<void(const QString &)> callback)
{
    onProgress_ = std::move(callback);
}

} // namespace vedit::workspace::module_tree
