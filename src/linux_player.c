#define _GNU_SOURCE

#include "player.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <glib-unix.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef const char *(*gdk_monitor_get_connector_fn)(GdkMonitor *monitor);

typedef struct {
    char *video_path;
    char *monitor_selector;
    gboolean verbose;
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *media_widget;
    GtkMediaStream *stream;
} AppState;

static void log_line(AppState *state, const char *level, const char *message) {
    if (!state->verbose && strcmp(level, "ERROR") != 0) {
        return;
    }
    g_printerr("[%s] %s\n", level, message);
}

static void log_fmt(AppState *state, const char *level, const char *fmt, const char *value) {
    if (!state->verbose && strcmp(level, "ERROR") != 0) {
        return;
    }
    g_printerr("[%s] ", level);
    g_printerr(fmt, value);
    g_printerr("\n");
}

static bool parse_monitor_index(const char *selector, guint *out_index) {
    if (selector == NULL || selector[0] == '\0') {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(selector, &end, 10);
    if (end == selector || *end != '\0') {
        return false;
    }

    *out_index = (guint)parsed;
    return true;
}

static const char *safe_model(GdkMonitor *monitor) {
    const char *model = gdk_monitor_get_model(monitor);
    return model ? model : "unknown";
}

static const char *safe_manufacturer(GdkMonitor *monitor) {
    const char *m = gdk_monitor_get_manufacturer(monitor);
    return m ? m : "unknown";
}

static GdkMonitor *select_monitor(AppState *state) {
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) {
        log_line(state, "ERROR", "No GDK display available (expected Wayland session).");
        return NULL;
    }

    GListModel *monitors = gdk_display_get_monitors(display);
    guint count = g_list_model_get_n_items(monitors);
    if (count == 0) {
        log_line(state, "ERROR", "No monitors were reported by GDK.");
        return NULL;
    }

    if (state->monitor_selector == NULL || strcmp(state->monitor_selector, "auto") == 0) {
        GdkMonitor *auto_picked = g_list_model_get_item(monitors, 0);
        if (state->verbose && auto_picked != NULL) {
            log_fmt(state, "INFO", "Using auto monitor: %s", safe_model(auto_picked));
        }
        return auto_picked;
    }

    guint requested_index = 0;
    if (parse_monitor_index(state->monitor_selector, &requested_index)) {
        if (requested_index < count) {
            GdkMonitor *picked = g_list_model_get_item(monitors, requested_index);
            if (state->verbose) {
                log_fmt(state, "INFO", "Using monitor index: %s", state->monitor_selector);
            }
            return picked;
        }

        log_fmt(state, "WARN", "Monitor index not found, falling back to auto: %s", state->monitor_selector);
    } else {
        gdk_monitor_get_connector_fn connector_fn =
            (gdk_monitor_get_connector_fn)dlsym(RTLD_DEFAULT, "gdk_monitor_get_connector");

        for (guint i = 0; i < count; i++) {
            GdkMonitor *monitor = g_list_model_get_item(monitors, i);
            if (monitor == NULL) {
                continue;
            }

            bool matched = false;
            const char *connector = NULL;

            if (connector_fn != NULL) {
                connector = connector_fn(monitor);
                if (connector != NULL && strcmp(connector, state->monitor_selector) == 0) {
                    matched = true;
                }
            }

            if (!matched) {
                const char *model = safe_model(monitor);
                const char *manufacturer = safe_manufacturer(monitor);
                if (strcmp(model, state->monitor_selector) == 0 || strcmp(manufacturer, state->monitor_selector) == 0) {
                    matched = true;
                }
            }

            if (matched) {
                if (state->verbose) {
                    if (connector != NULL) {
                        log_fmt(state, "INFO", "Matched monitor connector: %s", connector);
                    } else {
                        log_fmt(state, "INFO", "Matched monitor selector: %s", state->monitor_selector);
                    }
                }
                return monitor;
            }

            g_object_unref(monitor);
        }

        log_fmt(state, "WARN", "Monitor selector not found, falling back to auto: %s", state->monitor_selector);
    }

    log_line(state, "WARN", "Using monitor index 0 as fallback.");
    return g_list_model_get_item(monitors, 0);
}

static void on_stream_error_notify(GtkMediaStream *stream, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = (AppState *)user_data;
    const GError *err = gtk_media_stream_get_error(stream);
    if (err == NULL) {
        return;
    }

    if (state->verbose) {
        g_printerr("[ERROR] Media stream error: %s\n", err->message);
    } else {
        g_printerr("[ERROR] Media stream error\n");
    }
}

static void restart_stream_if_needed(GtkMediaStream *stream, AppState *state) {
    gboolean ended = FALSE;
    g_object_get(G_OBJECT(stream), "ended", &ended, NULL);

    if (ended) {
        if (state->verbose) {
            log_line(state, "INFO", "Stream reached end, seeking to start.");
        }
        gtk_media_stream_seek(stream, 0);
    }

    gtk_media_stream_play(stream);
}

