#include "bowser.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <string.h>

/* Opcoes copiadas para memoria propria: as strings originais vem do Go e
   nao sobrevivem ao retorno da chamada. */
static BowserOpts G;
static WebKitWebContext *g_ctx = NULL;
static WebKitUserContentManager *g_ucm = NULL;
static WebKitSettings *g_settings = NULL;
static int g_windows = 0;
static int g_exit_code = 0;

typedef struct {
    GtkWidget *window;
    GtkWidget *entry;
    GtkWidget *back;
    GtkWidget *forward;
    WebKitWebView *view;
    gboolean fullscreen;
    gboolean is_main;
    gboolean task_started;
} Browser;

static void task_begin(Browser *b);

/* ---------------------------------------------------------------- helpers */

static gboolean looks_like_host(const char *s)
{
    if (strchr(s, ' ') != NULL)
        return FALSE;
    if (g_str_has_prefix(s, "localhost") || g_str_has_prefix(s, "127.0.0.1") ||
        g_str_has_prefix(s, "0.0.0.0") || g_str_has_prefix(s, "[::1]"))
        return TRUE;
    return strchr(s, '.') != NULL;
}

/* Aceita URL completa, dominio sem esquema, caminho de arquivo ou termo de
   busca. Devolve string nova (g_free pelo chamador). */
static char *normalize_uri(const char *in)
{
    char *s, *uri;

    if (in == NULL || *in == '\0')
        return g_strdup("about:blank");

    s = g_strstrip(g_strdup(in));

    if (strstr(s, "://") != NULL || g_str_has_prefix(s, "about:") ||
        g_str_has_prefix(s, "data:") || g_str_has_prefix(s, "blob:"))
        return s;

    if (g_file_test(s, G_FILE_TEST_EXISTS)) {
        char *abs = g_canonicalize_filename(s, NULL);
        uri = g_filename_to_uri(abs, NULL, NULL);
        g_free(abs);
        if (uri != NULL) {
            g_free(s);
            return uri;
        }
    }

    if (looks_like_host(s)) {
        gboolean local = g_str_has_prefix(s, "localhost") ||
                         g_str_has_prefix(s, "127.0.0.1") ||
                         g_str_has_prefix(s, "0.0.0.0") ||
                         g_str_has_prefix(s, "[::1]");
        uri = g_strconcat(local ? "http://" : "https://", s, NULL);
        g_free(s);
        return uri;
    }

    {
        char *q = g_uri_escape_string(s, NULL, TRUE);
        uri = g_strconcat("https://duckduckgo.com/?q=", q, NULL);
        g_free(q);
        g_free(s);
        return uri;
    }
}

static void update_nav(Browser *b)
{
    if (b->back == NULL)
        return;
    gtk_widget_set_sensitive(b->back, webkit_web_view_can_go_back(b->view));
    gtk_widget_set_sensitive(b->forward, webkit_web_view_can_go_forward(b->view));
}

/* ---------------------------------------------------------------- signals */

static void on_title(GObject *obj, GParamSpec *spec, gpointer data)
{
    Browser *b = data;
    const char *title;

    (void)spec;
    if (G.title != NULL)
        return;

    title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(obj));
    gtk_window_set_title(GTK_WINDOW(b->window),
                         (title != NULL && *title != '\0') ? title : "bowser");
}

static void on_uri(GObject *obj, GParamSpec *spec, gpointer data)
{
    Browser *b = data;
    const char *uri;

    (void)spec;
    uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(obj));
    if (b->entry != NULL && uri != NULL && !gtk_widget_has_focus(b->entry))
        gtk_entry_set_text(GTK_ENTRY(b->entry), uri);
    update_nav(b);
}

static void on_progress(GObject *obj, GParamSpec *spec, gpointer data)
{
    Browser *b = data;
    gdouble p;

    (void)spec;
    if (b->entry == NULL)
        return;

    p = webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(obj));
    gtk_entry_set_progress_fraction(GTK_ENTRY(b->entry), p < 1.0 ? p : 0.0);
}

static void on_load_changed(WebKitWebView *view, WebKitLoadEvent ev, gpointer data)
{
    Browser *b = data;

    (void)view;
    update_nav(b);

    if (ev == WEBKIT_LOAD_FINISHED && b->is_main && !b->task_started)
        task_begin(b);
}

