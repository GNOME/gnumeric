/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * color-palette.c - A color selector palette
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 * This code was extracted from widget-color-combo.c
 *   written by Miguel de Icaza (miguel@kernel.org) and
 *   Dom Lachowicz (dominicl@seas.upenn.edu). The extracted
 *   code was re-packaged into a separate object by
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *   And later revised and polished by
 *   Almer S. Tigelaar (almer@gnome.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#undef GNOME_DISABLE_DEPRECATED
#warning "This file uses GNOME_DISABLE_DEPRECATED for GnomeColorPicker"

#include <gnumeric-config.h>
#include <gdk/gdkcolor.h>
#include <glib/gi18n.h>
#include <gnm-marshalers.h>
#include <gsf/gsf-impl-utils.h>
#include "color-group.h"
#include "color-palette.h"

#include <goffice/utils/go-color.h>
#include <gui-util.h>
#include <style-color.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkdrawingarea.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkimage.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomeui/gnome-color-picker.h>

#include <string.h>

#define COLOR_PREVIEW_WIDTH 10
#define COLOR_PREVIEW_HEIGHT 10

enum {
	COLOR_CHANGED,
	LAST_SIGNAL
};

struct _ColorNamePair {
	char const *color;	/* rgb color or otherwise - eg. "#FFFFFF" */
	char const *name;	/* english name - eg. "white" */
};

static guint color_palette_signals [LAST_SIGNAL] = { 0, };

#define PARENT_TYPE GTK_TYPE_VBOX
static GObjectClass *color_palette_parent_class;

#define make_color(P,COL) (((COL) != NULL) ? (COL) : ((P) ? ((P)->default_color) : NULL))

static void
color_palette_destroy (GtkObject *object)
{
	ColorPalette *P = COLOR_PALETTE (object);
	GtkObjectClass *klass = (GtkObjectClass *)color_palette_parent_class;

	if (P->tool_tip) {
		g_object_unref (P->tool_tip);
		P->tool_tip = NULL;
	}

	if (P->current_color) {
		gdk_color_free (P->current_color);
		P->current_color = NULL;
	}

	color_palette_set_group (P, NULL);

	memset (P->swatches, 0, P->total * sizeof (GtkWidget *));

	if (klass->destroy)
                klass->destroy (object);
}

static void
color_palette_finalize (GObject *object)
{
	ColorPalette *P = COLOR_PALETTE (object);

	g_free (P->swatches);

	(*color_palette_parent_class->finalize) (object);
}

static void
color_palette_class_init (GObjectClass *gobject_class)
{
	gobject_class->finalize = color_palette_finalize;
	((GtkObjectClass *)gobject_class)->destroy = color_palette_destroy;

	color_palette_parent_class = g_type_class_peek_parent (gobject_class);

	color_palette_signals [COLOR_CHANGED] =
		g_signal_new ("color_changed",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ColorPaletteClass, color_changed),
			      NULL, NULL,
			      gnm__VOID__POINTER_BOOLEAN_BOOLEAN_BOOLEAN,
			      G_TYPE_NONE, 4, G_TYPE_POINTER,
			      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
}

GSF_CLASS(ColorPalette,color_palette,color_palette_class_init,NULL,PARENT_TYPE)

static void
emit_color_changed (ColorPalette *P, GdkColor const *color,
		    gboolean custom, gboolean by_user, gboolean is_default)
{
	GdkColor *new_col = NULL;

	if (make_color (P, color) != NULL)
		new_col = gdk_color_copy (make_color (P, color));
	if (P->current_color)
		gdk_color_free (P->current_color);
	P->current_color = new_col;
	P->current_is_default = is_default;

	/* Only add custom colors to the group */
	if (custom && color)
		color_group_add_color (P->color_group, color);

	g_signal_emit (P, color_palette_signals [COLOR_CHANGED], 0,
		       color, custom, by_user, is_default);
}

/*
 * Add the new custom color as the first custom color in the custom color rows
 * and shift all of the others 'one step down'
 *
 * Also take care of setting up the GnomeColorPicker 'display'
 */
static void
color_palette_change_custom_color (ColorPalette *P, GdkColor const *new_color)
{
	int index;
	GtkWidget *swatch;
	GtkWidget *next_swatch = NULL;

	g_return_if_fail (P != NULL);
	g_return_if_fail (new_color != NULL);
	g_return_if_fail (P->picker);

	/* make sure there is room */
	if (P->custom_color_pos == -1)
		return;

	for (index = P->custom_color_pos; index < P->total - 1; index++) {
		GtkStyle *style;
		swatch = P->swatches[index];
		next_swatch = P->swatches[index + 1];

		style = gtk_style_copy (next_swatch->style);
		gtk_widget_set_style (swatch, style);
		g_object_unref (style);
	}
	if (next_swatch != NULL) {
		next_swatch->style->bg[GTK_STATE_NORMAL] = *new_color;
		gnome_color_picker_set_i16 (GNOME_COLOR_PICKER (P->picker),
			new_color->red, new_color->green, new_color->blue, 0);
	}
}

/*
 * The custom color box was clicked. Find out its value and emit it
 * And add it to the custom color row
 */
static void
cust_color_set (GtkWidget  *color_picker, guint r, guint g, guint b, guint a,
		ColorPalette *P)
{
	GdkColor c_color;

	c_color.red   = (gushort)r;
	c_color.green = (gushort)g;
	c_color.blue  = (gushort)b;

	gdk_colormap_alloc_color (gtk_widget_get_colormap (color_picker), &c_color, FALSE, TRUE);
	gdk_colormap_query_color (gtk_widget_get_colormap (color_picker), c_color.pixel, &c_color);
	emit_color_changed (P, &c_color, TRUE, TRUE, FALSE);
}

static gboolean
cb_default_release_event (GtkWidget *button, GdkEventButton *event, ColorPalette *P)
{
	emit_color_changed (P, P->default_color, FALSE, TRUE, TRUE);
	return TRUE;
}

static gboolean
swatch_activated (ColorPalette *P, GtkBin *button)
{
	GList *tmp = gtk_container_get_children (GTK_CONTAINER (gtk_bin_get_child (button)));
	GtkWidget *swatch = (tmp != NULL) ? tmp->data : NULL;

	g_list_free (tmp);

	g_return_val_if_fail (swatch != NULL, TRUE);

	emit_color_changed (P,
		&swatch->style->bg[GTK_STATE_NORMAL],
		FALSE, TRUE, FALSE);

	return TRUE;
}

static gboolean
cb_swatch_release_event (GtkBin *button, GdkEventButton *event, ColorPalette *P)
{
#warning TODO do I want to check for which button ?
	return swatch_activated (P, button);
}
static gboolean
cb_swatch_key_press (GtkBin *button, GdkEventKey *event, ColorPalette *P)
{
	if (event->keyval == GDK_Return ||
	    event->keyval == GDK_KP_Enter ||
	    event->keyval == GDK_space)
		return swatch_activated (P, button);
	else
		return FALSE;
}


static void
cb_group_custom_color_add (GtkObject *cg, GdkColor *color, ColorPalette *P)
{
	color_palette_change_custom_color (P, make_color (P, color));
}

/*
 * Find out if a color is in the default palette (not in the custom colors!)
 *
 * Utility function
 */
static gboolean
color_in_palette (ColorNamePair *set, GdkColor *color)
{
	int i;

	g_return_val_if_fail (set != NULL, FALSE);

	if (color == NULL)
		return TRUE;

	/* Iterator over all the colors and try to find
	 * if we can find @color
	 */
	for (i = 0; set[i].color != NULL; i++) {
		GdkColor current;

		gdk_color_parse (set[i].color, &current);

		if (gdk_color_equal (color, &current))
			return TRUE;
	}

	return FALSE;
}

/*
 * Create the individual color buttons
 *
 * Utility function
 */
static GtkWidget *
color_palette_button_new (ColorPalette *P, GtkTable* table,
			  GtkTooltips *tool_tip, ColorNamePair* color_name,
			  gint col, gint row, int data)
{
        GtkWidget *button, *swatch, *box;
	GdkColor   c;

	swatch = gtk_drawing_area_new ();
	gdk_color_parse (color_name->color, &c);
	gtk_widget_modify_bg (swatch, GTK_STATE_NORMAL, &c);
	gtk_widget_set_size_request (swatch, COLOR_PREVIEW_WIDTH, COLOR_PREVIEW_HEIGHT);

	/* Wrap inside a vbox with a border so that we can see the focus indicator */
	box = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (box), 2);
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (swatch), TRUE, TRUE, 0);

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_container_add (GTK_CONTAINER (button), box);
	gtk_tooltips_set_tip (tool_tip, button, _(color_name->name), "");

	gtk_table_attach (table, button,
		col, col+1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

	g_object_connect (button,
		"signal::button_release_event", G_CALLBACK (cb_swatch_release_event), P,
		"signal::key_press_event", G_CALLBACK (cb_swatch_key_press), P,
		NULL);
	return swatch;
}

