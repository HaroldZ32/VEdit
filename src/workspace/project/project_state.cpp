#include "project_state.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringLiteral>

#include <utility>
#include <QCoreApplication>

namespace vedit::workspace::project {

namespace {

QStringList stringList(const QJsonObject &object, const char *key)
{
    QStringList out;
    const QJsonArray array = object.value(QString::fromLatin1(key)).toArray();
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            out.append(value.toString());
        }
    }
    return out;
}

QJsonArray toArray(const QStringList &values)
{
    return QJsonArray::fromStringList(values);
}

/// G1-e: a 64-bit FNV-1a builder for the authored-inputs fingerprint.
///
/// FNV-1a is written out here (offset basis + prime, the published constants)
/// instead of `qHash` because `qHash` is salted per process and would make a
/// persisted fingerprint change on every launch. Every component is
/// LENGTH-PREFIXED, so the serialisation has no delimiter a path could forge:
/// ["ab", "c"] and ["a", "bc"] hash differently.
class FingerprintBuilder {
public:
    void marker(const char *text)
    {
        addBytes(QByteArray(text));
    }

    void text(const QString &value)
    {
        addBytes(value.toUtf8());
    }

    void bytes(const QByteArray &value)
    {
        addBytes(value);
    }

    void count(quint64 value)
    {
        for (int index = 0; index < 8; ++index) {
            addByte(static_cast<char>((value >> (8 * index)) & 0xff));
        }
    }

    void list(const QStringList &values)
    {
        count(static_cast<quint64>(values.size()));
        for (const QString &value : values) {
            text(value);
        }
    }

    QString hex() const
    {
        return QStringLiteral("fnv1a64:%1").arg(hash_, 16, 16, QLatin1Char('0'));
    }

private:
    void addByte(char value)
    {
        hash_ ^= static_cast<quint8>(value);
        hash_ *= kPrime;
    }

    /// A length prefix followed by the bytes; the prefix is why no delimiter is
    /// needed (and why no value can inject one).
    void addBytes(const QByteArray &value)
    {
        count(static_cast<quint64>(value.size()));
        for (const char byte : value) {
            addByte(byte);
        }
    }

    static constexpr quint64 kOffsetBasis = 14695981039346656037ULL;
    static constexpr quint64 kPrime = 1099511628211ULL;
    quint64 hash_ = kOffsetBasis;
};

/// The bytes of one authored filelist, or a marker that says why there are none.
/// The CONTENT is part of the fingerprint on purpose: a `.f` edited between two
/// runs (a file added or removed) must invalidate the persisted file set even
/// though its path did not change.
const char *filelistContent(const QString &path, QByteArray *content)
{
    content->clear();
    if (path.isEmpty()) {
        return "empty-path";
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return "missing";
    }
    if (!info.isFile()) {
        return "not-a-file";
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return "unreadable";
    }
    *content = file.readAll();
    return "content";
}

/// The `[project] auto_discover` value that is in force for `root` NOW. This is
/// the one authored input the persisted state does not carry (the Rust expansion
/// reads it from the project's own `vedit.toml`), so it is read back the same way
/// here: a tiny section-aware scan, not a TOML parser. The canonical value is
/// `set:<normalised value>`, and the markers distinguish "no file" from "no
/// key", so deleting the key is a change like any other.
QString autoDiscoverInput(const QString &root)
{
    if (root.isEmpty()) {
        return QStringLiteral("no-root");
    }
    QFile file(QDir(root).filePath(QStringLiteral("vedit.toml")));
    if (!file.exists()) {
        return QStringLiteral("missing");
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("unreadable");
    }
    bool inProject = false;
    QString value;
    bool found = false;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int comment = line.indexOf(QLatin1Char('#'));
        if (comment >= 0) {
            line = line.left(comment).trimmed();
        }
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inProject = line.mid(1, line.size() - 2).trimmed() == QStringLiteral("project");
            continue;
        }
        if (!inProject) {
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0 || line.left(equals).trimmed() != QStringLiteral("auto_discover")) {
            continue;
        }
        value = line.mid(equals + 1).trimmed().toLower();
        found = true;
    }
    return found ? QStringLiteral("set:%1").arg(value) : QStringLiteral("absent");
}

} // namespace

bool ProjectState::isValid() const
{
    return !root.isEmpty();
}

