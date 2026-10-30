/* Control for the VEdit keyboard probe.
 *
 * A minimal GTK3 window holding one editable GtkTextView that is explicitly
 * given the keyboard focus, mirroring what VEdit fails to do. Every buffer
 * change is written to /tmp/control-out.txt. Injecting the same XTest
 * keystroke used against VEdit proves whether the probe's input path itself
 * works: if this control records an "a" but VEdit's buffer stays untouched,
 * the failure is VEdit's, not the harness's.
 */
#include <gtk/gtk.h>

static void write_out(GtkTextBuffer *buffer) {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_file_set_contents("/tmp/control-out.txt", text, -1, NULL);
    g_free(text);
}

static void on_changed(GtkTextBuffer *buffer, gpointer unused) {
    (void)unused;
    write_out(buffer);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "xtype-control");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(window), view);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    g_signal_connect(buffer, "changed", G_CALLBACK(on_changed), NULL);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(view);
    write_out(buffer);
    gtk_main();
    return 0;
}
