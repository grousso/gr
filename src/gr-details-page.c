/* gr-details-page.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <stdlib.h>
#include <math.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef ENABLE_GSPELL
#include <gspell/gspell.h>
#endif

#include "gr-details-page.h"
#include "gr-recipe-store.h"
#include "gr-app.h"
#include "gr-window.h"
#include "gr-utils.h"
#include "gr-images.h"
#include "gr-image-viewer.h"
#include "gr-ingredients-list.h"
#include "gr-timer.h"
#include "gr-recipe-printer.h"
#include "gr-recipe-exporter.h"


typedef struct
{
        gboolean ingredients;
        gboolean preheat;
        gboolean instructions;
        GrTimer *timer;
} CookingData;

static void
timer_complete (GrTimer *timer)
{
        GApplication *app;
        g_autoptr(GNotification) notification = NULL;
        g_autofree char *body = NULL;
        g_autofree char *action = NULL;
        GrRecipeStore *store;
        g_autoptr(GrRecipe) recipe = NULL;
        const char *id;
        const char *name;

        app = g_application_get_default ();

        id = gr_timer_get_name (timer);

        store = gr_app_get_recipe_store (GR_APP (app));
        recipe = gr_recipe_store_get_recipe (store, id);
        name = gr_recipe_get_translated_name (recipe);

        body = g_strdup_printf (_("Your cooking timer for “%s” has expired."), name);

        notification = g_notification_new (_("Time is up!"));
        g_notification_set_body (notification, body);
        action = g_strdup_printf ("app.timer-expired::%s", id);
        g_notification_set_default_action (notification, action);

        g_application_send_notification (app, "timer", notification);

        gr_recipe_store_add_cooked (store, recipe);
}

static CookingData *
cooking_data_new (const char *id)
{
        CookingData *cd;

        cd = g_new (CookingData, 1);
        cd->ingredients = FALSE;
        cd->preheat = FALSE;
        cd->instructions = FALSE;
        cd->timer = gr_timer_new (id);

        g_signal_connect (cd->timer, "complete", G_CALLBACK (timer_complete), NULL);

        return cd;
}

static void
cooking_data_free (gpointer data)
{
        CookingData *cd = data;

        g_object_unref (cd->timer);
        g_free (cd);
}

struct _GrDetailsPage
{
        GtkBox parent_instance;

        GrRecipe *recipe;
        GrChef *chef;
        GrIngredientsList *ingredients;
        GHashTable *cooking;

        GrRecipePrinter *printer;
        GrRecipeExporter *exporter;

        GtkWidget *recipe_image;
        GtkWidget *prep_time_label;
        GtkWidget *cook_time_label;
        GtkWidget *serves_spin;
        GtkWidget *warning_box;
        GtkWidget *spicy_warning;
        GtkWidget *garlic_warning;
        GtkWidget *gluten_warning;
        GtkWidget *dairy_warning;
        GtkWidget *ingredients_box;
        GtkWidget *instructions_label;
        GtkWidget *cooking_revealer;
        GtkWidget *ingredients_check;
        GtkWidget *preheat_check;
        GtkWidget *instructions_check;
        GtkWidget *timer;
        GtkWidget *timer_stack;
        GtkWidget *timer_popover;
        GtkWidget *time_spin;
        GtkWidget *start_button;
        GtkWidget *favorite_button;
        GtkWidget *duration_stack;
        GtkWidget *remaining_time_label;
        GtkWidget *chef_label;
        GtkWidget *edit_button;
        GtkWidget *delete_button;
        GtkWidget *notes_field;
        GtkWidget *description_label;
        GtkWidget *export_button;
        GtkWidget *error_label;
        GtkWidget *error_revealer;

        guint save_timeout;

        char *uri;
};

G_DEFINE_TYPE (GrDetailsPage, gr_details_page, GTK_TYPE_BOX)

static void connect_store_signals (GrDetailsPage *page);

static void
timer_active_changed (GrTimer       *timer,
                      GParamSpec    *pspec,
                      GrDetailsPage *page)
{
        if (strcmp (gr_timer_get_name (timer), gr_recipe_get_id (page->recipe)) != 0)
                return;

        if (gr_timer_get_active (timer)) {
                gtk_stack_set_visible_child_name (GTK_STACK (page->timer_stack), "timer");
                gtk_stack_set_visible_child_name (GTK_STACK (page->duration_stack), "stop");
        }
        else {
                gtk_stack_set_visible_child_name (GTK_STACK (page->timer_stack), "icon");
                gtk_stack_set_visible_child_name (GTK_STACK (page->duration_stack), "start");
        }
}

static void
timer_remaining_changed (GrTimer       *timer,
                         GParamSpec    *pspec,
                         GrDetailsPage *page)
{
        guint64 remaining;
        guint hours, minutes, seconds;
        g_autofree char *buf = NULL;

        if (strcmp (gr_timer_get_name (timer), gr_recipe_get_id (page->recipe)) != 0)
                return;

        remaining = gr_timer_get_remaining (timer);

        seconds = remaining / (1000 * 1000);

        hours = seconds / (60 * 60);
        seconds -= hours * 60 * 60;
        minutes = seconds / 60;
        seconds -= minutes * 60;

        buf = g_strdup_printf ("%02d:%02d:%02d", hours, minutes, seconds);
        gtk_label_set_label (GTK_LABEL (page->remaining_time_label), buf);
}

static void
set_cooking (GrDetailsPage *page,
             gboolean       cooking)
{
        const char *id;
        CookingData *cd;

        id = gr_recipe_get_id (page->recipe);

        cd = g_hash_table_lookup (page->cooking, id);

        if (cooking) {
                if (!cd) {
                        cd = cooking_data_new (id);
                        g_hash_table_insert (page->cooking, g_strdup (id), cd);
                }

                g_object_set (page->ingredients_check, "active", cd->ingredients, NULL);
                g_object_set (page->preheat_check, "active", cd->preheat, NULL);
                g_object_set (page->instructions_check, "active", cd->instructions, NULL);
                g_object_set (page->timer, "timer", cd->timer, NULL);
                g_signal_connect (cd->timer, "notify::active", G_CALLBACK (timer_active_changed), page);
                timer_active_changed (cd->timer, NULL, page);
                g_signal_connect (cd->timer, "notify::remaining", G_CALLBACK (timer_remaining_changed), page);
                timer_remaining_changed (cd->timer, NULL, page);
                gtk_revealer_set_reveal_child (GTK_REVEALER (page->cooking_revealer), TRUE);
        }
        else {
                if (cd) {
                        g_signal_handlers_disconnect_by_func (cd->timer, G_CALLBACK (timer_active_changed), page);
                        g_signal_handlers_disconnect_by_func (cd->timer, G_CALLBACK (timer_remaining_changed), page);
                        g_hash_table_remove (page->cooking, id);
                }

                g_object_set (page->timer, "timer", NULL, NULL);
                gtk_revealer_set_reveal_child (GTK_REVEALER (page->cooking_revealer), FALSE);
        }
}

static void
delete_recipe (GrDetailsPage *page)
{
        GrRecipeStore *store;
        g_autoptr(GrRecipe) recipe = NULL;
        GtkWidget *window;

        recipe = g_object_ref (page->recipe);

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));
        gr_recipe_store_remove_recipe (store, page->recipe);
        g_set_object (&page->recipe, NULL);
        g_set_object (&page->chef, NULL);

        window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
        gr_window_go_back (GR_WINDOW (window));

        gr_window_offer_undelete (GR_WINDOW (window), recipe);
}

static void
edit_recipe (GrDetailsPage *page)
{
        GtkWidget *window;

        window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
        gr_window_edit_recipe (GR_WINDOW (window), page->recipe);
}

static gboolean
more_recipes (GrDetailsPage *page)
{
        GtkWidget *window;

        window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
        gr_window_show_chef (GR_WINDOW (window), page->chef);

        return TRUE;
}

static void
print_recipe (GrDetailsPage *page)
{
        if (!page->printer) {
                GtkWidget *window;

                window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
                page->printer = gr_recipe_printer_new (GTK_WINDOW (window));
        }

        gr_recipe_printer_print (page->printer, page->recipe);
}

static void
export_recipe (GrDetailsPage *page)
{
        if (!page->exporter) {
                GtkWidget *window;

                window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
                page->exporter = gr_recipe_exporter_new (GTK_WINDOW (window));
        }

        gr_recipe_exporter_export (page->exporter, page->recipe);
}

static void populate_ingredients (GrDetailsPage *page,
                                  int            num,
                                  int            denom);

static void
serves_value_changed (GrDetailsPage *page)
{
        int serves;
        int new_value;

        new_value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (page->serves_spin));
        serves = gr_recipe_get_serves (page->recipe);
        populate_ingredients (page, new_value, serves);
}

static void
start_or_stop_timer (GrDetailsPage *page)
{
        const char *id;
        CookingData *cd;

        id = gr_recipe_get_id (page->recipe);

        cd = g_hash_table_lookup (page->cooking, id);

        g_assert (cd && cd->timer);

        if (gr_timer_get_active (cd->timer)) {
                g_object_set (cd->timer, "active", FALSE, NULL);
        }
        else {
                int seconds;

                seconds = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (page->time_spin));
                g_object_set (cd->timer,
                              "duration", seconds * 1000 * 1000,
                              "active", TRUE,
                              NULL);
        }

        gtk_popover_popdown (GTK_POPOVER (page->timer_popover));
}

static int
time_spin_input (GtkSpinButton *spin_button,
                 double        *new_val)
{
        const char *text;
        gboolean found = FALSE;

        text = gtk_entry_get_text (GTK_ENTRY (spin_button));
        if (!strchr (text, ':')) {
                g_auto(GStrv) str = NULL;
                int num;
                char *endn;

                str = g_strsplit (text, " ", 2);
                num = strtol (str[0], &endn, 10);
                if (!*endn) {
                        if (str[1] == NULL) {
                                *new_val = num; /* minutes */
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("hour")) == 0 ||
                                 strcmp (str[1], _("hours")) == 0 ||
                                 strcmp (str[1], C_("hour abbreviation", "h")) == 0) {
                                *new_val = num * 3600; /* hours */
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("minute")) == 0 ||
                                 strcmp (str[1], _("minutes")) == 0 ||
                                 strcmp (str[1], C_("minute abbreviation", "min")) == 0 ||
                                 strcmp (str[1], C_("minute abbreviation", "m")) == 0) {
                                *new_val = num * 60;
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("second")) == 0 ||
                                 strcmp (str[1], _("seconds")) == 0 ||
                                 strcmp (str[1], C_("second abbreviation", "sec")) == 0 ||
                                 strcmp (str[1], C_("second abbreviation", "s")) == 0) {
                                *new_val = num;
                                found = TRUE;
                        }
                }
        }
        else {
                g_auto(GStrv) str = NULL;
                int hours;
                int minutes;
                int seconds;
                char *endh;
                char *endm;
                char *ends;

                str = g_strsplit (text, ":", 3);

                if (g_strv_length (str) == 3) {
                        hours = strtol (str[0], &endh,10);
                        minutes = strtol (str[1], &endm, 10);
                        seconds = strtol (str[2], &ends, 10);
                        if (!*endh && !*endm && !*ends &&
                            0 <= hours && hours < 24 &&
                            0 <=  minutes && minutes < 60 &&
                            0 <= seconds && seconds < 60) {
                                *new_val = (hours * 60 + minutes) * 60 + seconds;
                                found = TRUE;
                        }
                }
                else if (g_strv_length (str) == 2) {
                        hours = strtol (str[0], &endh, 10);
                        minutes = strtol (str[1], &endm, 10);
                        if (!*endh && !*endm &&
                            0 <= hours && hours < 24 &&
                            0 <=  minutes && minutes < 60) {
                                *new_val = (hours * 60 + minutes) * 60;
                                found = TRUE;
                        }
                }
        }

        if (!found) {
                *new_val = 0.0;
                return GTK_INPUT_ERROR;
        }

        return TRUE;
}