QString ProjectState::computeInputsFingerprint() const
{
    FingerprintBuilder builder;
    builder.marker("vedit-authored-project-inputs/v1");
    builder.text(root);
    // The declared filelists, in import order, with the CONTENT of each one.
    builder.marker("filelists");
    builder.count(static_cast<quint64>(filelists.size()));
    for (const QString &path : filelists) {
        builder.text(path);
        QByteArray content;
        builder.marker(filelistContent(path, &content));
        builder.bytes(content);
    }
    // The switch that turns the legacy folder scan back on (root vedit.toml).
    builder.marker("auto_discover");
    builder.text(autoDiscoverInput(root));
    // The panel's authored -I/-D entries AND the effective lists they produced.
    builder.marker("projectIncludeDirs");
    builder.list(projectIncludeDirs);
    builder.marker("projectDefines");
    builder.list(projectDefines);
    builder.marker("includeDirs");
    builder.list(includeDirs);
    builder.marker("defines");
    builder.list(defines);
    // The variable table(s): they are expanded inside the `.f` lines.
    builder.marker("variables");
    builder.list(variables);
    builder.marker("variablesByRoot");
    builder.count(static_cast<quint64>(variablesByRoot.size()));
    for (auto entry = variablesByRoot.constBegin(); entry != variablesByRoot.constEnd();
         ++entry) {
        builder.text(entry.key());
        builder.list(entry.value());
    }
    return builder.hex();
}

QJsonObject ProjectState::toJson() const
{
    QJsonObject object;
    // Version 5 (AC-1.5): + the project panel's own `-I`/`-D` entries. Version 4
    // and older files load unchanged with empty entries (tolerant load).
    // Version 6 (G1-e, batch-20261119): `files` is the DECLARED filelist set, not
    // whatever an old auto-discovering build scanned; a file below this version
    // loads with `files` dropped and `filesStale` set (see `fromJson`).
    object.insert(QStringLiteral("version"), kCurrentVersion);
    object.insert(QStringLiteral("projectRoot"), root);
    object.insert(QStringLiteral("capturedAtUtc"), capturedAt);
    object.insert(QStringLiteral("filelists"), toArray(filelists));
    object.insert(QStringLiteral("variables"), toArray(variables));
    // RM-3: the per-root variable tables. A file written by an older build has
    // no such key; a reader of this build tolerates the absence (and the model
    // seeds the live root's entry from `variables`).
    QJsonObject byRootObject;
    for (auto it = variablesByRoot.constBegin(); it != variablesByRoot.constEnd(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        byRootObject.insert(it.key(), toArray(it.value()));
    }
    object.insert(QStringLiteral("variablesByRoot"), byRootObject);
    // RM-17: the per-project TOP (module name, empty = Auto) and the per-root
    // memory of it, exactly like the variable tables above.
    object.insert(QStringLiteral("top"), top);
    QJsonObject topByRootObject;
    for (auto it = topByRoot.constBegin(); it != topByRoot.constEnd(); ++it) {
        if (!it.key().isEmpty() && !it.value().isEmpty()) {
            topByRootObject.insert(it.key(), it.value());
        }
    }
    object.insert(QStringLiteral("topByRoot"), topByRootObject);
    object.insert(QStringLiteral("files"), toArray(files));
    // G1-e (review): the fingerprint of the authored inputs `files` belongs to.
    // The model records it when it adopts an expansion answer; a state that was
    // never adopted (a test, a hand-built state) computes it here so a save/load
    // round trip is still consistent. Writing the RECORDED value - rather than
    // always recomputing - is what binds `files` to its inputs: an import saved
    // before the next expansion keeps the OLD fingerprint, so the next load sees
    // that the declared inputs changed and refuses the old file set.
    object.insert(QStringLiteral("inputsFingerprint"),
                  inputsFingerprint.isEmpty() ? computeInputsFingerprint() : inputsFingerprint);
    object.insert(QStringLiteral("projectIncludeDirs"), toArray(projectIncludeDirs));
    object.insert(QStringLiteral("projectDefines"), toArray(projectDefines));
    object.insert(QStringLiteral("includeDirs"), toArray(includeDirs));
    object.insert(QStringLiteral("defines"), toArray(defines));
    object.insert(QStringLiteral("diagnostics"), toArray(diagnostics));
    return object;
}

ProjectState ProjectState::fromJson(const QJsonObject &object)
{
    ProjectState state;
    // G1-e: the format version decides what `files` may mean. A missing `version`
    // key reads as 0 - an old file, which is exactly how it must be treated.
    const int version = object.value(QStringLiteral("version")).toInt();
    state.root = object.value(QStringLiteral("projectRoot")).toString();
    state.capturedAt = object.value(QStringLiteral("capturedAtUtc")).toString();
    state.filelists = stringList(object, "filelists");
    state.variables = stringList(object, "variables");
    const QJsonObject byRoot = object.value(QStringLiteral("variablesByRoot")).toObject();
    for (auto it = byRoot.constBegin(); it != byRoot.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isArray()) {
            continue;
        }
        QStringList values;
        for (const QJsonValue &value : it.value().toArray()) {
            if (value.isString()) {
                values.append(value.toString());
            }
        }
        state.variablesByRoot.insert(it.key(), values);
    }
    state.top = object.value(QStringLiteral("top")).toString();
    const QJsonObject topByRoot = object.value(QStringLiteral("topByRoot")).toObject();
    for (auto it = topByRoot.constBegin(); it != topByRoot.constEnd(); ++it) {
        if (!it.key().isEmpty() && it.value().isString() && !it.value().toString().isEmpty()) {
            state.topByRoot.insert(it.key(), it.value().toString());
        }
    }
    state.files = stringList(object, "files");
    // G1-e (review): the fingerprint recorded by the build that wrote `files`.
    // Absent in every file written before this key existed; `load` treats that
    // absence as stale.
    state.inputsFingerprint = object.value(QStringLiteral("inputsFingerprint")).toString();
    // G1-e: a version-5-and-older `files` array is NOT the declared set (it may be
    // an `auto_discover` folder scan this build never asked for and cannot
    // reproduce). Everything the user AUTHORED is kept - the filelists, the top,
    // the includes/defines and the variable tables - so the next live expansion
    // still runs with the same inputs; only the derived file set is dropped.
    if (version < kCurrentVersion) {
        state.files.clear();
        state.filesStale = true;
    }
    state.projectIncludeDirs = stringList(object, "projectIncludeDirs");
    state.projectDefines = stringList(object, "projectDefines");
    state.includeDirs = stringList(object, "includeDirs");
    state.defines = stringList(object, "defines");
    state.diagnostics = stringList(object, "diagnostics");
    return state;
}