static void
cb_custom_colors (GdkColor const *color, gpointer data)
{
	ColorPalette *P = data;

	if (color)
		color_palette_change_custom_color (P, color);
}

/*
 * Creates the color table
 */
static GtkWidget *
color_palette_setup (ColorPalette *P,
		     char const *no_color_label,
		     int ncols, int nrows,
		     ColorNamePair *color_names)
{
	GtkWidget *default_button;
	GtkWidget *cust_label;
	GtkWidget *table;
	GtkTooltips *tool_tip;
	int total, row, col;

	table = gtk_table_new (ncols, nrows, FALSE);

	if (no_color_label != NULL) {
		default_button = gtk_button_new_with_label (no_color_label);
		gtk_table_attach (GTK_TABLE (table), default_button,
				  0, ncols, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
		g_signal_connect (default_button,
			"button_release_event",
			G_CALLBACK (cb_default_release_event), P);
	}

	P->tool_tip = tool_tip = gtk_tooltips_new ();
	g_object_ref (P->tool_tip);
	gtk_object_sink (GTK_OBJECT (P->tool_tip));

	P->custom_color_pos = -1;
	total = 0;

	for (row = 0; row < nrows; row++) {
		for (col = 0; col < ncols; col++) {
			int pos;

			pos = row * ncols + col;
			/*
			 * If we are done with all of the colors in color_names
			 */
			if (color_names [pos].color == NULL) {
				/* This is the default custom color */
				ColorNamePair color_name  = {"#000", N_("custom")};
				row++;
				if (col == 0 || row < nrows) {
					/* Add a full row for custom colors */
					for (col = 0; col < ncols; col++) {
						/* Have we set custom pos yet ? */
						if (P->custom_color_pos == -1) {
							P->custom_color_pos = total;
						}
						P->swatches[total] =
							color_palette_button_new (
								P,
								GTK_TABLE (table),
								GTK_TOOLTIPS (tool_tip),
								&(color_name),
								col,
								row + 1,
								total);
						total++;
					}
				}
				/* Break out of two for-loops.  */
				row = nrows;
				break;
			}

			P->swatches[total] =
				color_palette_button_new (
					P,
					GTK_TABLE (table),
					GTK_TOOLTIPS (tool_tip),
					&(color_names [pos]),
					col,
					row + 1,
					total);
			total++;
		}
	}
	P->total = total;


	/* "Custom" color - we'll pop up a GnomeColorPicker */
	cust_label = gtk_label_new (_("Custom Color:"));
	gtk_table_attach (GTK_TABLE (table), cust_label, 0, ncols - 3 ,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	/*
	  Keep a pointer to the picker so that we can update it's color
	  to keep it in synch with that of other members of the group
	*/
	P->picker = gnome_color_picker_new ();
	gnome_color_picker_set_title (GNOME_COLOR_PICKER (P->picker),
	    _("Choose Custom Color"));
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (P->picker), ncols - 3, ncols,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	g_signal_connect (P->picker, "color_set",
			  G_CALLBACK (cust_color_set), P);
	return table;
}

void
color_palette_set_color_to_default (ColorPalette *P)
{
	g_return_if_fail (P != NULL);
	g_return_if_fail (IS_COLOR_GROUP (P->color_group));

	emit_color_changed (P, P->default_color, FALSE, TRUE, TRUE);
}

void
color_palette_set_current_color (ColorPalette *P, GdkColor *color)
{
	g_return_if_fail (P != NULL);
	g_return_if_fail (IS_COLOR_GROUP (P->color_group));

	if (color)
		emit_color_changed
			(P, color, color_in_palette (P->default_set, color),
			 FALSE, FALSE);
	else
		color_palette_set_color_to_default (P);
}

GdkColor *
color_palette_get_current_color (ColorPalette *P, gboolean *is_default)
{
	g_return_val_if_fail (P != NULL, NULL);
	g_return_val_if_fail (IS_COLOR_GROUP (P->color_group), NULL);

	if (is_default != NULL)
		*is_default = P->current_is_default;

	return P->current_color ? gdk_color_copy (P->current_color) : NULL;
}

GtkWidget *
color_palette_get_color_picker (ColorPalette *P)
{
	g_return_val_if_fail (IS_COLOR_PALETTE (P), NULL);

	return P->picker;
}


/*
 * More verbose constructor. Allows for specifying the rows, columns, and
 * Colors this palette will contain
 *
 * Note that if after placing all of the color_names there remains an entire
 * row available then a row of custum colors (initialized to black) is added
 */
static GtkWidget *
color_palette_new_with_vals (char const *no_color_label,
			     int ncols, int nrows, ColorNamePair *color_names,
			     GdkColor const *default_color,
			     ColorGroup *cg)
{
	ColorPalette *P;

	g_return_val_if_fail (color_names != NULL, NULL);

	P = g_object_new (COLOR_PALETTE_TYPE, NULL);

	P->default_set   = color_names;
	P->default_color = default_color;
	P->current_color = default_color ? gdk_color_copy (default_color) : NULL;
	P->current_is_default = TRUE;
	P->swatches = g_malloc (sizeof (GtkWidget *) * ncols * nrows);
	color_palette_set_group (P, cg);

	gtk_container_add (GTK_CONTAINER (P),
		color_palette_setup (P, no_color_label, ncols, nrows,
				     P->default_set));
	/* Sync our own palette with all the custom colors in the group */
	color_group_get_custom_colors (P->color_group,
		(CbCustomColors) cb_custom_colors, P);

	return GTK_WIDGET (P);
}

/**
 * color_palette_set_group : absorbs the reference to the group
 */
void
color_palette_set_group (ColorPalette *P, ColorGroup *cg)
{
	if (P->color_group == cg)
		return;

	if (P->color_group) {
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (P->color_group),
			G_CALLBACK (cb_group_custom_color_add),
			P);
		g_object_unref (G_OBJECT (P->color_group));
		P->color_group = NULL;
	}
	if (cg != NULL) {
		P->color_group = COLOR_GROUP (cg);
		g_signal_connect (G_OBJECT (cg), "custom_color_add",
			G_CALLBACK (cb_group_custom_color_add),
			P);

	}
}

static ColorNamePair default_color_set [] = {
	{"#000000", N_("black")},
	{"#993300", N_("light brown")},
	{"#333300", N_("brown gold")},
	{"#003300", N_("dark green #2")},
	{"#003366", N_("navy")},
	{"#000080", N_("dark blue")},
	{"#333399", N_("purple #2")},
	{"#333333", N_("very dark gray")},


	{"#800000", N_("dark red")},
	{"#FF6600", N_("red-orange")},
	{"#808000", N_("gold")},
	{"#008000", N_("dark green")},
	{"#008080", N_("dull blue")},
	{"#0000FF", N_("blue")},
	{"#666699", N_("dull purple")},
	{"#808080", N_("dark gray")},


	{"#FF0000", N_("red")},
	{"#FF9900", N_("orange")},
	{"#99CC00", N_("lime")},
	{"#339966", N_("dull green")},
	{"#33CCCC",N_("dull blue #2")},
	{"#3366FF", N_("sky blue #2")},
	{"#800080", N_("purple")},
	{"#969696", N_("gray")},


	{"#FF00FF", N_("magenta")},
	{"#FFCC00", N_("bright orange")},
	{"#FFFF00", N_("yellow")},
	{"#00FF00", N_("green")},
	{"#00FFFF", N_("cyan")},
	{"#00CCFF", N_("bright blue")},
	{"#993366", N_("red purple")},
	{"#C0C0C0", N_("light gray")},


	{"#FF99CC", N_("pink")},
	{"#FFCC99", N_("light orange")},
	{"#FFFF99", N_("light yellow")},
	{"#CCFFCC", N_("light green")},
	{"#CCFFFF", N_("light cyan")},
	{"#99CCFF", N_("light blue")},
	{"#CC99FF", N_("light purple")},
	{"#FFFFFF", N_("white")},

	/* Disable these for now, they are mostly repeats */
	{NULL, NULL},

	{"#9999FF", N_("purplish blue")},
	{"#993366", N_("red purple")},
	{"#FFFFCC", N_("light yellow")},
	{"#CCFFFF", N_("light blue")},
	{"#660066", N_("dark purple")},
	{"#FF8080", N_("pink")},
	{"#0066CC", N_("sky blue")},
	{"#CCCCFF", N_("light purple")},

	{"#000080", N_("dark blue")},
	{"#FF00FF", N_("magenta")},
	{"#FFFF00", N_("yellow")},
	{"#00FFFF", N_("cyan")},
	{"#800080", N_("purple")},
	{"#800000", N_("dark red")},
	{"#008080", N_("dull blue")},
	{"#0000FF", N_("blue")},

	{NULL, NULL}
};



/*
 * Default constructor. Pass an optional label for
 * the no/auto color button.
 */
GtkWidget*
color_palette_new (char const *no_color_label,
		   GdkColor const *default_color,
		   ColorGroup *color_group)
{
	/* specify 6 rows to allow for a row of custom colors */
	return color_palette_new_with_vals (no_color_label,
					    8, 6,
					    default_color_set, default_color,
					    color_group);
}

/***********************************************************************/

typedef GtkMenu	GOMenuColor;
typedef struct {
	GtkMenuClass	base;
	void (* color_changed) (GOMenuColor *menu, GdkColor *color,
				gboolean custom, gboolean by_user, gboolean is_default);
} GOMenuColorClass;

static guint go_menu_color_signals [LAST_SIGNAL] = { 0, };
static void
go_menu_color_class_init (GObjectClass *gobject_class)
{
	go_menu_color_signals [COLOR_CHANGED] =
		g_signal_new ("color_changed",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GOMenuColorClass, color_changed),
			      NULL, NULL,
			      gnm__VOID__POINTER_BOOLEAN_BOOLEAN_BOOLEAN,
			      G_TYPE_NONE, 4, G_TYPE_POINTER,
			      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
}

static GSF_CLASS (GOMenuColor, go_menu_color,
		  go_menu_color_class_init, NULL,
		  GTK_TYPE_MENU)

static GtkWidget *
make_colored_menu_item (char const *label, GdkColor const *c)
{
	GtkWidget *button;
	GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
		COLOR_PREVIEW_WIDTH, COLOR_PREVIEW_HEIGHT);
	gdk_pixbuf_fill (pixbuf, GDK_TO_UINT (*c));

	button = gtk_image_menu_item_new_with_label (label);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (button),
		gtk_image_new_from_pixbuf (pixbuf));
	g_object_unref (pixbuf);
	gtk_widget_show_all (button);

	g_object_set_data_full (G_OBJECT (button), "gdk_color",
		gdk_color_copy  (c), (GDestroyNotify) gdk_color_free);
	return button;
}

