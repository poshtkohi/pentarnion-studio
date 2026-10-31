// Adapted from GTG Cell Studio pane_hover.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
class QMainWindow;
class QWidget;
namespace penta::desktop {
// Accents only pane headers; contents, context menus and geometry stay native.
void installStudioPaneHover(QMainWindow* window);
void registerStudioPaneHover(QWidget* pane,QWidget* header);
bool studioPaneHoverActive(const QWidget* widget);
}
