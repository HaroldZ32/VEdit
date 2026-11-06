#include "catalog_client.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QVector>

#include <cstdio>

#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
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

ModuleCatalog toCatalog(const VdModuleCatalog &catalog, const QString &root, int fileCount)
{
    ModuleCatalog snapshot;
    snapshot.projectRoot = root;
    snapshot.tool = fromC(catalog.tool);
    snapshot.elapsedMs = catalog.elapsed_ms;
    snapshot.peakRssKb = catalog.peak_rss_kb;
    snapshot.warning = fromC(catalog.warning);
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

void CatalogClient::start(const QString &projectRoot)
{
    resetJobs();
    projectRoot_ = projectRoot;
#if !defined(VEDIT_MODULE_TREE_HAVE_FFI)
    Q_UNUSED(projectRoot);
    fail(QCoreApplication::translate(
        "VEdit",
        "this build has no Rust service layer (vd-ffi), so the module catalog cannot run"));
    return;
#else
    if (service_ == nullptr) {
        fail(QCoreApplication::translate("VEdit", "the Rust service handle could not be created"));
        return;
    }
    // 1) The project model's persisted state is the single source of truth for
    //    the root and the effective file set: the union of the imported .f files
    //    and the folder scan, with the tools page's -I/-D.
#if defined(VEDIT_MODULE_TREE_HAVE_PROJECT)
    {
        project::ProjectStateStore store(stateDirectory_);
        bool ok = false;
        QString loadError;
        const project::ProjectState state = store.load(&ok, &loadError);
        const bool rootMatches =
            !state.root.isEmpty() && (projectRoot.isEmpty() || state.root == projectRoot);
        if (ok && rootMatches && !state.files.isEmpty()) {
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
        "VEdit", "no project root: open a folder or start vedit-app with --project <dir>"));
#endif
}

void CatalogClient::beginProjectFallback()
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    const QByteArray root = projectRoot_.toUtf8();
    // No imported filelists / settings path here on purpose: this path only runs
    // when the project model has not produced its persisted state yet; the root's
    // own scan is what the service can expand without that state.
    job_ = vd_project_expand_v2(static_cast<VdService *>(service_),
                                root.isEmpty() ? nullptr : root.constData(), nullptr, nullptr,
                                nullptr);
    catalogPhase_ = false;
    ticks_ = 0;
    if (job_ == 0) {
        fail(QCoreApplication::translate("VEdit", "project expansion was rejected: %1")
                 .arg(serviceError()));
        return;
    }
    source_ = QStringLiteral("vd_project_expand_v2(%1)").arg(projectRoot_);
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
    if (files.isEmpty()) {
        fail(QCoreApplication::translate(
            "VEdit", "the project has no source files, so there is no module catalog to build"));
        return;
    }
    BorrowedList fileList;
    fileList.fill(files);
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
    options.timeout_ms = 0;      // the service default (30 s)
    options.max_json_bytes = 0;  // the service default (256 MiB)
    options.max_files = 0;       // the service default (oversize warning)

    source_ = source;
    fileCount_ = fileCount;
    projectRoot_ = root.isEmpty() ? projectRoot_ : root;
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        std::fprintf(stderr, "module-catalog: run source='%s' files=%d root='%s'\n",
                     qPrintable(source), fileCount, qPrintable(projectRoot_));
    }
    job_ = vd_catalog_start(static_cast<VdService *>(service_), &fileView, &options);
    catalogPhase_ = true;
    ticks_ = 0;
    if (job_ == 0) {
        fail(QCoreApplication::translate("VEdit", "module catalog was rejected: %1")
                 .arg(serviceError()));
        return;
    }
    if (onProgress_ != nullptr) {
        onProgress_(QCoreApplication::translate("VEdit", "cataloguing %1 project file(s)…")
                        .arg(fileCount));
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
    // 30 s at 50 ms; a stuck worker must not poll forever.
    if (++ticks_ > 600) {
        resetJobs();
        fail(QCoreApplication::translate(
            "VEdit", "the module catalog service did not answer within 30 s"));
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
                     .arg(serviceError()));
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
    const int status = vd_catalog_poll(static_cast<VdService *>(service_), job_, &raw);
    if (status == VD_STATUS_PENDING) {
        return;
    }
    if (status != VD_STATUS_READY) {
        job_ = 0;
        timer_->stop();
        fail(QCoreApplication::translate("VEdit", "module catalog failed: %1")
                 .arg(serviceError()));
        return;
    }
    const ModuleCatalog catalog = toCatalog(raw, projectRoot_, fileCount_);
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

void CatalogClient::fail(const QString &message)
{
    if (onFailed_ != nullptr) {
        onFailed_(message);
    }
}

QString CatalogClient::serviceError() const
{
#if defined(VEDIT_MODULE_TREE_HAVE_FFI)
    if (service_ != nullptr) {
        VdError error{};
        if (vd_service_error(static_cast<VdService *>(service_), &error) != 0) {
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

void CatalogClient::setOnFailed(std::function<void(const QString &)> callback)
{
    onFailed_ = std::move(callback);
}

void CatalogClient::setOnProgress(std::function<void(const QString &)> callback)
{
    onProgress_ = std::move(callback);
}

} // namespace vedit::workspace::module_tree
