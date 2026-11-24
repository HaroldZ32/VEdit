#include "project_panel.h"

#include <algorithm>

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
#include <QHash>
#include <QHelpEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
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

#include <memory>

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
    // T9: the source is asked once per PAINTED row; converting the whole shell
    // map on every ask would be O(project files) per row. One cache, replaced by
    // a structural publish and patched by a per-file delta.
    struct StatusCache {
        QHash<QString, int> map;
        quint64 revision = 0;
        quint64 deltaRevision = 0;
    };
    auto statusCache = std::make_shared<StatusCache>();
    setFileStatusSource(
        [this, statusCache](const QString &path) -> int {
            QWidget *top = window();
            if (top == nullptr) {
                return 0;
            }
            const quint64 revision = top->property("vedit.diagnostics.viewRevision").toULongLong();
            if (revision != statusCache->revision) {
                statusCache->revision = revision;
                statusCache->map.clear();
                const QVariantMap map = top->property("vedit.diagnostics.fileStatus").toMap();
                for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                    statusCache->map.insert(it.key(), it.value().toInt());
                }
                statusCache->deltaRevision =
                    top->property("vedit.diagnostics.fileStatusDeltaRevision").toULongLong();
                return statusCache->map.value(path, 0);
            }
            const quint64 delta =
                top->property("vedit.diagnostics.fileStatusDeltaRevision").toULongLong();
            if (delta != statusCache->deltaRevision) {
                statusCache->deltaRevision = delta;
                const QVariantMap changed =
                    top->property("vedit.diagnostics.fileStatusDelta").toMap();
                for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
                    statusCache->map.insert(it.key(), it.value().toInt());
                }
            }
            return statusCache->map.value(path, 0);
        },
        [this]() -> quint64 {
            QWidget *top = window();
            return top != nullptr
                       ? top->property("vedit.diagnostics.statusRevision").toULongLong()
                       : 0;
        });
    // A revision bump only repaints the viewport: the delegate asks the source
    // per painted row, so nothing walks the 1176 items.
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        // T9 fix (4): a hidden panel does no status work; the delegate reads the
        // source at paint time, so a re-shown list is correct immediately.
        if (!isVisible()) {
            return;
        }
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

    auto *includeButtons = new QHBoxLayout();
    addIncludeButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Add Include Dir…"), this);
    addIncludeButton_->setObjectName(QStringLiteral("project-add-include"));
    removeIncludeButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Remove"), this);
    removeIncludeButton_->setObjectName(QStringLiteral("project-remove-include"));
    removeIncludeButton_->setEnabled(false);
    includeButtons->addWidget(addIncludeButton_);
    includeButtons->addWidget(removeIncludeButton_);
    includeButtons->addStretch(1);
    layout->addLayout(includeButtons);

    definesHeader_ = new QLabel(QCoreApplication::translate("VEdit", "Defines (-D) (0)"), this);
    definesHeader_->setObjectName(QStringLiteral("project-defines-header"));
    makeHeader(definesHeader_);
    layout->addWidget(definesHeader_);

    defines_ = makeList(this, QStringLiteral("project-defines"));
    defines_->setMaximumHeight(80);
    layout->addWidget(defines_);

    auto *defineButtons = new QHBoxLayout();
    addDefineButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Add Define…"), this);
    addDefineButton_->setObjectName(QStringLiteral("project-add-define"));
    removeDefineButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Remove"), this);
    removeDefineButton_->setObjectName(QStringLiteral("project-remove-define"));
    removeDefineButton_->setEnabled(false);
    defineButtons->addWidget(addDefineButton_);
    defineButtons->addWidget(removeDefineButton_);
    defineButtons->addStretch(1);
    layout->addLayout(defineButtons);

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
    connect(addIncludeButton_, &QPushButton::clicked, this, &ProjectPanel::onAddIncludeRequested);
    connect(addDefineButton_, &QPushButton::clicked, this, &ProjectPanel::onAddDefineRequested);
    connect(removeIncludeButton_, &QPushButton::clicked, this,
            &ProjectPanel::removeSelectedIncludeDir);
    connect(removeDefineButton_, &QPushButton::clicked, this, &ProjectPanel::removeSelectedDefine);
    connect(includeDirs_, &QListWidget::currentRowChanged, this, [this](int row) {
        removeIncludeButton_->setEnabled(row >= 0);
    });
    connect(defines_, &QListWidget::currentRowChanged, this, [this](int row) {
        removeDefineButton_->setEnabled(row >= 0);
    });
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