static int
time_spin_output (GtkSpinButton *spin_button)
{
        GtkAdjustment *adjustment;
        char *buf;
        double hours;
        double minutes;
        double seconds;

        adjustment = gtk_spin_button_get_adjustment (spin_button);
        hours = gtk_adjustment_get_value (adjustment) / 3600.0;
        minutes = (hours - floor (hours)) * 60.0;
        seconds = (minutes - floor (minutes)) * 60.0;
        buf = g_strdup_printf ("%02.0f:%02.0f:%02.0f", floor (hours), floor (minutes), floor (seconds + 0.5));
        if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin_button))))
                gtk_entry_set_text (GTK_ENTRY (spin_button), buf);
        g_free (buf);

        return TRUE;
}

static void
check_clicked (GtkWidget     *button,
               GrDetailsPage *page)
{
        CookingData *cd;
        const char *id;
        gboolean active;

        id = gr_recipe_get_id (page->recipe);
        cd = g_hash_table_lookup (page->cooking, id);

        g_assert (cd);

        g_object_get (button, "active", &active, NULL);

        if (button == page->ingredients_check)
                cd->ingredients = active;
        else if (button == page->preheat_check)
                cd->preheat = active;
        else if (button == page->instructions_check)
                cd->instructions = active;
}

static void
cook_it_later (GrDetailsPage *page)
{
        GrRecipeStore *store;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->favorite_button)))
                gr_recipe_store_add_favorite (store, page->recipe);
        else
                gr_recipe_store_remove_favorite (store, page->recipe);
}

