#include "plugin.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define namespace _namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

namespace {

struct App {
    struct wl_display* display = nullptr;
    struct wl_registry* registry = nullptr;
    struct wl_compositor* compositor = nullptr;
    struct zwlr_layer_shell_v1* layer_shell = nullptr;
    struct wl_surface* surface = nullptr;
    struct zwlr_layer_surface_v1* layer_surface = nullptr;
    struct wl_egl_window* egl_window = nullptr;
    struct wl_callback* frame_callback = nullptr;

    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLConfig egl_config = nullptr;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;

    int width = 0;
    int height = 0;
    bool configured = false;
    bool running = true;

    float master_opacity = 1.0f;
    std::unique_ptr<Plugin> plugin;
    std::chrono::steady_clock::time_point last_time;
};

static App* g_app = nullptr;

void sig_handler(int sig) {
    (void)sig;
    if (g_app) {
        g_app->running = false;
    }
}

bool init_egl(App* app) {
    app->egl_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(app->display));
    if (app->egl_display == EGL_NO_DISPLAY) {
        std::cerr << "Error: eglGetDisplay failed.\n";
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(app->egl_display, &major, &minor)) {
        std::cerr << "Error: eglInitialize failed.\n";
        return false;
    }

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig configs[32];
    EGLint count = 0;
    if (!eglChooseConfig(app->egl_display, attribs, configs, 32, &count) || count == 0) {
        std::cerr << "Error: No matching EGLConfig with 8-bit alpha found.\n";
        return false;
    }
    app->egl_config = configs[0];

    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    app->egl_context = eglCreateContext(app->egl_display, app->egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (app->egl_context == EGL_NO_CONTEXT) {
        std::cerr << "Error: Failed to create EGL context.\n";
        return false;
    }

    return true;
}

void render_frame(App* app);

void on_frame_done(void* data, struct wl_callback* callback, uint32_t time) {
    (void)time;
    auto* app = static_cast<App*>(data);
    if (callback) {
        wl_callback_destroy(callback);
    }
    app->frame_callback = nullptr;

    if (app->running) {
        render_frame(app);
    }
}

static const struct wl_callback_listener frame_listener = {
    .done = on_frame_done
};

void render_frame(App* app) {
    if (!app->configured || app->width <= 0 || app->height <= 0) return;

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - app->last_time).count();
    if (dt > 0.1f) dt = 0.016f;
    app->last_time = now;

    eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface, app->egl_context);

    // Call current plugin render pass
    if (app->plugin) {
        app->plugin->render(dt, app->master_opacity);
    }

    // Set up frame callback for next vsync tick BEFORE swap buffers
    app->frame_callback = wl_surface_frame(app->surface);
    wl_callback_add_listener(app->frame_callback, &frame_listener, app);

    // Swap buffers (commits the Wayland buffer)
    eglSwapBuffers(app->egl_display, app->egl_surface);
}

void layer_surface_configure(void* data, struct zwlr_layer_surface_v1* layer_surface,
                             uint32_t serial, uint32_t width, uint32_t height) {
    auto* app = static_cast<App*>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (width == 0 || height == 0) {
        width = app->width > 0 ? app->width : 1920;
        height = app->height > 0 ? app->height : 1080;
    }

    app->width = width;
    app->height = height;

    if (!app->egl_window) {
        app->egl_window = wl_egl_window_create(app->surface, app->width, app->height);
        if (!app->egl_window) {
            std::cerr << "Error: Failed to create wl_egl_window.\n";
            app->running = false;
            return;
        }
        app->egl_surface = eglCreateWindowSurface(app->egl_display, app->egl_config,
                                                  reinterpret_cast<EGLNativeWindowType>(app->egl_window), nullptr);
        if (app->egl_surface == EGL_NO_SURFACE) {
            std::cerr << "Error: Failed to create EGL window surface.\n";
            app->running = false;
            return;
        }
    } else {
        wl_egl_window_resize(app->egl_window, app->width, app->height, 0, 0);
    }

    eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface, app->egl_context);

    // 100% CLICK-THROUGH: Set input region to EMPTY so no mouse clicks are ever intercepted
    struct wl_region* empty_region = wl_compositor_create_region(app->compositor);
    wl_surface_set_input_region(app->surface, empty_region);
    wl_region_destroy(empty_region);

    if (!app->configured) {
        app->configured = true;
        app->last_time = std::chrono::steady_clock::now();
        if (app->plugin) {
            app->plugin->init(app->width, app->height);
        }
        render_frame(app);
    } else {
        if (app->plugin) {
            app->plugin->resize(app->width, app->height);
        }
    }
}

void layer_surface_closed(void* data, struct zwlr_layer_surface_v1* layer_surface) {
    (void)layer_surface;
    auto* app = static_cast<App*>(data);
    std::cout << "Layer surface closed by compositor.\n";
    app->running = false;
}

const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed
};

void registry_global(void* data, struct wl_registry* registry,
                     uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<App*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<struct wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layer_shell = static_cast<struct zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1));
    }
    (void)version;
}

void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove
};

