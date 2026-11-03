// LiNotch Spike 0: prove the Qt 6.4 Wayland surface path for zwlr-layer-shell.
//
// Modes:
//   --mode=manual    (default) drive the Qt-created wl_surface with a wl_shm buffer
//   --mode=qt-bypass use Qt's own rendering (QBackingStore) with
//                    QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1 + BypassWindowManagerHint
//   --mode=qt-quick  same ordering, but a QQuickView paints the surface (Spike 0b)
//   --mode=qt-show   control: show() without bypass; expected wl_surface role conflict
//
// This is throwaway spike code, not LiNotch product code.

#include <QGuiApplication>
#include <QWindow>
#include <QSurfaceFormat>
#include <QBackingStore>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QTimer>
#include <QByteArray>
#include <qpa/qplatformnativeinterface.h>

#ifdef SPIKE_HAS_QUICK
#include <QQuickView>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
#endif

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

namespace {

using SteadyClock = std::chrono::steady_clock;
SteadyClock::time_point g_startTime;

long long elapsedMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - g_startTime).count();
}

void phase(const char *fmt, ...)
{
    std::fprintf(stderr, "[%8lld ms] ", elapsedMs());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

struct Options {
    std::string mode = "manual";
    int durationSec = 60;
    int width = 260;
    int height = 36;
    std::string qml = "Pill.qml";
    bool animate = false;
    bool plain = false;
    bool alpha = true;
    bool alphaProbe = false;
    bool region = false;
};

Options parseOptions(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&a](const char *prefix) -> const char * {
            const size_t n = std::strlen(prefix);
            return a.rfind(prefix, 0) == 0 ? a.c_str() + n : nullptr;
        };
        if (const char *v = value("--mode="))
            o.mode = v;
        else if (const char *v = value("--duration="))
            o.durationSec = std::atoi(v);
        else if (const char *v = value("--width="))
            o.width = std::atoi(v);
        else if (const char *v = value("--height="))
            o.height = std::atoi(v);
        else if (const char *v = value("--qml="))
            o.qml = v;
        else if (a == "--animate")
            o.animate = true;
        else if (a == "--plain")
            o.plain = true;
        else if (a == "--no-alpha")
            o.alpha = false;
        else if (a == "--alpha-probe")
            o.alphaProbe = true;
        else if (a == "--region")
            o.region = true;
    }
    if (o.qml.find_first_of("/\\") != std::string::npos || o.qml.empty() || o.qml.find("..") != std::string::npos) {
        std::fprintf(stderr, "ERROR: --qml must be a bare filename in spike/qml/\n");
        std::exit(2);
    }
    return o;
}

struct Spike {
    explicit Spike(Options o, QGuiApplication &a)
        : opt(std::move(o)), app(a), width(opt.width), height(opt.height)
    {
    }

    Options opt;
    QGuiApplication &app;
    std::unique_ptr<QWindow> window;
    std::unique_ptr<QBackingStore> backingStore;

    wl_display *display = nullptr;
    wl_surface *surface = nullptr;
    wl_registry *registry = nullptr;
    wl_shm *shm = nullptr;
    zwlr_layer_shell_v1 *layerShell = nullptr;
    zwlr_layer_surface_v1 *layerSurface = nullptr;
    wl_buffer *buffer = nullptr;
    wl_callback *frameCallback = nullptr;
    void *pixels = nullptr;
    size_t pixelsSize = 0;

    int width = 260;
    int height = 36;
    uint32_t boundLayerShellVersion = 0;

    bool initialCommitSent = false;
    bool configureSeen = false;
    bool mapped = false;
    bool framePresented = false;
    bool finished = false;
    int quickFrames = 0;
    int bypassFrames = 0;
    int animTick = 0;

