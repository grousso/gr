/* gr-recipe-exporter.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
    <file preprocess="xml-stripblanks">gr-big-cuisine-tile.ui</file>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n.h>
#ifdef ENABLE_AUTOAR
#include <gnome-autoar/gnome-autoar.h>
#include "glnx-shutil.h"
#endif

#include "gr-recipe-exporter.h"
#include "gr-images.h"
#include "gr-chef.h"
#include "gr-recipe.h"
#include "gr-recipe-store.h"
#include "gr-app.h"
#include "gr-utils.h"


struct _GrRecipeExporter
{
        GObject parent_instance;

        GList *recipes;
        GtkWindow *window;

#ifdef ENABLE_AUTOAR
        AutoarCompressor *compressor;
#endif
        GFile *dest;
        GFile *output;
        GList *sources;
        char *dir;

        GtkWidget *dialog_heading;
};

G_DEFINE_TYPE (GrRecipeExporter, gr_recipe_exporter, G_TYPE_OBJECT)

static void
gr_recipe_exporter_finalize (GObject *object)
{
        GrRecipeExporter *exporter = GR_RECIPE_EXPORTER (object);

        g_list_free_full (exporter->recipes, g_object_unref);
        g_clear_object (&exporter->dest);
        g_clear_object (&exporter->output);
        g_list_free_full (exporter->sources, g_object_unref);
        g_free (exporter->dir);

        G_OBJECT_CLASS (gr_recipe_exporter_parent_class)->finalize (object);
}

static void
gr_recipe_exporter_class_init (GrRecipeExporterClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gr_recipe_exporter_finalize;
}

static void
gr_recipe_exporter_init (GrRecipeExporter *self)
{
}

GrRecipeExporter *
gr_recipe_exporter_new (GtkWindow *parent)
{
        GrRecipeExporter *exporter;

        exporter = g_object_new (GR_TYPE_RECIPE_EXPORTER, NULL);

        exporter->window = parent;

        return exporter;
}

static void
cleanup_export (GrRecipeExporter *exporter)
{
#ifdef ENABLE_AUTOAR
        g_autoptr(GError) error = NULL;

        if (!glnx_shutil_rm_rf_at (-1, exporter->dir, NULL, &error))
                g_warning ("Failed to clean up temp directory %s: %s", exporter->dir, error->message);

        g_clear_object (&exporter->compressor);
#endif

        g_clear_pointer (&exporter->dir, g_free);
        g_list_free_full (exporter->recipes, g_object_unref);
        exporter->recipes = NULL;
        g_clear_object (&exporter->output);
        g_clear_object (&exporter->dest);
        g_list_free_full (exporter->sources, g_object_unref);
        exporter->sources = NULL;
}

#ifdef ENABLE_AUTOAR
static void
completed_cb (AutoarCompressor *compressor,
              GrRecipeExporter *exporter)
{
        GtkWidget *dialog;
        g_autofree char *path =  NULL;
        int n;

        n = g_list_length (exporter->recipes);
        path = g_file_get_path (exporter->dest);

        g_message (ngettext ("%d recipe has been exported as “%s”",
                             "%d recipes have been exported as “%s”", n),
                   n, path);

        dialog = gtk_message_dialog_new (exporter->window,
                                         GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_INFO,
                                         GTK_BUTTONS_OK,
                                         ngettext ("%d recipe has been exported as “%s”",
                                                   "%d recipes have been exported as “%s”", n),
                                         n, path);
        g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
        gtk_widget_show (dialog);

        cleanup_export (exporter);
}

static void
decide_dest_cb (AutoarCompressor *compressor,
                GFile            *file,
                GrRecipeExporter *exporter)
{
        g_set_object (&exporter->dest, file);
}
#endif

static gboolean
export_one_recipe (GrRecipeExporter  *exporter,
                   GrRecipe          *recipe,
                   GKeyFile          *keyfile,
                   GError           **error)
{
        const char *key;
        const char *name;
        const char *author;
        const char *description;
        const char *cuisine;
        const char *season;
        const char *category;
        const char *prep_time;
        const char *cook_time;
        const char *ingredients;
        const char *instructions;
        const char *notes;
        int serves;
        GrDiets diets;
        GDateTime *ctime;
        GDateTime *mtime;
        g_autoptr(GrChef) chef = NULL;
        g_autoptr(GArray) images = NULL;
        g_auto(GStrv) paths = NULL;
        g_autofree int *angles = NULL;
        g_autofree gboolean *dark = NULL;
        int i, j;

        key = gr_recipe_get_id (recipe);
        name = gr_recipe_get_name (recipe);
        author = gr_recipe_get_author (recipe);
        description = gr_recipe_get_description (recipe);
        serves = gr_recipe_get_serves (recipe);
        cuisine = gr_recipe_get_cuisine (recipe);
        season = gr_recipe_get_season (recipe);
        category = gr_recipe_get_category (recipe);
        prep_time = gr_recipe_get_prep_time (recipe);
        cook_time = gr_recipe_get_cook_time (recipe);
        diets = gr_recipe_get_diets (recipe);
        ingredients = gr_recipe_get_ingredients (recipe);
        instructions = gr_recipe_get_instructions (recipe);
        notes = gr_recipe_get_notes (recipe);
        ctime = gr_recipe_get_ctime (recipe);
        mtime = gr_recipe_get_mtime (recipe);

        g_object_get (recipe, "images", &images, NULL);
        paths = g_new0 (char *, images->len + 1);
        angles = g_new0 (int, images->len + 1);
        dark = g_new0 (gboolean, images->len + 1);
        for (i = 0, j = 0; i < images->len; i++) {
                GrRotatedImage *ri = &g_array_index (images, GrRotatedImage, i);
                g_autoptr(GFile) source = NULL;
                g_autoptr(GFile) dest = NULL;
                g_autofree char *basename = NULL;
                g_autofree char *destname = NULL;

                source = g_file_new_for_path (ri->path);
                basename = g_file_get_basename (source);
                destname = g_build_filename (exporter->dir, basename, NULL);

                dest = g_file_new_for_path (destname);

                if (!g_file_copy (source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) {
                        return FALSE;
                }

                exporter->sources = g_list_append (exporter->sources, g_object_ref (dest));

                paths[j] = g_strdup (basename);
                angles[j] = ri->angle;
                dark[j] = ri->dark_text;

                j++;
        }

        g_key_file_set_string (keyfile, key, "Name", name ? name : "");
        g_key_file_set_string (keyfile, key, "Author", author ? author : "");
        g_key_file_set_string (keyfile, key, "Description", description ? description : "");
        g_key_file_set_string (keyfile, key, "Cuisine", cuisine ? cuisine : "");
        g_key_file_set_string (keyfile, key, "Season", season ? season : "");
        g_key_file_set_string (keyfile, key, "Category", category ? category : "");
        g_key_file_set_string (keyfile, key, "PrepTime", prep_time ? prep_time : "");
        g_key_file_set_string (keyfile, key, "CookTime", cook_time ? cook_time : "");
        g_key_file_set_string (keyfile, key, "Ingredients", ingredients ? ingredients : "");
        g_key_file_set_string (keyfile, key, "Instructions", instructions ? instructions : "");
        g_key_file_set_string (keyfile, key, "Notes", notes ? notes : "");
        g_key_file_set_integer (keyfile, key, "Serves", serves);
        g_key_file_set_integer (keyfile, key, "Diets", diets);

        g_key_file_set_string_list (keyfile, key, "Images", (const char * const *)paths, g_strv_length (paths));
        g_key_file_set_integer_list (keyfile, key, "Angles", angles, g_strv_length (paths));
        g_key_file_set_integer_list (keyfile, key, "DarkText", dark, g_strv_length (paths));


        if (ctime) {
                g_autofree char *created = date_time_to_string (ctime);
                g_key_file_set_string (keyfile, key, "Created", created);
        }
        if (mtime) {
                g_autofree char *modified = date_time_to_string (mtime);
                g_key_file_set_string (keyfile, key, "Modified", modified);
        }

        return TRUE;
}

static gboolean
export_one_chef (GrRecipeExporter  *exporter,
                 GrChef            *chef,
                 GKeyFile          *keyfile,
                 GError           **error)
{
        const char *key;
        const char *name;
        const char *fullname;
        const char *description;
        const char *image_path;

        key = gr_chef_get_id (chef);
        name = gr_chef_get_name (chef);
        fullname = gr_chef_get_fullname (chef);
        description = gr_chef_get_description (chef);
        image_path = gr_chef_get_image (chef);
        if (image_path) {
                g_autoptr(GFile) source = NULL;
                g_autoptr(GFile) dest = NULL;
                g_autofree char *basename = NULL;
                g_autofree char *destname = NULL;

                source = g_file_new_for_path (image_path);
                basename = g_file_get_basename (source);
                destname = g_build_filename (exporter->dir, basename, NULL);

                dest = g_file_new_for_path (destname);

                if (!g_file_copy (source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) {
                        return FALSE;
                }
                else {
                        g_key_file_set_string (keyfile, key, "Image", basename);
                        exporter->sources = g_list_append (exporter->sources, g_object_ref (dest));
                }
        }

        g_key_file_set_string (keyfile, key, "Name", name ? name : "");
        g_key_file_set_string (keyfile, key, "Fullname", fullname ? fullname : "");
        g_key_file_set_string (keyfile, key, "Description", description ? description : "");

        return TRUE;
}

static gboolean
prepare_export (GrRecipeExporter  *exporter,
                GError           **error)
{
#ifndef ENABLE_AUTOAR
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     _("This build does not support exporting"));

        return FALSE;
#else
        g_autofree char *path = NULL;
        g_autoptr(GKeyFile) keyfile = NULL;
        GrRecipeStore *store;
        GList *l;
        g_autoptr(GHashTable) chefs = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        g_assert (exporter->dir == NULL);
        g_assert (exporter->sources == NULL);

        exporter->dir = g_mkdtemp (g_build_filename (g_get_tmp_dir (), "recipeXXXXXX", NULL));
        path = g_build_filename (exporter->dir, "recipes.db", NULL);
        keyfile = g_key_file_new ();

        for (l = exporter->recipes; l; l = l->next) {
                GrRecipe *recipe = l->data;

                if (!export_one_recipe (exporter, recipe, keyfile, error))
                        return FALSE;
        }

        if (!g_key_file_save_to_file (keyfile, path, error))
                return FALSE;

        exporter->sources = g_list_append (exporter->sources, g_file_new_for_path (path));

        g_clear_pointer (&path, g_free);
        g_clear_pointer (&keyfile, g_key_file_unref);

        path = g_build_filename (exporter->dir, "chefs.db", NULL);
        keyfile = g_key_file_new ();

        chefs = g_hash_table_new (g_str_hash, g_str_equal);
        for (l = exporter->recipes; l; l = l->next) {
                GrRecipe *recipe = l->data;
                const char *author;
                g_autoptr(GrChef) chef = NULL;

                author = gr_recipe_get_author (recipe);
                if (g_hash_table_contains (chefs, author))
                        continue;

                chef = gr_recipe_store_get_chef (store, author);
                if (!chef)
                        continue;

                if (!export_one_chef (exporter, chef, keyfile, error))
                        return FALSE;

                g_hash_table_add (chefs, (gpointer)author);
        }

        if (!g_key_file_save_to_file (keyfile, path, error))
                return FALSE;

        exporter->sources = g_list_append (exporter->sources, g_file_new_for_path (path));

        return TRUE;
#endif
}

static void
error_cb (gpointer          compressor,
          GError           *error,
          GrRecipeExporter *exporter)
{
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new (exporter->window,
                                         GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK,
                                         _("Error while exporting:\n%s"),
                                         error->message);
        g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
        gtk_widget_show (dialog);

        cleanup_export (exporter);
}

static void
start_export (GrRecipeExporter *exporter)
{
        g_autoptr(GError) error = NULL;

        if (!prepare_export (exporter, &error)) {
                error_cb (NULL, error, exporter);
                return;
        }

#ifdef ENABLE_AUTOAR
        exporter->compressor = autoar_compressor_new (exporter->sources, exporter->output, AUTOAR_FORMAT_TAR, AUTOAR_FILTER_GZIP, FALSE);

        autoar_compressor_set_output_is_dest (exporter->compressor, TRUE);
        g_signal_connect (exporter->compressor, "decide-dest", G_CALLBACK (decide_dest_cb), exporter);
        g_signal_connect (exporter->compressor, "completed", G_CALLBACK (completed_cb), exporter);
        g_signal_connect (exporter->compressor, "error", G_CALLBACK (error_cb), exporter);

        autoar_compressor_start_async (exporter->compressor, NULL);
#endif
}

static void
export_dialog_response (GtkWidget        *dialog,
                        int               response_id,
                        GrRecipeExporter *exporter)
{
        if (response_id == GTK_RESPONSE_CANCEL) {
                g_message ("not exporting now");
        }
        else if (response_id == GTK_RESPONSE_OK) {
                g_autofree char *dir = NULL;
                g_autofree char *path = NULL;

                g_message ("exporting %d recipes now", g_list_length (exporter->recipes));
                dir = g_dir_make_tmp ("recipesXXXXXX", NULL);
                path = g_build_filename (dir, "recipes.tar.gz", NULL);
                exporter->output = g_file_new_for_path (path);

                start_export (exporter);
        }

        gtk_widget_destroy (dialog);
        exporter->dialog_heading = NULL;
}

static void
update_heading (GrRecipeExporter *exporter)
{
        g_autofree char *tmp = NULL;
        int n;

        n = g_list_length (exporter->recipes);
        tmp = g_strdup_printf (ngettext ("%d recipe selected for export",
                                         "%d recipes selected for export", n), n);
        gtk_label_set_label (GTK_LABEL (exporter->dialog_heading), tmp);
}

static void
row_activated (GtkListBox *list,
               GtkListBoxRow *row,
               GrRecipeExporter *exporter)
{
        GrRecipe *recipe;
        GtkWidget *image;

        recipe = GR_RECIPE (g_object_get_data (G_OBJECT (row), "recipe"));
        image = GTK_WIDGET (g_object_get_data (G_OBJECT (row), "check"));

        if (gtk_widget_get_opacity (image) > 0.5) {
                exporter->recipes = g_list_remove (exporter->recipes, recipe);
                g_object_unref (recipe);
                gtk_widget_set_opacity (image, 0.0);
        }
        else {
                exporter->recipes = g_list_append (exporter->recipes, g_object_ref (recipe));
                gtk_widget_set_opacity (image, 1.0);
        }

        update_heading (exporter);
}

static void
add_recipe_row (GrRecipeExporter *exporter,
                GtkWidget *list,
                GrRecipe  *recipe)
{
        GtkWidget *row;
        GtkWidget *box;
        GtkWidget *label;
        GtkWidget *image;

        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 20);
        gtk_widget_show (box);
        g_object_set (box, "margin", 10, NULL);
        label = gtk_label_new (gr_recipe_get_name (recipe));
        gtk_label_set_xalign (GTK_LABEL (label), 0.0);
        gtk_widget_show (label);
        gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

        image = gtk_image_new_from_icon_name ("object-select-symbolic", 1);
        gtk_widget_show (image);
        gtk_style_context_add_class (gtk_widget_get_style_context (image), "dim-label");
        gtk_box_pack_start (GTK_BOX (box), image, FALSE, TRUE, 0);

        row = gtk_list_box_row_new ();
        gtk_widget_show (row);
        gtk_container_add (GTK_CONTAINER (row), box);
        g_object_set_data_full (G_OBJECT (row), "recipe", g_object_ref (recipe), g_object_unref);
        g_object_set_data (G_OBJECT (row), "check", image);

        gtk_container_add (GTK_CONTAINER (list), row);

}

static void
all_headers (GtkListBoxRow *row,
             GtkListBoxRow *before,
             gpointer       user_data)
{
        GtkWidget *header;

        header = gtk_list_box_row_get_header (row);
        if (header)
                return;

        header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
        gtk_list_box_row_set_header (row, header);
}

static int
sort_recipe_row (GtkListBoxRow *row1,
                 GtkListBoxRow *row2,
                 gpointer       data)
{
        GrRecipe *r1, *r2;

        r1 = GR_RECIPE (g_object_get_data (G_OBJECT (row1), "recipe"));
        r2 = GR_RECIPE (g_object_get_data (G_OBJECT (row2), "recipe"));

        return g_strcmp0 (gr_recipe_get_name (r1), gr_recipe_get_name (r2));
}

static void
populate_recipe_list (GrRecipeExporter *exporter,
                      GtkWidget        *list)
{
        GList *l;

        gtk_list_box_set_header_func (GTK_LIST_BOX (list), all_headers, NULL, NULL);
        gtk_list_box_set_sort_func (GTK_LIST_BOX (list), sort_recipe_row, NULL, NULL);

        g_signal_connect (list, "row-activated", G_CALLBACK (row_activated), exporter);
        for (l = exporter->recipes; l; l = l->next) {
                GrRecipe *recipe = l->data;
                add_recipe_row (exporter, list, recipe);
        }
}

static void
show_export_dialog (GrRecipeExporter *exporter)
{
        g_autoptr(GtkBuilder) builder = NULL;
        GtkWidget *dialog;
        GtkWidget *list;
        g_autofree char *tmp = NULL;

        builder = gtk_builder_new_from_resource ("/org/gnome/Recipes/recipe-export-dialog.ui");
        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "dialog"));
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (exporter->window));

        list = GTK_WIDGET (gtk_builder_get_object (builder, "recipe_list"));
        populate_recipe_list (exporter, list);

        exporter->dialog_heading = GTK_WIDGET (gtk_builder_get_object (builder, "heading"));
        update_heading (exporter);

        g_signal_connect (dialog, "response", G_CALLBACK (export_dialog_response), exporter);
        gtk_widget_show (dialog);
}

void
gr_recipe_exporter_export (GrRecipeExporter *exporter,
                           GrRecipe         *recipe)
{
        GList *l;

        // TODO: listen for ::recipe-removed and filter out the list
        for (l = exporter->recipes; l; l = l->next) {
                if (l->data == (gpointer)recipe)
                        goto dialog;
        }

        exporter->recipes = g_list_append (exporter->recipes, g_object_ref (recipe));

dialog:
        show_export_dialog (exporter);
}
