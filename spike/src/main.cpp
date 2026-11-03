// LiNotch Spike 0: prove the Qt 6.4 Wayland surface path for zwlr-layer-shell.
//
// Modes:
//   --mode=manual    (default) drive the Qt-created wl_surface with a wl_shm buffer
//   --mode=qt-bypass use Qt's own rendering (QBackingStore) with
//                    QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1 + BypassWindowManagerHint
//   --mode=qt-show   control: show() without bypass; expected wl_surface role conflict
//
// This is throwaway spike code, not LiNotch product code.

#include <QGuiApplication>
#include <QWindow>
#include <QBackingStore>
#include <QPainter>
#include <QTimer>
#include <QByteArray>
#include <qpa/qplatformnativeinterface.h>

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

    bool start()
    {
        phase("platform=%s", qPrintable(QGuiApplication::platformName()));
        if (QGuiApplication::platformName() != QLatin1String("wayland")) {
            phase("ERROR: not running on the wayland Qt platform plugin; Wayland-only spike");
            return false;
        }

        window = std::make_unique<QWindow>();
        window->setTitle(QStringLiteral("LiNotch Spike"));
        window->resize(width, height);
        if (opt.mode == "qt-bypass")
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

    void paintWithQt()
    {
        phase("qt-bypass: resizing window to %dx%d, show(), then QBackingStore paint", width, height);
        window->resize(width, height);
        window->show();

        backingStore = std::make_unique<QBackingStore>(window.get());
        backingStore->resize(QSize(width, height));

        const QRegion paintRegion(0, 0, width, height);
        backingStore->beginPaint(paintRegion);
        QPainter painter(backingStore->paintDevice());
        painter.fillRect(QRect(0, 0, width, height), QColor(0, 0, 0));
        painter.end();
        backingStore->endPaint();

        requestFrameProof();
        backingStore->flush(paintRegion);
        phase("qt-bypass: QBackingStore flush issued (Qt owns the buffer)");
        onMapped("Qt QBackingStore buffer committed");
    }

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
    if (opt.mode == "qt-bypass") {
        qputenv("QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT", "1");
        phase("mode=qt-bypass: set QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1 before QGuiApplication");
    }
    if (opt.mode != "manual" && opt.mode != "qt-bypass" && opt.mode != "qt-show") {
        phase("ERROR: unknown --mode=%s (manual|qt-bypass|qt-show)", opt.mode.c_str());
        return 2;
    }

    QGuiApplication app(argc, argv);
    Spike spike(opt, app);

    if (!spike.start())
        return 1;

    const int rc = app.exec();
    spike.cleanup();
    spike.logSelfMetrics();
    phase("exit rc=%d", rc);
    return rc;
}
