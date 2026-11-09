#include "project_panel.h"

#include "file_dialog_filters.h" // src/common: the ONE file-dialog filter table (RM-16)
#include "file_status.h" // src/common: the ONE status vocabulary (RM-9)
#include "project_model.h"
#include "project_variables_dialog.h"

#if defined(VEDIT_PROJECT_HAVE_JUMP)
#include "jump/jump_registry.h"
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QFileDialog>
#include <QHelpEvent>
#include <QMap>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QStringLiteral>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariant>
#include <QCoreApplication>

namespace vedit::workspace::project {

namespace {

void makeHeader(QLabel *label)
{
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

QListWidget *makeList(QWidget *parent, const QString &objectName)
{
    auto *list = new QListWidget(parent);
    list->setObjectName(objectName);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setUniformItemSizes(true);
    return list;
}

QStringList listTexts(const QListWidget *list)
{
    QStringList out;
    if (list == nullptr) {
        return out;
    }
    for (int row = 0; row < list->count(); ++row) {
        out.append(list->item(row)->text());
    }
    return out;
}

} // namespace

/// RM-9: the status mark before a file name in the Project panel's FILES list
/// (the only place a filelist-only file can be listed). The mark and its colour
/// come from src/common/file_status.h - the same vocabulary the file tree uses.
/// The status is asked AT PAINT TIME for the visible rows only; a revision bump
/// therefore costs one viewport update, never a 1176-item walk.
class FileStatusListDelegate final : public QStyledItemDelegate {
public:
    explicit FileStatusListDelegate(const ProjectPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , panel_(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const int status = panel_ != nullptr ? panel_->fileStatusAt(index.row()) : 0;
        if (status <= 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
        const QString text = opt.text;

        // Background / selection without the text, then the mark, then the text.
        QStyleOptionViewItem background = opt;
        background.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, opt.widget);

        const QRect textRect =
            style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        constexpr int dotDiameter = 9;
        constexpr int dotGap = 4;
        const int centreY = opt.rect.center().y();
        const QRect dotRect(textRect.left() + 1, centreY - dotDiameter / 2, dotDiameter,
                            dotDiameter);
        const QColor colour = vedit::common::fileStatusColor(status);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (vedit::common::fileStatusIsNotAnalyzed(status)) {
            // Hollow grey ring: in the analysed set, no trustworthy verdict yet
            // (queued / pass running) or excluded (dirty buffer). The tooltip
            // carries the meaning - the user must never mistake it for clean.
            QPen pen(colour);
            pen.setWidthF(2.0); // opaque enough for the ring to be sampled by the evidence
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(dotRect);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colour);
            painter->drawEllipse(dotRect);
        }
        painter->restore();

        QRect shifted = textRect;
        shifted.setLeft(dotRect.right() + dotGap + 1);
        if (shifted.width() > 0) {
            const QString elided =
                opt.fontMetrics.elidedText(text, opt.textElideMode, shifted.width());
            const QPalette::ColorRole role = (opt.state & QStyle::State_Selected)
                                                 ? QPalette::HighlightedText
                                                 : QPalette::Text;
            style->drawItemText(painter, shifted, opt.displayAlignment | Qt::AlignVCenter,
                                opt.palette, (opt.state & QStyle::State_Enabled) != 0, elided,
                                role);
        }
    }

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event != nullptr && event->type() == QEvent::ToolTip && view != nullptr) {
            const int status = panel_ != nullptr ? panel_->fileStatusAt(index.row()) : 0;
            QString tip = index.data(Qt::DisplayRole).toString();
            if (status > 0) {
                tip += QLatin1String("\n") + vedit::common::fileStatusHint(status);
            }
            const QString path = index.data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                tip += QLatin1String("\n") + path;
            }
            QToolTip::showText(event->globalPos(), tip, view);
            return true;
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    const ProjectPanel *panel_ = nullptr;
};

ProjectPanel::ProjectPanel(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , model_(model)
{
    setObjectName(QStringLiteral("project-panel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("project-summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    auto *buttons = new QHBoxLayout();
    refreshButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Refresh"), this);
    refreshButton_->setObjectName(QStringLiteral("project-refresh"));
    importButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Import Filelist…"), this);
    importButton_->setObjectName(QStringLiteral("project-import-filelist"));
    importButton_->setToolTip(
        QCoreApplication::translate("VEdit", "Add one or more .f filelists to the project file set"));
    buttons->addWidget(refreshButton_);
    buttons->addWidget(importButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    emptyHint_ = new QLabel(
        QCoreApplication::translate("VEdit", "Open a folder from the File Tree (or pass --project <dir>); the expanded "
                       "file list, include dirs and defines appear here."),
        this);
    emptyHint_->setObjectName(QStringLiteral("project-empty-hint"));
    emptyHint_->setWordWrap(true);
    layout->addWidget(emptyHint_);

    filelistsHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Filelists (0)"), this);
    filelistsHeader_->setObjectName(QStringLiteral("project-filelists-header"));
    makeHeader(filelistsHeader_);
    layout->addWidget(filelistsHeader_);

    filelists_ = makeList(this, QStringLiteral("project-filelist-list"));
    filelists_->setMaximumHeight(90);
    filelists_->setToolTip(QCoreApplication::translate("VEdit", "Imported .f filelists; double-click to open one"));
    layout->addWidget(filelists_);

    auto *filelistButtons = new QHBoxLayout();
    removeButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Remove Selected Filelist"), this);
    removeButton_->setObjectName(QStringLiteral("project-remove-filelist"));
    filelistButtons->addWidget(removeButton_);
    filelistButtons->addStretch(1);
    layout->addLayout(filelistButtons);

    filesHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Files (0)"), this);
    filesHeader_->setObjectName(QStringLiteral("project-files-header"));
    makeHeader(filesHeader_);
    layout->addWidget(filesHeader_);

    fileList_ = makeList(this, QStringLiteral("project-file-list"));
    fileList_->setToolTip(QCoreApplication::translate("VEdit", "Double-click a file to open it (AC-7.4)"));
    // RM-9: the Files list is the only surface that can list a filelist-only
    // file, so it must show the same status mark as the tree.
    statusDelegate_ = new FileStatusListDelegate(this, this);
    fileList_->setItemDelegate(statusDelegate_);
    // Default production source: the diagnostics lane publishes the map and its
    // revision on the top-level window (the shell), exactly like the file tree
    // reads it. No link dependency, and project_integration.cpp is untouched.
    setFileStatusSource(
        [this](const QString &path) -> int {
            QWidget *top = window();
            if (top == nullptr) {
                return 0;
            }
            const QVariantMap map = top->property("vedit.diagnostics.fileStatus").toMap();
            return map.value(path, 0).toInt();
        },
        [this]() -> quint64 {
            QWidget *top = window();
            return top != nullptr
                       ? top->property("vedit.diagnostics.viewRevision").toULongLong()
                       : 0;
        });
    // A revision bump only repaints the viewport: the delegate asks the source
    // per painted row, so nothing walks the 1176 items.
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        const quint64 revision = statusRevisionSource_ ? statusRevisionSource_() : 0;
        if (revision == statusRevision_) {
            return;
        }
        statusRevision_ = revision;
        if (fileList_ != nullptr) {
            fileList_->viewport()->update();
        }
    });
    statusTimer_->start();
    layout->addWidget(fileList_, 3);

    includeHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Include dirs (-I) (0)"), this);
    includeHeader_->setObjectName(QStringLiteral("project-include-header"));
    makeHeader(includeHeader_);
    layout->addWidget(includeHeader_);

    includeDirs_ = makeList(this, QStringLiteral("project-include-dirs"));
    includeDirs_->setMaximumHeight(80);
    layout->addWidget(includeDirs_);

    definesHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Defines (-D) (0)"), this);
    definesHeader_->setObjectName(QStringLiteral("project-defines-header"));
    makeHeader(definesHeader_);
    layout->addWidget(definesHeader_);

    defines_ = makeList(this, QStringLiteral("project-defines"));
    defines_->setMaximumHeight(80);
    layout->addWidget(defines_);

    variablesHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Variables (0)"), this);
    variablesHeader_->setObjectName(QStringLiteral("project-variables-header"));
    makeHeader(variablesHeader_);
    layout->addWidget(variablesHeader_);

    variables_ = makeList(this, QStringLiteral("project-variables"));
    variables_->setMaximumHeight(80);
    variables_->setToolTip(QCoreApplication::translate("VEdit", "Project variable table; wins over the environment"));
    layout->addWidget(variables_);

    auto *variableButtons = new QHBoxLayout();
    editVariablesButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Edit Variables…"), this);
    editVariablesButton_->setObjectName(QStringLiteral("project-edit-variables"));
    variableButtons->addWidget(editVariablesButton_);
    variableButtons->addStretch(1);
    layout->addLayout(variableButtons);

    diagnosticsHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Diagnostics (0)"), this);
    diagnosticsHeader_->setObjectName(QStringLiteral("project-diagnostics-header"));
    makeHeader(diagnosticsHeader_);
    layout->addWidget(diagnosticsHeader_);

    diagnostics_ = makeList(this, QStringLiteral("project-diagnostics"));
    diagnostics_->setWordWrap(true);
    layout->addWidget(diagnostics_, 2);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("project-status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    connect(refreshButton_, &QPushButton::clicked, this, &ProjectPanel::refresh);
    connect(importButton_, &QPushButton::clicked, this, &ProjectPanel::onImportRequested);
    connect(removeButton_, &QPushButton::clicked, this, &ProjectPanel::onRemoveRequested);
    connect(editVariablesButton_, &QPushButton::clicked, this,
            &ProjectPanel::onEditVariablesRequested);
    connect(fileList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        onFileActivated(fileList_->row(item));
    });
    connect(filelists_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        onFilelistActivated(filelists_->row(item));
    });
    connect(filelists_, &QListWidget::currentRowChanged, this, [this](int row) {
        removeButton_->setEnabled(row >= 0);
    });
    if (model_ != nullptr) {
        connect(model_, &ProjectModel::stateChanged, this, &ProjectPanel::updateUi);
        connect(model_, &ProjectModel::filelistsChanged, this, &ProjectPanel::updateUi);
        connect(model_, &ProjectModel::refreshFinished, this,
                [this](bool, const QString &) { updateUi(); });
        connect(model_, &ProjectModel::missingVariablesDetected, this,
                [this](const QStringList &) {
                    if (interactive_) {
                        promptForMissingVariables();
                    }
                });
    }
    updateUi();
}

ProjectModel *ProjectPanel::model() const
{
    return model_;
}

void ProjectPanel::refresh()
{
    if (model_ != nullptr) {
        model_->refresh();
    }
}

bool ProjectPanel::importFilelists(const QStringList &paths)
{
    if (model_ == nullptr) {
        setStatus(QCoreApplication::translate("VEdit", "No project model is attached"), true);
        return false;
    }
    QString error;
    int added = 0;
    if (!model_->importFilelists(paths, &error, &added)) {
        setStatus(error, true);
        return false;
    }
    if (added == 0) {
        // FIX 4 (F4): re-opening an already-imported `.f` is a no-op in the
        // model; do not claim an import happened. Refresh is the re-parse path.
        setStatus(QCoreApplication::translate(
                      "VEdit", "Already imported: nothing changed (use Refresh to re-read an edited filelist)"),
                  false);
    } else {
        setStatus(QCoreApplication::translate("VEdit", "Imported %1 filelist%2")
                      .arg(added)
                      .arg(added == 1 ? QString() : QStringLiteral("s")),
                  false);
    }
    updateUi();
    return true;
}

bool ProjectPanel::removeFilelist(const QString &path)
{
    if (model_ == nullptr) {
        setStatus(QCoreApplication::translate("VEdit", "No project model is attached"), true);
        return false;
    }
    if (!model_->removeFilelist(path)) {
        setStatus(QCoreApplication::translate("VEdit", "%1 is not an imported filelist").arg(path), true);
        return false;
    }
    setStatus(QCoreApplication::translate("VEdit", "Removed %1; the file set is being re-expanded").arg(path), false);
    updateUi();
    return true;
}

void ProjectPanel::activateFile(int row)
{
    onFileActivated(row);
}

QStringList ProjectPanel::displayedVariables() const
{
    return listTexts(variables_);
}

void ProjectPanel::setInteractiveDialogs(bool enabled)
{
    interactive_ = enabled;
}

bool ProjectPanel::interactiveDialogs() const
{
    return interactive_;
}

bool ProjectPanel::promptForMissingVariables()
{
    if (model_ == nullptr) {
        return false;
    }
    const QStringList names = model_->missingVariables();
    if (names.isEmpty()) {
        return false;
    }
    if (!interactive_) {
        setStatus(QCoreApplication::translate("VEdit", "%1 unresolved filelist variable(s): %2")
                      .arg(names.size())
                      .arg(names.join(QStringLiteral(", "))),
                  true);
        return false;
    }
    const QMap<QString, QString> known = ProjectVariablesDialog::parseTable(model_->variables());
    ProjectVariablesDialog dialog(variableRowNames(), known, this);
    if (dialog.exec() != QDialog::Accepted) {
        setStatus(QCoreApplication::translate("VEdit", "Import completed with %1 unresolved variable(s); the affected "
                                 ".f lines stay listed below")
                      .arg(names.size()),
                  true);
        return false;
    }
    applyVariableTable(dialog.variableTable()); // persists the table and re-expands
    return true;
}

bool ProjectPanel::editVariables()
{
    if (model_ == nullptr || !model_->hasProject()) {
        setStatus(QCoreApplication::translate("VEdit", "Open a folder before editing variables"), true);
        return false;
    }
    if (!interactive_) {
        return false;
    }
    const QMap<QString, QString> known = ProjectVariablesDialog::parseTable(model_->variables());
    // RM-3: an EMPTY table still opens the dialog — adding a variable is the
    // whole point, so "the project uses none yet" is not a reason to refuse.
    ProjectVariablesDialog dialog(variableRowNames(), known, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    applyVariableTable(dialog.variableTable());
    return true;
}

void ProjectPanel::onEditVariablesRequested()
{
    editVariables();
}

QStringList ProjectPanel::variableRowNames() const
{
    QStringList names;
    if (model_ == nullptr) {
        return names;
    }
    // Known names first (the table order), then the names the project still
    // needs. The dialog shows the whole table either way, but this order is what
    // the auto-prompt after an import should focus on last.
    const QMap<QString, QString> known = ProjectVariablesDialog::parseTable(model_->variables());
    names = known.keys();
    for (const QString &missing : model_->missingVariables()) {
        if (!names.contains(missing)) {
            names.append(missing);
        }
    }
    return names;
}

bool ProjectPanel::applyVariableTable(const QStringList &values)
{
    if (model_ == nullptr || !model_->hasProject()) {
        return false;
    }
    // RM-3: REPLACE, never append. Appending made a deleted variable immortal
    // (its old entry was still in the model after the dialog closed).
    const QStringList before = model_->variables();
    const QStringList table = ProjectVariablesDialog::toTable(
        ProjectVariablesDialog::parseTable(values));
    if (table == before) {
        setStatus(QCoreApplication::translate("VEdit", "Project variables unchanged"), false);
        updateUi();
        return true;
    }
    model_->setVariables(table); // persists per root and re-expands
    setStatus(QCoreApplication::translate("VEdit", "Saved %1 variable(s); re-expanding")
                  .arg(table.size()),
              false);
    updateUi();
    return true;
}

void ProjectPanel::onFileActivated(int row)
{
    const QStringList absolute = model_ != nullptr ? model_->files() : QStringList();
    if (row < 0 || row >= absolute.size()) {
        return;
    }
    jumpToPath(absolute.at(row));
}

void ProjectPanel::onFilelistActivated(int row)
{
    const QStringList lists = model_ != nullptr ? model_->filelists() : QStringList();
    if (row < 0 || row >= lists.size()) {
        return;
    }
    jumpToPath(lists.at(row));
}

void ProjectPanel::jumpToPath(const QString &path)
{
#if defined(VEDIT_PROJECT_HAVE_JUMP)
    const jump::JumpResult result = jump::jumpTo(path, 1, 1);
    if (result.status != jump::JumpStatus::Delivered) {
        setStatus(QCoreApplication::translate("VEdit", "Cannot open %1: no editor jump target is registered").arg(path),
                  true);
    }
#else
    // A build without the jump lane (the lane's standalone test build) has no
    // editor to open the file in; say so instead of pretending.
    setStatus(QCoreApplication::translate("VEdit", "Cannot open %1: this build has no jump seam").arg(path), true);
#endif
}

QString ProjectPanel::summaryText() const
{
    return summary_ != nullptr ? summary_->text() : QString();
}

void ProjectPanel::setFileStatusSource(std::function<int(const QString &)> status,
                                       std::function<quint64()> revision)
{
    statusSource_ = std::move(status);
    statusRevisionSource_ = std::move(revision);
    statusRevision_ = statusRevisionSource_ ? statusRevisionSource_() : 0;
    if (fileList_ != nullptr) {
        fileList_->viewport()->update();
    }
}

int ProjectPanel::fileStatusAt(int row) const
{
    if (fileList_ == nullptr || row < 0 || row >= fileList_->count() || !statusSource_) {
        return 0;
    }
    const QListWidgetItem *item = fileList_->item(row);
    if (item == nullptr) {
        return 0;
    }
    // The item text is the display path; UserRole keeps the absolute one, which
    // is the key of the diagnostics status map.
    return statusSource_(item->data(Qt::UserRole).toString());
}

int ProjectPanel::fileCount() const
{
    return fileList_ != nullptr ? fileList_->count() : 0;
}

QStringList ProjectPanel::displayedFiles() const
{
    return listTexts(fileList_);
}

QStringList ProjectPanel::displayedFilelists() const
{
    return listTexts(filelists_);
}

QStringList ProjectPanel::displayedIncludeDirs() const
{
    return listTexts(includeDirs_);
}

QStringList ProjectPanel::displayedDefines() const
{
    return listTexts(defines_);
}

QStringList ProjectPanel::displayedDiagnostics() const
{
    return listTexts(diagnostics_);
}

QString ProjectPanel::statusText() const
{
    return status_ != nullptr ? status_->text() : QString();
}

void ProjectPanel::setStatus(const QString &text, bool error)
{
    if (status_ == nullptr) {
        return;
    }
    status_->setText(error ? QStringLiteral("⚠ %1").arg(text) : text);
    status_->setStyleSheet(error ? QStringLiteral("color: #c0392b;") : QString());
}

void ProjectPanel::onImportRequested()
{
    if (model_ == nullptr) {
        return;
    }
    if (!model_->hasProject()) {
        setStatus(QCoreApplication::translate("VEdit", "Open a folder before importing a filelist"), true);
        return;
    }
    // RM-16: a dialog opened specifically to import a filelist must never hide
    // filelists. The filter table and the DEFAULT (the selected filter) come
    // from src/common/file_dialog_filters.h, the same source the open-file
    // dialog uses; "All files" stays available for an unusual spelling.
    QFileDialog dialog(this, QCoreApplication::translate("VEdit", "Import Filelist"),
                       model_->projectRoot());
    dialog.setObjectName(QStringLiteral("project-import-filelist-dialog"));
    vedit::common::useOwnFileDialog(dialog);
    vedit::common::configureImportFilelistDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QStringList paths = dialog.selectedFiles();
    if (paths.isEmpty()) {
        return;
    }
    importFilelists(paths);
}

void ProjectPanel::onRemoveRequested()
{
    if (filelists_ == nullptr || filelists_->currentRow() < 0) {
        return;
    }
    // FIX 1: the row TEXT is the display path (relative when under the root);
    // the absolute path the model stores is the item's Qt::UserRole data.
    const QString absolute = filelists_->currentItem()->data(Qt::UserRole).toString();
    removeFilelist(absolute.isEmpty() ? filelists_->currentItem()->text() : absolute);
}

void ProjectPanel::updateUi()
{
    if (model_ == nullptr) {
        return;
    }
    summary_->setText(model_->summary());

    const QStringList lists = model_->filelists();
    // FIX 1: one display rule everywhere. The list shows display paths; the
    // model keeps the absolute spelling for the importer and the state store, so
    // it travels in the item's data.
    const QStringList listDisplays = model_->displayFilelists();
    filelistsHeader_->setText(QCoreApplication::translate("VEdit", "Filelists (%1)").arg(lists.size()));
    filelists_->clear();
    for (int row = 0; row < lists.size(); ++row) {
        auto *item = new QListWidgetItem(listDisplays.value(row), filelists_);
        item->setData(Qt::UserRole, lists.value(row));
    }
    filelists_->setVisible(!lists.isEmpty());
    filelistsHeader_->setVisible(!lists.isEmpty());
    removeButton_->setVisible(!lists.isEmpty());
    removeButton_->setEnabled(filelists_->currentRow() >= 0);

    const QStringList files = model_->displayFiles();
    filesHeader_->setText(QCoreApplication::translate("VEdit", "Files (%1)").arg(files.size()));
    fileList_->clear();
    for (int row = 0; row < files.size(); ++row) {
        auto *item = new QListWidgetItem(files.at(row), fileList_);
        // The absolute path the jump target needs (the row shows it relative).
        item->setData(Qt::UserRole, model_->files().value(row));
    }

    const QStringList includeDirs = model_->includeDirs();
    includeHeader_->setText(QCoreApplication::translate("VEdit", "Include dirs (-I) (%1)").arg(includeDirs.size()));
    includeDirs_->clear();
    includeDirs_->addItems(includeDirs);

    const QStringList defines = model_->defines();
    definesHeader_->setText(QCoreApplication::translate("VEdit", "Defines (-D) (%1)").arg(defines.size()));
    defines_->clear();
    defines_->addItems(defines);

    const QStringList variables = model_->variables();
    variablesHeader_->setText(QCoreApplication::translate("VEdit", "Variables (%1)").arg(variables.size()));
    variables_->clear();
    variables_->addItems(variables);
    variables_->setVisible(!variables.isEmpty());
    variablesHeader_->setVisible(!variables.isEmpty());
    editVariablesButton_->setEnabled(model_->hasProject());

    const QStringList diagnostics = model_->diagnostics();
    diagnosticsHeader_->setText(QCoreApplication::translate("VEdit", "Diagnostics (%1)").arg(diagnostics.size()));
    diagnostics_->clear();
    diagnostics_->addItems(diagnostics);

    if (emptyHint_ != nullptr) {
        emptyHint_->setVisible(!model_->hasProject());
    }
    refreshButton_->setEnabled(model_->hasProject());
    importButton_->setEnabled(model_->hasProject());
}

} // namespace vedit::workspace::project
