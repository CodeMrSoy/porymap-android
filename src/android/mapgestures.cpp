#include "androidmapgestures.h"
#include "editor.h"

#include <QApplication>
#include <QGraphicsView>
#include <QLineF>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QTouchEvent>
#include <QVariant>
#include <cmath>

namespace {
class GestureOverlay : public QWidget {
public:
    GestureOverlay(QWidget *parent, QGraphicsView *view, Editor *editor) : QWidget(parent), mapView(view), engine(editor) {
        setObjectName("androidQuickMenu");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }
    bool menu = false;
    int selected = -1;
    bool undoEnabled = false, redoEnabled = false;
    QList<QPointF> stroke;
    QList<QRectF> targets;
    QGraphicsView *mapView;
    QPointer<Editor> engine;
    void openMenu(QPointF origin, bool undo, bool redo) {
        setGeometry(parentWidget()->rect());
        menu = true; stroke.clear(); selected = -1;
        undoEnabled = undo; redoEnabled = redo;
        const bool wide = width() >= 500;
        const qreal halfWidth = wide ? 244.0 : 144.0;
        const qreal halfHeight = wide ? 54.0 : 112.0;
        const qreal cx = qBound(halfWidth, origin.x(), qMax(halfWidth, width() - halfWidth));
        const qreal desiredY = wide ? origin.y() + (origin.y() > height() / 2.0 ? -70 : 70) : origin.y();
        const qreal cy = qBound(halfHeight, desiredY, qMax(halfHeight, height() - halfHeight));
        const QList<QPointF> offsets = wide
            ? QList<QPointF>{{-188, 0}, {-94, 0}, {0, 0}, {94, 0}, {188, 0}}
            : QList<QPointF>{{0, -78}, {-94, -16}, {94, -16}, {-54, 58}, {54, 58}};
        targets.clear();
        for (int i = 0; i < offsets.size(); ++i) {
            const QRectF rect(QPointF(cx, cy) + offsets[i] - QPointF(44, 24), QSizeF(88, 48));
            targets.append(rect);
            setProperty(qPrintable(QString("target%1").arg(i)), rect);
        }
        setProperty("selection", -1);
        show(); raise(); update();
    }
    int hit(QPointF point) const {
        for (int i = 0; i < targets.size(); ++i) {
            if ((i == 3 && !undoEnabled) || (i == 4 && !redoEnabled)) continue;
            if (targets[i].contains(point)) return i;
        }
        return -1;
    }
    void highlight(QPointF point) {
        selected = hit(point); setProperty("selection", selected); update();
    }
    void preview(const QList<QPointF> &points) {
        menu = false; stroke = points; setGeometry(parentWidget()->rect()); show(); raise(); update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!engine) return;
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        if (!menu) {
            if (stroke.size() < 2) return;
            if (engine->getEditMode() == Editor::EditMode::Metatiles && engine->getEditAction() == Editor::EditAction::Paint &&
                engine->layout && engine->current_metatile_selection_item) {
                const QPixmap brush = engine->current_metatile_selection_item->pixmap();
                const QPointF anchor = mapView->mapToScene(stroke.first().toPoint()) / 16.0;
                const int ax = int(std::floor(anchor.x())), ay = int(std::floor(anchor.y()));
                const int columns = qMax(1, brush.width() / 16), rows = qMax(1, brush.height() / 16);
                QSet<QPair<int, int>> visited;
                painter.setClipRect(mapView->mapFromScene(QRectF(0, 0, engine->layout->pixelWidth(), engine->layout->pixelHeight())).boundingRect());
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false); painter.setOpacity(0.85);
                for (const auto &point : stroke) {
                    const QPointF scene = mapView->mapToScene(point.toPoint()) / 16.0;
                    const int x = ax + int(std::floor((std::floor(scene.x()) - ax) / columns)) * columns;
                    const int y = ay + int(std::floor((std::floor(scene.y()) - ay) / rows)) * rows;
                    const QPair<int, int> key{x, y}; if (visited.contains(key)) continue; visited.insert(key);
                    const QRect target = mapView->mapFromScene(QRectF(x * 16, y * 16, brush.width(), brush.height())).boundingRect();
                    painter.drawPixmap(target, brush);
                    painter.setPen(QPen(QColor("#83cdb6"), 1)); painter.setBrush(Qt::NoBrush); painter.drawRect(target);
                }
                return;
            }
            painter.setPen(QPen(QColor(125, 211, 184, 170), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 1; i < stroke.size(); ++i) painter.drawLine(stroke[i - 1], stroke[i]);
            return;
        }
        painter.fillRect(rect(), QColor(8, 17, 23, 90));
        QRectF panel;
        for (const auto &target : targets) panel = panel.united(target);
        panel.adjust(-10, -10, 10, 30);
        painter.setPen(QPen(QColor("#526976"), 1));
        painter.setBrush(QColor("#253540")); painter.drawRoundedRect(panel, 18, 18);
        const QStringList labels{tr("Paint"), tr("Fill"), tr("Pick tile"), tr("Undo"), tr("Redo")};
        QFont labelFont = font(); labelFont.setBold(true); painter.setFont(labelFont);
        for (int i = 0; i < targets.size(); ++i) {
            const bool enabled = (i != 3 || undoEnabled) && (i != 4 || redoEnabled);
            painter.setPen(Qt::NoPen);
            painter.setBrush(i == selected ? QColor("#83cdb6") : QColor("#344854"));
            painter.drawRoundedRect(targets[i], 10, 10);
            painter.setPen(!enabled ? QColor("#798d99") : i == selected ? QColor("#132e28") : QColor("#e2edf1"));
            painter.drawText(targets[i], Qt::AlignCenter, labels[i]);
        }
        painter.setFont(font()); painter.setPen(QColor("#adc0ca"));
        painter.drawText(QRectF(panel.left(), panel.bottom() - 26, panel.width(), 22), Qt::AlignCenter, tr("Slide to choose · release to apply"));
    }
};