    bool start()
    {
        phase("platform=%s", qPrintable(QGuiApplication::platformName()));
        if (QGuiApplication::platformName() != QLatin1String("wayland")) {
            phase("ERROR: not running on the wayland Qt platform plugin; Wayland-only spike");
            return false;
        }

#ifdef SPIKE_HAS_QUICK
        if (opt.mode == "qt-quick") {
            auto *view = new QQuickView();
            view->setResizeMode(QQuickView::SizeRootObjectToView);
            window.reset(view);
            phase("qt-quick: QQuickView stands in for QWindow (SizeRootObjectToView, transparent fill)");
        } else {
            window = std::make_unique<QWindow>();
        }
#else
        window = std::make_unique<QWindow>();
#endif
        window->setTitle(QStringLiteral("LiNotch Spike"));
        // The notch silhouette needs real transparency: without an alpha buffer
        // Qt's wl_shm path picks XRGB8888 and the rounded corners come out
        // opaque black. The format is requested process-wide via
        // QSurfaceFormat::setDefaultFormat() because setFormat() on the window
        // is not honoured here (measured: alpha_buffer stayed -1).
        window->resize(width, height);
        if (opt.mode == "qt-bypass" || opt.mode == "qt-quick")
            window->setFlag(Qt::BypassWindowManagerHint);

        window->create();
        phase("QWindow::create() returned (winId=%llu, visible=%d)",
              static_cast<unsigned long long>(window->winId()), window->isVisible() ? 1 : 0);

        QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
        if (!pni) {
            phase("ERROR: QGuiApplication::platformNativeInterface() is null");
            return false;
        }

        display = static_cast<wl_display *>(pni->nativeResourceForIntegration(QByteArrayLiteral("display")));
        surface = static_cast<wl_surface *>(pni->nativeResourceForWindow(QByteArrayLiteral("surface"), window.get()));

        phase("OQ1 probe: nativeResourceForIntegration(\"display\")=%p (%s)", static_cast<void *>(display),
              display ? "non-null" : "NULL");
        phase("OQ1 probe: nativeResourceForWindow(\"surface\")=%p (%s)", static_cast<void *>(surface),
              surface ? "non-null" : "NULL");

        if (!display || !surface) {
            phase("OQ1 answer: NO (a required native resource was null)");
            return false;
        }
        phase("OQ1 answer: YES (display and surface are both non-null after create(), before any commit)");
        phase("wl_surface protocol version=%d", wl_proxy_get_version(reinterpret_cast<wl_proxy *>(surface)));

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registryListener, this);
        wl_display_flush(display);
        phase("registry created; globals will arrive through Qt's event loop");
        return true;
    }

    void maybeCreateLayerSurface()
    {
        if (layerSurface || !shm || !layerShell)
            return;

        layerSurface = zwlr_layer_shell_v1_get_layer_surface(
            layerShell, surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "linotch-spike");
        if (!layerSurface) {
            phase("ERROR: get_layer_surface returned null");
            app.exit(2);
            return;
        }

        zwlr_layer_surface_v1_add_listener(layerSurface, &layerSurfaceListener, this);
        zwlr_layer_surface_v1_set_size(layerSurface, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        zwlr_layer_surface_v1_set_anchor(layerSurface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
        zwlr_layer_surface_v1_set_exclusive_zone(layerSurface, 0);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

        phase("layer surface created: layer=TOP anchor=top size=%dx%d exclusive_zone=0 keyboard=none "
              "(layer_shell bound v%u)",
              width, height, boundLayerShellVersion);

        if (opt.mode == "qt-show") {
            phase("control mode: calling QWindow::show() WITHOUT bypass; expecting a wl_surface role conflict");
            window->show();
            wl_display_flush(display);
            QTimer::singleShot(5000, &app, [this] {
                if (!finished) {
                    finished = true;
                    phase("qt-show: still alive after 5 s; no protocol error observed (unexpected)");
                    app.quit();
                }
            });
            return;
        }

        wl_surface_commit(surface);
        wl_display_flush(display);
        initialCommitSent = true;
        phase("initial commit sent with NO buffer attached; waiting for configure");

        QTimer::singleShot(5000, &app, [this] {
            if (!configureSeen && !finished) {
                finished = true;
                phase("ERROR: no layer_surface.configure within 5 s");
                app.exit(3);
            }
        });
    }

    bool createShmBuffer(int w, int h)
    {
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
        }
        if (pixels) {
            munmap(pixels, pixelsSize);
            pixels = nullptr;
            pixelsSize = 0;
        }

        const int stride = w * 4;
        const size_t size = static_cast<size_t>(stride) * static_cast<size_t>(h);

        const int fd = memfd_create("linotch-spike", MFD_CLOEXEC);
        if (fd < 0) {
            phase("ERROR: memfd_create: %s", std::strerror(errno));
            return false;
        }
        if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
            phase("ERROR: ftruncate: %s", std::strerror(errno));
            close(fd);
            return false;
        }

        void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            phase("ERROR: mmap: %s", std::strerror(errno));
            close(fd);
            return false;
        }

        std::fill_n(static_cast<uint32_t *>(p), static_cast<size_t>(w) * static_cast<size_t>(h), 0xFF000000u);

        wl_shm_pool *pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
        buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);

        if (!buffer) {
            phase("ERROR: wl_shm_pool_create_buffer returned null");
            munmap(p, size);
            return false;
        }

        pixels = p;
        pixelsSize = size;
        phase("shm buffer ready: %dx%d stride=%d size=%zu format=XRGB8888", w, h, stride, size);
        return true;
    }

    void requestFrameProof()
    {
        frameCallback = wl_surface_frame(surface);
        if (frameCallback)
            wl_callback_add_listener(frameCallback, &frameListener, this);
        QTimer::singleShot(3000, &app, [this] {
            if (!framePresented && mapped && !finished)
                phase("note: no frame callback within 3 s (surface committed, presentation unconfirmed)");
        });
    }

    // The same content as qml/Pill.qml and qml/Panel.qml, painted by hand, so
    // that the two paint paths are compared on equal work. Spike 0's 21.6 MB
    // was a blank rect, and a blank rect is not the product.
    void drawProductContent(QPainter &p)
    {
        const bool panel = height > 100;
        p.setRenderHint(QPainter::Antialiasing, true);
        // Clear first: everything outside the notch silhouette must be
        // transparent, and a fresh QBackingStore is uninitialised.
        p.fillRect(QRect(0, 0, width, height), Qt::transparent);

        if (opt.alphaProbe) {
            // Left half opaque black, right half 37 % black. On an ARGB surface
            // the right half reads as a tint over whatever is behind the notch;
            // on XRGB the two halves are indistinguishable. Needed because
            // QWindow::format() reports alpha_buffer=-1 on every path measured
            // here, so the API cannot answer the transparency question.
            p.fillRect(QRect(0, 0, width / 2, height), QColor(0, 0, 0, 255));
            p.fillRect(QRect(width / 2, 0, width - width / 2, height), QColor(0, 0, 0, 96));
            return;
        }

        // The notch silhouette: square where it meets the bezel, ~18 px radius
        // on the two bottom corners. Three lines here; Qt Quick 6.4 has no
        // per-corner radius property at all.
        QPainterPath shape;
        const int r = 18;
        shape.moveTo(0, 0);
        shape.lineTo(width, 0);
        shape.lineTo(width, height - r);
        shape.quadTo(width, height, width - r, height);
        shape.lineTo(r, height);
        shape.quadTo(0, height, 0, height - r);
        shape.closeSubpath();
        p.fillPath(shape, panel ? QColor("#ff0a0a0c") : QColor("#ff000000"));
        if (opt.plain)
            return; // isolates the cost of text, glyphs and paths from the cost of the surface itself

        QFont font(QStringLiteral("Sans Serif"));
        const auto drawText = [&](const QRect &rect, const QString &text, int px, const QColor &color,
                                  QFont::Weight weight = QFont::Normal, Qt::Alignment align = Qt::AlignVCenter) {
            font.setPixelSize(px);
            font.setWeight(weight);
            p.setFont(font);
            p.setPen(color);
            p.drawText(rect, Qt::TextSingleLine | int(align), text);
        };

        if (!panel) {
            drawText(QRect(14, 0, 48, height), QStringLiteral("14:32"), 15, QColor("#f2ffffff"), QFont::DemiBold);
            p.fillRect(QRect(70, 11, 1, 14), QColor("#2affffff"));
            for (int i = 0; i < 4; ++i) {
                const int h = 4 + ((animTick + i * 3) % 11);
                p.fillRect(QRect(84 + i * 5, (height - h) / 2, 3, h), QColor("#ff4ad9a0"));
            }
            p.setBrush(QColor("#ff5b9dff"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(112, 15, 6, 6));
            drawText(QRect(130, 0, 60, height), QStringLiteral("87%"), 13, QColor("#d9ffffff"));
            return;
        }

        // Panel: header, now-playing, four meters, five tiles, twelve rows.
        drawText(QRect(14, 8, 260, 18), QStringLiteral("Tuesday, 3 November"), 12, QColor("#b3ffffff"));
        p.setBrush(QColor("#1affffff"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(382, 8, 22, 22));

        p.setBrush(QColor("#ff2b3a55"));
        p.drawRoundedRect(QRectF(14, 34, 44, 44), 8, 8);
        drawText(QRect(70, 34, 240, 18), QStringLiteral("Anemosyne"), 14, QColor("#f2ffffff"));
        drawText(QRect(70, 52, 240, 16), QStringLiteral("Julianna Barwic \xe2\x80\x94 Nepenthe"), 11,
                 QColor("#99ffffff"));
        p.setBrush(QColor("#33ffffff"));
        p.drawRoundedRect(QRectF(70, 72, 240, 3), 1, 1);
        p.setBrush(QColor("#ff4ad9a0"));
        p.drawRoundedRect(QRectF(70, 72, 100, 3), 1, 1);
        drawText(QRect(330, 40, 70, 30), QStringLiteral("\xe2\x8f\xae \xe2\x96\xb6 \xe2\x8f\xad"), 15,
                 QColor("#e6ffffff"));

        static const char *meterLabels[] = {"BAT 87%", "CPU", "RAM", "NET"};
        for (int i = 0; i < 4; ++i) {
            const int x = 14 + i * 100;
            drawText(QRect(x, 92, 92, 14), QString::fromLatin1(meterLabels[i]), 10, QColor("#8cffffff"));
            p.setBrush(QColor("#26ffffff"));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(x, 108, 92, 4), 2, 2);
            const int frac = 20 + ((animTick + i * 7) % 65);
            p.setBrush(QColor(i == 1 ? "#ffffc466" : "#ff5b9dff"));
            p.drawRoundedRect(QRectF(x, 108, 92 * frac / 100, 4), 2, 2);
        }

        static const char *tileLabels[] = {"\xf0\x9f\x94\x8a 42%", "\xe2\x98\x80 70%", "\xe2\x98\xbe DND",
                                           "\xe2\x8f\x9e wi", "\xe2\x9c\xa6 bt"};
        // Square tiles: 420 - 2*14 margins - 4*8 gaps = 352 over 5 tiles = 72.
        const int tileEdge = (width - 28 - 4 * 8) / 5;
        for (int i = 0; i < 5; ++i) {
            const int x = 14 + i * (tileEdge + 8);
            p.setBrush(QColor("#17ffffff"));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(x, 122, tileEdge, tileEdge), 10, 10);
            drawText(QRect(x, 122, tileEdge, tileEdge), QString::fromUtf8(tileLabels[i]), 11, QColor("#e6ffffff"),
                     QFont::Normal, Qt::AlignCenter);
        }

        const int listTop = 122 + tileEdge + 6;
        p.setBrush(QColor("#0dffffff"));
        p.drawRoundedRect(QRectF(14, listTop, width - 28, height - listTop - 14), 10, 10);
        for (int i = 0; i < 12; ++i) {
            const int y = listTop + 8 + i * 24;
            if (y + 22 > height - 14)
                break;
            p.setBrush(QColor("#33ffffff"));
            p.drawRoundedRect(QRectF(22, y, 16, 16), 4, 4);
            drawText(QRect(46, y - 2, width - 74, 12), QStringLiteral("Build finished"), 11, QColor("#e6ffffff"));
            drawText(QRect(46, y + 9, width - 74, 11),
                     QStringLiteral("spike0/layer-shell \xc2\xb7 notification body text %1").arg(i), 10,
                     QColor("#8cffffff"));
        }
    }

    void repaintContent(const QRegion &region)
    {
        backingStore->beginPaint(region);
        QPainter painter(backingStore->paintDevice());
        drawProductContent(painter);
        painter.end();
        backingStore->endPaint();
        backingStore->flush(region);
    }

    void paintWithQt()
    {
        phase("qt-bypass: resizing window to %dx%d, show(), then QBackingStore paint", width, height);
        window->resize(width, height);
        window->show();

        backingStore = std::make_unique<QBackingStore>(window.get());
        backingStore->resize(QSize(width, height));

        requestFrameProof();
        repaintContent(QRegion(0, 0, width, height));
        // The decisive transparency question is what buffer Qt actually
        // allocated, not what QWindow claims it requested: alphaBufferSize()
        // reads -1 on every path measured here. The raster backing store's paint
        // device is a QImage, and its format is the format of the shm buffer.
        const auto *store = dynamic_cast<const QImage *>(backingStore->paintDevice());
        phase("qt-bypass: flush issued (Qt owns the buffer); content=%s alpha_buffer=%d "
              "format_hasAlpha=%d store_format=%d store_hasAlpha=%d",
              height > 100 ? "Panel" : "Pill", window->format().alphaBufferSize(),
              window->format().hasAlpha() ? 1 : 0, store ? int(store->format()) : -1,
              store && store->hasAlphaChannel() ? 1 : 0);
        logLoadedGlStack();
        logSelfMetrics();
        onMapped("Qt QBackingStore buffer committed");

        if (opt.animate) {
            auto *ticker = new QTimer(&app);
            QObject::connect(ticker, &QTimer::timeout, &app, [this] {
                ++animTick;
                // Default: full-surface repaint, comparable with the Qt Quick
                // scene graph, which cannot damage a sub-region. With --region,
                // only the equaliser column is repainted - the thing a real
                // implementation would do, and a cost Quick cannot match.
                const QRegion dirty = (opt.region && height <= 100) ? QRegion(80, 0, 30, height)
                                                                    : QRegion(0, 0, width, height);
                repaintContent(dirty);
                if (++bypassFrames == 60)
                    logSelfMetrics();
            });
            ticker->start(16);
        }
    }

    void logLoadedGlStack()
    {
        static const char *driverNeedles[] = {"libEGL", "libwayland-egl", "libOpenGL", "libgallium",
                                              "libnvidia", "libGLX", "libdrm", "/dri/", "swrast"};
        static const char *quickNeedles[] = {"libQt6Quick", "libQt6Qml", "QtQuick/libqtquick2"};

        std::string drivers;
        std::string quick;
        size_t mappedLines = 0;
        FILE *f = std::fopen("/proc/self/maps", "r");
        if (!f) {
            phase("note: /proc/self/maps unreadable");
            return;
        }
        const auto appendBase = [](std::string &hay, const char *line) {
            const char *slash = std::strrchr(line, '/');
            std::string name = slash ? slash + 1 : line;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' '))
                name.pop_back();
            if (hay.find(name) == std::string::npos) {
                hay += hay.empty() ? "" : ", ";
                hay += name;
            }
        };

        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            ++mappedLines;
            for (const char *needle : driverNeedles) {
                if (std::strstr(line, needle))
                    appendBase(drivers, line);
            }
            for (const char *needle : quickNeedles) {
                if (std::strstr(line, needle))
                    appendBase(quick, line);
            }
        }
        std::fclose(f);

        phase("gl-stack: %s", drivers.empty() ? "NONE (no EGL/GL driver mapped; Qt Quick is software-rendering)"
                                                : drivers.c_str());
        phase("quick-stack: %s over %zu mapped lines", quick.empty() ? "not mapped" : quick.c_str(), mappedLines);
    }

