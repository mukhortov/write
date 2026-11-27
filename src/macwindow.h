#pragma once

#include <QColor>

class QWidget;

// What Qt leaves to AppKit about a window.
namespace MacWindow {

// Paints the title bar in the page's colour: the bar goes transparent over
// the window's background, which takes the colour. Nothing off Cocoa.
void setTitleBarColour(QWidget *window, const QColor &colour);
// The colour the title bar shows, or an invalid colour while it is AppKit's.
QColor titleBarColour(QWidget *window);
// AppKit's own About panel: the icon, the name and the version from the
// bundle's Info.plist. Nothing off Cocoa.
void showAboutPanel();

}