class AndroidMapGestures : public QObject {
public:
    AndroidMapGestures(QGraphicsView *view, Editor *editor, bool menu)
        : QObject(view), view(view), editor(editor), allowMenu(menu), overlay(new GestureOverlay(view->viewport(), view, editor)) {
        setObjectName("androidGestures");
        view->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
        view->viewport()->installEventFilter(this);
        hold.setSingleShot(true); hold.setInterval(480);
        connect(&hold, &QTimer::timeout, this, [this] {
            if (state != Pending || !allowMenu || !this->editor || !this->editor->layout || !overlay) return;
            state = Menu;
            overlay->openMenu(origin, this->editor->editGroup.canUndo(), this->editor->editGroup.canRedo());
        });
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const auto type = event->type();
        if (type == QEvent::Hide) reset();
        if (type == QEvent::Resize) {
            // Insets and layout changes must not drop a two-finger gesture.
            if (state == Navigate) rebaseNavigation = true;
            else reset();
        }
        if (object != view->viewport() ||
            (type != QEvent::TouchBegin && type != QEvent::TouchUpdate && type != QEvent::TouchEnd && type != QEvent::TouchCancel))
            return false;
        if (!editor || !editor->project || !editor->layout || !view->scene() || !overlay) return false;
        auto *touch = static_cast<QTouchEvent *>(event);
        event->accept();
        if (type == QEvent::TouchCancel) { reset(); return true; }
        if (type == QEvent::TouchBegin) reset();
        QPointF released = last;
        for (const auto &point : touch->points()) {
            if (point.state() != QEventPoint::State::Released) contacts.insert(point.id(), point.globalPosition());
            else { contacts.remove(point.id()); released = view->viewport()->mapFromGlobal(point.globalPosition()); }
        }
        if (type == QEvent::TouchEnd) contacts.clear();
        const auto ids = contacts.keys();
        if (ids.size() >= 2) {
            const bool changedPair = !contacts.contains(firstPointer) || !contacts.contains(secondPointer);
            if (state != Navigate || changedPair) { firstPointer = ids[0]; secondPointer = ids[1]; rebaseNavigation = true; }
            const QPointF a = localPosition(firstPointer), b = localPosition(secondPointer);
            const QPointF center = (a + b) / 2;
            const qreal distance = QLineF(a, b).length();
            if (state != Navigate || rebaseNavigation) {
                hold.stop(); path.clear(); overlay->hide(); state = Navigate;
                navigationAnchor = view->viewportTransform().inverted().map(center);
                navigationDistance = qMax(1.0, distance);
                navigationScale = view->transform().m11();
                contentBounds = view->scene()->itemsBoundingRect();
                rebaseNavigation = false;
            }
            moveCamera(center, distance);
            return true;
        }
        if (state == Navigate) {
            // Do not turn the remaining finger into a paint stroke.
            rebaseNavigation = true;
            if (ids.isEmpty()) reset();
            return true;
        }
        if (type == QEvent::TouchBegin && ids.size() == 1) {
            origin = last = localPosition(ids[0]); path.append(origin); state = Pending;
            if (allowMenu) hold.start();
            return true;
        }
        const QPointF position = ids.isEmpty() ? released : localPosition(ids[0]);
        if (state == Menu) {
            if (QLineF(origin, position).length() > QApplication::startDragDistance()) engaged = true;
            if (engaged) overlay->highlight(position);
            if (ids.isEmpty()) {
                const int selected = engaged ? overlay->hit(position) : -1;
                reset();
                if (selected == 0) editor->setEditAction(Editor::EditAction::Paint);
                else if (selected == 1) editor->setEditAction(Editor::EditAction::Fill);
                else if (selected == 2) editor->setEditAction(Editor::EditAction::Pick);
                else if (selected == 3) editor->editGroup.undo();
                else if (selected == 4) editor->editGroup.redo();
                setProperty("lastAction", selected);
            }
            return true;
        }
        if (state == Pending || state == Stroke) {
            if (QLineF(origin, position).length() > QApplication::startDragDistance()) {
                hold.stop(); state = Stroke;
            }
            if (QLineF(last, position).length() >= 1) path.append(position);
            last = position;
            if (state == Stroke) overlay->preview(path);
            if (ids.isEmpty()) {
                // Commit the native stroke only after all fingers are up. A second
                // finger or OS cancellation therefore leaves the undo/redo history intact.
                const auto stroke = path;
                reset();
                if (!stroke.isEmpty()) {
                    mouse(QEvent::MouseButtonPress, stroke.first());
                    for (int i = 1; i < stroke.size(); ++i) mouse(QEvent::MouseMove, stroke[i]);
                    mouse(QEvent::MouseButtonRelease, stroke.last());
                    if (allowMenu && editor->getEditAction() == Editor::EditAction::Pick)
                        editor->setEditAction(Editor::EditAction::Paint);
                }
            }
        }
        return true;
    }
private:
    enum State { Idle, Pending, Stroke, Navigate, Menu } state = Idle;
    QGraphicsView *view;
    QPointer<Editor> editor;
    bool allowMenu, engaged = false;
    QPointer<GestureOverlay> overlay;
    QTimer hold;
    QPointF origin, last, navigationAnchor;
    QMap<int, QPointF> contacts;
    int firstPointer = -1, secondPointer = -1;
    bool rebaseNavigation = true;
    qreal navigationDistance = 1, navigationScale = 1;
    QRectF contentBounds;
    QList<QPointF> path;
    void reset() {
        hold.stop(); if (overlay) overlay->hide(); path.clear(); contacts.clear();
        state = Idle; engaged = false; rebaseNavigation = true; firstPointer = secondPointer = -1;
    }
    QPointF localPosition(int id) const { return view->viewport()->mapFromGlobal(contacts.value(id)); }
    void moveCamera(QPointF center, qreal distance) {
        // An absolute scene anchor avoids accumulating rounded scrollbar deltas.
        const qreal requested = navigationScale * distance / navigationDistance;
        const qreal scale = qBound(0.5, requested, 6.0);
        if (qAbs(view->transform().m11() - scale) > 0.000001) editor->setAndroidMapScale(scale);
        const QSizeF size(view->viewport()->width() / scale, view->viewport()->height() / scale);
        const QPointF middle(view->viewport()->width() / 2.0, view->viewport()->height() / 2.0);
        const QPointF camera = navigationAnchor - (center - middle) / scale;
        QRectF visible(camera - QPointF(size.width() / 2, size.height() / 2), size);
        // Expand around the camera at every zoom level, including maps smaller
        // than the viewport. Navigation must never be pinned by scene bounds.
        view->setSceneRect(contentBounds.united(visible.adjusted(-size.width(), -size.height(), size.width(), size.height())));
        view->centerOn(camera);
        if (!qFuzzyCompare(requested, scale)) {
            // Reverse direction immediately at a zoom limit instead of having
            // to unwind the finger movement that went beyond that limit.
            navigationScale = scale; navigationDistance = qMax(1.0, distance);
        }
    }
    void mouse(QEvent::Type type, QPointF point) {
        QMouseEvent event(type, point, view->viewport()->mapToGlobal(point.toPoint()),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
    }
};
}

void installAndroidMapGestures(QGraphicsView *view, Editor *editor, bool quickMenu) {
    new AndroidMapGestures(view, editor, quickMenu);
}