static void
cb_menu_default_activate (GtkWidget *button, GtkWidget *menu)
{
	g_signal_emit (menu, go_menu_color_signals [COLOR_CHANGED], 0,
		       NULL, FALSE, TRUE, TRUE);
}

static void
cb_menu_color_activate (GtkWidget *button, GtkWidget *menu)
{
	GdkColor const *color = g_object_get_data (G_OBJECT (button), "gdk_color");
	g_signal_emit (menu, go_menu_color_signals [COLOR_CHANGED], 0,
		       color, FALSE, TRUE, FALSE);
}

GtkWidget *
color_palette_make_menu (char const *no_color_label,
			 GdkColor const *default_color,
			 ColorGroup *color_group)
{
	int ncols = 8;
	int nrows = 6;
	int col, row, pos, table_row = 0;
	ColorNamePair *color_names = default_color_set;
        GtkWidget *button, *submenu;
	GdkColor   c;

	submenu = g_object_new (go_menu_color_get_type (), NULL);

	if (no_color_label != NULL) {
		if (default_color != NULL)
			button = make_colored_menu_item (no_color_label, default_color);
		else
			button = gtk_menu_item_new_with_label (no_color_label);
		gtk_menu_attach (GTK_MENU (submenu), button, 0, ncols, 0, 1);

		g_signal_connect (G_OBJECT (button),
			"activate",
			G_CALLBACK (cb_menu_default_activate), submenu);
		table_row++;
	}
	for (row = 0; row < nrows; row++, table_row++) {
		for (col = 0; col < ncols; col++) {
			pos = row * ncols + col;
			if (color_names [pos].color == NULL) {
				/* Break out of two for-loops.  */
				row = nrows;
				break;
			}

			gdk_color_parse (color_names [pos].color, &c);
			button = make_colored_menu_item (" ", &c);
			gtk_menu_attach (GTK_MENU (submenu), button,
				col, col+1, table_row, table_row+1);
			g_signal_connect (G_OBJECT (button),
				"activate",
				G_CALLBACK (cb_menu_color_activate), submenu);
		}
	}
	gtk_widget_show (submenu);

	return submenu;
}
