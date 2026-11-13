#include "DisplayWidget.h"
#include "Keymap.h"

#include <QDebug>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace {

constexpr uint64_t kModInvalid = 0x00ffffffffffffffULL;

using GlEglImageTargetTexture2DOES = void (*)(unsigned target, void *image);

struct EglFns
{
    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    GlEglImageTargetTexture2DOES targetTexture = nullptr;
    bool ok() const { return createImage && destroyImage && targetTexture; }
};

const EglFns &eglFns()
{
    static EglFns f = [] {
        EglFns e;
        e.createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        e.destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        e.targetTexture = reinterpret_cast<GlEglImageTargetTexture2DOES>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        return e;
    }();
    return f;
}

// qnum codes for host-side hotkeys
constexpr uint32_t kLCtrl = 0x1d, kRCtrl = 0x9d, kLAlt = 0x38, kRAlt = 0xb8;
constexpr uint32_t kEvdevF = 33, kEvdevG = 34;

} // namespace

DisplayWidget::DisplayWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_uiInfoTimer(new QTimer(this))
    , m_ackTimer(new QTimer(this))
    , m_dropOverlay(new QLabel(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setMinimumSize(320, 200);

    m_uiInfoTimer->setSingleShot(true);
    m_uiInfoTimer->setInterval(250);
    connect(m_uiInfoTimer, &QTimer::timeout, this, &DisplayWidget::sendUiInfo);

    // If we don't get to paint (window hidden, compositor throttling) don't
    // stall the guest's GPU waiting for our acknowledgement.
    m_ackTimer->setSingleShot(true);
    m_ackTimer->setInterval(50);
    connect(m_ackTimer, &QTimer::timeout, this, [this] {
        if (m_display)
            m_display->ackFrame();
    });

    m_dropOverlay->setObjectName(QStringLiteral("dropOverlay"));
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    m_dropOverlay->setText(QStringLiteral("Drop files to copy them into the VM"));
    m_dropOverlay->setStyleSheet(QStringLiteral(
        "QLabel#dropOverlay { background: rgba(20, 24, 33, 190); color: white; font-size: 20pt;"
        " border: 3px dashed rgba(255,255,255,150); border-radius: 18px; }"));
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropOverlay->hide();
}

DisplayWidget::~DisplayWidget()
{
    detach();
    if (m_glReady) {
        makeCurrent();
        releaseTexture();
        m_vbo.destroy();
        m_vao.destroy();
        delete m_prog;
        doneCurrent();
    }
    if (m_pendingDmabuf)
        m_pendingDmabuf->closeFds();
}

void DisplayWidget::attach(DBusDisplay *display)
{
    detach();
    m_display = display;
    connect(display, &DBusDisplay::scanoutDmabuf, this, &DisplayWidget::onScanoutDmabuf);
    connect(display, &DBusDisplay::updateDmabuf, this, &DisplayWidget::onUpdateDmabuf);
    connect(display, &DBusDisplay::scanoutImage, this, &DisplayWidget::onScanoutImage);
    connect(display, &DBusDisplay::updateImage, this, &DisplayWidget::onUpdateImage);
    connect(display, &DBusDisplay::disabled, this, &DisplayWidget::onDisabled);
    connect(display, &DBusDisplay::cursorDefined, this, &DisplayWidget::onCursorDefined);
    connect(display, &DBusDisplay::mouseSet, this, &DisplayWidget::onMouseSet);
    connect(display, &DBusDisplay::connected, this, [this] { m_uiInfoTimer->start(); });
}

void DisplayWidget::detach()
{
    if (m_display) {
        releaseAllInput();
        disconnect(m_display, nullptr, this, nullptr);
        m_display->ackFrame();
    }
    m_display = nullptr;
}

void DisplayWidget::setAutoResize(bool on)
{
    m_autoResize = on;
    if (on)
        m_uiInfoTimer->start();
}

void DisplayWidget::setError(const QString &e)
{
    if (m_error == e)
        return;
    m_error = e;
    if (!e.isEmpty()) {
        qWarning() << "display:" << e;
        Q_EMIT renderError(e);
    }
}

// ---------------------------------------------------------------------------
// GL

void DisplayWidget::initializeGL()
{
    initializeOpenGLFunctions();
    const bool gles = context()->isOpenGLES();
    const QByteArray header = gles ? QByteArray("#version 100\nprecision mediump float;\n") : QByteArray("#version 120\n");

    m_prog = new QOpenGLShaderProgram;
    m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex, header + R"(
        attribute vec2 pos;
        attribute vec2 uv;
        varying vec2 v_uv;
        void main() { v_uv = uv; gl_Position = vec4(pos, 0.0, 1.0); }
    )");
    m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment, header + R"(
        uniform sampler2D tex;
        varying vec2 v_uv;
        void main() { gl_FragColor = vec4(texture2D(tex, v_uv).rgb, 1.0); }
    )");
    m_prog->bindAttributeLocation("pos", 0);
    m_prog->bindAttributeLocation("uv", 1);
    if (!m_prog->link())
        setError(QStringLiteral("Shader link failed: %1").arg(m_prog->log()));

    m_vao.create();
    m_vbo.create();
    m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    m_eglOk = context()->nativeInterface<QNativeInterface::QEGLContext>() != nullptr && eglFns().ok();
    if (!m_eglOk)
        setError(QStringLiteral("The OpenGL context is not EGL-based, so GPU frames can't be imported. "
                                "Use the QEMU window display mode for this VM."));
    m_glReady = true;
}

