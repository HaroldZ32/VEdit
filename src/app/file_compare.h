#pragma once

#include <QByteArray>
#include <QPointer>
#include <QString>

class QWidget;

namespace vedit::app {

class ApplicationShell;

struct CompareInput {
    // An empty local path denotes a repository snapshot, including a missing side.
    QString localPath;
    QString label;
    QByteArray snapshot;
    bool missing = false;
    QString logicalPath = {};
    QPointer<QWidget> editorView = {};
};

void chooseFilesToCompare(ApplicationShell &shell, const QString &first = {});
void showFileComparison(ApplicationShell &shell, CompareInput left, CompareInput right);

} // namespace vedit::app