#ifdef SPIKE_HAS_QUICK
    void paintWithQuick()
    {
        auto *view = static_cast<QQuickView *>(window.get());
        view->setColor(Qt::transparent);
        view->rootContext()->setContextProperty("spikeAnimate", opt.animate ? 1 : 0);

        const QString source = QString::fromLocal8Bit(SPIKE_QML_DIR "/") + QString::fromStdString(opt.qml);
        phase("qt-quick: loading %s", qPrintable(source));
        view->setSource(QUrl::fromLocalFile(source));
        if (view->status() == QQuickView::Error) {
            const QString err = view->errors().isEmpty() ? QStringLiteral("<no error reported>")
                                                         : view->errors().first().toString();
            phase("ERROR: QQuickView failed to load the QML tree: %s", qPrintable(err));
            finished = true;
            app.exit(5);
            return;
        }

        QObject::connect(view, &QQuickWindow::frameSwapped, &app, [this] {
            ++quickFrames;
            if (quickFrames == 1) {
                phase("qt-quick: first frameSwapped at %lld ms", elapsedMs());
                logLoadedGlStack();
                logSelfMetrics();
                onMapped("QQuickWindow frameSwapped");
            } else if (quickFrames == 60) {
                phase("qt-quick: 60 frames swapped; anim=%d size=%dx%d", opt.animate ? 1 : 0, width, height);
                logSelfMetrics();
                logLoadedGlStack();
            }
        });

        requestFrameProof();
        view->resize(width, height);
        view->show();
        phase("qt-quick: show() issued after configure ack; alpha_buffer=%d hasAlpha=%d",
              view->format().alphaBufferSize(), view->format().hasAlpha() ? 1 : 0);

        QTimer::singleShot(5000, &app, [this] {
            if (quickFrames == 0 && !finished)
                phase("note: qt-quick produced no frameSwapped within 5 s (surface committed, rendering unconfirmed)");
        });
    }
