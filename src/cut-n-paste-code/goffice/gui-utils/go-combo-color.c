/*
 * WidgetColorCombo: A color selector combo box
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include "gtk-combo-box.h"
#include "widget-color-combo.h"

#define COLOR_PREVIEW_WIDTH 15
#define COLOR_PREVIEW_HEIGHT 15

enum {
	CHANGED,
	LAST_SIGNAL
};

static gint color_combo_signals [LAST_SIGNAL] = { 0, };

static GtkObjectClass *color_combo_parent_class;

static void
color_combo_finalize (GtkObject *object)
{
	ColorCombo *cc = COLOR_COMBO (object);

	g_free (cc->items);
	(*color_combo_parent_class->finalize) (object);
}

static void
color_combo_class_init (GtkObjectClass *object_class)
{
	object_class->finalize = color_combo_finalize;

	color_combo_parent_class = gtk_type_class (gtk_combo_box_get_type ());
	
	color_combo_signals [CHANGED] =
		gtk_signal_new (
			"changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (ColorComboClass, changed),
			gtk_marshal_NONE__POINTER_INT,
			GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, color_combo_signals, LAST_SIGNAL);
}

GtkType
color_combo_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"ColorCombo",
			sizeof (ColorCombo),
			sizeof (ColorComboClass),
			(GtkClassInitFunc) color_combo_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_combo_box_get_type (), &info);
	}

	return type;
}

static void
color_clicked (GtkWidget *button, ColorCombo *combo)
{
	int index = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (button)));
	GnomeCanvasItem *item = combo->items [index];
	GdkColor *gdk_color;
	
	gtk_object_get (
		GTK_OBJECT (item),
		"fill_color_gdk", &gdk_color,
		NULL);

	combo->last_index = index;
	
	gtk_signal_emit (
		GTK_OBJECT (combo), color_combo_signals [CHANGED], gdk_color, index);

	gnome_canvas_item_set (
		combo->preview_color_item,
		"fill_color_gdk", gdk_color,
		NULL);

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (combo));
	g_free (gdk_color);
}

static GtkWidget *
color_table_setup (ColorCombo *cc, gboolean no_color, int ncols, int nrows, char **color_names)
{
	GtkWidget *label;
	GtkWidget *table;
	int total, row, col;
	
	table = gtk_table_new (ncols, nrows, 0);
	
	if (no_color){
		label = gtk_button_new_with_label (_("None"));

		gtk_table_attach (GTK_TABLE (table), label, 0, ncols, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	}
	total = 0;
	for (row = 0; row < nrows; row++){
		for (col = 0; col < ncols; col++){
			GtkWidget *button;
			GtkWidget *canvas;
			int pos;

			pos = row * ncols + col;

			if (color_names [pos] == NULL) {
				/* Break out of two for-loops.  */
				row = nrows;
				break;
			}

			button = gtk_button_new ();
			gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

			gtk_widget_push_visual (gdk_imlib_get_visual ());
			gtk_widget_push_colormap (gdk_imlib_get_colormap ());
			canvas = gnome_canvas_new ();
			gtk_widget_pop_colormap ();
			gtk_widget_pop_visual ();

			gtk_widget_set_usize (canvas, COLOR_PREVIEW_WIDTH, COLOR_PREVIEW_HEIGHT);
			gtk_container_add (GTK_CONTAINER (button), canvas);

			cc->items [total] = gnome_canvas_item_new (
				GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (canvas))),
				gnome_canvas_rect_get_type (),
				"x1", 0.0,
				"y1", 0.0,
				"x2", (double) COLOR_PREVIEW_WIDTH,
				"y2", (double) COLOR_PREVIEW_HEIGHT,
				"fill_color", color_names [pos],
				NULL);

			gtk_table_attach (GTK_TABLE (table), button,
					  col, col+1, row+1, row+2, GTK_FILL, GTK_FILL, 1, 1);

			gtk_signal_connect (GTK_OBJECT (button), "clicked",
					    GTK_SIGNAL_FUNC(color_clicked), cc);
			gtk_object_set_user_data (GTK_OBJECT (button),
						  GINT_TO_POINTER (total));
			total++;
		}
	}
	cc->total = total;
	
	gtk_widget_show_all (table);
	return table;
}

static void
emit_change (GtkWidget *button, ColorCombo *cc)
{
	GdkColor *color = NULL;
	
	if (cc->last_index != -1){
		gtk_object_get (GTK_OBJECT (cc->items [cc->last_index]),
				"fill_color_gdk", &color,
				NULL);
	}

	gtk_signal_emit (
		GTK_OBJECT (cc), color_combo_signals [CHANGED], color, cc->last_index);

	if (color)
		g_free (color);
}

