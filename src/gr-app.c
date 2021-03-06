/* gr-app.c:
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
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "gr-app.h"
#include "gr-window.h"
#include "gr-preferences.h"
#include "gr-recipe-store.h"
#include "gr-cuisine.h"
#include "gr-shell-search-provider.h"
#include "gr-utils.h"

struct _GrApp
{
        GtkApplication parent_instance;

        GrRecipeStore *store;
        GrShellSearchProvider *search_provider;
};

G_DEFINE_TYPE (GrApp, gr_app, GTK_TYPE_APPLICATION)


static void
gr_app_finalize (GObject *object)
{
        GrApp *self = GR_APP (object);

        g_clear_object (&self->store);

        G_OBJECT_CLASS (gr_app_parent_class)->finalize (object);
}

static void
gr_app_activate (GApplication *app)
{
        GtkWindow *win;

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        if (!win)
                win = GTK_WINDOW (gr_window_new (GR_APP (app)));
        gtk_window_present (win);
}

static void
preferences_activated (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       app)
{
        GrPreferences *prefs;
        GtkWindow *win;

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        prefs = gr_preferences_new (win);
        gtk_window_present (GTK_WINDOW (prefs));
}

static GtkWidget *
find_child_with_name (GtkWidget  *parent,
                      const char *name)
{
        GList *children, *l;
        GtkWidget *result = NULL;

        children = gtk_container_get_children (GTK_CONTAINER (parent));
        for (l = children; l; l = l->next) {
                GtkWidget *child = l->data;

                if (g_strcmp0 (gtk_buildable_get_name (GTK_BUILDABLE (child)), name) == 0) {
                        result = child;
                        break;
                }
        }
        g_list_free (children);

        if (result == NULL)
                g_warning ("Didn't find %s in GtkAboutDialog\n", name);
        return result;
}

static void
builder_info (GtkButton *button, GtkWidget *about)
{
        const char *uri = "http://wiki.gnome.org/Apps/Builder";
        g_autoptr(GError) error = NULL;

        gtk_show_uri_on_window (GTK_WINDOW (about), uri, GDK_CURRENT_TIME, &error);
        if (error)
                g_warning ("Unable to show '%s': %s", uri, error->message);
}

static void
pixbuf_fill_rgb (GdkPixbuf *pixbuf,
                 guint      r,
                 guint      g,
                 guint      b)
{
        guchar *pixels;
        guchar *p;
        guint w, h;

        pixels = gdk_pixbuf_get_pixels (pixbuf);
        h = gdk_pixbuf_get_height (pixbuf);
        while (h--) {
                w = gdk_pixbuf_get_width (pixbuf);
                p = pixels;
                while (w--) {
                        p[0] = r;
                        p[1] = g;
                        p[2] = b;
                        p += 4;
                }
                pixels += gdk_pixbuf_get_rowstride (pixbuf);
        }
}

static void
style_updated (GtkWidget *widget)
{
        g_autoptr(GdkPixbuf) pixbuf = NULL;
        GtkStyleContext *context;
        GdkRGBA color;
        guint r, g, b;
        guint32 pixel;
        guint32 old_pixel;

        context = gtk_widget_get_style_context (widget);
        gtk_style_context_get_color (context, gtk_style_context_get_state (context), &color);

        r = 255 * color.red;
        g = 255 * color.green;
        b = 255 * color.blue;

        pixel = (r << 24) | (g  << 16) | (b << 8);
        old_pixel = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "pixel"));

        if (old_pixel == pixel)
                return;

        g_object_set_data (G_OBJECT (widget), "pixel", GUINT_TO_POINTER (pixel));

        pixbuf = g_object_ref (gtk_image_get_pixbuf (GTK_IMAGE (widget)));
        pixbuf_fill_rgb (pixbuf, r, g, b);
        gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
}

static void
add_built_logo (GtkAboutDialog *about)
{
        GtkWidget *content;
        GtkWidget *box;
        GtkWidget *stack;
        GtkWidget *page_vbox;
        GtkWidget *license_label;
        GtkWidget *copyright_label;
        GtkWidget *button;
        GtkWidget *image;
        g_autoptr(GdkPixbuf) pixbuf = NULL;

        content = gtk_dialog_get_content_area (GTK_DIALOG (about));
        box = find_child_with_name (content, "box");
        stack = find_child_with_name (box, "stack");
        page_vbox = find_child_with_name (stack, "page_vbox");
        license_label = find_child_with_name (page_vbox, "license_label");
        copyright_label = find_child_with_name (page_vbox, "copyright_label");

        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_show (box);
        button = gtk_button_new ();
        g_signal_connect (button, "clicked", G_CALLBACK (builder_info), about);
        gtk_style_context_add_class (gtk_widget_get_style_context (button), "image-button");
        gtk_box_pack_start (GTK_BOX (box), button, FALSE, TRUE, 0);
        gtk_widget_set_valign (button, GTK_ALIGN_END);
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text (button, _("Learn more about Builder"));
        gtk_widget_show (button);
        image = gtk_image_new ();
        pixbuf = gdk_pixbuf_new_from_resource ("/org/gnome/Recipes/built-with-builder.png", NULL);
        gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
        g_signal_connect (image, "style-updated", G_CALLBACK (style_updated), NULL);
        gtk_widget_show (image);
        gtk_container_add (GTK_CONTAINER (button), image);

        g_object_ref (license_label);
        g_object_ref (copyright_label);

        gtk_container_remove (GTK_CONTAINER (page_vbox), license_label);
        gtk_container_remove (GTK_CONTAINER (page_vbox), copyright_label);

        gtk_box_pack_start (GTK_BOX (box), license_label, TRUE, TRUE, 0);
        gtk_label_set_justify (GTK_LABEL (license_label), GTK_JUSTIFY_LEFT);
        gtk_widget_set_valign (license_label, GTK_ALIGN_END);

        gtk_container_add (GTK_CONTAINER (page_vbox), copyright_label);
        gtk_container_add (GTK_CONTAINER (page_vbox), box);

        g_object_unref (license_label);
        g_object_unref (copyright_label);
}

static gboolean
in_flatpak_sandbox (void)
{
        g_autofree char *path = NULL;

        path = g_build_filename (g_get_user_runtime_dir (), "flatpak-info", NULL);

        return g_file_test (path, G_FILE_TEST_EXISTS);
}

static void
get_flatpak_runtime_information (char **id,
                                 char **branch,
                                 char **version,
                                 char **commit)
{
        g_autoptr(JsonParser) parser = NULL;
        JsonNode *root;
        JsonObject *object;
        g_autoptr(GError) error = NULL;

        parser = json_parser_new ();
        if (!json_parser_load_from_file (parser, "/usr/manifest.json", &error)) {
                g_message ("Failed to load runtime information: %s", error->message);
                goto error;
        }

        root = json_parser_get_root (parser);
        if (!JSON_NODE_HOLDS_OBJECT (root))
                goto error;

        object = json_node_get_object (root);

        *id = g_strdup (json_object_get_string_member (object, "id-platform"));
        *branch = g_strdup (json_object_get_string_member (object, "branch"));
        *version = g_strdup (json_object_get_string_member (object, "runtime-version"));
        *commit = g_strdup (json_object_get_string_member (object, "runtime-commit"));
        return;

error:
        *id = g_strdup (_("Unknown"));
        *branch = g_strdup (_("Unknown"));
        *version = g_strdup (_("Unknown"));
        *commit = g_strdup (_("Unknown"));
}

static void
text_buffer_append (GtkTextBuffer *buffer,
                    const char    *text)
{
        GtkTextIter iter;

        gtk_text_buffer_get_end_iter (buffer, &iter);
        gtk_text_buffer_insert (buffer, &iter, text, -1);
}

static void
text_buffer_append_printf (GtkTextBuffer *buffer,
                           const char    *format,
                           ...)
{
        va_list args;
        char *buf;
        int len;

        va_start (args, format);

        len = g_vasprintf (&buf, format, args);
        if (len >= 0) {
                text_buffer_append (buffer, buf);
                g_free (buf);
        }

        va_end (args);
}

static void
text_buffer_append_link (GtkTextBuffer *buffer,
                         const char    *name,
                         const char    *uri)
{
        GdkRGBA color;
        GtkTextTag *tag;
        GtkTextIter iter;

        gdk_rgba_parse (&color, "blue");

        tag = gtk_text_buffer_create_tag (buffer, NULL,
                                          "foreground-rgba", &color,
                                          "underline", PANGO_UNDERLINE_SINGLE,
                                          NULL);
        g_object_set_data_full (G_OBJECT (tag), "uri", g_strdup (uri), g_free);
        gtk_text_buffer_get_end_iter (buffer, &iter);
        gtk_text_buffer_insert_with_tags (buffer, &iter, name, -1, tag, NULL);
}

static void
populate_system_tab (GtkTextView *view)
{
        GtkTextBuffer *buffer;
        PangoTabArray *tabs;
        GtkTextIter start, end;

        tabs = pango_tab_array_new (3, TRUE);
        pango_tab_array_set_tab (tabs, 0, PANGO_TAB_LEFT, 20);
        pango_tab_array_set_tab (tabs, 1, PANGO_TAB_LEFT, 150);
        pango_tab_array_set_tab (tabs, 2, PANGO_TAB_LEFT, 230);
        gtk_text_view_set_tabs (view, tabs);
        pango_tab_array_free (tabs);

        buffer = gtk_text_view_get_buffer (view);

        if (in_flatpak_sandbox ()) {
                g_autofree char *id = NULL;
                g_autofree char *branch = NULL;
                g_autofree char *version = NULL;
                g_autofree char *commit = NULL;

                get_flatpak_runtime_information (&id, &branch, &version, &commit);

                text_buffer_append (buffer, _("Runtime"));
                text_buffer_append (buffer, "\n");
                text_buffer_append_printf (buffer, "\t%s\t%s\n", C_("Runtime metadata", "ID"), id);
                text_buffer_append_printf (buffer, "\t%s\t%s\n", C_("Runtime metadata", "Version"), version);
                text_buffer_append_printf (buffer, "\t%s\t%s\n", C_("Runtime metadata", "Branch"), branch);
                text_buffer_append_printf (buffer, "\t%s\t%s\n", C_("Runtime metadata", "Commit"), commit);

                text_buffer_append (buffer, "\n");
                text_buffer_append (buffer, _("Bundled libraries"));
                text_buffer_append (buffer, "\n");

#if ENABLE_AUTOAR
                text_buffer_append_printf (buffer, "\tgnome-autoar\t%s\t", AUTOAR_VERSION);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
                text_buffer_append (buffer, "\n");
#endif
#if ENABLE_GSPELL
                text_buffer_append_printf (buffer, "\tgspell\t%s\t", GSPELL_VERSION);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
                text_buffer_append (buffer, "\n");
#endif
                text_buffer_append_printf (buffer, "\tlibgd\t%s\t", LIBGD_INFO);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
                text_buffer_append (buffer, "\n");
                text_buffer_append_printf (buffer, "\tlibglnx\t%s\t", LIBGLNX_INFO);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
                text_buffer_append (buffer, "\n");
        }
        else {
                text_buffer_append (buffer, _("System libraries"));
                text_buffer_append (buffer, "\n");
                text_buffer_append_printf (buffer, "\tGLib\t%d.%d.%d\n",
                                           glib_major_version,
                                           glib_minor_version,
                                           glib_micro_version);
                text_buffer_append_printf (buffer, "\tGTK+\t%d.%d.%d\n",
                                           gtk_get_major_version (),
                                           gtk_get_minor_version (),
                                           gtk_get_micro_version ());
#if ENABLE_AUTOAR
                text_buffer_append_printf (buffer, "\tgnome-autoar\t%s\n", AUTOAR_VERSION);
#endif
#if ENABLE_GSPELL
                text_buffer_append_printf (buffer, "\tgspell\t%s\n", GSPELL_VERSION);
#endif

                text_buffer_append (buffer, "\n");
                text_buffer_append (buffer, _("Bundled libraries"));
                text_buffer_append (buffer, "\n");

                text_buffer_append_printf (buffer, "\tlibgd\t%s\t", LIBGD_INFO);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
                text_buffer_append (buffer, "\n");
                text_buffer_append_printf (buffer, "\tlibglnx\t%s\t", LIBGLNX_INFO);
                text_buffer_append_link (buffer, "LGPLv2", "http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html");
       }

        gtk_text_buffer_create_tag (buffer, "smaller", "scale", PANGO_SCALE_SMALL, NULL);
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        gtk_text_buffer_apply_tag_by_name (buffer, "smaller", &start, &end);
}

static void
follow_if_link (GtkAboutDialog *about,
                GtkTextView    *text_view,
                GtkTextIter    *iter)
{
  GSList *tags = NULL, *tagp = NULL;
  gchar *uri = NULL;

  tags = gtk_text_iter_get_tags (iter);
  for (tagp = tags; tagp != NULL && !uri; tagp = tagp->next)
    {
      GtkTextTag *tag = tagp->data;

      uri = g_object_get_data (G_OBJECT (tag), "uri");
      if (uri)
        gtk_show_uri_on_window (GTK_WINDOW (about), uri, GDK_CURRENT_TIME, NULL);
    }

  g_slist_free (tags);
}

static gboolean
text_view_key_press_event (GtkWidget      *text_view,
                           GdkEventKey    *event,
                           GtkAboutDialog *about)
{
  GtkTextIter iter;
  GtkTextBuffer *buffer;

  switch (event->keyval)
    {
      case GDK_KEY_Return:
      case GDK_KEY_ISO_Enter:
      case GDK_KEY_KP_Enter:
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
        gtk_text_buffer_get_iter_at_mark (buffer, &iter,
                                          gtk_text_buffer_get_insert (buffer));
        follow_if_link (about, GTK_TEXT_VIEW (text_view), &iter);
        break;

      default:
        break;
    }

  return FALSE;
}

static gboolean
text_view_event_after (GtkWidget      *text_view,
                       GdkEvent       *event,
                       GtkAboutDialog *about)
{
  GtkTextIter start, end, iter;
  GtkTextBuffer *buffer;
  GdkEventButton *button_event;
  gint x, y;

  if (event->type != GDK_BUTTON_RELEASE)
    return FALSE;

  button_event = (GdkEventButton *)event;

  if (button_event->button != GDK_BUTTON_PRIMARY)
    return FALSE;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

  /* we shouldn't follow a link if the user has selected something */
  gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
  if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end))
    return FALSE;

  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         button_event->x, button_event->y, &x, &y);

  gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (text_view), &iter, x, y);

  follow_if_link (about, GTK_TEXT_VIEW (text_view), &iter);

  return FALSE;
}

