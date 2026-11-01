#include "settings_client.h"

#if defined(VEDIT_SETTINGS_HAVE_FFI)
#include "vd_ffi.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <QByteArray>
#include <QVector>

#include <cstring>
#include <utility>
#endif

namespace vedit::workspace::settings {

namespace {
QString textOf(const QVector<Entry> &entries, const QString &key)
{
    for (const Entry &entry : entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return {};
}
} // namespace

SettingsClient::SettingsClient(QString path, QObject *parent)
    : QObject(parent)
    , path_(std::move(path))
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    service_ = vd_service_create();
#endif
}

SettingsClient::~SettingsClient()
{
    load_job_.reset();
    save_job_.reset();
    delete timer_;
    timer_ = nullptr;
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    vd_service_destroy(service_);
    service_ = nullptr;
#endif
}

QString SettingsClient::valueOf(const QVector<Entry> &entries, const QString &key)
{
    return textOf(entries, key);
}

void SettingsClient::setValue(QVector<Entry> *entries, const QString &key, const QString &value)
{
    for (int index = 0; index < entries->size(); ++index) {
        if (entries->at(index).key == key) {
            if (value.isEmpty()) {
                entries->removeAt(index);
            } else {
                (*entries)[index].value = value;
            }
            return;
        }
    }
    if (!value.isEmpty()) {
        entries->append(Entry{key, value});
    }
}

bool SettingsClient::ffiAvailable()
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    return true;
#else
    return false;
#endif
}

bool SettingsClient::available() const
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    return service_ != nullptr;
#else
    return false;
#endif
}

bool SettingsClient::busy() const
{
    return load_job_ != nullptr || save_job_ != nullptr;
}

void SettingsClient::ensureTimer()
{
    if (timer_ == nullptr) {
        timer_ = new QTimer();
        timer_->setInterval(10);
        QObject::connect(timer_, &QTimer::timeout, timer_, [this]() { pollJobs(); });
    }
    if (!timer_->isActive()) {
        timer_->start();
    }
}

void SettingsClient::load(std::function<void(Snapshot)> done)
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ != nullptr) {
        const unsigned long long job = vd_settings_load(service_, path_.toUtf8().constData());
        if (job != 0) {
            load_job_ = std::make_unique<PendingLoad>();
            load_job_->job = job;
            load_job_->done = std::move(done);
            ensureTimer();
            return;
        }
    }
#endif
    Snapshot snapshot;
    snapshot.error = available() ? QStringLiteral("the Rust settings store rejected the path")
                                 : QStringLiteral("this build has no vd-ffi settings ABI");
    if (done) {
        done(snapshot);
    }
}

void SettingsClient::save(const QVector<Entry> &entries, std::function<void(bool, QString)> done)
{
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ != nullptr) {
        QVector<VdSettingInput> inputs;
        inputs.reserve(entries.size());
        QVector<QByteArray> keys;
        QVector<QByteArray> values;
        keys.reserve(entries.size());
        values.reserve(entries.size());
        for (const Entry &entry : entries) {
            keys.append(entry.key.toUtf8());
            values.append(entry.value.toUtf8());
        }
        for (int index = 0; index < entries.size(); ++index) {
            VdSettingInput input{};
            input.key = keys.at(index).constData();
            input.value = values.at(index).constData();
            inputs.append(input);
        }
        const unsigned long long job = vd_settings_save(
            service_, path_.toUtf8().constData(), inputs.constData(),
            static_cast<std::size_t>(inputs.size()));
        if (job != 0) {
            save_job_ = std::make_unique<PendingSave>();
            save_job_->job = job;
            save_job_->done = std::move(done);
            ensureTimer();
            return;
        }
    }
#endif
    if (done) {
        done(false, available() ? QStringLiteral("the Rust settings store rejected the write")
                                : QStringLiteral("this build has no vd-ffi settings ABI"));
    }
}

void SettingsClient::pollJobs()
{
    ++poll_rounds_;
#if defined(VEDIT_SETTINGS_HAVE_FFI)
    if (service_ == nullptr) {
        return;
    }
    if (load_job_) {
        VdSettings raw{};
        const int state = vd_settings_poll(service_, load_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            Snapshot snapshot;
            snapshot.completed = true;
            for (std::size_t index = 0; index < raw.count; ++index) {
                snapshot.entries.append(Entry{QString::fromUtf8(raw.items[index].key),
                                              QString::fromUtf8(raw.items[index].value)});
            }
            for (std::size_t index = 0; index < raw.warnings.count; ++index) {
                snapshot.warnings.append(QString::fromUtf8(raw.warnings.items[index]));
            }
            vd_settings_free(&raw);
            auto done = std::move(load_job_->done);
            load_job_.reset();
            if (done) {
                done(std::move(snapshot));
            }
        } else if (state != VD_STATUS_PENDING) {
            Snapshot snapshot;
            snapshot.error = QStringLiteral("settings load failed (state %1)").arg(state);
            VdError error{};
            if (vd_service_error(service_, &error) == 1) {
                snapshot.error = QString::fromUtf8(error.message);
            }
            vd_settings_free(&raw);
            auto done = std::move(load_job_->done);
            load_job_.reset();
            if (done) {
                done(std::move(snapshot));
            }
        }
    }
    if (save_job_) {
        VdSettings raw{};
        const int state = vd_settings_poll(service_, save_job_->job, &raw);
        if (state == VD_STATUS_READY) {
            vd_settings_free(&raw);
            auto done = std::move(save_job_->done);
            save_job_.reset();
            if (done) {
                done(true, QString());
            }
        } else if (state != VD_STATUS_PENDING) {
            QString message = QStringLiteral("settings save failed (state %1)").arg(state);
            VdError error{};
            if (vd_service_error(service_, &error) == 1) {
                message = QString::fromUtf8(error.message);
            }
            vd_settings_free(&raw);
            auto done = std::move(save_job_->done);
            save_job_.reset();
            if (done) {
                done(false, message);
            }
        }
    }
#endif
    if (!busy() && timer_ != nullptr) {
        timer_->stop();
    }
}

bool SettingsClient::flush(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (busy() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return !busy();
}

} // namespace vedit::workspace::settings
