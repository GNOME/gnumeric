/*
 * WidgetColorCombo: A color selector combo box
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 * Reworked and split up into a separate ColorPalette object:
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *
 */
#include <config.h>
#include <gnome.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-canvas.h>
#include "color.h"
#include "gtk-combo-box.h"
#include "widget-color-combo.h"
#include "color-palette.h"

enum {
	CHANGED,
	LAST_SIGNAL
};

struct _ColorCombo {
	GtkComboBox     combo_box;

	/*
	 * Canvas where we display
	 */
	GtkWidget       *preview_button;
	GnomeCanvas     *preview_canvas;
	GnomeCanvasItem *preview_color_item;
	ColorPalette    *palette;

        GdkColor *default_color;
};

typedef struct {
	GnomeCanvasClass parent_class;

	/* Signals emited by this widget */
	void (* changed) (ColorCombo *color_combo, GdkColor *color);
} ColorComboClass;

static gint color_combo_signals [LAST_SIGNAL] = { 0, };

static GtkObjectClass *color_combo_parent_class;

#define make_color(CC,COL) (((COL) != NULL) ? (COL) : ((CC) ? ((CC)->default_color) : NULL))

static void
set_color (ColorCombo *cc, GdkColor *color)
{
	GdkColor *new_color;
	GdkColor *outline_color;

	new_color = make_color (cc,color);
	/* If the new and the default are NULL draw an outline */
	outline_color = (new_color) ? new_color : &gs_dark_gray;

	gnome_canvas_item_set (cc->preview_color_item,
			       "fill_color_gdk", new_color,
			       "outline_color_gdk", outline_color,
			       NULL);
}

static void
color_combo_finalize (GtkObject *object)
{
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
			gtk_marshal_NONE__POINTER,
			GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

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

/*
 * Fires signal "changed" with the current color as its param
 */
static void
emit_change (ColorCombo *cc)
{
  	gtk_signal_emit (
		GTK_OBJECT (cc),
		color_combo_signals [CHANGED],
		color_palette_get_current_color(cc->palette));
	gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

static void
cb_color_change (ColorPalette *P, GdkColor *color, gboolean custom, ColorCombo *cc)
{
	set_color(cc, color);
	emit_change(cc);
}



static void
preview_clicked (GtkWidget *button, ColorCombo *cc)
{
	emit_change (cc);
}

/*
 * Creates the color table
 */
static void
color_table_setup (ColorCombo *cc, char const * const no_color_label, gchar *group_name)
{
	g_return_if_fail(cc != NULL);

	/* Tell the palette that we will be changing it's custom colors */
	cc->palette = 
		COLOR_PALETTE (color_palette_new (no_color_label, 
						  cc->default_color,
						  group_name));

	gtk_signal_connect (GTK_OBJECT (cc->palette), "changed",
			    GTK_SIGNAL_FUNC (cb_color_change), cc);

	gtk_widget_show_all (GTK_WIDGET(cc->palette));

	return;
}

/*
 * Where the actual construction goes on
 */
static void
color_combo_construct (ColorCombo *cc, char **icon,
		       char const * const no_color_label,
		       gchar *group_name)
{
	GdkImlibImage *image;

	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));

	/*
	 * Our button with the canvas preview
	 */
	cc->preview_button = gtk_button_new ();
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_button_set_relief (GTK_BUTTON (cc->preview_button), GTK_RELIEF_NONE);

	gtk_widget_push_visual (gdk_imlib_get_visual ());
	gtk_widget_push_colormap (gdk_imlib_get_colormap ());
	cc->preview_canvas = GNOME_CANVAS (gnome_canvas_new ());
	gtk_widget_pop_colormap ();
	gtk_widget_pop_visual ();

	image = gdk_imlib_create_image_from_xpm_data (icon);

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
		"fill_color", "black",
		"width_pixels", 1,
		NULL);
	gtk_container_add (GTK_CONTAINER (cc->preview_button), GTK_WIDGET (cc->preview_canvas));
	gtk_widget_set_usize (GTK_WIDGET (cc->preview_canvas), 24, 24);
	gtk_signal_connect (GTK_OBJECT (cc->preview_button), "clicked",
			    GTK_SIGNAL_FUNC (preview_clicked), cc);

	
	
	/*
	 * Our table selector
	 */
	color_table_setup (cc, no_color_label, group_name);

	gtk_widget_show_all (cc->preview_button);

	gtk_combo_box_construct (GTK_COMBO_BOX (cc),
				 cc->preview_button,
				 GTK_WIDGET (cc->palette));

	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (cc), GTK_RELIEF_NONE);

	/* Start with the default color */
	set_color (cc, NULL);
}

/*
 * Default constructor. Pass an XPM icon and an optional label for
 * the no/auto color button.
 */
GtkWidget *
color_combo_new (char **icon, char const * const no_color_label,
		 GdkColor *default_color, gchar *group_name)
{
	ColorCombo *cc;

	g_return_val_if_fail (icon != NULL, NULL);

	cc = gtk_type_new (color_combo_get_type ());

        cc->default_color = default_color;

	color_combo_construct (cc, icon, no_color_label, group_name);

	return GTK_WIDGET (cc);
}