static void
set_cursor_if_appropriate (GtkAboutDialog *about,
                           GtkTextView    *text_view,
                           GdkDevice      *device,
                           gint            x,
                           gint            y)
{
  GSList *tags = NULL, *tagp = NULL;
  GtkTextIter iter;
  gboolean hovering_over_link = FALSE;
  gboolean was_hovering = FALSE;

  gtk_text_view_get_iter_at_location (text_view, &iter, x, y);

  tags = gtk_text_iter_get_tags (&iter);
  for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
    {
      GtkTextTag *tag = tagp->data;
      gchar *uri = g_object_get_data (G_OBJECT (tag), "uri");

      if (uri != NULL)
        {
          hovering_over_link = TRUE;
          break;
        }
    }

  was_hovering = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (about), "hovering-over-link"));
  if (hovering_over_link != was_hovering)
    {
      GdkCursor *cursor;

      g_object_set_data (G_OBJECT (about), "hovering-over-link", GINT_TO_POINTER (hovering_over_link));

      if (hovering_over_link)
        cursor = GDK_CURSOR (g_object_get_data (G_OBJECT (about), "pointer-cursor"));
      else
        cursor = GDK_CURSOR (g_object_get_data (G_OBJECT (about), "text-cursor"));

      gdk_window_set_device_cursor (gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT), device, cursor);
    }

  g_slist_free (tags);
}