static void
details_page_finalize (GObject *object)
{
        GrDetailsPage *self = GR_DETAILS_PAGE (object);

        g_clear_object (&self->recipe);
        g_clear_object (&self->chef);
        g_clear_object (&self->ingredients);
        g_clear_object (&self->printer);
        g_clear_object (&self->exporter);
        g_clear_pointer (&self->cooking, g_hash_table_unref);
        g_clear_pointer (&self->uri, g_free);

        if (self->save_timeout) {
                g_source_remove (self->save_timeout);
                self->save_timeout = 0;
        }

        G_OBJECT_CLASS (gr_details_page_parent_class)->finalize (object);
}

static gboolean
gdouble_to_boolean (GBinding     *binding,
                    const GValue *from_value,
                    GValue       *to_value,
                    gpointer      user_data)
{
  if (g_value_get_double (from_value))
    g_value_set_boolean (to_value, TRUE);
  else
    g_value_set_boolean (to_value, FALSE);

  return TRUE;
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

static gboolean
save_notes (gpointer data)
{
        GrDetailsPage *page = data;
        GtkTextBuffer *buffer;
        GtkTextIter start, end;
        g_autofree char *text = NULL;
        GrRecipeStore *store;
        g_autofree char *id = NULL;
        g_autofree char *notes = NULL;
        g_autoptr(GError) error = NULL;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (page->notes_field));
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

        g_object_get (page->recipe,
                      "id", &id,
                      "notes", &notes,
                      NULL);
        if (g_strcmp0 (notes, text) == 0)
                goto out;

        g_object_set (page->recipe, "notes", text, NULL);

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));
        if (!gr_recipe_store_update_recipe (store, page->recipe, id, &error)) {
                g_print ("Error: %s\n", error->message);
        }