static gboolean on_load_failed(WebKitWebView *view, WebKitLoadEvent ev,
                               gchar *uri, GError *error, gpointer data)
{
    (void)view;
    (void)ev;
    (void)data;
    if (error == NULL || error->code != WEBKIT_NETWORK_ERROR_CANCELLED)
        fprintf(stderr, "bowser: falha ao carregar %s: %s\n", uri,
                error != NULL ? error->message : "erro desconhecido");
    return FALSE;
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    (void)w;
    g_free(data);
    if (--g_windows <= 0)
        gtk_main_quit();
}

static void on_entry_activate(GtkEntry *entry, gpointer data)
{
    Browser *b = data;
    char *uri = normalize_uri(gtk_entry_get_text(entry));

    webkit_web_view_load_uri(b->view, uri);
    g_free(uri);
    gtk_widget_grab_focus(GTK_WIDGET(b->view));
}

static void on_back(GtkButton *btn, gpointer data)
{
    (void)btn;
    webkit_web_view_go_back(((Browser *)data)->view);
}

static void on_forward(GtkButton *btn, gpointer data)
{
    (void)btn;
    webkit_web_view_go_forward(((Browser *)data)->view);
}

static void on_reload(GtkButton *btn, gpointer data)
{
    (void)btn;
    webkit_web_view_reload(((Browser *)data)->view);
}

static Browser *browser_new(WebKitWebView *related);

static GtkWidget *on_create(WebKitWebView *view, WebKitNavigationAction *action,
                            gpointer data)
{
    Browser *nb;

    (void)action;
    (void)data;
    nb = browser_new(view);
    return GTK_WIDGET(nb->view);
}

static void on_ready_to_show(WebKitWebView *view, gpointer data)
{
    (void)view;
    gtk_widget_show_all(((Browser *)data)->window);
}

static void on_close(WebKitWebView *view, gpointer data)
{
    (void)view;
    gtk_widget_destroy(((Browser *)data)->window);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
    Browser *b = data;
    gboolean ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (ev->state & GDK_SHIFT_MASK) != 0;
    gboolean alt = (ev->state & GDK_MOD1_MASK) != 0;
    guint key = gdk_keyval_to_lower(ev->keyval);

    (void)w;

    if (ctrl) {
        switch (key) {
        case GDK_KEY_l:
            if (b->entry != NULL) {
                gtk_widget_grab_focus(b->entry);
                gtk_editable_select_region(GTK_EDITABLE(b->entry), 0, -1);
            }
            return TRUE;
        case GDK_KEY_r:
            if (shift)
                webkit_web_view_reload_bypass_cache(b->view);
            else
                webkit_web_view_reload(b->view);
            return TRUE;
        case GDK_KEY_q:
            gtk_main_quit();
            return TRUE;
        case GDK_KEY_w:
            gtk_widget_destroy(b->window);
            return TRUE;
        case GDK_KEY_i:
            if (shift) {
                webkit_web_inspector_show(webkit_web_view_get_inspector(b->view));
                return TRUE;
            }
            break;
        case GDK_KEY_plus:
        case GDK_KEY_equal:
            webkit_web_view_set_zoom_level(b->view,
                webkit_web_view_get_zoom_level(b->view) + 0.1);
            return TRUE;
        case GDK_KEY_minus:
            webkit_web_view_set_zoom_level(b->view,
                MAX(0.2, webkit_web_view_get_zoom_level(b->view) - 0.1));
            return TRUE;
        case GDK_KEY_0:
            webkit_web_view_set_zoom_level(b->view, G.zoom > 0 ? G.zoom : 1.0);
            return TRUE;
        default:
            break;
        }
    }

    if (alt && key == GDK_KEY_Left) {
        webkit_web_view_go_back(b->view);
        return TRUE;
    }
    if (alt && key == GDK_KEY_Right) {
        webkit_web_view_go_forward(b->view);
        return TRUE;
    }

    switch (ev->keyval) {
    case GDK_KEY_F5:
        webkit_web_view_reload(b->view);
        return TRUE;
    case GDK_KEY_F12:
        webkit_web_inspector_show(webkit_web_view_get_inspector(b->view));
        return TRUE;
    case GDK_KEY_F11:
        b->fullscreen = !b->fullscreen;
        if (b->fullscreen)
            gtk_window_fullscreen(GTK_WINDOW(b->window));
        else
            gtk_window_unfullscreen(GTK_WINDOW(b->window));
        return TRUE;
    case GDK_KEY_Escape:
        if (b->entry != NULL && gtk_widget_has_focus(b->entry)) {
            gtk_widget_grab_focus(GTK_WIDGET(b->view));
            return TRUE;
        }
        webkit_web_view_stop_loading(b->view);
        return TRUE;
    default:
        break;
    }

    return FALSE;
}