void DisplayWidget::resizeGL(int, int)
{
}

void DisplayWidget::releaseTexture()
{
    if (m_eglImage) {
        eglFns().destroyImage(eglGetCurrentDisplay(), static_cast<EGLImageKHR>(m_eglImage));
        m_eglImage = nullptr;
    }
    if (m_tex) {
        glDeleteTextures(1, &m_tex);
        m_tex = 0;
    }
    m_texSize = {};
}

void DisplayWidget::importPendingDmabuf()
{
    DmabufScanout s = std::move(*m_pendingDmabuf);
    m_pendingDmabuf.reset();
    releaseTexture();

    if (!m_eglOk) {
        s.closeFds();
        return;
    }

    static const EGLint fdAttr[] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT,
                                    EGL_DMA_BUF_PLANE3_FD_EXT};
    static const EGLint offAttr[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                                     EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    static const EGLint pitchAttr[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                       EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
    static const EGLint modLoAttr[] = {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                                       EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
    static const EGLint modHiAttr[] = {EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                                       EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};

    QVector<EGLint> attrs{EGL_WIDTH, EGLint(s.backingWidth), EGL_HEIGHT, EGLint(s.backingHeight),
                          EGL_LINUX_DRM_FOURCC_EXT, EGLint(s.fourcc)};
    for (int i = 0; i < s.planes.size() && i < 4; ++i) {
        attrs << fdAttr[i] << s.planes[i].fd << offAttr[i] << EGLint(s.planes[i].offset) << pitchAttr[i]
              << EGLint(s.planes[i].stride);
        if (s.modifier != kModInvalid) {
            attrs << modLoAttr[i] << EGLint(s.modifier & 0xffffffff) << modHiAttr[i] << EGLint(s.modifier >> 32);
        }
    }
    attrs << EGL_NONE;

    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLImageKHR img = eglFns().createImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attrs.constData());
    s.closeFds();   // EGL keeps its own reference to the buffer
    if (img == EGL_NO_IMAGE_KHR) {
        setError(QStringLiteral("Importing the guest frame failed (EGL error 0x%1, fourcc %2, modifier 0x%3).")
                     .arg(eglGetError(), 0, 16)
                     .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(&s.fourcc), 4)))
                     .arg(s.modifier, 0, 16));
        m_haveFrame = false;
        return;
    }
    m_eglImage = img;
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    eglFns().targetTexture(GL_TEXTURE_2D, img);
    m_texSize = QSize(int(s.backingWidth), int(s.backingHeight));
    setError(QString());
}

void DisplayWidget::uploadImage()
{
    if (m_image.isNull())
        return;
    const QSize sz = m_image.size();
    if (m_imageFull || m_tex == 0 || m_eglImage || m_texSize != sz) {
        releaseTexture();
        const QImage rgba = m_image.convertToFormat(QImage::Format_RGBA8888);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz.width(), sz.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
        m_texSize = sz;
    } else if (!m_imageDirty.isEmpty()) {
        const QRect r = m_imageDirty.intersected(QRect(QPoint(0, 0), sz));
        if (!r.isEmpty()) {
            const QImage patch = m_image.copy(r).convertToFormat(QImage::Format_RGBA8888);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.x(), r.y(), r.width(), r.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                            patch.constBits());
        }
    }
    m_imageFull = false;
    m_imageDirty = QRect();
}

QRectF DisplayWidget::displayRect() const
{
    if (!m_haveFrame || m_srcRect.isEmpty())
        return {};
    const qreal dpr = devicePixelRatioF();
    const QSizeF guest = QSizeF(m_srcRect.size()) / dpr;   // guest pixels in logical units
    const qreal scale = qMin(width() / guest.width(), height() / guest.height());
    const QSizeF sz = guest * scale;
    return QRectF(QPointF((width() - sz.width()) / 2.0, (height() - sz.height()) / 2.0), sz);
}

