#include "project_panel.h"

#include "project_model.h"
#include "project_variables_dialog.h"

#if defined(VEDIT_PROJECT_HAVE_JUMP)
#include "jump/jump_registry.h"
#endif

#include <QAbstractItemView>
#include <QFileDialog>
#include <QMap>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStringLiteral>
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
    ProjectVariablesDialog dialog(names, known, this);
    if (dialog.exec() != QDialog::Accepted) {
        setStatus(QCoreApplication::translate("VEdit", "Import completed with %1 unresolved variable(s); the affected "
                                 ".f lines stay listed below")
                      .arg(names.size()),
                  true);
        return false;
    }
    QStringList updated = model_->variables();
    updated.append(dialog.variableTable());
    model_->setVariables(updated); // persists the table and re-expands
    setStatus(QCoreApplication::translate("VEdit", "Saved %1 variable(s); re-expanding").arg(dialog.variableTable().size()),
              false);
    updateUi();
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
    QStringList names = known.keys();
    for (const QString &missing : model_->missingVariables()) {
        if (!names.contains(missing)) {
            names.append(missing);
        }
    }
    if (names.isEmpty()) {
        setStatus(QCoreApplication::translate("VEdit", "No variables are used by this project yet"), false);
        return false;
    }
    ProjectVariablesDialog dialog(names, known, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    QStringList merged = model_->variables();
    merged.append(dialog.variableTable());
    model_->setVariables(merged);
    setStatus(QCoreApplication::translate("VEdit", "Project variables updated; re-expanding"), false);
    updateUi();
    return true;
}

void ProjectPanel::onEditVariablesRequested()
{
    editVariables();
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
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QCoreApplication::translate("VEdit", "Import Filelist"), model_->projectRoot(),
        QCoreApplication::translate("VEdit", "Verilog filelists (*.f *.F *.flist);;All files (*)"));
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