// G1-a: the ONE question a `.f` opened as a document may ask. The modal's
// default button is "Open Only": "open a file" means "show it", and importing a
// forgotten list into the project is exactly the accident this guards against.
// A test replaces the answer; a non-interactive panel never shows the modal and
// answers OpenOnly, so an unattended run cannot import by accident either.
FilelistOpenChoice ProjectPanel::confirmFilelistImport(const QString &absolutePath)
{
    if (filelistOpenConfirmation_) {
        return filelistOpenConfirmation_(absolutePath);
    }
    if (!interactive_) {
        return FilelistOpenChoice::OpenOnly;
    }
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("filelist-import-confirm"));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QCoreApplication::translate("VEdit", "Import Filelist?"));
    box.setText(QCoreApplication::translate(
        "VEdit", "This is a project file list (.f).\n\nImport it as the project file list?"));
    box.setInformativeText(QCoreApplication::translate(
        "VEdit", "Importing adds its files to the project file set. Opening it as text changes "
                 "nothing; you can import it later with Project \u25b8 Import Filelist."));
    box.setDetailedText(absolutePath);
    QPushButton *importButton =
        box.addButton(QCoreApplication::translate("VEdit", "Import"), QMessageBox::AcceptRole);
    QPushButton *openOnlyButton = box.addButton(
        QCoreApplication::translate("VEdit", "Open Only"), QMessageBox::RejectRole);
    box.setDefaultButton(openOnlyButton);
    box.setEscapeButton(openOnlyButton);
    box.exec();
    return box.clickedButton() == importButton ? FilelistOpenChoice::Import
                                               : FilelistOpenChoice::OpenOnly;
}

