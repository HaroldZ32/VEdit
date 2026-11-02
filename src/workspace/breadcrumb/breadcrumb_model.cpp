#include "breadcrumb_model.h"

#include <QStringList>

#include <utility>

namespace vedit::breadcrumb {

void Model::setPath(std::vector<Entry> path)
{
    ++updateCount_;
    if (path != path_) {
        path_ = std::move(path);
        ++changeCount_;
    }
}

const std::vector<Entry> &Model::path() const
{
    return path_;
}

QString Model::display(QStringView separator) const
{
    QStringList parts;
    for (const Entry &entry : path_) {
        const QString name = QString::fromStdString(entry.name);
        parts.append(name.isEmpty() ? QString::fromStdString(entry.kind) : name);
    }
    return parts.join(separator);
}

QString Model::describe() const
{
    QStringList parts;
    for (const Entry &entry : path_) {
        parts.append(QStringLiteral("%1:%2")
                         .arg(QString::fromStdString(entry.kind),
                              QString::fromStdString(entry.name)));
    }
    return parts.join(QStringLiteral(" > "));
}

quint64 Model::updateCount() const
{
    return updateCount_;
}

quint64 Model::changeCount() const
{
    return changeCount_;
}

bool Model::matches(const std::vector<Entry> &other) const
{
    return path_ == other;
}

} // namespace vedit::breadcrumb