#endif // SPIKE_HAS_QUICK

    void onMapped(const char *how)
    {
        if (mapped)
            return;
        mapped = true;
        phase("mapped=yes (%s); surface=%p size=%dx%d", how, static_cast<void *>(surface), width, height);

        if (opt.durationSec > 0) {
            phase("running for %d s from map; samples taken by scripts/measure-oq2.sh", opt.durationSec);
            QTimer::singleShot(opt.durationSec * 1000, &app, [this] {
                if (!finished) {
                    finished = true;
                    phase("duration elapsed; quitting");
                    app.quit();
                }
            });
        } else {
            phase("duration=0: running until terminated");
        }
    }

    void cleanup()
    {
        if (frameCallback) {
            wl_callback_destroy(frameCallback);
            frameCallback = nullptr;
        }
        if (layerSurface) {
            zwlr_layer_surface_v1_destroy(layerSurface);
            layerSurface = nullptr;
        }
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
        }
        if (pixels) {
            munmap(pixels, pixelsSize);
            pixels = nullptr;
            pixelsSize = 0;
        }
        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        if (shm) {
            wl_shm_destroy(shm);
            shm = nullptr;
        }
        if (layerShell) {
            zwlr_layer_shell_v1_destroy(layerShell);
            layerShell = nullptr;
        }
        if (display)
            wl_display_flush(display);

        backingStore.reset();
        window.reset();
    }

    void logSelfMetrics()
    {
        long rss = -1;
        long pss = -1;
        if (FILE *f = std::fopen("/proc/self/smaps_rollup", "r")) {
            char line[256];
            while (std::fgets(line, sizeof line, f)) {
                long v = 0;
                if (std::sscanf(line, "Rss: %ld kB", &v) == 1) {
                    rss = v;
                } else if (std::sscanf(line, "Pss: %ld kB", &v) == 1) {
                    pss = v;
                }
            }
            std::fclose(f);
        }
        phase("final self metrics: rss_kb=%ld pss_kb=%ld", rss, pss);
    }

    static void handleGlobal(void *data, wl_registry *reg, uint32_t name, const char *interface, uint32_t version)
    {
        auto *s = static_cast<Spike *>(data);
        if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            s->shm = static_cast<wl_shm *>(wl_registry_bind(reg, name, &wl_shm_interface, std::min(version, 1u)));
            phase("global wl_shm v%u (bound v1)", version);
        } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
            s->boundLayerShellVersion = std::min(version, 5u);
            s->layerShell = static_cast<zwlr_layer_shell_v1 *>(
                wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, s->boundLayerShellVersion));
            phase("global zwlr_layer_shell_v1 v%u (bound v%u)", version, s->boundLayerShellVersion);
        }
        s->maybeCreateLayerSurface();
    }

    static void handleGlobalRemove(void *, wl_registry *, uint32_t) {}

    static void handleConfigure(void *data, zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h)
    {
        auto *s = static_cast<Spike *>(data);
        s->configureSeen = true;

        const int nw = w ? static_cast<int>(w) : s->width;
        const int nh = h ? static_cast<int>(h) : s->height;
        s->width = nw;
        s->height = nh;

        zwlr_layer_surface_v1_ack_configure(ls, serial);
        phase("event configure: serial=%u requested=%ux%u using=%dx%d (acked)", serial, w, h, nw, nh);

        if (s->opt.mode == "manual") {
            if (!s->createShmBuffer(nw, nh)) {
                s->finished = true;
                s->app.exit(4);
                return;
            }
            s->requestFrameProof();
            wl_surface_attach(s->surface, s->buffer, 0, 0);
            if (wl_proxy_get_version(reinterpret_cast<wl_proxy *>(s->surface)) >= 4)
                wl_surface_damage_buffer(s->surface, 0, 0, nw, nh);
            else
                wl_surface_damage(s->surface, 0, 0, nw, nh);
            wl_surface_commit(s->surface);
            wl_display_flush(s->display);
            s->onMapped("manual wl_shm buffer attached and committed");
        } else if (s->opt.mode == "qt-bypass") {
            QTimer::singleShot(0, &s->app, [s] { s->paintWithQt(); });
#ifdef SPIKE_HAS_QUICK
        } else if (s->opt.mode == "qt-quick") {
            QTimer::singleShot(0, &s->app, [s] { s->paintWithQuick(); });
#endif
        }
    }

    static void handleClosed(void *data, zwlr_layer_surface_v1 *)
    {
        auto *s = static_cast<Spike *>(data);
        phase("event closed: compositor asked the layer surface to close");
        if (!s->finished) {
            s->finished = true;
            s->app.quit();
        }
    }

    static void handleFrameDone(void *data, wl_callback *cb, uint32_t time)
    {
        auto *s = static_cast<Spike *>(data);
        s->framePresented = true;
        phase("event frame done (time=%u): compositor presented the surface", time);
        wl_callback_destroy(cb);
        if (s->frameCallback == cb)
            s->frameCallback = nullptr;
    }

    inline static const wl_registry_listener registryListener = {
        &Spike::handleGlobal,
        &Spike::handleGlobalRemove,
    };

    inline static const zwlr_layer_surface_v1_listener layerSurfaceListener = {
        &Spike::handleConfigure,
        &Spike::handleClosed,
    };

    inline static const wl_callback_listener frameListener = {
        &Spike::handleFrameDone,
    };
};

} // namespace