/* -------------------------------------------------- tarefas para agentes */

static void task_quit(Browser *b)
{
    if (G.stay)
        return;
    (void)b;
    gtk_main_quit();
}

static void snapshot_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
    GError *err = NULL;
    cairo_surface_t *surface;

    surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(obj), res, &err);
    if (surface == NULL) {
        fprintf(stderr, "bowser: falha na captura: %s\n",
                err != NULL ? err->message : "erro desconhecido");
        g_clear_error(&err);
        g_exit_code = 1;
    } else {
        if (cairo_surface_write_to_png(surface, G.screenshot) != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "bowser: não consegui escrever %s\n", G.screenshot);
            g_exit_code = 1;
        } else {
            fprintf(stderr, "bowser: screenshot salvo em %s (%dx%d)\n", G.screenshot,
                    cairo_image_surface_get_width(surface),
                    cairo_image_surface_get_height(surface));
        }
        cairo_surface_destroy(surface);
    }

    task_quit(data);
}

static void task_screenshot(Browser *b)
{
    if (G.screenshot == NULL) {
        task_quit(b);
        return;
    }

    webkit_web_view_get_snapshot(
        b->view,
        G.full_page ? WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT
                    : WEBKIT_SNAPSHOT_REGION_VISIBLE,
        WEBKIT_SNAPSHOT_OPTIONS_NONE, NULL, snapshot_cb, b);
}

static void eval_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
    GError *err = NULL;
    JSCValue *value;

    value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &err);
    if (value == NULL) {
        fprintf(stderr, "bowser: erro no --eval: %s\n",
                err != NULL ? err->message : "erro desconhecido");
        g_clear_error(&err);
        g_exit_code = 1;
    } else {
        char *out = jsc_value_is_string(value) || jsc_value_is_undefined(value) ||
                            jsc_value_is_null(value)
                        ? jsc_value_to_string(value)
                        : jsc_value_to_json(value, 0);

        if (out == NULL)
            out = jsc_value_to_string(value);
        printf("%s\n", out != NULL ? out : "");
        fflush(stdout);
        g_free(out);
        g_object_unref(value);
    }

    task_screenshot(data);
}

static void task_eval(Browser *b)
{
    if (G.eval_js == NULL) {
        task_screenshot(b);
        return;
    }
    webkit_web_view_evaluate_javascript(b->view, G.eval_js, -1, NULL, NULL, NULL,
                                        eval_cb, b);
}

static gboolean task_run(gpointer data)
{
    task_eval((Browser *)data);
    return G_SOURCE_REMOVE;
}

static void selector_cb(GObject *obj, GAsyncResult *res, gpointer data);

static gboolean selector_poll(gpointer data)
{
    Browser *b = data;
    char *esc = g_strescape(G.wait_sel, NULL);
    char *js = g_strdup_printf("!!document.querySelector(\"%s\")", esc);

    webkit_web_view_evaluate_javascript(b->view, js, -1, NULL, NULL, NULL,
                                        selector_cb, b);
    g_free(js);
    g_free(esc);
    return G_SOURCE_REMOVE;
}

static void selector_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
    GError *err = NULL;
    JSCValue *value;
    gboolean found = FALSE;

    value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &err);
    if (value != NULL) {
        found = jsc_value_to_boolean(value);
        g_object_unref(value);
    }
    g_clear_error(&err);

    if (found)
        g_timeout_add(G.wait_ms, task_run, data);
    else
        g_timeout_add(100, selector_poll, data);
}

static void task_begin(Browser *b)
{
    if (G.screenshot == NULL && G.eval_js == NULL)
        return;

    b->task_started = TRUE;

    if (G.wait_sel != NULL)
        selector_poll(b);
    else
        g_timeout_add(G.wait_ms, task_run, b);
}

