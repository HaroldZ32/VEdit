#pragma once

class QLabel;
class QMainWindow;
class QString;
class QWidget;

namespace vedit::app {

void installWorkbenchInteraction(QMainWindow *window);
QLabel *showWorkbenchDragFeedback(QWidget *window, const QString &title);
void finishWorkbenchDragFeedback(QLabel *badge, bool dropped);

} // namespace vedit::app