static gboolean
text_view_motion_notify_event (GtkWidget      *text_view,
                               GdkEventMotion *event,
                               GtkAboutDialog *about)
{
  gint x, y;

  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view),
                                         GTK_TEXT_WINDOW_WIDGET,
                                         event->x, event->y, &x, &y);

  set_cursor_if_appropriate (about, GTK_TEXT_VIEW (text_view), event->device, x, y);

  gdk_event_request_motions (event);

  return FALSE;
}

static void
add_system_tab (GtkAboutDialog *about)
{
        GtkWidget *content;
        GtkWidget *box;
        GtkWidget *stack;
        GtkWidget *sw;
        GtkWidget *view;
        GdkCursor *cursor;

        content = gtk_dialog_get_content_area (GTK_DIALOG (about));
        box = find_child_with_name (content, "box");
        stack = find_child_with_name (box, "stack");

        sw = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                        GTK_POLICY_AUTOMATIC,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                             GTK_SHADOW_IN);
        gtk_widget_show (sw);
        view = gtk_text_view_new ();
        gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
        gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), 10);
        gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), 10);
        gtk_text_view_set_top_margin (GTK_TEXT_VIEW (view), 10);
        gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (view), 10);

        cursor = gdk_cursor_new_from_name (gdk_display_get_default (), "pointer");
        g_object_set_data_full (G_OBJECT (about), "pointer-cursor", cursor, g_object_unref);

        cursor = gdk_cursor_new_from_name (gdk_display_get_default (), "text");
        g_object_set_data_full (G_OBJECT (about), "text-cursor", cursor, g_object_unref);

        g_signal_connect (view, "event-after", G_CALLBACK (text_view_event_after), about);
        g_signal_connect (view, "key-press-event", G_CALLBACK (text_view_key_press_event), about);
        g_signal_connect (view, "motion-notify-event", G_CALLBACK (text_view_motion_notify_event), about);
        gtk_widget_show (view);
        gtk_container_add (GTK_CONTAINER (sw), view);

        gtk_stack_add_titled (GTK_STACK (stack), sw, "system", _("System"));

        populate_system_tab (GTK_TEXT_VIEW (view));
}