QString ProjectState::nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

ProjectStateStore::ProjectStateStore(QString directory)
    : directory_(std::move(directory))
{
}

QString ProjectStateStore::fileName()
{
    return QStringLiteral("project-model.json");
}

QString ProjectStateStore::path() const
{
    QString directory = directory_;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (directory.isEmpty()) {
        return QString();
    }
    return QDir(directory).filePath(fileName());
}

ProjectState ProjectStateStore::load(bool *ok, QString *error, bool restoreCachedFiles) const
{
    const auto fail = [ok, error](const QString &reason) {
        if (ok != nullptr) {
            *ok = false;
        }
        if (error != nullptr) {
            *error = reason;
        }
        return ProjectState{};
    };

    const QString filePath = path();
    if (filePath.isEmpty()) {
        return fail(QCoreApplication::translate("VEdit", "no state directory is available"));
    }
    QFile file(filePath);
    if (!file.exists()) {
        return fail(QString());
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QCoreApplication::translate("VEdit", "cannot read %1: %2").arg(filePath, file.errorString()));
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("%1 is not a project state file: %2")
                        .arg(filePath, parseError.errorString()));
    }
    QJsonObject archive = document.object();
    if (!restoreCachedFiles) {
        // Only authored settings are useful before the live expansion. Avoid
        // decoding stale rows or re-reading filelists on the GUI startup path.
        archive.remove(QStringLiteral("files"));
        archive.remove(QStringLiteral("includeDirs"));
        archive.remove(QStringLiteral("defines"));
        archive.remove(QStringLiteral("diagnostics"));
    }
    ProjectState state = ProjectState::fromJson(archive);
    if (!state.isValid()) {
        return fail(QStringLiteral("%1 carries no project root").arg(filePath));
    }
    // G1-e: this store is the ONE reader of the file, so the "never adopt a stale
    // file set" rule is enforced here, at the boundary every consumer (the panel
    // and the module-tree catalog client) calls.
    //   * the version gate in `fromJson` already dropped the files of a format
    //     below 6;
    //   * this check is the OTHER half (the two are an AND): recompute the
    //     authored-inputs fingerprint from what is on disk NOW - the `.f` bytes
    //     and the root `vedit.toml` included - and compare it with the value
    //     recorded when `files` was produced. A mismatch, or a file that records
    //     no fingerprint at all, means no current input reproduces that list, so
    //     the derived list is dropped and the next live expansion refills it.
    // Everything the user AUTHORED (filelists, top, -I/-D, variables) stays.
    if (!restoreCachedFiles || state.filesStale ||
        state.inputsFingerprint != state.computeInputsFingerprint()) {
        state.files.clear();
        state.filesStale = true;
    }

    if (ok != nullptr) {
        *ok = true;
    }
    if (error != nullptr) {
        error->clear();
    }
    return state;
}

bool ProjectStateStore::save(const ProjectState &state, QString *error) const
{
    const QString filePath = path();
    if (filePath.isEmpty()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "no state directory is available");
        }
        return false;
    }
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot create %1").arg(QFileInfo(filePath).absolutePath());
        }
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot write %1: %2").arg(filePath, file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(state.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot commit %1: %2").arg(filePath, file.errorString());
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

} // namespace vedit::workspace::project