static void
color_combo_construct (ColorCombo *cc, char **icon, gboolean no_color,
		       int ncols, int nrows, char **color_names)
{
	GdkImlibImage *image;
	
	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));
	g_return_if_fail (color_names != NULL);

	/*
	 * Our button with the canvas preview
	 */
	cc->preview_button = gtk_button_new ();
	gtk_widget_push_visual (gdk_imlib_get_visual ());
	gtk_widget_push_colormap (gdk_imlib_get_colormap ());
	cc->preview_canvas = GNOME_CANVAS (gnome_canvas_new ());
	gtk_widget_pop_colormap ();
	gtk_widget_pop_visual ();
	
	image = gdk_imlib_create_image_from_xpm_data (icon);
	cc->items = g_malloc (sizeof (GnomeCanvasItem *) * ncols * nrows);
	gnome_canvas_set_scroll_region (cc->preview_canvas, 0, 0, 24, 24);
	
	gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (cc->preview_canvas)),
		gnome_canvas_image_get_type (),
		"image",  image,
		"x",      0.0,
		"y",      0.0,
		"width",  (double) image->rgb_width,
		"height", (double) image->rgb_height,
		"anchor", GTK_ANCHOR_NW, 
		NULL);

	cc->preview_color_item = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (cc->preview_canvas)),
		gnome_canvas_rect_get_type (),
		"x1",         3.0,
		"y1",         19.0,
		"x2",         20.0,
		"y2",         22.0,
		"fill_color", color_names [0],
		NULL);
	gtk_container_add (GTK_CONTAINER (cc->preview_button), GTK_WIDGET (cc->preview_canvas));
	gtk_widget_set_usize (GTK_WIDGET (cc->preview_canvas), 24, 24);
	gtk_signal_connect (GTK_OBJECT (cc->preview_button), "clicked",
			    GTK_SIGNAL_FUNC (emit_change), cc);

	/*
	 * Our table selector
	 */
	cc->color_table = color_table_setup (cc, no_color, ncols, nrows, color_names);

	gtk_widget_show_all (cc->preview_button);
	
	gtk_combo_box_construct (GTK_COMBO_BOX (cc),
				 cc->preview_button,
				 cc->color_table);
}

GtkWidget *
color_combo_new_with_vals (char **icon, gboolean no_color,
			   int ncols, int nrows, char **color_names)
{
	ColorCombo *cc;
	
	g_return_val_if_fail (icon != NULL, NULL);
	g_return_val_if_fail (color_names != NULL, NULL);
	
	cc = gtk_type_new (color_combo_get_type ());

	color_combo_construct (cc, icon, no_color, ncols, nrows, color_names);
	
	return GTK_WIDGET (cc);
}

/*
 * this list of colors should match the Excel 2000 list of colors
 */
static char *default_colors [] = {
	"rgb:0/0/0",
	"rgb:FF/FF/FF",
	"rgb:FF/0/0",
	"rgb:0/FF/0",

	"rgb:0/0/FF",
	"rgb:FF/FF/0",
	"rgb:FF/0/FF",
	"rgb:0/FF/FF",
	
	"rgb:80/0/0",
	"rgb:0/80/0",
	"rgb:0/0/80",
	"rgb:80/80/0",
	
	"rgb:80/0/80",
	"rgb:0/80/80",
	"rgb:c0/c0/c0",
	"rgb:80/80/80",
	
	"rgb:99/99/FF",
	"rgb:99/33/66",
	"rgb:FF/FF/CC",
	"rgb:CC/FF/FF",
	
	"rgb:66/0/66",
	"rgb:FF/80/80",
	"rgb:0/66/CC",
	"rgb:CC/CC/FF",
	
	"rgb:0/0/80",
	"rgb:FF/0/FF",
	"rgb:FF/FF/0",
	"rgb:0/FF/FF",
	
	"rgb:80/0/80",
	"rgb:80/0/0",
	"rgb:0/80/80",
	"rgb:0/0/FF",
	
	"rgb:0/CC/FF",
	"rgb:CC/FF/FF",
	"rgb:CC/FF/CC",
	"rgb:FF/FF/99",
	
	"rgb:99/CC/FF",
	"rgb:FF/99/CC",
	"rgb:CC/99/FF",
	"rgb:FF/CC/99",
	
	"rgb:33/66/FF",
	"rgb:33/CC/CC",
	"rgb:99/CC/0",
	"rgb:FF/CC/0",
	
	"rgb:FF/99/0",
	"rgb:FF/66/0",
	"rgb:66/66/99",
	"rgb:96/96/96",
	
	"rgb:0/33/66",
	"rgb:33/99/66",
	"rgb:0/33/0",
	"rgb:33/33/0",
	
	"rgb:99/33/0",
	"rgb:99/33/66",
	"rgb:33/33/99",
	"rgb:33/33/33",
	NULL
};

GtkWidget *
color_combo_new (char **icon)
{
	return color_combo_new_with_vals (icon, TRUE, 8, 5, default_colors);
}

void
color_combo_select_color (ColorCombo *cc, int idx)
{
	GdkColor *color;
	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));
	g_return_if_fail (idx < cc->total);

	gtk_object_get (
		GTK_OBJECT (cc->items [idx]),
		"fill_color_gdk", &color,
		NULL);
	
	cc->last_index = idx;
	gnome_canvas_item_set (
		cc->preview_color_item,
		"fill_color_gdk", color,
		NULL);
	g_free (color);
}
