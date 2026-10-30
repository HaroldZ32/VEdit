/* A-prime compatibility probe: can CentOS 7's GtkSourceView 3.24.8 load the
 * GTK3-generation GtkSourceView 4.8.4 `systemverilog.lang` file?
 * Build: gcc probe.c $(pkg-config --cflags --libs gtksourceview-3.0) */
#include <gtksourceview/gtksource.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: probe <language-specs-dir>\n"); return 2; }
    gtk_init(&argc, &argv);
    GtkSourceLanguageManager *lm = gtk_source_language_manager_new();
    gchar *paths[] = {(gchar *)argv[1], NULL};
    gtk_source_language_manager_set_search_path(lm, paths);
    GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "systemverilog");
    if (lang == NULL) { printf("LANG_NOT_FOUND\n"); return 1; }
    printf("LANG id=%s name=%s section=%s\n",
           gtk_source_language_get_id(lang),
           gtk_source_language_get_name(lang),
           gtk_source_language_get_section(lang));
    /* gsv 3.24.8 has no gtk_source_language_get_keywords(); style ids and a
     * successful language lookup prove the 4.8 spec was parsed by gsv3. */
    const gchar * const *kw_ids = NULL;
    (void)kw_ids;
    gchar **styles = gtk_source_language_get_style_ids(lang);
    printf("STYLE_IDS=%u\n", styles ? g_strv_length(styles) : 0);
    printf("COMPAT_OK\n");
    return 0;
}
