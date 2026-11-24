#include "waiver_actions.h"

#include <QCoreApplication>
#include <QStringList>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
#include <vd_ffi.h>
#endif

namespace vedit::workspace::diagnostics {

namespace {

/// Whether `text` is a rule / option name: `[A-Za-z0-9_-]+`. Mirrors
/// `is_rule_name` in `crates/vd-ffi/src/waiver.rs`; a preview must never write a
/// name the engine would refuse.
bool isRuleName(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar character : text) {
        const ushort code = character.unicode();
        const bool letter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        if (!letter && !digit && code != '_' && code != '-') {
            return false;
        }
    }
    return true;
}

/// Escape the RE2 metacharacters a file name may plausibly contain. Mirrors
/// `regex_escape` in the ABI (note: `/` and `-` stay unescaped).
QString regexEscape(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '.':
        case '+':
        case '*':
        case '?':
        case '(':
        case ')':
        case '|':
        case '[':
        case ']':
        case '{':
        case '}':
        case '^':
        case '$':
        case '\\':
            escaped.append(QLatin1Char('\\'));
            break;
        default:
            break;
        }
        escaped.append(character);
    }
    return escaped;
}

/// Escape a string for a TOML basic string. Mirrors `toml_escape` in the ABI.
QString tomlEscape(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '"':
            escaped.append(QLatin1String("\\\""));
            break;
        case '\\':
            escaped.append(QLatin1String("\\\\"));
            break;
        case '\n':
            escaped.append(QLatin1String("\\n"));
            break;
        case '\r':
            escaped.append(QLatin1String("\\r"));
            break;
        case '\t':
            escaped.append(QLatin1String("\\t"));
            break;
        default:
            escaped.append(character);
            break;
        }
    }
    return escaped;
}

/// Parse one `"value"` TOML basic string, allowing a trailing `# comment`.
/// Mirrors `parse_toml_string` in the ABI (same accepted escapes).
bool parseTomlString(const QString &text, QString *value)
{
    if (!text.startsWith(QLatin1Char('"'))) {
        return false;
    }
    QString parsed;
    int index = 1;
    while (index < text.size()) {
        const QChar character = text.at(index);
        ++index;
        if (character == QLatin1Char('"')) {
            const QString tail = text.mid(index).trimmed();
            if (!tail.isEmpty() && !tail.startsWith(QLatin1Char('#'))) {
                return false;
            }
            *value = parsed;
            return true;
        }
        if (character == QLatin1Char('\\')) {
            if (index >= text.size()) {
                return false;
            }
            const QChar escape = text.at(index);
            ++index;
            if (escape == QLatin1Char('"')) {
                parsed.append(QLatin1Char('"'));
            } else if (escape == QLatin1Char('\\')) {
                parsed.append(QLatin1Char('\\'));
            } else if (escape == QLatin1Char('n')) {
                parsed.append(QLatin1Char('\n'));
            } else if (escape == QLatin1Char('r')) {
                parsed.append(QLatin1Char('\r'));
            } else if (escape == QLatin1Char('t')) {
                parsed.append(QLatin1Char('\t'));
            } else {
                return false;
            }
            continue;
        }
        parsed.append(character);
    }
    return false;
}

QString slangWithoutOptionReason()
{
    // AC-5.8: the exact sentence the ABI refuses with, so a panel that shows the
    // C++ reason and the engine's own reason can never contradict itself.
    return QCoreApplication::translate(
        "VEdit",
        "this diagnostic has no option name (an error usually has none), so slang can only "
        "waive its whole scope — that would waive every diagnostic of the file, and VEdit "
        "refuses to widen it");
}

} // namespace

