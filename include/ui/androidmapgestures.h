#pragma once

class QGraphicsView;
class Editor;

// Own the complete finger sequence so holds and navigation never paint.
void installAndroidMapGestures(QGraphicsView *view, Editor *editor, bool quickMenu);
