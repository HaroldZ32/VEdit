/* Tier 2/3 probe: a real GTK text view that prints what actually landed in the
 * buffer, so an IME commit or an X11 key injection can be asserted from stdout.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gboolean quit_cb(gpointer data)
{
    (void) data;
    gtk_main_quit();
    return FALSE;
}

static void dump(GtkTextView *view)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(view);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    printf("BUFFER_BYTES=%zu\n", strlen(text));
    printf("BUFFER_TEXT=%s\n", text);
    printf("BUFFER_HEX=");
    for (const unsigned char *p = (const unsigned char *) text; *p != '\0'; p++)
        printf("%02x", *p);
    printf("\n");
    fflush(stdout);
    g_free(text);
}

int main(int argc, char **argv)
{
    int seconds = 12;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = atoi(argv[++i]);
    }
    gtk_init(&argc, &argv);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "VEdit IME probe");
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 260);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(win), view);
    gtk_widget_show_all(win);
    gtk_widget_grab_focus(view);

    const char *module = g_getenv("GTK_IM_MODULE");
    const char *xmod = g_getenv("XMODIFIERS");
    printf("GTK_IM_MODULE=%s\n", module != NULL ? module : "<unset>");
    printf("XMODIFIERS=%s\n", xmod != NULL ? xmod : "<unset>");
    fflush(stdout);

    for (int i = 0; i < 60; i++) {
        while (gtk_events_pending())
            gtk_main_iteration();
        g_usleep(20000);
    }
    printf("WINDOW_READY=1\n");
    fflush(stdout);

    g_timeout_add_seconds((guint) seconds, quit_cb, NULL);
    gtk_main();
    dump(GTK_TEXT_VIEW(view));
    return 0;
}
