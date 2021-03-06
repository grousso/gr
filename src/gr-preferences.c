/* gr-preferences.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
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
#include <gtk/gtk.h>

#ifdef ENABLE_GSPELL
#include <gspell/gspell.h>
#endif

#include "gr-preferences.h"
#include "gr-chef.h"
#include "gr-recipe-store.h"
#include "gr-app.h"
#include "gr-utils.h"


struct _GrPreferences
{
        GtkDialog parent_instance;

        GtkWidget *fullname;
        GtkWidget *name;
        GtkWidget *description;
        GtkWidget *image;
        GtkWidget *error_revealer;
        GtkWidget *error_label;

        char *image_path;
};

G_DEFINE_TYPE (GrPreferences, gr_preferences, GTK_TYPE_DIALOG)

static void
dismiss_error (GrPreferences *self)
{
        gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer), FALSE);
}

static void
update_image (GrPreferences *self)
{
        if (self->image_path != NULL && self->image_path[0] != '\0') {
                g_autoptr(GdkPixbuf) pixbuf = NULL;
                pixbuf = load_pixbuf_fit_size (self->image_path, 0, 64, 64, TRUE);
                gtk_image_set_from_pixbuf (GTK_IMAGE (self->image), pixbuf);
                gtk_style_context_remove_class (gtk_widget_get_style_context (self->image), "dim-label");
        }
        else {
                gtk_image_set_from_icon_name (GTK_IMAGE (self->image), "camera-photo-symbolic", 1);
                gtk_image_set_pixel_size (GTK_IMAGE (self->image), 24);
                gtk_style_context_add_class (gtk_widget_get_style_context (self->image), "dim-label");
        }
}

static void
file_chooser_response (GtkNativeDialog *self,
                       gint             response_id,
                       GrPreferences   *prefs)
{
        if (response_id == GTK_RESPONSE_ACCEPT) {
                g_free (prefs->image_path);
                prefs->image_path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (self));
                update_image (prefs);
        }
}

static void
image_button_clicked (GrPreferences *self)
{
        GtkFileChooserNative *chooser;
        g_autoptr(GtkFileFilter) filter = NULL;

        chooser = gtk_file_chooser_native_new (_("Select an Image"),
                                               GTK_WINDOW (self),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               _("Open"),
                                               _("Cancel"));
        gtk_native_dialog_set_modal (GTK_NATIVE_DIALOG (chooser), TRUE);

        filter = gtk_file_filter_new ();
        gtk_file_filter_set_name (filter, _("Image files"));
        gtk_file_filter_add_mime_type (filter, "image/*");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);
        gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (chooser), filter);

        g_signal_connect (chooser, "response", G_CALLBACK (file_chooser_response), self);

        gtk_native_dialog_show (GTK_NATIVE_DIALOG (chooser));
}

static void
gr_preferences_finalize (GObject *object)
{
        GrPreferences *self = GR_PREFERENCES (object);

        g_free (self->image_path);

        G_OBJECT_CLASS (gr_preferences_parent_class)->finalize (object);
}

static gboolean
save_preferences (GrPreferences  *self,
                  GError        **error)
{
        g_autoptr(GrChef) chef = NULL;
        GrRecipeStore *store;
        const char *id;
        const char *name;
        const char *fullname;
        g_autofree char *description = NULL;
        GtkTextBuffer *buffer;
        GtkTextIter start, end;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        id = gr_recipe_store_get_user_key (store);

        name = gtk_entry_get_text (GTK_ENTRY (self->name));
        fullname = gtk_entry_get_text (GTK_ENTRY (self->fullname));
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->description));
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        description = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

        chef = g_object_new (GR_TYPE_CHEF,
                             "id", id,
                             "fullname", fullname,
                             "name", name,
                             "description", description,
                             "image-path", self->image_path,
                             NULL);

        return gr_recipe_store_update_user (store, chef, error);
}

static gboolean
window_close (GrPreferences *self)
{
        g_autoptr(GError) error = NULL;

        if (!save_preferences (self, &error)) {
                gtk_label_set_label (GTK_LABEL (self->error_label), error->message);
                gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer), TRUE);
        return TRUE;
        }

        return FALSE;
}

static void
gr_preferences_init (GrPreferences *self)
{
        GrRecipeStore *store;
        g_autoptr(GrChef) chef = NULL;
        const char *id;

        gtk_widget_init_template (GTK_WIDGET (self));

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        id = gr_recipe_store_get_user_key (store);

        if (id != NULL && id[0] != '\0')
                chef = gr_recipe_store_get_chef (store, id);

        if (chef) {
                const char *fullname;
                const char *name;
                const char *description;
                const char *image_path;

                fullname = gr_chef_get_fullname (chef);
                name = gr_chef_get_name (chef);
                description = gr_chef_get_description (chef);
                image_path = gr_chef_get_image (chef);

                gtk_entry_set_text (GTK_ENTRY (self->fullname), fullname ? fullname : "");
                gtk_entry_set_text (GTK_ENTRY (self->name), name ? name : "");
                gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->description)),
                                          description ? description : "", -1);

                self->image_path = g_strdup (image_path);
        }

        update_image (self);

        g_signal_connect_swapped (self, "delete-event", G_CALLBACK (window_close), self);

#ifdef ENABLE_GSPELL
        {
                GspellTextView *gspell_view;

                gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (self->description));
                gspell_text_view_basic_setup (gspell_view);
        }
#endif
}

static void
gr_preferences_class_init (GrPreferencesClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = gr_preferences_finalize;

        gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                                     "/org/gnome/Recipes/gr-preferences.ui");

        gtk_widget_class_bind_template_child (widget_class, GrPreferences, fullname);
        gtk_widget_class_bind_template_child (widget_class, GrPreferences, name);
        gtk_widget_class_bind_template_child (widget_class, GrPreferences, description);
        gtk_widget_class_bind_template_child (widget_class, GrPreferences, image);
        gtk_widget_class_bind_template_child (widget_class, GrPreferences, error_revealer);
        gtk_widget_class_bind_template_child (widget_class, GrPreferences, error_label);

        gtk_widget_class_bind_template_callback (widget_class, dismiss_error);
        gtk_widget_class_bind_template_callback (widget_class, image_button_clicked);
}

GrPreferences *
gr_preferences_new (GtkWindow *win)
{
        return g_object_new (GR_TYPE_PREFERENCES,
                             "transient-for", win,
                             "use-header-bar", TRUE,
                             NULL);
}