static gboolean on_timeout(gpointer data)
{
    (void)data;
    fprintf(stderr, "bowser: tempo esgotado (%d ms) antes de concluir a tarefa\n",
            G.timeout_ms);
    g_exit_code = 4;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ setup */

static GtkWidget *icon_button(const char *icon, const char *tip)
{
    GtkWidget *btn = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn, tip);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "flat");
    return btn;
}

static void build_chrome(Browser *b)
{
    GtkWidget *bar = gtk_header_bar_new();
    GtkWidget *reload;

    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);

    b->back = icon_button("go-previous-symbolic", "Voltar (Alt+esquerda)");
    b->forward = icon_button("go-next-symbolic", "Avançar (Alt+direita)");
    reload = icon_button("view-refresh-symbolic", "Recarregar (Ctrl+R)");

    g_signal_connect(b->back, "clicked", G_CALLBACK(on_back), b);
    g_signal_connect(b->forward, "clicked", G_CALLBACK(on_forward), b);
    g_signal_connect(reload, "clicked", G_CALLBACK(on_reload), b);

    b->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(b->entry),
                                   "URL ou termo de busca (Ctrl+L)");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(b->entry),
                                      GTK_ENTRY_ICON_PRIMARY,
                                      G.private_mode ? "view-private-symbolic"
                                                     : "web-browser-symbolic");
    gtk_widget_set_hexpand(b->entry, TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(b->entry), 50);
    g_signal_connect(b->entry, "activate", G_CALLBACK(on_entry_activate), b);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), b->back);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), b->forward);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), reload);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(bar), b->entry);

    gtk_window_set_titlebar(GTK_WINDOW(b->window), bar);
    update_nav(b);
}

static Browser *browser_new(WebKitWebView *related)
{
    Browser *b = g_new0(Browser, 1);

    if (related != NULL) {
        b->view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(related));
        webkit_web_view_set_settings(b->view, g_settings);
    } else {
        b->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                               "web-context", g_ctx,
                                               "settings", g_settings,
                                               "user-content-manager", g_ucm,
                                               NULL));
    }

    if (G.zoom > 0)
        webkit_web_view_set_zoom_level(b->view, G.zoom);

    b->is_main = (related == NULL);
    b->window = G.hidden ? gtk_offscreen_window_new()
                         : gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(b->window), G.width, G.height);
    gtk_window_set_title(GTK_WINDOW(b->window),
                         G.title != NULL ? G.title : "bowser");
    /* app_id / WM_CLASS: deixa o compositor identificar a janela */
    if (!G.hidden) {
        gtk_window_set_role(GTK_WINDOW(b->window), "bowser");
        /* Compositores em tiling ignoram o tamanho pedido; a dica de dialogo
           faz a janela abrir flutuante no tamanho exato do viewport. */
        if (G.floating)
            gtk_window_set_type_hint(GTK_WINDOW(b->window),
                                     GDK_WINDOW_TYPE_HINT_DIALOG);
    }

    if (G.chrome && !G.hidden)
        build_chrome(b);

    if (G.fixed_viewport) {
        /* Compositores em tiling ignoram o tamanho pedido para a janela.
           Fixar o tamanho da propria webview e centraliza-la garante que a
           pagina enxergue exatamente o viewport do preset, em qualquer WM. */
        GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        gtk_widget_set_size_request(GTK_WIDGET(b->view), G.width, G.height);
        gtk_widget_set_halign(GTK_WIDGET(b->view), GTK_ALIGN_CENTER);
        gtk_widget_set_valign(GTK_WIDGET(b->view), GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(center), GTK_WIDGET(b->view), TRUE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b->window), center);
    } else {
        gtk_container_add(GTK_CONTAINER(b->window), GTK_WIDGET(b->view));
    }

    g_signal_connect(b->view, "notify::title", G_CALLBACK(on_title), b);
    g_signal_connect(b->view, "notify::uri", G_CALLBACK(on_uri), b);
    g_signal_connect(b->view, "notify::estimated-load-progress",
                     G_CALLBACK(on_progress), b);
    g_signal_connect(b->view, "load-changed", G_CALLBACK(on_load_changed), b);
    g_signal_connect(b->view, "load-failed", G_CALLBACK(on_load_failed), b);
    g_signal_connect(b->view, "create", G_CALLBACK(on_create), b);
    g_signal_connect(b->view, "ready-to-show", G_CALLBACK(on_ready_to_show), b);
    g_signal_connect(b->view, "close", G_CALLBACK(on_close), b);
    g_signal_connect(b->window, "key-press-event", G_CALLBACK(on_key), b);
    g_signal_connect(b->window, "destroy", G_CALLBACK(on_destroy), b);

    g_windows++;

    if (related == NULL) {
        if (G.fullscreen)
            gtk_window_fullscreen(GTK_WINDOW(b->window));
        gtk_widget_show_all(b->window);
        gtk_widget_grab_focus(GTK_WIDGET(b->view));
    }

    return b;
}