WaiverEntry planWaiverEntry(LintEngineKind engine,
                            const DiagnosticEntry &row,
                            const QString &optionName,
                            const QString &fileFallback)
{
    WaiverEntry entry;
    entry.engine = engine;
    entry.file = row.file.isEmpty() ? fileFallback : row.file;
    if (entry.file.isEmpty()) {
        entry.reason = QCoreApplication::translate(
            "VEdit", "the diagnostic names no file, so no waiver can be scoped");
        return entry;
    }
    if (engine == LintEngineKind::Slang) {
        const QString option = optionName.trimmed();
        if (option.isEmpty()) {
            entry.reason = slangWithoutOptionReason();
            return entry;
        }
        if (!isRuleName(option)) {
            entry.reason = QCoreApplication::translate(
                               "VEdit",
                               "`%1` is not a slang warning option name (letters, digits, `_`, "
                               "`-`)")
                               .arg(option);
            return entry;
        }
        entry.optionName = option;
        entry.valid = true;
        return entry;
    }

    const QString rule = row.code.trimmed();
    if (rule.isEmpty()) {
        entry.reason = QCoreApplication::translate(
            "VEdit", "this diagnostic carries no rule id, so it cannot be waived by verible");
        return entry;
    }
    if (!isRuleName(rule)) {
        entry.reason = QCoreApplication::translate(
                           "VEdit", "`%1` is not a verible rule id (letters, digits, `_`, `-`)")
                           .arg(rule);
        return entry;
    }
    entry.rule = rule;
    entry.line = row.line + 1;
    entry.endLine = row.endLine > row.line ? row.endLine + 1 : entry.line;
    entry.valid = true;
    return entry;
}

QString veribleLocationForFile(const QString &file)
{
    int slash = file.lastIndexOf(QLatin1Char('/'));
    slash = qMax(slash, file.lastIndexOf(QLatin1Char('\\')));
    const QString name = slash >= 0 ? file.mid(slash + 1) : file;
    return QStringLiteral("(^|/)%1$").arg(regexEscape(name));
}

QString veribleWaiverLine(const WaiverEntry &entry)
{
    if (!entry.valid || entry.engine != LintEngineKind::VeribleLint || entry.file.isEmpty()
        || entry.rule.isEmpty()) {
        return QString();
    }
    QString line = QStringLiteral("waive --rule=%1").arg(entry.rule);
    if (entry.line > 0) {
        if (entry.endLine > entry.line) {
            line += QStringLiteral(" --line=%1:%2").arg(entry.line).arg(entry.endLine);
        } else {
            line += QStringLiteral(" --line=%1").arg(entry.line);
        }
    }
    const QString location =
        entry.location.isEmpty() ? veribleLocationForFile(entry.file) : entry.location;
    line += QStringLiteral(" --location=\"%1\"").arg(location);
    return line;
}

QString slangScopeForFile(const QString &file)
{
    const QString normalised = QString(file).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalised.startsWith(QLatin1Char('/'))) {
        return normalised;
    }
    return QStringLiteral("**/%1").arg(normalised);
}

SlangWaiverText slangWaiverBlock(const QString &file,
                                 const QString &optionName,
                                 const QString &regex)
{
    SlangWaiverText block;
    if (file.trimmed().isEmpty()) {
        block.reason = QCoreApplication::translate(
            "VEdit", "the diagnostic names no file, so no waiver can be scoped");
        return block;
    }
    if (optionName.trimmed().isEmpty()) {
        block.reason = slangWithoutOptionReason();
        return block;
    }
    if (!isRuleName(optionName.trimmed())) {
        block.reason = QCoreApplication::translate(
                           "VEdit",
                           "`%1` is not a slang warning option name (letters, digits, `_`, `-`)")
                           .arg(optionName);
        return block;
    }
    // Exactly ONE scope key: `file` here, never `hier` as well.
    QString content = QStringLiteral("[[waivers]]\nfile = \"%1\"\ndiagnostic = \"%2\"\n")
                          .arg(tomlEscape(slangScopeForFile(file)),
                               tomlEscape(optionName.trimmed()));
    if (!regex.isEmpty()) {
        content += QStringLiteral("regex = \"%1\"\n").arg(tomlEscape(regex));
    }
    block.valid = true;
    block.content = content;
    return block;
}