static void
about_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       app)
{
        GtkWindow *win;
        const char *authors[] = {
                "Emel Elvin Yıldız",
                "Matthias Clasen",
                "Jakub Steiner",
                "Christian Hergert",
                "Matthew Leeds",
                "Mohammed Sadiq",
                "Sam Hewitt",
                NULL
        };
        const char *recipe_authors[] = {
                "Ray Strode",
                "Bastian Ilsø",
                "Frederik Fyksen",
                "Matthias Clasen",
                NULL
        };

        g_autoptr(GdkPixbuf) logo = NULL;
        static gboolean first_time = TRUE;

        logo = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                         "org.gnome.Recipes",
                                         256,
                                         GTK_ICON_LOOKUP_FORCE_SIZE,
                                         NULL);

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        gtk_show_about_dialog (GTK_WINDOW (win),
                               "program-name", _("GNOME Recipes"),
#if MICRO_VERSION % 2 == 1
                               "version", COMMIT_ID,
#else
                               "version", PACKAGE_VERSION,
#endif
                               "copyright", "© 2016 Matthias Clasen",
                               "license-type", GTK_LICENSE_GPL_3_0,
                               "comments", _("GNOME loves to cook"),
                               "authors", authors,
                               "translator-credits", _("translator-credits"),
                               "logo", logo,
                               "title", _("About GNOME Recipes"),
                               "website", "https://wiki.gnome.org/Apps/Recipes",
                               "website-label", _("Learn more about GNOME Recipes"),
                               NULL);

        if (first_time) {
                GtkAboutDialog *dialog;

                first_time = FALSE;

                dialog = GTK_ABOUT_DIALOG (g_object_get_data (G_OBJECT (win), "gtk-about-dialog"));
                gtk_about_dialog_add_credit_section (dialog, _("Recipes by"), recipe_authors);
                add_built_logo (dialog);
                add_system_tab (dialog);
        }

}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       app)
{
        g_application_quit (G_APPLICATION (app));
}