static void init_engine(void)
{
    WebKitWebsiteDataManager *dm;
    char *data_dir = NULL;

    if (G.private_mode) {
        dm = webkit_website_data_manager_new_ephemeral();
    } else {
        char *cache_dir;

        data_dir = g_build_filename(g_get_user_data_dir(), "bowser", G.profile, NULL);
        cache_dir = g_build_filename(g_get_user_cache_dir(), "bowser", G.profile, NULL);
        g_mkdir_with_parents(data_dir, 0700);
        g_mkdir_with_parents(cache_dir, 0700);
        dm = webkit_website_data_manager_new("base-data-directory", data_dir,
                                             "base-cache-directory", cache_dir,
                                             NULL);
        g_free(cache_dir);
    }

    g_ctx = webkit_web_context_new_with_website_data_manager(dm);
    g_object_unref(dm);

    if (!G.private_mode) {
        WebKitCookieManager *cm = webkit_web_context_get_cookie_manager(g_ctx);
        char *cookies = g_build_filename(data_dir, "cookies.sqlite", NULL);

        webkit_cookie_manager_set_persistent_storage(
            cm, cookies, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
        webkit_cookie_manager_set_accept_policy(
            cm, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
        g_free(cookies);
        g_free(data_dir);
    }

    if (G.insecure)
        webkit_website_data_manager_set_tls_errors_policy(
            webkit_web_context_get_website_data_manager(g_ctx),
            WEBKIT_TLS_ERRORS_POLICY_IGNORE);

    g_settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(g_settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(g_settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(g_settings, TRUE);
    webkit_settings_set_enable_back_forward_navigation_gestures(g_settings, TRUE);
    if (G.console)
        webkit_settings_set_enable_write_console_messages_to_stdout(g_settings, TRUE);
    if (G.user_agent != NULL)
        webkit_settings_set_user_agent(g_settings, G.user_agent);

    g_ucm = webkit_user_content_manager_new();
    if (G.script != NULL) {
        WebKitUserScript *us = webkit_user_script_new(
            G.script,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            NULL, NULL);
        webkit_user_content_manager_add_script(g_ucm, us);
        webkit_user_script_unref(us);
    }
}

static const char *dup_or_null(const char *s)
{
    return (s != NULL && *s != '\0') ? g_strdup(s) : NULL;
}

int bowser_run(BowserOpts *opts)
{
    Browser *b;
    char *uri;

    G = *opts;
    G.url = dup_or_null(opts->url);
    G.title = dup_or_null(opts->title);
    G.user_agent = dup_or_null(opts->user_agent);
    G.script = dup_or_null(opts->script);
    G.profile = g_strdup(opts->profile != NULL && *opts->profile != '\0'
                             ? opts->profile : "default");
    G.screenshot = dup_or_null(opts->screenshot);
    G.eval_js = dup_or_null(opts->eval_js);
    G.wait_sel = dup_or_null(opts->wait_sel);

    /* No Wayland o app_id vem do prgname. Janelas de preset usam um app_id
       proprio para que uma unica regra do compositor as faca flutuar sem
       afetar o navegador normal. */
    g_set_prgname(G.floating && !G.hidden ? "bowser-float" : "bowser");

    if (!gtk_init_check(NULL, NULL)) {
        fprintf(stderr, "bowser: sem display gráfico disponível "
                        "(WAYLAND_DISPLAY/DISPLAY não definidos?)\n");
        return 3;
    }

    init_engine();
    b = browser_new(NULL);

    uri = normalize_uri(G.url);
    webkit_web_view_load_uri(b->view, uri);
    g_free(uri);

    if (G.devtools)
        webkit_web_inspector_show(webkit_web_view_get_inspector(b->view));

    if (G.timeout_ms > 0 && (G.screenshot != NULL || G.eval_js != NULL))
        g_timeout_add(G.timeout_ms, on_timeout, NULL);

    gtk_main();
    return g_exit_code;
}
