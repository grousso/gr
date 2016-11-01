/* gr-cuisine-page.c:
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
 * GNU General Public License for more edit.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gr-cuisine-page.h"
#include "gr-recipe-store.h"
#include "gr-recipe.h"
#include "gr-recipe-tile.h"
#include "gr-app.h"
#include "gr-utils.h"
#include "gr-category.h"

typedef struct {
        const char *name;
        GtkWidget *item;
        GtkWidget *label;
        GtkWidget *box;
} Category;

struct _GrCuisinePage
{
        GtkBox     parent_instance;

        GtkWidget *sidebar;
        GtkWidget *category_box;

        int n_categories;
        Category *categories;
        Category *other;
};

G_DEFINE_TYPE (GrCuisinePage, gr_cuisine_page, GTK_TYPE_BOX)

static void
cuisine_page_finalize (GObject *object)
{
        GrCuisinePage *self = GR_CUISINE_PAGE (object);

        g_clear_pointer (&self->categories, g_free);

        G_OBJECT_CLASS (gr_cuisine_page_parent_class)->finalize (object);
}

static void
populate_initially (GrCuisinePage *self)
{
        const char **names;
        const char *title;
        int length;
        int i;

        names = gr_category_get_names (&length);

        self->n_categories = length;
        self->categories = g_new (Category, length);

        for (i = 0; i < length; i++) {
                GtkWidget *item, *label, *box;

                title = gr_category_get_title (names[i]);

                item = gtk_label_new (title);
                g_object_set (item, "xalign", 0, NULL);
                gtk_widget_show (item);
                gtk_style_context_add_class (gtk_widget_get_style_context (item), "sidebar");
                gtk_list_box_insert (GTK_LIST_BOX (self->sidebar), item, -1);

                label = gtk_label_new (title);
                g_object_set (label, "xalign", 0, NULL);
                gtk_style_context_add_class (gtk_widget_get_style_context (label), "heading");
                gtk_widget_show (label);
                gtk_container_add (GTK_CONTAINER (self->category_box), label);

                box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 20);
                gtk_widget_show (box);
                gtk_container_add (GTK_CONTAINER (self->category_box), box);

                self->categories[i].name = names[i];
                self->categories[i].item = item;
                self->categories[i].label = label;
                self->categories[i].box = box;

                if (strcmp (names[i], "other") == 0)
                        self->other = &self->categories[i];
        }
}

static void
gr_cuisine_page_init (GrCuisinePage *page)
{
        gtk_widget_set_has_window (GTK_WIDGET (page), FALSE);
        gtk_widget_init_template (GTK_WIDGET (page));
        populate_initially (page);
}

static void
gr_cuisine_page_class_init (GrCuisinePageClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = cuisine_page_finalize;

        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Recipes/gr-cuisine-page.ui");

        gtk_widget_class_bind_template_child (widget_class, GrCuisinePage, sidebar);
        gtk_widget_class_bind_template_child (widget_class, GrCuisinePage, category_box);
}

GtkWidget *
gr_cuisine_page_new (void)
{
        GrCuisinePage *page;

        page = g_object_new (GR_TYPE_CUISINE_PAGE, NULL);

        return GTK_WIDGET (page);
}

void
gr_cuisine_page_set_cuisine (GrCuisinePage *self, const char *cuisine)
{
        GrRecipeStore *store;
        g_autofree char *name = NULL;
        g_autofree char **keys = NULL;
        guint length;
        int i, j;
        GtkContainer *box;

        for (i = 0; i < self->n_categories; i++) {
                container_remove_all (GTK_CONTAINER (self->categories[i].box));
                gtk_widget_hide (self->categories[i].item);
                gtk_widget_hide (self->categories[i].label);
                gtk_widget_hide (self->categories[i].box);
        }

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        keys = gr_recipe_store_get_recipe_keys (store, &length);
        for (j = 0; j < length; j++) {
                g_autoptr(GrRecipe) recipe = NULL;
                g_autofree char *name = NULL;
                g_autofree char *cuisine2 = NULL;
                g_autofree char *category = NULL;
                GtkWidget *tile;
                Category *c;

                recipe = gr_recipe_store_get (store, keys[j]);
                g_object_get (recipe,
                              "name", &name,
                              "cuisine", &cuisine2,
                              "category", &category,
                              NULL);

                if (g_strcmp0 (cuisine, cuisine2) != 0)
                        continue;

                c = self->other;
                for (i = 0; i < self->n_categories; i++) {
                        if (strcmp (self->categories[i].name, category) == 0) {
                                c = &self->categories[i];
                                break;
                        }
                }

                gtk_widget_show (c->item);
                gtk_widget_show (c->label);
                gtk_widget_show (c->box);

                tile = gr_recipe_tile_new (recipe);
                gtk_widget_show (tile);
                gtk_container_add (GTK_CONTAINER (c->box), tile);
        }

        gtk_list_box_unselect_all (GTK_LIST_BOX (self->sidebar));
}