out:
        page->save_timeout = 0;

        return G_SOURCE_REMOVE;
}

static void
schedule_save (GtkTextBuffer *buffer, GrDetailsPage *page)
{
        if (page->save_timeout) {
                g_source_remove (page->save_timeout);
                page->save_timeout = 0;
        }

        page->save_timeout = g_timeout_add (250, save_notes, page);
}

static gboolean
activate_uri_at_idle (gpointer data)
{
        GrDetailsPage *page = data;
        g_autofree char *uri = NULL;

        uri = page->uri;
        page->uri = NULL;

        if (g_str_has_prefix (uri, "image:")) {
                int idx;

                idx = (int)g_ascii_strtoll (uri + strlen ("image:"), NULL, 10);
                gr_image_viewer_show_image (GR_IMAGE_VIEWER (page->recipe_image), idx);
        }
        else if (g_str_has_prefix (uri, "recipe:")) {
                GrRecipeStore *store;
                const char *id;
                g_autoptr(GrRecipe) recipe = NULL;

                store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

                id = uri + strlen ("recipe:");
                recipe = gr_recipe_store_get_recipe (store, id);
                if (recipe) {
                        GtkWidget *window;

                        window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
                        gr_window_show_recipe (GR_WINDOW (window), recipe);
                }
                else {
                        gtk_label_set_label (GTK_LABEL (page->error_label),
                                             _("Could not find this recipe."));
                        gtk_revealer_set_reveal_child (GTK_REVEALER (page->error_revealer), TRUE);
                }
        }

        return G_SOURCE_REMOVE;
}