void ProjectPanel::setFilelistOpenConfirmation(
    std::function<FilelistOpenChoice(const QString &)> confirmation)
{
    filelistOpenConfirmation_ = std::move(confirmation);
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

bool ProjectPanel::addIncludeDir(const QString &entry, QString *error)
{
    if (model_ == nullptr) {
        const QString reason =
            QCoreApplication::translate("VEdit", "No project model is attached");
        setStatus(reason, true);
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    QString reason;
    if (!model_->addIncludeDir(entry, &reason)) {
        setStatus(reason, true);
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    // The model persisted the typed spelling and re-expanded; the list now shows
    // it among the project's own rows.
    setStatus(QCoreApplication::translate("VEdit", "Added -I %1").arg(entry.trimmed()), false);
    updateUi();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ProjectPanel::addDefine(const QString &entry, QString *error)
{
    if (model_ == nullptr) {
        const QString reason =
            QCoreApplication::translate("VEdit", "No project model is attached");
        setStatus(reason, true);
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    QString reason;
    if (!model_->addDefine(entry, &reason)) {
        setStatus(reason, true);
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    setStatus(QCoreApplication::translate("VEdit", "Added -D %1").arg(entry.trimmed()), false);
    updateUi();
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ProjectPanel::removeIncludeDir(const QString &entry)
{
    if (model_ == nullptr) {
        setStatus(QCoreApplication::translate("VEdit", "No project model is attached"), true);
        return false;
    }
    if (!model_->removeIncludeDir(entry)) {
        // An effective row that came from the filelist/project file/tools page is
        // not this project's entry to delete here; say so instead of nothing.
        setStatus(QCoreApplication::translate(
                      "VEdit", "%1 is not a project entry (it comes from a filelist, vedit.toml or the tools page)")
                      .arg(entry),
                  true);
        return false;
    }
    setStatus(QCoreApplication::translate("VEdit", "Removed -I %1; re-expanding").arg(entry),
              false);
    updateUi();
    return true;
}

bool ProjectPanel::removeDefine(const QString &entry)
{
    if (model_ == nullptr) {
        setStatus(QCoreApplication::translate("VEdit", "No project model is attached"), true);
        return false;
    }
    if (!model_->removeDefine(entry)) {
        setStatus(QCoreApplication::translate(
                      "VEdit", "%1 is not a project entry (it comes from a filelist, vedit.toml or the tools page)")
                      .arg(entry),
                  true);
        return false;
    }
    setStatus(QCoreApplication::translate("VEdit", "Removed -D %1; re-expanding").arg(entry),
              false);
    updateUi();
    return true;
}

bool ProjectPanel::removeSelectedIncludeDir()
{
    if (includeDirs_ == nullptr || includeDirs_->currentRow() < 0) {
        return false;
    }
    QListWidgetItem *item = includeDirs_->currentItem();
    return item != nullptr && removeIncludeDir(item->data(Qt::UserRole).toString());
}

bool ProjectPanel::removeSelectedDefine()
{
    if (defines_ == nullptr || defines_->currentRow() < 0) {
        return false;
    }
    QListWidgetItem *item = defines_->currentItem();
    return item != nullptr && removeDefine(item->data(Qt::UserRole).toString());
}

void ProjectPanel::onAddIncludeRequested()
{
    if (!interactive_) {
        return;
    }
    bool accepted = false;
    const QString entry = QInputDialog::getText(
        this, QCoreApplication::translate("VEdit", "Add Include Dir"),
        QCoreApplication::translate("VEdit", "Directory (relative paths resolve against the project root):"),
        QLineEdit::Normal, QString(), &accepted);
    if (accepted) {
        addIncludeDir(entry);
    }
}

void ProjectPanel::onAddDefineRequested()
{
    if (!interactive_) {
        return;
    }
    bool accepted = false;
    const QString entry = QInputDialog::getText(
        this, QCoreApplication::translate("VEdit", "Add Define"),
        QCoreApplication::translate("VEdit", "Define (NAME or NAME=VALUE):"), QLineEdit::Normal,
        QString(), &accepted);
    if (accepted) {
        addDefine(entry);
    }
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
    ProjectVariablesDialog dialog(variableRowNames(), known, this, variableLayers());
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
    ProjectVariablesDialog dialog(variableRowNames(), known, this, variableLayers());
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

QMap<QString, QString> ProjectPanel::variableLayers() const
{
    QMap<QString, QString> layers;
    if (model_ == nullptr) {
        return layers;
    }
    // A table entry BELONGS to the project layer (highest priority), so it answers
    // `project` even when the same name is also exported in the process env: the
    // table is what wins, and that is what the dialog must say.
    for (const QString &name : ProjectVariablesDialog::parseTable(model_->variables()).keys()) {
        layers.insert(name, QStringLiteral("project"));
    }
    // The expansion's own answer for the names it referenced. A projected name is
    // already `project` above; every other layer is added verbatim.
    const QStringList referenced = model_->referencedVariables();
    const QStringList referencedLayers = model_->referencedVariableLayers();
    const int count = std::min(referenced.size(), referencedLayers.size());
    for (int index = 0; index < count; ++index) {
        if (!layers.contains(referenced.at(index))) {
            layers.insert(referenced.at(index), referencedLayers.at(index));
        }
    }
    // Required (still unresolved) names: nothing provides them.
    for (const QString &missing : model_->missingVariables()) {
        layers.insert(missing, QStringLiteral("missing"));
    }
    return layers;
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

    const QStringList paths = model_->files();
    if (paths != displayedFilePaths_) {
        displayedFilePaths_ = paths;
        const QStringList files = model_->displayFiles();
        filesHeader_->setText(QCoreApplication::translate("VEdit", "Files (%1)").arg(files.size()));
        fileList_->clear();
        for (int row = 0; row < files.size(); ++row) {
            auto *item = new QListWidgetItem(files.at(row), fileList_);
            item->setData(Qt::UserRole, paths.at(row));
        }
    }

    // AC-1.5: the list shows the project's OWN entries as typed (editable rows)
    // followed by the effective entries the expansion contributed. The raw value
    // travels in UserRole, so Remove acts on what the row really is.
    const QStringList includeDirs = model_->displayIncludeDirs();
    includeHeader_->setText(QCoreApplication::translate("VEdit", "Include dirs (-I) (%1)").arg(includeDirs.size()));
    includeDirs_->clear();
    for (const QString &row : includeDirs) {
        auto *item = new QListWidgetItem(row, includeDirs_);
        item->setData(Qt::UserRole, row);
        item->setToolTip(row);
    }
    removeIncludeButton_->setEnabled(includeDirs_->currentRow() >= 0);

    const QStringList defines = model_->displayDefines();
    definesHeader_->setText(QCoreApplication::translate("VEdit", "Defines (-D) (%1)").arg(defines.size()));
    defines_->clear();
    for (const QString &row : defines) {
        auto *item = new QListWidgetItem(row, defines_);
        item->setData(Qt::UserRole, row);
        item->setToolTip(row);
    }
    removeDefineButton_->setEnabled(defines_->currentRow() >= 0);
    if (addIncludeButton_ != nullptr) {
        addIncludeButton_->setEnabled(model_->hasProject());
    }
    if (addDefineButton_ != nullptr) {
        addDefineButton_->setEnabled(model_->hasProject());
    }

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
