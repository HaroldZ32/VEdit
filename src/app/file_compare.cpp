#include "file_compare.h"

#include "app_shell.h"
#include "editor_area.h"

#include <KTextEditor/View>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include <filesystem>
#include <utility>

namespace vedit::app {

void openFileComparisonPage(EditorArea &area, const QString &id, const QString &title,
                            CompareInput left, CompareInput right);

namespace {

QString sideName(const CompareInput &input)
{
    return input.localPath.isEmpty() ? input.label : QFileInfo(input.localPath).fileName();
}

QString comparisonId(const CompareInput &left, const CompareInput &right)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto *side : {&left, &right}) {
        const bool local = !side->localPath.isEmpty();
        hash.addData(local ? QByteArrayView("file") : side->missing ? QByteArrayView("absent") : QByteArrayView("snapshot"));
        const auto fields = local ? QList<QByteArray>{side->localPath.toUtf8()}
                                  : QList<QByteArray>{side->logicalPath.toUtf8(), side->label.toUtf8(), side->snapshot};
        for (const auto &field : fields) {
            hash.addData(QByteArray::number(field.size()));
            hash.addData(QByteArrayView(":"));
            hash.addData(field);
        }
        if (const auto *view = qobject_cast<KTextEditor::View *>(side->editorView.data())) {
            hash.addData(QByteArray::number(reinterpret_cast<quintptr>(view->document())));
        }
    }
    return QStringLiteral("file-comparison-") + QString::fromLatin1(hash.result().toHex());
}

} // namespace

void showFileComparison(ApplicationShell &shell, CompareInput left, CompareInput right)
{
    for (auto *input : {&left, &right}) {
        if (!input->localPath.isEmpty()) {
            const QFileInfo info(input->localPath);
            if (input->label.isEmpty()) {
                input->label = info.absoluteFilePath();
            }
            const QString canonical = info.canonicalFilePath();
            input->localPath = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
        } else if (input->label.isEmpty()) {
            input->label = QCoreApplication::translate("FileComparison", "Snapshot");
        }
    }
    std::error_code identityError;
    if (!left.localPath.isEmpty() && !right.localPath.isEmpty()
        && (left.localPath == right.localPath
            || std::filesystem::equivalent(QFile::encodeName(left.localPath).constData(),
                                           QFile::encodeName(right.localPath).constData(), identityError))) {
        QMessageBox::information(&shell, QCoreApplication::translate("FileComparison", "Compare Files"), QCoreApplication::translate("FileComparison", "Choose two different files to compare."));
        return;
    }
    auto &area = *shell.editorArea();
    const QString id = comparisonId(left, right);
    if (area.activateAuxiliaryTab(id)) {
        return;
    }
    const QString title = QCoreApplication::translate("FileComparison", "%1 ↔ %2").arg(sideName(left), sideName(right));
    openFileComparisonPage(area, id, title, std::move(left), std::move(right));
}

void chooseFilesToCompare(ApplicationShell &shell, const QString &first)
{
    const QString left = first.isEmpty() ? QFileDialog::getOpenFileName(&shell, QCoreApplication::translate("FileComparison", "Choose left file"), shell.projectRoot())
                                        : first;
    if (left.isEmpty()) {
        return;
    }
    const QString right = QFileDialog::getOpenFileName(&shell, QCoreApplication::translate("FileComparison", "Choose right file"), QFileInfo(left).absolutePath());
    if (!right.isEmpty()) {
        showFileComparison(shell, {left, {}, {}, false}, {right, {}, {}, false});
    }
}

} // namespace vedit::app