static gboolean
activate_link (GtkLabel      *label,
               const char    *uri,
               GrDetailsPage *page)
{
        g_free (page->uri);
        page->uri = g_strdup (uri);

        // FIXME: We can avoid the idle with GTK+ 3.22.6 or newer
        g_idle_add (activate_uri_at_idle, page);

        return TRUE;
}

static void
dismiss_error (GrDetailsPage *page)
{
        gtk_revealer_set_reveal_child (GTK_REVEALER (page->error_revealer), FALSE);
}

static void
gr_details_page_init (GrDetailsPage *page)
{
        gtk_widget_set_has_window (GTK_WIDGET (page), FALSE);
        gtk_widget_init_template (GTK_WIDGET (page));
        connect_store_signals (page);
        page->cooking = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, cooking_data_free);

        g_object_bind_property_full (page->time_spin, "value",
                                     page->start_button, "sensitive",
                                     0,
                                     gdouble_to_boolean,
                                     NULL,
                                     NULL,
                                     NULL);

        g_signal_connect (gtk_text_view_get_buffer (GTK_TEXT_VIEW (page->notes_field)), "changed", G_CALLBACK (schedule_save), page);

#ifdef ENABLE_GSPELL
        {
                GspellTextView *gspell_view;
                gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (page->notes_field));
                gspell_text_view_basic_setup (gspell_view);
        }
#endif

#ifdef ENABLE_AUTOAR
        gtk_widget_show (page->export_button);
#endif
}

static void
gr_details_page_class_init (GrDetailsPageClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = details_page_finalize;

        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Recipes/gr-details-page.ui");

        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, recipe_image);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, prep_time_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, cook_time_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, serves_spin);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, warning_box);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, spicy_warning);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, garlic_warning);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, dairy_warning);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, gluten_warning);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, ingredients_box);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, instructions_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, cooking_revealer);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, ingredients_check);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, instructions_check);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, preheat_check);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, timer);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, timer_stack);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, timer_popover);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, time_spin);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, start_button);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, favorite_button);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, duration_stack);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, remaining_time_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, chef_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, edit_button);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, delete_button);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, notes_field);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, description_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, export_button);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, error_label);
        gtk_widget_class_bind_template_child (widget_class, GrDetailsPage, error_revealer);

        gtk_widget_class_bind_template_callback (widget_class, edit_recipe);
        gtk_widget_class_bind_template_callback (widget_class, delete_recipe);
        gtk_widget_class_bind_template_callback (widget_class, more_recipes);
        gtk_widget_class_bind_template_callback (widget_class, print_recipe);
        gtk_widget_class_bind_template_callback (widget_class, export_recipe);
        gtk_widget_class_bind_template_callback (widget_class, serves_value_changed);
        gtk_widget_class_bind_template_callback (widget_class, start_or_stop_timer);
        gtk_widget_class_bind_template_callback (widget_class, time_spin_input);
        gtk_widget_class_bind_template_callback (widget_class, time_spin_output);
        gtk_widget_class_bind_template_callback (widget_class, check_clicked);
        gtk_widget_class_bind_template_callback (widget_class, cook_it_later);
        gtk_widget_class_bind_template_callback (widget_class, activate_link);
        gtk_widget_class_bind_template_callback (widget_class, dismiss_error);
}

GtkWidget *
gr_details_page_new (void)
{
        GrDetailsPage *page;

        page = g_object_new (GR_TYPE_DETAILS_PAGE, NULL);

        return GTK_WIDGET (page);
}

