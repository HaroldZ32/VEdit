#include "help_viewer.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextEdit>
#include <QVBoxLayout>

// Q_INIT_RESOURCE is what makes the resource reachable from a STATIC library:
// the generated rcc object lives in the archive, and this reference is the
// symbol the linker follows (the shell library is linked into vedit-app, never
// loaded as a plugin). The macro declares `qInitResources_<name>` in whatever
// namespace it is written in and Qt forbids using it inside a namespace, so the
// initialiser is a global-scope function.
static void vedit_init_user_manual_resource()
{
    Q_INIT_RESOURCE(user_manual);
}

namespace vedit::app {

namespace {

// The Qt resource name of resources/help/user_manual.qrc.
constexpr auto kManualResource = ":/help/USER_GUIDE.md";

} // namespace

HelpViewer::HelpViewer(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("vedit-help-viewer"));
    setWindowTitle(tr("VEdit User Manual"));
    resize(980, 760);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    text_ = new QTextEdit(this);
    text_->setObjectName(QStringLiteral("help-viewer-text"));
    text_->setReadOnly(true);
    text_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text_->setMarkdown(manualMarkdown());
    layout->addWidget(text_);
}

QTextEdit *HelpViewer::textView() const
{
    return text_;
}

QString HelpViewer::manualMarkdown()
{
    vedit_init_user_manual_resource();
    QFile file(QString::fromLatin1(kManualResource));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString HelpViewer::manualTitle()
{
    const QString markdown = manualMarkdown();
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("# "))) {
            return line.mid(2).trimmed();
        }
    }
    return tr("User Manual");
}

HelpViewer *HelpViewer::open(QWidget *parent)
{
    auto *viewer = new HelpViewer(parent);
    viewer->setAttribute(Qt::WA_DeleteOnClose, true);
    viewer->show();
    return viewer;
}

} // namespace vedit::app