static void
timer_expired (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       app)
{
        GtkWindow *win;
        const char *id;
        g_autoptr(GrRecipe) recipe = NULL;

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        id = g_variant_get_string (parameter, NULL);
        recipe = gr_recipe_store_get_recipe (GR_APP (app)->store, id);
        if (recipe)
                gr_window_show_recipe (GR_WINDOW (win), recipe);
        gtk_window_present (win);
}

static void
import_activated (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       app)
{
        GtkWindow *win;

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        gtk_window_present (win);
        gr_window_load_recipe (GR_WINDOW (win), NULL);
}

static void
details_activated (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       application)
{
        GrApp *app = GR_APP (application);
        GtkWindow *win;
        const char *id, *search;
        g_autoptr(GrRecipe) recipe = NULL;

        g_variant_get (parameter, "(&s&s)", &id, &search);

        gr_app_activate (G_APPLICATION (app));
        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        recipe = gr_recipe_store_get_recipe (app->store, id);
        gr_window_show_recipe (GR_WINDOW (win), recipe);
}

static void
search_activated (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       application)
{
        GrApp *app = GR_APP (application);
        GtkWindow *win;
        const char *search;

        g_variant_get (parameter, "&s", &search);

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        gr_app_activate (G_APPLICATION (app));
        gr_window_show_search (GR_WINDOW (win), search);
}

