#pragma once

#include <QJsonObject>
#include <QString>

class QWidget;
class QDialog;

namespace vedit::workspace::signal_trace {
struct Location {
    QString file;
    int line = 0;
    int column = 0;
};

Location locationOf(const QJsonObject &object);
void jumpTo(QWidget *host, const Location &location);
QDialog *showResults(QWidget *host, const QString &title, const QJsonObject &answer, bool full);
}