QString validateSlangWaiverBlock(const QString &content)
{
    // Mirrors `parse_slang_waivers` in crates/vd-ffi/src/waiver.rs: what slang
    // accepts is exactly what this returns empty for.
    struct Block {
        bool hasScope = false;
        bool hasDiagnostic = false;
    };
    QVector<Block> blocks;
    bool open = false;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const int number = index + 1;
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line == QLatin1String("[[waivers]]")) {
            blocks.append(Block());
            open = true;
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            return QCoreApplication::translate(
                       "VEdit", "line %1: only `[[waivers]]` tables are allowed, found `%2`")
                .arg(number)
                .arg(line);
        }
        if (!open) {
            return QCoreApplication::translate(
                       "VEdit", "line %1: `%2` is outside a `[[waivers]]` table")
                .arg(number)
                .arg(line);
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            return QCoreApplication::translate(
                       "VEdit", "line %1: expected `key = \"value\"`, found `%2`")
                .arg(number)
                .arg(line);
        }
        const QString key = line.left(equals).trimmed();
        const QString shown = line.mid(equals + 1).trimmed();
        QString value;
        if (!parseTomlString(shown, &value)) {
            return QCoreApplication::translate(
                       "VEdit", "line %1: `%2` must be a quoted string, found `%3`")
                .arg(number)
                .arg(key, shown);
        }
        Block &block = blocks.last();
        if (key == QLatin1String("file") || key == QLatin1String("hier")) {
            if (block.hasScope) {
                return QCoreApplication::translate(
                           "VEdit",
                           "line %1: `%2` repeats a scope key (exactly one `file` or `hier` is "
                           "allowed per waiver)")
                    .arg(number)
                    .arg(key);
            }
            block.hasScope = true;
        } else if (key == QLatin1String("diagnostic")) {
            if (block.hasDiagnostic) {
                return QCoreApplication::translate("VEdit", "line %1: `diagnostic` repeats a key")
                    .arg(number);
            }
            block.hasDiagnostic = true;
        } else if (key == QLatin1String("regex")) {
            // A legal alternative to `diagnostic`; never written by this module,
            // but an existing file may carry one.
        } else {
            return QCoreApplication::translate(
                       "VEdit",
                       "line %1: unknown key `%2` — slang refuses a waiver file with an unknown "
                       "key")
                .arg(number)
                .arg(key);
        }
    }
    for (int index = 0; index < blocks.size(); ++index) {
        if (!blocks.at(index).hasScope) {
            return QCoreApplication::translate(
                       "VEdit", "waiver %1: no scope — exactly one `file` or `hier` is required")
                .arg(index + 1);
        }
    }
    return QString();
}

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)

struct WaiverActions::Impl {
    VdService *service = nullptr;

    ~Impl()
    {
        if (service != nullptr) {
            vd_service_destroy(service);
        }
    }

    QString lastError() const
    {
        VdError error{};
        if (service != nullptr) {
            vd_service_error(service, &error);
        }
        return QString::fromUtf8(error.message);
    }
};

WaiverActions::WaiverActions()
    : impl_(std::make_unique<Impl>())
{
    impl_->service = vd_service_create();
}

WaiverActions::~WaiverActions() = default;

bool WaiverActions::available() const
{
    return impl_ != nullptr && impl_->service != nullptr;
}

quint64 WaiverActions::append(const QString &waiverPath,
                              const WaiverEntry &entry,
                              bool createParentDirs,
                              QString *error)
{
    const auto refuse = [error](const QString &reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return static_cast<quint64>(0);
    };
    if (!available()) {
        return refuse(QCoreApplication::translate(
            "VEdit", "the Rust service layer is not available, so no waiver can be written"));
    }
    if (waiverPath.trimmed().isEmpty()) {
        return refuse(QCoreApplication::translate("VEdit", "the waiver file path must not be empty"));
    }
    if (!entry.valid) {
        return refuse(entry.reason.isEmpty()
                          ? QCoreApplication::translate("VEdit", "this entry cannot be waived")
                          : entry.reason);
    }
    if (entry.file.isEmpty()) {
        return refuse(QCoreApplication::translate(
            "VEdit", "the diagnostic names no file, so no waiver can be scoped"));
    }
    if (entry.engine == LintEngineKind::Slang && entry.optionName.isEmpty()) {
        return refuse(slangWithoutOptionReason());
    }
    if (entry.engine == LintEngineKind::VeribleLint && entry.rule.isEmpty()) {
        return refuse(QCoreApplication::translate(
            "VEdit", "this diagnostic carries no rule id, so it cannot be waived by verible"));
    }

    const QByteArray path = waiverPath.toUtf8();
    const QByteArray file = entry.file.toUtf8();
    const QByteArray rule = entry.rule.toUtf8();
    const QByteArray option = entry.optionName.toUtf8();
    const QByteArray location = entry.location.toUtf8();
    VdWaiverEntry raw{};
    raw.rule = entry.engine == LintEngineKind::VeribleLint ? rule.constData() : nullptr;
    raw.file = file.constData();
    if (entry.engine == LintEngineKind::VeribleLint) {
        raw.line = static_cast<uint32_t>(entry.line > 0 ? entry.line : 0);
        raw.line_end = static_cast<uint32_t>(entry.endLine > entry.line ? entry.endLine
                                                                       : entry.line);
    }
    raw.option_name = entry.engine == LintEngineKind::Slang ? option.constData() : nullptr;
    raw.location = entry.location.isEmpty() ? nullptr : location.constData();
    VdWaiverOptions options{};
    options.dedupe = 1;
    options.create_parent_dirs = createParentDirs ? 1 : 0;
    const int32_t format = entry.engine == LintEngineKind::Slang ? VD_WAIVER_FORMAT_SLANG
                                                                : VD_WAIVER_FORMAT_VERIBLE;
    const quint64 job =
        vd_waiver_append_start(impl_->service, path.constData(), format, &raw, &options);
    if (job == 0) {
        return refuse(impl_->lastError());
    }
    return job;
}