static GActionEntry app_entries[] =
{
        { "preferences", preferences_activated, NULL, NULL, NULL },
        { "about", about_activated, NULL, NULL, NULL },
        { "import", import_activated, NULL, NULL, NULL },
        { "details", details_activated, "(ss)", NULL, NULL },
        { "search", search_activated, "s", NULL, NULL },
        { "timer-expired", timer_expired, "s", NULL, NULL },
        { "quit", quit_activated, NULL, NULL, NULL }
};

static void
gr_app_startup (GApplication *app)
{
        const gchar *quit_accels[2] = { "<Ctrl>Q", NULL };
        g_autoptr(GtkCssProvider) css_provider = NULL;
        g_autoptr(GFile) file = NULL;
        g_autofree char *css = NULL;
        const char *path;

        G_APPLICATION_CLASS (gr_app_parent_class)->startup (app);

        g_action_map_add_action_entries (G_ACTION_MAP (app),
                                         app_entries, G_N_ELEMENTS (app_entries),
                                         app);

#ifndef ENABLE_AUTOAR
        {
                GAction *action;

                action = g_action_map_lookup_action (G_ACTION_MAP (app), "import");
                g_object_set (action, "enabled", FALSE, NULL);
        }
#endif

        gtk_application_set_accels_for_action (GTK_APPLICATION (app),
                                               "app.quit",
                                               quit_accels);

        css_provider = gtk_css_provider_new ();
        if (g_file_test ("recipes.css", G_FILE_TEST_EXISTS)) {
                path = "recipes.css";
                file = g_file_new_for_path (path);
        }
        else if (g_file_test ("src/recipes.css", G_FILE_TEST_EXISTS)) {
                path = "src/recipes.css";
                file = g_file_new_for_path (path);
        }
        else {
                path = "resource:///org/gnome/Recipes/recipes.css";
                file = g_file_new_for_uri (path);
        }
        g_message ("Load CSS from: %s", path);
        gtk_css_provider_load_from_file (css_provider, file, NULL);
        gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                   GTK_STYLE_PROVIDER (css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (css_provider);

        css_provider = gtk_css_provider_new ();
        css = gr_cuisine_get_css ();
        gtk_css_provider_load_from_data (css_provider, css, -1, NULL);
        gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                   GTK_STYLE_PROVIDER (css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
gr_app_open (GApplication  *app,
             GFile        **files,
             gint           n_files,
             const char    *hint)
{
        GtkWindow *win;

        if (n_files > 1)
                g_warning ("Can only open one file at a time.");

        win = gtk_application_get_active_window (GTK_APPLICATION (app));
        if (!win)
                win = GTK_WINDOW (gr_window_new (GR_APP (app)));

        gr_window_load_recipe (GR_WINDOW (win), files[0]);

        gtk_window_present (win);
}

static gboolean
gr_app_dbus_register (GApplication    *application,
                      GDBusConnection *connection,
                      const gchar     *object_path,
                      GError         **error)
{
        GrApp *app = GR_APP (application);

        app->search_provider = gr_shell_search_provider_new ();
        gr_shell_search_provider_setup (app->search_provider, app->store);

        return gr_shell_search_provider_register (app->search_provider, connection, error);
}

static void
gr_app_dbus_unregister (GApplication    *application,
                        GDBusConnection *connection,
                        const gchar     *object_path)
{
        GrApp *app = GR_APP (application);

        if (app->search_provider != NULL) {
                gr_shell_search_provider_unregister (app->search_provider);
                g_clear_object (&app->search_provider);
        }
}


static void
gr_app_init (GrApp *self)
{
        self->store = gr_recipe_store_new ();
}

static void
gr_app_class_init (GrAppClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        object_class->finalize = gr_app_finalize;

        application_class->startup = gr_app_startup;
        application_class->activate = gr_app_activate;
        application_class->open = gr_app_open;
        application_class->dbus_register = gr_app_dbus_register;
        application_class->dbus_unregister = gr_app_dbus_unregister;
}

GrApp *
gr_app_new (void)
{
        return g_object_new (GR_TYPE_APP,
                             "application-id", "org.gnome.Recipes",
                             "flags", G_APPLICATION_HANDLES_OPEN | G_APPLICATION_CAN_OVERRIDE_APP_ID,
                             NULL);
}

GrRecipeStore *
gr_app_get_recipe_store (GrApp *app)
{
        return app->store;
}
