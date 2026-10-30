/* Tier 1 probe: which GTK input-method context does GTK actually create?
 *
 * Run with the packaged GtkSourceView/GTK payload on LD_LIBRARY_PATH and the
 * payload's immodule cache on GTK_IM_MODULE_FILE, once with GTK_IM_MODULE=xim
 * and once with GTK_IM_MODULE=ibus. The printed context id is the id of the
 * slave context GtkIMContextMulticontext instantiated, i.e. it shows whether the
 * module was really loaded ("xim"/"ibus") or GTK fell back to "simple".
 */
#include <gtk/gtk.h>
#include <stdio.h>

static void pump(int rounds, gint micros)
{
    for (int i = 0; i < rounds; i++) {
        while (gtk_events_pending())
            gtk_main_iteration();
        g_usleep((gulong) micros);
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(win), view);
    gtk_widget_show_all(win);
    pump(50, 20000);

    GtkIMContext *ctx = gtk_im_multicontext_new();
    GdkWindow *gw = gtk_widget_get_window(view);
    if (gw != NULL)
        gtk_im_context_set_client_window(ctx, gw);
    pump(20, 10000);

    const char *module = g_getenv("GTK_IM_MODULE");
    const char *id = gtk_im_multicontext_get_context_id(GTK_IM_MULTICONTEXT(ctx));
    printf("GTK_IM_MODULE=%s\n", module != NULL ? module : "<unset>");
    printf("IM_CONTEXT_ID=%s\n", id != NULL ? id : "<null>");
    printf("IM_CONTEXT_TYPE=%s\n", G_OBJECT_TYPE_NAME(ctx));
    printf("GTK_VERSION=%d.%d.%d\n", gtk_major_version, gtk_minor_version, gtk_micro_version);
    fflush(stdout);

    gtk_widget_destroy(win);
    return 0;
}
