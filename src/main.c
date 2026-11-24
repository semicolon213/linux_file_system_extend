#include "app_info.h"

#include <gtk/gtk.h>

typedef struct {
    gboolean smoke;
} FilescopeOptions;

static gboolean filescope_quit(gpointer application) {
    g_application_quit(G_APPLICATION(application));
    return G_SOURCE_REMOVE;
}

static gint filescope_handle_options(GApplication *application, GVariantDict *options,
                                     gpointer user_data) {
    FilescopeOptions *app_options = user_data;

    (void)application;
    app_options->smoke = g_variant_dict_contains(options, "smoke");
    return -1;
}

static void filescope_activate(GtkApplication *application, gpointer user_data) {
    FilescopeOptions *app_options = user_data;
    GtkWidget *window = gtk_application_window_new(application);

    gtk_window_set_title(GTK_WINDOW(window), filescope_app_name());
    gtk_widget_show_all(window);

    if (app_options->smoke) {
        g_print("%s\n", gtk_window_get_title(GTK_WINDOW(window)));
        g_idle_add(filescope_quit, application);
    }
}

int main(int argc, char *argv[]) {
    FilescopeOptions options = {0};
    GtkApplication *application =
        gtk_application_new(FILESCOPE_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    int status;

    g_application_add_main_option(G_APPLICATION(application), "smoke", 0, G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_NONE, "Print the window title and exit", NULL);
    g_signal_connect(application, "handle-local-options", G_CALLBACK(filescope_handle_options),
                     &options);
    g_signal_connect(application, "activate", G_CALLBACK(filescope_activate), &options);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