void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] [PLUGIN]\n\n"
              << "VanGoghFlow Wayland: Lightweight GPU-Accelerated Transparent Desktop Visualizer\n\n"
              << "Options:\n"
              << "  -l, --list             List all compiled-in visualizer plugins and exit\n"
              << "  -p, --plugin <name>    Run the specified plugin (default: 'starfield')\n"
              << "  -o, --opacity <0.0-1>  Set master opacity multiplier (default: 1.0)\n"
              << "  -h, --help             Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog << "                 # Run default starfield\n"
              << "  " << prog << " --list          # Show all available plugins\n"
              << "  " << prog << " matrix          # Run Matrix rain plugin\n"
              << "  " << prog << " aurora -o 0.75  # Run Aurora with 75% opacity\n";
}

void print_plugin_list() {
    std::cout << "\n=========================================\n"
              << " VanGoghFlow Wayland Compiled-in Plugins\n"
              << "=========================================\n\n";

    auto list = PluginRegistry::instance().list();
    if (list.empty()) {
        std::cout << "No plugins registered.\n";
        return;
    }

    for (const auto& p : list) {
        std::cout << "  * " << std::left << std::setw(12) << p.name
                  << " - " << p.description << "\n";
    }
    std::cout << "\nDefault: 'starfield'\n\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string chosen_plugin = "starfield";
    float master_opacity = 1.0f;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "-l") {
            print_plugin_list();
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if ((arg == "--plugin" || arg == "-p") && i + 1 < argc) {
            chosen_plugin = argv[++i];
        } else if ((arg == "--opacity" || arg == "-o") && i + 1 < argc) {
            master_opacity = std::stof(argv[++i]);
            if (master_opacity < 0.0f) master_opacity = 0.0f;
            if (master_opacity > 1.0f) master_opacity = 1.0f;
        } else if (arg.rfind("-", 0) != 0) {
            chosen_plugin = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\nRun '" << argv[0] << " --help' for usage.\n";
            return 1;
        }
    }

    if (!PluginRegistry::instance().has(chosen_plugin)) {
        std::cerr << "Error: Unknown plugin '" << chosen_plugin << "'.\n";
        print_plugin_list();
        return 1;
    }

    App app;
    g_app = &app;
    app.master_opacity = master_opacity;
    app.plugin = PluginRegistry::instance().create(chosen_plugin);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Connect to Wayland compositor
    app.display = wl_display_connect(nullptr);
    if (!app.display) {
        std::cerr << "Error: Could not connect to Wayland display. Is WAYLAND_DISPLAY set?\n";
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor) {
        std::cerr << "Error: wl_compositor interface not supported by compositor.\n";
        return 1;
    }
    if (!app.layer_shell) {
        std::cerr << "Error: zwlr_layer_shell_v1 protocol not supported.\n"
                  << "Please run under a layer-shell compatible Wayland compositor (Wayfire, Sway, Labwc, Hyprland).\n";
        return 1;
    }

    if (!init_egl(&app)) {
        return 1;
    }

    // Create transparent overlay surface
    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) {
        std::cerr << "Error: Failed to create Wayland surface.\n";
        return 1;
    }

    // Layer OVERLAY puts visuals above all desktop windows
    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell,
        app.surface,
        nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "vangoghflow_overlay"
    );

    // Anchor to all 4 edges for full-screen coverage
    zwlr_layer_surface_v1_set_anchor(
        app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    );

    // Exclusive zone -1: never move or push away panels or windows
    zwlr_layer_surface_v1_set_exclusive_zone(app.layer_surface, -1);

    // Keyboard interactivity NONE: never intercept keyboard focus
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE
    );

    zwlr_layer_surface_v1_add_listener(app.layer_surface, &layer_surface_listener, &app);

    // Initial commit to trigger configure
    wl_surface_commit(app.surface);

    std::cout << "VanGoghFlow Wayland running plugin: [" << app.plugin->name() << "] - "
              << app.plugin->description() << "\n"
              << "Layer: OVERLAY (100% click-through transparent HUD)\n"
              << "Press Ctrl+C to exit.\n" << std::flush;

    // Main event loop
    while (app.running && wl_display_dispatch(app.display) != -1) {
        // Events are dispatched to callbacks
    }

    // Clean teardown
    std::cout << "\nShutting down VanGoghFlow Wayland...\n";

    if (app.plugin) {
        eglMakeCurrent(app.egl_display, app.egl_surface, app.egl_surface, app.egl_context);
        app.plugin->cleanup();
    }

    if (app.frame_callback) {
        wl_callback_destroy(app.frame_callback);
    }
    if (app.layer_surface) {
        zwlr_layer_surface_v1_destroy(app.layer_surface);
    }
    if (app.surface) {
        wl_surface_destroy(app.surface);
    }
    if (app.egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(app.egl_display, app.egl_surface);
    }
    if (app.egl_window) {
        wl_egl_window_destroy(app.egl_window);
    }
    if (app.egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(app.egl_display, app.egl_context);
    }
    if (app.egl_display != EGL_NO_DISPLAY) {
        eglTerminate(app.egl_display);
    }
    if (app.registry) {
        wl_registry_destroy(app.registry);
    }
    if (app.display) {
        wl_display_disconnect(app.display);
    }

    return 0;
}