void DisplayWidget::paintGL()
{
    if (m_pendingDmabuf)
        importPendingDmabuf();
    if (!m_eglImage && !m_image.isNull() && (m_imageFull || !m_imageDirty.isEmpty() || m_tex == 0))
        uploadImage();

    const qreal dpr = devicePixelRatioF();
    const int W = int(width() * dpr), H = int(height() * dpr);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    const QRectF dr = displayRect();
    if (m_haveFrame && m_tex && !dr.isEmpty() && m_prog && m_prog->isLinked()) {
        const float x0 = float(2.0 * dr.left() / width() - 1.0);
        const float x1 = float(2.0 * dr.right() / width() - 1.0);
        const float yt = float(1.0 - 2.0 * dr.top() / height());
        const float yb = float(1.0 - 2.0 * dr.bottom() / height());

        const float tw = float(qMax(1, m_texSize.width())), th = float(qMax(1, m_texSize.height()));
        const float u0 = m_srcRect.left() / tw, u1 = (m_srcRect.left() + m_srcRect.width()) / tw;
        float vTop = m_srcRect.top() / th, vBot = (m_srcRect.top() + m_srcRect.height()) / th;
        // QEMU's y0_top=true means GL-style bottom-up rows (QEMU's own GTK UI
        // flips when it is false because it blits to a bottom-left origin FB).
        if (m_flipY) {
            vTop = 1.0f - vTop;
            vBot = 1.0f - vBot;
        }
        const float verts[] = {
            x0, yt, u0, vTop,
            x1, yt, u1, vTop,
            x0, yb, u0, vBot,
            x1, yb, u1, vBot,
        };

        // Sharp pixels when shown 1:1, smooth when scaled.
        const bool exact = qAbs(dr.width() * dpr - m_srcRect.width()) < 0.5;
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, exact ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, exact ? GL_NEAREST : GL_LINEAR);

        QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
        m_prog->bind();
        m_vbo.bind();
        m_vbo.allocate(verts, sizeof(verts));
        m_prog->enableAttributeArray(0);
        m_prog->enableAttributeArray(1);
        m_prog->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        m_prog->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
        m_prog->setUniformValue("tex", 0);
        glActiveTexture(GL_TEXTURE0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_prog->disableAttributeArray(0);
        m_prog->disableAttributeArray(1);
        m_vbo.release();
        m_prog->release();
    }

    if (m_display && m_display->hasPendingFrame()) {
        glFlush();
        m_ackTimer->stop();
        m_display->ackFrame();
    }

    if (!m_haveFrame || !m_error.isEmpty()) {
        QPainter p(this);
        p.setPen(QColor(200, 200, 200));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.2);
        p.setFont(f);
        const QString text = !m_error.isEmpty() ? m_error : QStringLiteral("Waiting for the display…");
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
}

// ---------------------------------------------------------------------------
// Frame events

void DisplayWidget::scheduleAck()
{
    if (!isVisible() || (window() && window()->windowState() & Qt::WindowMinimized)) {
        if (m_display)
            m_display->ackFrame();
        return;
    }
    m_ackTimer->start();
}

void DisplayWidget::onScanoutDmabuf(const DmabufScanout &s)
{
    if (m_pendingDmabuf)
        m_pendingDmabuf->closeFds();
    m_pendingDmabuf = s;
    m_image = QImage();
    const QSize old = m_srcRect.size();
    m_srcRect = s.visible;
    m_flipY = s.y0Top;
    m_haveFrame = true;
    if (old != m_srcRect.size())
        Q_EMIT guestSizeChanged(m_srcRect.size());
    update();
}

void DisplayWidget::onUpdateDmabuf(const QRect &)
{
    update();
    scheduleAck();
}

void DisplayWidget::onScanoutImage(const QImage &img)
{
    m_image = img;
    m_imageFull = true;
    m_imageDirty = QRect();
    const QSize old = m_srcRect.size();
    m_srcRect = QRect(QPoint(0, 0), img.size());
    m_flipY = false;
    m_haveFrame = !img.isNull();
    if (m_pendingDmabuf) {
        m_pendingDmabuf->closeFds();
        m_pendingDmabuf.reset();
    }
    if (old != m_srcRect.size())
        Q_EMIT guestSizeChanged(m_srcRect.size());
    update();
}