int main(int argc, char **argv)
{
    g_startTime = SteadyClock::now();

    const Options opt = parseOptions(argc, argv);
    if (opt.mode == "qt-bypass" || opt.mode == "qt-quick") {
        qputenv("QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT", "1");
        if (opt.mode == "qt-bypass")
            phase("mode=qt-bypass: set QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1 before QGuiApplication");
    }
    if (opt.mode != "manual" && opt.mode != "qt-bypass" && opt.mode != "qt-show" && opt.mode != "qt-quick") {
        phase("ERROR: unknown --mode=%s (manual|qt-bypass|qt-show|qt-quick)", opt.mode.c_str());
        return 2;
    }

    if (opt.alpha && (opt.mode == "qt-bypass" || opt.mode == "qt-quick")) {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setAlphaBufferSize(8);
        QSurfaceFormat::setDefaultFormat(fmt);
        phase("QSurfaceFormat::setDefaultFormat(alpha=8) applied before QGuiApplication");
    }

    QGuiApplication app(argc, argv);

#ifdef SPIKE_HAS_QUICK
    if (opt.mode == "qt-quick") {
        // Deliberately not set in code: the whole point of Spike 0b is which
        // combination of these two knobs is affordable, so the runner owns them.
        phase("qt-quick env: QT_QUICK_BACKEND=%s QT_WAYLAND_DISABLE_WINDOWDECORATION=%s",
              qEnvironmentVariable("QT_QUICK_BACKEND", QStringLiteral("<unset>")).toUtf8().constData(),
              qEnvironmentVariable("QT_WAYLAND_DISABLE_WINDOWDECORATION", QStringLiteral("<unset>")).toUtf8().constData());
    }
#else
    if (opt.mode == "qt-quick") {
        phase("ERROR: this binary was built without Qt Quick; use linotch-spike-quick");
        return 2;
    }
#endif

    Spike spike(opt, app);

    if (!spike.start())
        return 1;

    const int rc = app.exec();
    spike.cleanup();
    spike.logSelfMetrics();
    phase("exit rc=%d", rc);
    return rc;
}