static void
populate_ingredients (GrDetailsPage *page,
                      int            num,
                      int            denom)
{
        g_autoptr(GtkSizeGroup) group = NULL;
        g_autofree char **segments = NULL;
        int i, j;
        GtkWidget *list;
        GtkWidget *label;

        container_remove_all (GTK_CONTAINER (page->ingredients_box));

        group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
        segments = gr_ingredients_list_get_segments (page->ingredients);
        for (j = 0; segments[j]; j++) {
                g_auto(GStrv) ings = NULL;

                if (segments[j] && segments[j][0]) {
                        label = gtk_label_new (segments[j]);
                        gtk_widget_show (label);
                        gtk_label_set_xalign (GTK_LABEL (label), 0);
                        gtk_style_context_add_class (gtk_widget_get_style_context (label), "heading");
                        gtk_container_add (GTK_CONTAINER (page->ingredients_box), label);
                }

                list = gtk_list_box_new ();
                gtk_widget_show (list);
                gtk_style_context_add_class (gtk_widget_get_style_context (list), "frame");
                gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
                gtk_list_box_set_header_func (GTK_LIST_BOX (list), all_headers, NULL, NULL);
                gtk_container_add (GTK_CONTAINER (page->ingredients_box), list);

                ings = gr_ingredients_list_get_ingredients (page->ingredients, segments[j]);
                for (i = 0; ings[i]; i++) {
                        GtkWidget *row;
                        GtkWidget *box;
                        g_autofree char *s = NULL;

                        box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
                        gtk_widget_show (box);

                        s = gr_ingredients_list_scale_unit (page->ingredients, segments[j], ings[i], num, denom);
                        label = gtk_label_new (s);
                        g_object_set (label,
                                      "visible", TRUE,
                                      "xalign", 0.0,
                                      "margin", 10,
                                      NULL);
                        gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
                        gtk_container_add (GTK_CONTAINER (box), label);
                        gtk_size_group_add_widget (group, label);

                        label = gtk_label_new (ings[i]);
                        g_object_set (label,
                                      "visible", TRUE,
                                      "xalign", 0.0,
                                      "margin", 10,
                                      NULL);
                        gtk_container_add (GTK_CONTAINER (box), label);

                        gtk_container_add (GTK_CONTAINER (list), box);
                        row = gtk_widget_get_parent (box);
                        gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
                }
        }

        gtk_widget_hide (page->warning_box);
        gtk_widget_hide (page->garlic_warning);
        gtk_widget_hide (page->dairy_warning);
        gtk_widget_hide (page->gluten_warning);
        gtk_widget_hide (page->spicy_warning);

        if (gr_recipe_contains_garlic (page->recipe)) {
                gtk_widget_show (page->warning_box);
                gtk_widget_show (page->garlic_warning);
        }
        if (gr_recipe_contains_dairy (page->recipe)) {
                gtk_widget_show (page->warning_box);
                gtk_widget_show (page->dairy_warning);
        }
        if (gr_recipe_contains_gluten (page->recipe)) {
                gtk_widget_show (page->warning_box);
                gtk_widget_show (page->gluten_warning);
        }

        if (gr_recipe_get_spiciness (page->recipe) > 50) {
                gtk_widget_show (page->warning_box);
                gtk_widget_show (page->spicy_warning);
                if (gr_recipe_get_spiciness (page->recipe) > 75) {
                        gtk_widget_set_tooltip_text (page->spicy_warning, _("Very spicy"));
                        gtk_style_context_add_class (gtk_widget_get_style_context (page->spicy_warning),
                                                     "very-spicy");
                }
                else {
                        gtk_widget_set_tooltip_text (page->spicy_warning, _("Spicy"));
                        gtk_style_context_remove_class (gtk_widget_get_style_context (page->spicy_warning),
                                                        "very-spicy");
                }
        }
}

