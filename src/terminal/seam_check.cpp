// Compile-time proof that the terminal panel fits the application shell's panel
// seam (task T35).
//
// src/app/panel_registry.h declares the seam every panel lane plugs into:
//
//     void setFactory(const QString &id, std::function<QWidget *()> factory);
//
// This translation unit is compiled as an OBJECT library (never linked), so it
// type-checks the wiring without dragging the shell's implementation into the
// terminal lane: if the factory signature or the panel class ever stops matching
// PanelSpec's factory type, the lane's own build fails here instead of in the
// assembly step. It is skipped when src/app has not landed.
#include "../app/panel_registry.h"
#include "terminal_panel.h"

#include <QStringLiteral>
#include <QWidget>

#include <functional>
#include <type_traits>

static_assert(std::is_same_v<decltype(vedit::terminal::terminalPanelFactory()), std::function<QWidget *()>>,
              "PanelRegistry::setFactory takes std::function<QWidget *()>; keep the terminal factory in that shape");
static_assert(std::is_same_v<decltype(&vedit::terminal::createTerminalPanel), QWidget *(*)()>,
              "createTerminalPanel must stay a plain QWidget*() entry point");

namespace {

// Never called and never linked: this function exists so the compiler checks the
// exact call the shell will make. Internal linkage keeps an unemitted body from
// leaving an undefined PanelRegistry symbol in the lane's binaries.
[[maybe_unused]] void wireTerminalPanel(vedit::app::PanelRegistry &panels)
{
    panels.setFactory(QStringLiteral("terminal"), vedit::terminal::terminalPanelFactory());
}

} // namespace
