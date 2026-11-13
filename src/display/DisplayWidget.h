#pragma once

#include "DBusDisplay.h"

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointer>
#include <QSet>

#include <optional>

class QLabel;
class QOpenGLShaderProgram;
class QTimer;

// Renders a QEMU D-Bus display and forwards input to it.
class DisplayWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit DisplayWidget(QWidget *parent = nullptr);
    ~DisplayWidget() override;

    void attach(DBusDisplay *display);
    void detach();

    void setAutoResize(bool on);
    void setAcceptFileDrops(bool on) { m_acceptFiles = on; }
    QSize guestSize() const { return m_srcRect.size(); }
    bool hasFrame() const { return m_haveFrame; }

    // Press all keys in order, then release them in reverse (e.g. Ctrl+Alt+Del).
    void sendKeyCombo(const QList<uint32_t> &qnums);
    void releaseAllInput();
    QImage grabGuestFrame();
    QString lastError() const { return m_error; }

Q_SIGNALS:
    void filesDropped(const QStringList &paths);
    void toggleFullscreenRequested();
    void releaseKeyboardRequested();
    void guestSizeChanged(const QSize &size);
    void renderError(const QString &error);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    bool event(QEvent *e) override;
    bool focusNextPrevChild(bool) override { return false; }
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void onScanoutDmabuf(const DmabufScanout &s);
    void onUpdateDmabuf(const QRect &r);
    void onScanoutImage(const QImage &img);
    void onUpdateImage(const QRect &r, const QImage &patch);
    void onDisabled();
    void onCursorDefined(const QImage &img, int hotX, int hotY);
    void onMouseSet(int x, int y, bool visible);

    void importPendingDmabuf();
    void uploadImage();
    void releaseTexture();
    void setError(const QString &e);
    QRectF displayRect() const;   // logical widget coordinates
    bool toGuest(const QPointF &pos, uint32_t *gx, uint32_t *gy) const;
    void sendUiInfo();
    void scheduleAck();
    void applyCursor();
    static uint32_t buttonFor(Qt::MouseButton b, bool *ok);

    QPointer<DBusDisplay> m_display;

    // GL state
    bool m_glReady = false;
    bool m_eglOk = false;
    QOpenGLShaderProgram *m_prog = nullptr;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
    unsigned m_tex = 0;
    void *m_eglImage = nullptr;

    // Frame source
    std::optional<DmabufScanout> m_pendingDmabuf;
    QImage m_image;
    QRect m_imageDirty;
    bool m_imageFull = false;
    bool m_haveFrame = false;
    QSize m_texSize;
    QRect m_srcRect;
    bool m_flipY = false;
    QString m_error;

    // Input state
    QSet<uint32_t> m_pressedKeys;
    QSet<uint32_t> m_pressedButtons;
    int m_wheelAccum = 0;
    QPointF m_lastPos;
    bool m_autoResize = true;
    bool m_acceptFiles = true;
    QCursor m_guestCursor;
    bool m_haveGuestCursor = false;
    bool m_cursorVisible = true;

    QTimer *m_uiInfoTimer;
    QTimer *m_ackTimer;
    QLabel *m_dropOverlay;
};
