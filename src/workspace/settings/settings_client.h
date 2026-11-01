#pragma once

// Qt-side consumer of the Rust settings store (Slice 3 / AC-S3.1).
//
// The store itself is `crates/vd-settings` (flat TOML, atomic save, bad-config
// warnings) exposed through `crates/vd-ffi/src/settings.rs` as two jobs:
// `vd_settings_load` and `vd_settings_save`. This client never touches the file:
// every read/write is a C ABI job polled from a QTimer, so the GUI thread never
// performs disk IO (the same discipline as the VCS lane, T99).
//
// A build without the FFI library keeps compiling: `available()` is false and
// every request reports a diagnostic instead of silently doing nothing.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <functional>
#include <memory>

class QTimer;
struct VdService;

namespace vedit::workspace::settings {

struct Entry {
    QString key;
    QString value;
};

struct Snapshot {
    QVector<Entry> entries;
    QStringList warnings; // the Rust store's own bad-config diagnostics, already phrased
    bool completed = false;
    QString error; // non-empty when the job could not deliver a snapshot

    bool ok() const { return completed && error.isEmpty(); }
};

class SettingsClient : public QObject {
    Q_OBJECT

public:
    explicit SettingsClient(QString path, QObject *parent = nullptr);
    ~SettingsClient() override;

    SettingsClient(const SettingsClient &) = delete;
    SettingsClient &operator=(const SettingsClient &) = delete;

    QString path() const { return path_; }

    // True when this BUILD was compiled with the vd-ffi settings ABI.
    static bool ffiAvailable();
    // False when this build has no vd-ffi settings ABI.
    bool available() const;

    // Starts the read job; `done` runs on the calling (GUI) thread from the poll
    // timer. The store never fails a read: problems arrive in `warnings`.
    void load(std::function<void(Snapshot)> done);

    // Starts the write job; the Rust side writes atomically (temp file + rename).
    void save(const QVector<Entry> &entries, std::function<void(bool, QString)> done);

    // Processes events until no request is in flight (tests/startup helpers).
    bool flush(int timeoutMs = 15000);

    bool busy() const;
    std::size_t pollRounds() const { return poll_rounds_; }

    static QString valueOf(const QVector<Entry> &entries, const QString &key);
    // Upserts `key`; an empty value removes the key (the store holds strings only).
    static void setValue(QVector<Entry> *entries, const QString &key, const QString &value);

private:
    void ensureTimer();
    void pollJobs();
    void finishLoad(int state);
    void finishSave(int state);

    QString path_;
    VdService *service_ = nullptr;
    QTimer *timer_ = nullptr;

    struct PendingLoad {
        unsigned long long job = 0;
        std::function<void(Snapshot)> done;
    };
    struct PendingSave {
        unsigned long long job = 0;
        std::function<void(bool, QString)> done;
    };
    std::unique_ptr<PendingLoad> load_job_;
    std::unique_ptr<PendingSave> save_job_;
    std::size_t poll_rounds_ = 0;
};

} // namespace vedit::workspace::settings
