#include "undo_ops.h"

#include <KTextEditor/Document>

#include <QMetaObject>
#include <QMetaMethod>

namespace vedit::editor {
namespace {

constexpr auto kUndoMethod = "undo()";
constexpr auto kRedoMethod = "redo()";

bool slotAvailable(KTextEditor::Document *document, const char *method)
{
    if (document == nullptr) {
        return false;
    }
    return document->metaObject()->indexOfMethod(method) >= 0;
}

bool invokeSlot(KTextEditor::Document *document, const char *method, const char *label,
                QString *error)
{
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("%1: no document").arg(QLatin1String(label));
        }
        return false;
    }
    const int index = document->metaObject()->indexOfMethod(method);
    if (index < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("%1: the runtime document exposes no %2 slot "
                                    "(KTextEditor changed its API)")
                         .arg(QLatin1String(label), QLatin1String(method));
        }
        return false;
    }
    // The meta-method name for invokeMethod is the bare name ("undo");
    // indexOfMethod() above needs the signature form ("undo()").
    const QByteArray bareName = QByteArray(method).chopped(2);
    if (!QMetaObject::invokeMethod(document, bareName.constData(), Qt::DirectConnection)) {
        if (error != nullptr) {
            *error = QStringLiteral("%1: invoking %2 failed").arg(QLatin1String(label),
                                                                 QLatin1String(method));
        }
        return false;
    }
    return true;
}

} // namespace

bool undoSlotAvailable(KTextEditor::Document *document)
{
    return slotAvailable(document, kUndoMethod);
}

bool redoSlotAvailable(KTextEditor::Document *document)
{
    return slotAvailable(document, kRedoMethod);
}

bool documentUndo(KTextEditor::Document *document, QString *error)
{
    return invokeSlot(document, kUndoMethod, "documentUndo", error);
}

bool documentRedo(KTextEditor::Document *document, QString *error)
{
    return invokeSlot(document, kRedoMethod, "documentRedo", error);
}

} // namespace vedit::editor
