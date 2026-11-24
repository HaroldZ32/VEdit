#include "vcs_history.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QUrl>

namespace vedit::workspace::vcs {
namespace {

QString repositoryAddress(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme();
    const bool local = scheme == QStringLiteral("file") && url.host().isEmpty() && url.path().startsWith(u'/');
    const bool remote = (scheme == QStringLiteral("https") || scheme == QStringLiteral("http") ||
                         scheme == QStringLiteral("svn") || scheme == QStringLiteral("svn+ssh")) && !url.host().isEmpty();
    if (!url.isValid() || (!local && !remote) || url.authority().contains(u'@') || url.hasQuery() || url.hasFragment()) {
        return {};
    }
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
}

} // namespace

void VcsHistoryPanel::setSavedRepositoriesFile(const QString &path)
{
    savedRepositoriesFile_ = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return; }
    const QSignalBlocker block(repository_);
    const auto urls = QJsonDocument::fromJson(file.read(65536)).array();
    for (const auto &entry : urls) {
        const auto url = repositoryAddress(entry.toString());
        if (repository_->count() < 101 && !url.isEmpty() && repository_->findData(url) == -1) {
            repository_->addItem(url, url);
        }
    }
}

void VcsHistoryPanel::addSavedRepository()
{
    bool ok = false;
    const auto text = QInputDialog::getText(this, tr("Add SVN Repository"), tr("Repository URL"), QLineEdit::Normal, {}, &ok);
    if (!ok) { return; }
    const auto url = repositoryAddress(text);
    if (url.isEmpty()) {
        message_->setText(tr("Enter a valid SVN repository URL without embedded credentials."));
        message_->show();
        return;
    }
    int index = repository_->findData(url);
    if (index == -1 && repository_->count() < 101) {
        repository_->addItem(url, url);
        index = repository_->count() - 1;
        storeSavedRepositories();
    }
    if (index > 0) { repository_->setCurrentIndex(index); }
}

void VcsHistoryPanel::removeSavedRepository()
{
    const int index = repository_->currentIndex();
    if (index <= 0) { return; }
    {
        const QSignalBlocker block(repository_);
        repository_->removeItem(index);
    }
    storeSavedRepositories();
    if (repository_->currentIndex() != 0) { repository_->setCurrentIndex(0); }
    else { refresh(); }
}

void VcsHistoryPanel::storeSavedRepositories()
{
    if (savedRepositoriesFile_.isEmpty()) { return; }
    QJsonArray urls;
    for (int index = 1; index < repository_->count(); ++index) {
        urls.append(repository_->itemData(index).toString());
    }
    const auto bytes = QJsonDocument(urls).toJson(QJsonDocument::Indented);
    const auto dir = QFileInfo(savedRepositoriesFile_).dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        message_->setText(tr("Could not create the SVN repository settings directory."));
        message_->show();
        return;
    }
    QSaveFile file(savedRepositoriesFile_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        message_->setText(tr("Could not save SVN repositories: %1").arg(file.errorString()));
        message_->show();
    }
}

} // namespace vedit::workspace::vcs