void DisplayWidget::onUpdateImage(const QRect &r, const QImage &patch)
{
    if (m_image.isNull())
        return;
    QPainter p(&m_image);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r.topLeft(), patch);
    p.end();
    m_imageDirty = m_imageDirty.united(r);
    update();
}

void DisplayWidget::onDisabled()
{
    m_haveFrame = false;
    update();
}

void DisplayWidget::onCursorDefined(const QImage &img, int hotX, int hotY)
{
    if (img.isNull()) {
        m_haveGuestCursor = false;
    } else {
        QPixmap pm = QPixmap::fromImage(img);
        m_guestCursor = QCursor(pm, hotX, hotY);
        m_haveGuestCursor = true;
    }
    applyCursor();
}

void DisplayWidget::onMouseSet(int, int, bool visible)
{
    m_cursorVisible = visible;
    applyCursor();
}

void DisplayWidget::applyCursor()
{
    if (!m_cursorVisible)
        setCursor(Qt::BlankCursor);
    else if (m_haveGuestCursor)
        setCursor(m_guestCursor);
    else
        unsetCursor();
}

QImage DisplayWidget::grabGuestFrame()
{
    if (!m_haveFrame)
        return {};
    const QImage full = grabFramebuffer();
    const qreal dpr = full.width() / qreal(qMax(1, width()));
    const QRectF dr = displayRect();
    const QRect px(qRound(dr.left() * dpr), qRound(dr.top() * dpr), qRound(dr.width() * dpr), qRound(dr.height() * dpr));
    return full.copy(px.intersected(full.rect()));
}

// ---------------------------------------------------------------------------
// Input

bool DisplayWidget::event(QEvent *e)
{
    // Keep window shortcuts from firing while the VM has the keyboard.
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QOpenGLWidget::event(e);
}

void DisplayWidget::keyPressEvent(QKeyEvent *e)
{
    e->accept();
    if (e->isAutoRepeat() || !m_display)
        return;   // the guest does its own key repeat
    const uint32_t evdev = e->nativeScanCode() > 8 ? e->nativeScanCode() - 8 : 0;
    const uint32_t q = Keymap::evdevToQnum(evdev);
    if (!q)
        return;

    const bool ctrl = m_pressedKeys.contains(kLCtrl) || m_pressedKeys.contains(kRCtrl);
    const bool alt = m_pressedKeys.contains(kLAlt) || m_pressedKeys.contains(kRAlt);
    if (ctrl && alt && (evdev == kEvdevF || evdev == kEvdevG)) {
        releaseAllInput();
        if (evdev == kEvdevF)
            Q_EMIT toggleFullscreenRequested();
        else
            Q_EMIT releaseKeyboardRequested();
        return;
    }
    m_pressedKeys.insert(q);
    m_display->keyPress(q);
}

void DisplayWidget::keyReleaseEvent(QKeyEvent *e)
{
    e->accept();
    if (e->isAutoRepeat() || !m_display)
        return;
    const uint32_t evdev = e->nativeScanCode() > 8 ? e->nativeScanCode() - 8 : 0;
    const uint32_t q = Keymap::evdevToQnum(evdev);
    if (q && m_pressedKeys.remove(q))
        m_display->keyRelease(q);
}

void DisplayWidget::sendKeyCombo(const QList<uint32_t> &qnums)
{
    if (!m_display)
        return;
    for (uint32_t q : qnums)
        m_display->keyPress(q);
    for (auto it = qnums.crbegin(); it != qnums.crend(); ++it)
        m_display->keyRelease(*it);
}

void DisplayWidget::releaseAllInput()
{
    if (!m_display)
        return;
    for (uint32_t q : std::as_const(m_pressedKeys))
        m_display->keyRelease(q);
    for (uint32_t b : std::as_const(m_pressedButtons))
        m_display->mouseRelease(b);
    m_pressedKeys.clear();
    m_pressedButtons.clear();
}

bool DisplayWidget::toGuest(const QPointF &pos, uint32_t *gx, uint32_t *gy) const
{
    const QRectF dr = displayRect();
    if (dr.isEmpty())
        return false;
    const qreal fx = (pos.x() - dr.left()) / dr.width();
    const qreal fy = (pos.y() - dr.top()) / dr.height();
    const bool inside = fx >= 0 && fx < 1 && fy >= 0 && fy < 1;
    *gx = uint32_t(qBound(0, int(fx * m_srcRect.width()), m_srcRect.width() - 1));
    *gy = uint32_t(qBound(0, int(fy * m_srcRect.height()), m_srcRect.height() - 1));
    return inside;
}

void DisplayWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_display)
        return;
    if (m_display->mouseIsAbsolute()) {
        uint32_t x, y;
        if (toGuest(e->position(), &x, &y) || !m_pressedButtons.isEmpty())
            m_display->mouseMoveAbs(x, y);
    } else {
        const QPointF d = e->position() - m_lastPos;
        const qreal s = displayRect().isEmpty() ? 1.0 : m_srcRect.width() / displayRect().width();
        m_display->mouseMoveRel(int(d.x() * s), int(d.y() * s));
    }
    m_lastPos = e->position();
}

uint32_t DisplayWidget::buttonFor(Qt::MouseButton b, bool *ok)
{
    *ok = true;
    switch (b) {
    case Qt::LeftButton: return DBusDisplay::Left;
    case Qt::MiddleButton: return DBusDisplay::Middle;
    case Qt::RightButton: return DBusDisplay::Right;
    case Qt::BackButton: return DBusDisplay::Side;
    case Qt::ForwardButton: return DBusDisplay::Extra;
    default: *ok = false; return 0;
    }
}

void DisplayWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    if (!m_display)
        return;
    uint32_t x, y;
    if (m_display->mouseIsAbsolute() && toGuest(e->position(), &x, &y))
        m_display->mouseMoveAbs(x, y);
    bool ok;
    const uint32_t b = buttonFor(e->button(), &ok);
    if (ok) {
        m_pressedButtons.insert(b);
        m_display->mousePress(b);
    }
}

void DisplayWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_display)
        return;
    bool ok;
    const uint32_t b = buttonFor(e->button(), &ok);
    if (ok && m_pressedButtons.remove(b))
        m_display->mouseRelease(b);
}

void DisplayWidget::wheelEvent(QWheelEvent *e)
{
    e->accept();
    if (!m_display)
        return;
    m_wheelAccum += e->angleDelta().y();
    while (m_wheelAccum >= 120) {
        m_display->mousePress(DBusDisplay::WheelUp);
        m_display->mouseRelease(DBusDisplay::WheelUp);
        m_wheelAccum -= 120;
    }
    while (m_wheelAccum <= -120) {
        m_display->mousePress(DBusDisplay::WheelDown);
        m_display->mouseRelease(DBusDisplay::WheelDown);
        m_wheelAccum += 120;
    }
}

void DisplayWidget::focusOutEvent(QFocusEvent *e)
{
    releaseAllInput();
    QOpenGLWidget::focusOutEvent(e);
}

void DisplayWidget::hideEvent(QHideEvent *e)
{
    if (m_display)
        m_display->ackFrame();
    QOpenGLWidget::hideEvent(e);
}

// ---------------------------------------------------------------------------
// Resize → guest resolution

void DisplayWidget::resizeEvent(QResizeEvent *e)
{
    QOpenGLWidget::resizeEvent(e);
    m_dropOverlay->setGeometry(rect().adjusted(24, 24, -24, -24));
    if (m_autoResize)
        m_uiInfoTimer->start();
}

void DisplayWidget::sendUiInfo()
{
    if (!m_display || !m_autoResize || !m_display->isConnected())
        return;
    const qreal dpr = devicePixelRatioF();
    const uint32_t w = uint32_t(qMax(640.0, width() * dpr)) & ~7u;
    const uint32_t h = uint32_t(qMax(400.0, height() * dpr)) & ~1u;
    qreal dpi = 96.0;
    if (QScreen *s = screen())
        dpi = s->physicalDotsPerInch() > 10 ? s->physicalDotsPerInch() : 96.0;
    const auto mm = [dpi](uint32_t px) { return uint16_t(qBound(1.0, px / dpi * 25.4, 65535.0)); };
    m_display->setUiInfo(w, h, mm(w), mm(h));
}

// ---------------------------------------------------------------------------
// Drag & drop

static QStringList localFiles(const QMimeData *md)
{
    QStringList out;
    if (!md || !md->hasUrls())
        return out;
    for (const QUrl &u : md->urls())
        if (u.isLocalFile())
            out << u.toLocalFile();
    return out;
}

void DisplayWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_acceptFiles && !localFiles(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
        m_dropOverlay->raise();
        m_dropOverlay->show();
    } else {
        e->ignore();
    }
}

void DisplayWidget::dragMoveEvent(QDragMoveEvent *e)
{
    e->acceptProposedAction();
}

void DisplayWidget::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropOverlay->hide();
}

void DisplayWidget::dropEvent(QDropEvent *e)
{
    m_dropOverlay->hide();
    const QStringList files = localFiles(e->mimeData());
    if (files.isEmpty())
        return;
    e->acceptProposedAction();
    Q_EMIT filesDropped(files);
}