static void on_stream_ended_notify(GtkMediaStream *stream, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = (AppState *)user_data;
    restart_stream_if_needed(stream, state);
}

static gboolean playback_watchdog(gpointer user_data) {
    AppState *state = (AppState *)user_data;
    if (state == NULL || state->stream == NULL) {
        return G_SOURCE_CONTINUE;
    }

    const GError *err = gtk_media_stream_get_error(state->stream);
    if (err != NULL) {
        return G_SOURCE_CONTINUE;
    }

    restart_stream_if_needed(state->stream, state);
    return G_SOURCE_CONTINUE;
}

static gboolean on_unix_signal(gpointer user_data) {
    AppState *state = (AppState *)user_data;
    if (state != NULL && state->app != NULL) {
        log_line(state, "INFO", "Signal received. Quitting wallpaper-runner.");
        g_application_quit(G_APPLICATION(state->app));
    }
    return G_SOURCE_CONTINUE;
}

static void on_shutdown(GApplication *application, gpointer user_data) {
    (void)application;
    AppState *state = (AppState *)user_data;
    if (state == NULL || state->stream == NULL) {
        return;
    }

    gtk_media_stream_pause(state->stream);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    if (!gtk_layer_is_supported()) {
        log_line(state, "ERROR", "gtk-layer-shell is not supported in this compositor/session.");
        g_application_quit(G_APPLICATION(app));
        return;
    }

    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "wallpaper-runner");
    gtk_window_set_decorated(GTK_WINDOW(state->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);

    gtk_layer_init_for_window(GTK_WINDOW(state->window));
    gtk_layer_set_namespace(GTK_WINDOW(state->window), "wallpaper-runner");
    gtk_layer_set_layer(GTK_WINDOW(state->window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
    gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(state->window), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(state->window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    GdkMonitor *monitor = select_monitor(state);
    if (monitor != NULL) {
        gtk_layer_set_monitor(GTK_WINDOW(state->window), monitor);
        g_object_unref(monitor);
    }

    /*
     * Use GtkPicture + GtkMediaFile to render frames without GtkVideo controls.
     * GtkVideo may surface a bottom transport bar on pointer interaction.
     */
    state->stream = GTK_MEDIA_STREAM(gtk_media_file_new_for_filename(state->video_path));
    gtk_media_stream_set_muted(state->stream, TRUE);
    g_signal_connect(state->stream, "notify::error", G_CALLBACK(on_stream_error_notify), state);
    g_signal_connect(state->stream, "notify::ended", G_CALLBACK(on_stream_ended_notify), state);

    state->media_widget = gtk_picture_new_for_paintable(GDK_PAINTABLE(state->stream));
    gtk_picture_set_can_shrink(GTK_PICTURE(state->media_widget), TRUE);
    gtk_picture_set_keep_aspect_ratio(GTK_PICTURE(state->media_widget), FALSE);
    gtk_widget_set_hexpand(state->media_widget, TRUE);
    gtk_widget_set_vexpand(state->media_widget, TRUE);
    gtk_window_set_child(GTK_WINDOW(state->window), state->media_widget);
    gtk_window_present(GTK_WINDOW(state->window));
    gtk_media_stream_play(state->stream);

    g_timeout_add_seconds(2, playback_watchdog, state);

    log_line(state, "INFO", "Wallpaper player started.");
}

int run_wallpaper_player(const char *video_path, const char *monitor_selector, int verbose) {
    if (video_path == NULL || video_path[0] == '\0') {
        g_printerr("[ERROR] --video is required\n");
        return 2;
    }

    if (!g_file_test(video_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
        g_printerr("[ERROR] video does not exist: %s\n", video_path);
        return 2;
    }

    if (g_getenv("GDK_BACKEND") == NULL) {
        g_setenv("GDK_BACKEND", "wayland", FALSE);
    }

    AppState state = {0};
    state.video_path = g_strdup(video_path);
    state.monitor_selector = g_strdup((monitor_selector == NULL || monitor_selector[0] == '\0') ? "auto" : monitor_selector);
    state.verbose = verbose != 0;

    state.app = gtk_application_new("com.boss.wallpaper_runner", G_APPLICATION_NON_UNIQUE);

    g_signal_connect(state.app, "activate", G_CALLBACK(on_activate), &state);
    g_signal_connect(state.app, "shutdown", G_CALLBACK(on_shutdown), &state);

    g_unix_signal_add(SIGINT, on_unix_signal, &state);
    g_unix_signal_add(SIGTERM, on_unix_signal, &state);

    int status = g_application_run(G_APPLICATION(state.app), 0, NULL);

    g_object_unref(state.app);
    g_free(state.monitor_selector);
    g_free(state.video_path);

    return status;
}