quint64 WaiverActions::registerFile(const QString &settingsPath,
                                    LintEngineKind engine,
                                    const QString &waiverPath,
                                    QString *error)
{
    const auto refuse = [error](const QString &reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return static_cast<quint64>(0);
    };
    if (!available()) {
        return refuse(QCoreApplication::translate(
            "VEdit", "the Rust service layer is not available, so no waiver can be registered"));
    }
    if (settingsPath.trimmed().isEmpty()) {
        return refuse(QCoreApplication::translate(
            "VEdit", "the settings path must not be empty to register a waiver file"));
    }
    if (waiverPath.trimmed().isEmpty()) {
        return refuse(QCoreApplication::translate("VEdit", "the waiver file path must not be empty"));
    }
    const QByteArray settings = settingsPath.toUtf8();
    const QByteArray path = waiverPath.toUtf8();
    const int32_t format = engine == LintEngineKind::Slang ? VD_WAIVER_FORMAT_SLANG
                                                          : VD_WAIVER_FORMAT_VERIBLE;
    const quint64 job =
        vd_waiver_register_start(impl_->service, settings.constData(), format, path.constData());
    if (job == 0) {
        return refuse(impl_->lastError());
    }
    return job;
}

WaiverActions::Result WaiverActions::poll(quint64 job)
{
    Result result;
    if (!available() || job == 0) {
        result.state = State::Unknown;
        result.error = QCoreApplication::translate("VEdit", "no waiver job to poll");
        return result;
    }
    VdWaiverResult raw{};
    const int32_t status = vd_waiver_poll(impl_->service, job, &raw);
    if (status == VD_STATUS_PENDING) {
        result.state = State::Pending;
        return result;
    }
    if (status == VD_STATUS_READY) {
        result.state = State::Ready;
        result.written = raw.written != 0;
        result.deduped = raw.deduped != 0;
        result.path = raw.path != nullptr ? QString::fromUtf8(raw.path) : QString();
        result.content = raw.content != nullptr ? QString::fromUtf8(raw.content) : QString();
        result.entries = static_cast<int>(raw.entries);
        result.skipped = static_cast<int>(raw.skipped);
        for (std::size_t index = 0; index < raw.warnings.count; ++index) {
            if (raw.warnings.items != nullptr && raw.warnings.items[index] != nullptr) {
                result.warnings.append(QString::fromUtf8(raw.warnings.items[index]));
            }
        }
        vd_waiver_result_free(&raw);
        return result;
    }
    result.state = status == VD_STATUS_FAILED ? State::Failed : State::Unknown;
    result.error = impl_->lastError();
    vd_waiver_result_free(&raw);
    return result;
}

void WaiverActions::release(quint64 job)
{
    if (!available() || job == 0) {
        return;
    }
    vd_waiver_release(impl_->service, job);
}

#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

} // namespace vedit::workspace::diagnostics