void
gr_details_page_set_recipe (GrDetailsPage *page,
                            GrRecipe      *recipe)
{
        const char *id;
        const char *author;
        const char *prep_time;
        const char *cook_time;
        int serves;
        const char *ingredients;
        const char *instructions;
        const char *notes;
        const char *description;
        GrRecipeStore *store;
        g_autoptr(GrChef) chef = NULL;
        g_autoptr(GrIngredientsList) ing = NULL;
        g_autoptr(GArray) images = NULL;
        gboolean cooking;
        gboolean favorite;

        g_set_object (&page->recipe, recipe);

        id = gr_recipe_get_id (recipe);
        author = gr_recipe_get_author (recipe);
        serves = gr_recipe_get_serves (recipe);
        prep_time = gr_recipe_get_prep_time (recipe);
        cook_time = gr_recipe_get_cook_time (recipe);
        ingredients = gr_recipe_get_ingredients (recipe);
        notes = gr_recipe_get_notes (recipe);
        instructions = gr_recipe_get_translated_instructions (recipe);
        description = gr_recipe_get_translated_description (recipe);

        g_object_get (recipe, "images", &images, NULL);
        gr_image_viewer_set_images (GR_IMAGE_VIEWER (page->recipe_image), images);

        ing = gr_ingredients_list_new (ingredients);
        g_set_object (&page->ingredients, ing);

        populate_ingredients (page, serves, serves);

        if (prep_time[0] == '\0')
                gtk_label_set_label (GTK_LABEL (page->prep_time_label), "");
        else
                gtk_label_set_label (GTK_LABEL (page->prep_time_label), _(prep_time));

        if (cook_time[0] == '\0')
                gtk_label_set_label (GTK_LABEL (page->cook_time_label), "");
        else
                gtk_label_set_label (GTK_LABEL (page->cook_time_label), _(cook_time));
        gtk_label_set_label (GTK_LABEL (page->instructions_label), instructions);
        gtk_label_set_track_visited_links (GTK_LABEL (page->instructions_label), FALSE);

        gtk_spin_button_set_value (GTK_SPIN_BUTTON (page->serves_spin), serves);
        gtk_widget_set_sensitive (page->serves_spin, ing != NULL);

        cooking = g_hash_table_lookup (page->cooking, id) != NULL;
        set_cooking (page, cooking);

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        favorite = gr_recipe_store_is_favorite (store, recipe);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->favorite_button), favorite);

        chef = gr_recipe_store_get_chef (store, author);
        g_set_object (&page->chef, chef);

        if (chef) {
                g_autofree char *tmp = NULL;
                g_autofree char *link = NULL;

                link = g_strdup_printf ("<a href=\"chef\">%s</a>", gr_chef_get_name (chef));
                tmp = g_strdup_printf (_("Recipe by %s"), link);
                gtk_widget_show (page->chef_label);
                gtk_label_set_markup (GTK_LABEL (page->chef_label), tmp);
        }
        else {
                gtk_widget_hide (page->chef_label);
        }

        gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (page->notes_field)),
                                  notes ? notes : "", -1);

        if (description && description[0]) {
                gtk_label_set_label (GTK_LABEL (page->description_label), description);
                gtk_widget_show (page->description_label);
        }
        else {
                gtk_widget_hide (page->description_label);
        }

        if (gr_recipe_is_readonly (recipe)) {
                gtk_widget_hide (page->edit_button);
                gtk_widget_hide (page->delete_button);
        }
        else {
                gtk_widget_show (page->edit_button);
                gtk_widget_show (page->delete_button);
        }
}

GrRecipe *
gr_details_page_get_recipe (GrDetailsPage *page)
{
        return page->recipe;
}

static void
details_page_reload (GrDetailsPage *page,
                     GrRecipe      *recipe)
{
        const char *name;
        const char *new_name;

        name = gr_recipe_get_id (page->recipe);
        new_name = gr_recipe_get_id (recipe);
        if (strcmp (name, new_name) == 0)
                gr_details_page_set_recipe (page, recipe);
}

static void
connect_store_signals (GrDetailsPage *page)
{
        GrRecipeStore *store;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        g_signal_connect_swapped (store, "recipe-changed", G_CALLBACK (details_page_reload), page);
}

gboolean
gr_details_page_is_cooking (GrDetailsPage *page)
{
        return gtk_revealer_get_reveal_child (GTK_REVEALER (page->cooking_revealer));
}

void
gr_details_page_set_cooking (GrDetailsPage *page,
                             gboolean       cooking)
{
        set_cooking (page, cooking);
}

