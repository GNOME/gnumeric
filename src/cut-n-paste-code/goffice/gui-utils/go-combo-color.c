/*
 * WidgetColorCombo: A color selector combo box
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 */
#include <config.h>
#include <gnome.h>
#include "color.h"
#include "gtk-combo-box.h"
#include "widget-color-combo.h"

#define COLOR_PREVIEW_WIDTH 15
#define COLOR_PREVIEW_HEIGHT 15
#define DEFAULT_COLOR &gs_black

enum {
	CHANGED,
	LAST_SIGNAL
};

static gint color_combo_signals [LAST_SIGNAL] = { 0, };

static GtkObjectClass *color_combo_parent_class;

typedef enum { COLOR_COPY, COLOR_NO_COPY } colcopy;

static void
set_color (ColorCombo *cc, GdkColor *color, colcopy copy)
{
	g_return_if_fail (color != NULL);

	if (cc->current)
		g_free (cc->current);

	if (copy == COLOR_COPY) {
		cc->current = g_new (GdkColor, 1);
		memcpy (cc->current, color, sizeof (GdkColor));
	} else
		cc->current = color;
}

static void
color_combo_finalize (GtkObject *object)
{
	ColorCombo *cc = COLOR_COMBO (object);

	if (cc->current)
		g_free (cc->current);
	
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
 * Fires signal "changed" with cc->current as its param
 */
static void
emit_change (ColorCombo *cc)
{
  	gtk_signal_emit (
		GTK_OBJECT (cc), color_combo_signals [CHANGED], cc->current);

	gnome_canvas_item_set (
		cc->preview_color_item,
		"fill_color_gdk", cc->current,
		NULL);

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

static void
preview_clicked (GtkWidget *button, ColorCombo *cc)
{
        emit_change (cc);
}

/*
 * The descriptive label was clicked. This should do absolutely nothing but hide the combo box
 * We *DO NOT* want to change or emit anything here
 */
static void
desc_label_clicked (GtkWidget *button, ColorCombo *cc)
{
        gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

/*
 * Something in our table was clicked. Find out what and emit it
 */
static void
color_clicked (GtkWidget *button, ColorCombo *cc)
{
	int              index;
	GnomeCanvasItem *item;
	GdkColor        *gdk_color;
	
	index = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (button)));
	item  = cc->items [index];

	gtk_object_get (
		GTK_OBJECT (item),
		"fill_color_gdk", &gdk_color,
		NULL);

	set_color (cc, gdk_color, COLOR_NO_COPY);
	emit_change (cc);
}

/*
 * The custom color box was clicked. Find out its value and emit it
 */
static void
cust_color_set (GtkWidget  *color_picker, guint r, guint g, guint b, guint a,
		ColorCombo *cc)
{
	static gboolean valid = FALSE;
        static GdkColor gdk_color;

	if (!valid)
		valid = TRUE;
	else
	        gdk_colormap_free_colors (gdk_imlib_get_colormap (), &gdk_color, 1);

	gdk_color.red   = (gushort)r;
	gdk_color.green = (gushort)g;
	gdk_color.blue  = (gushort)b;

	if (!gdk_colormap_alloc_color (gdk_imlib_get_colormap (),
				       &gdk_color,
				       TRUE, TRUE))
	        return;

	set_color   (cc, &gdk_color, COLOR_COPY);
	emit_change (cc);
}

static void
cust_color_clicked (GtkWidget *widget, ColorCombo *cc)
{
        gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

/*
 * Creates the color table
 */
static GtkWidget *
color_table_setup (ColorCombo *cc, char const * const no_color_label, int ncols, int nrows, ColorNamePair *color_names)
{
	GtkWidget *desc_label;
	GtkWidget *cust_label;
	GtkWidget *cust_color;
	GtkWidget *table;
	GtkTooltips *tool_tip;
	int total, row, col;
	
	table = gtk_table_new (ncols, nrows, 0);

	if (no_color_label != NULL) {
	        desc_label = gtk_button_new_with_label (no_color_label);

		gtk_table_attach (GTK_TABLE (table), desc_label,
				  0, ncols, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (desc_label), "clicked",
				    GTK_SIGNAL_FUNC(desc_label_clicked), cc);
	}

	tool_tip = gtk_tooltips_new();
	total = 0;
	for (row = 0; row < nrows; row++){
		for (col = 0; col < ncols; col++){
			GtkWidget *button;
			GtkWidget *canvas;
			int pos;

			pos = row * ncols + col;

			if (color_names [pos].color == NULL) {
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
				"fill_color", color_names [pos].color,
				NULL);

			gtk_tooltips_set_tip (tool_tip, button, _(color_names [pos].name),
					      "Private+Unused");

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
	
	/* "Custom" color - we'll pop up a GnomeColorPicker */
	cust_label = gtk_label_new ( _("Custom Color:"));
	gtk_table_attach (GTK_TABLE (table), cust_label, 0, ncols - 3 ,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	
	cust_color = gnome_color_picker_new ();
	gnome_color_picker_set_title (GNOME_COLOR_PICKER (cust_color),
				      _("Choose Custom Color"));
	gtk_table_attach (GTK_TABLE (table), cust_color, ncols - 3, ncols,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_signal_connect (GTK_OBJECT (cust_color), "color_set",
			    GTK_SIGNAL_FUNC (cust_color_set), cc);
	gtk_signal_connect (GTK_OBJECT (cust_color), "clicked",
			    GTK_SIGNAL_FUNC (cust_color_clicked), cc);

	gtk_widget_show_all (table);

	return table;
}

/*
 * Where the actual construction goes on
 */
static void
color_combo_construct (ColorCombo *cc, char **icon, 
		       char const * const no_color_label, 
		       int ncols, int nrows, ColorNamePair *color_names)
{
	GdkImlibImage *image;
	
	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));
	g_return_if_fail (color_names != NULL);

	set_color (cc, DEFAULT_COLOR, COLOR_COPY);

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
		"fill_color", color_names [0].color,
		NULL);
	gtk_container_add (GTK_CONTAINER (cc->preview_button), GTK_WIDGET (cc->preview_canvas));
	gtk_widget_set_usize (GTK_WIDGET (cc->preview_canvas), 24, 24);
	gtk_signal_connect (GTK_OBJECT (cc->preview_button), "clicked",
			    GTK_SIGNAL_FUNC (preview_clicked), cc);

	/*
	 * Our table selector
	 */
	cc->color_table = color_table_setup (cc, no_color_label, ncols, nrows, color_names);

	gtk_widget_show_all (cc->preview_button);
	
	gtk_combo_box_construct (GTK_COMBO_BOX (cc),
				 cc->preview_button,
				 cc->color_table);

	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (cc), GTK_RELIEF_NONE);
}

/*
 * More verbose constructor. Allows for specifying the rows, columns, and 
 * Colors this box will contain
 */
GtkWidget *
color_combo_new_with_vals (char **icon, char const * const no_color_label,
			   int ncols, int nrows, ColorNamePair *color_names)
{
	ColorCombo *cc;
	
	g_return_val_if_fail (icon != NULL, NULL);
	g_return_val_if_fail (color_names != NULL, NULL);
	
	cc = gtk_type_new (color_combo_get_type ());

	color_combo_construct (cc, icon, no_color_label, ncols, nrows, color_names);
	
	return GTK_WIDGET (cc);
}

/*
 * this list of colors should match the Excel 2000 list of colors
 */
static ColorNamePair default_colors [] = {
	{"rgb:0/0/0", N_("black")},
	{"rgb:99/33/0", N_("light brown")},
	{"rgb:33/33/0", N_("brown gold")},
	{"rgb:0/33/0", N_("dark green #2")},
	{"rgb:0/33/66", N_("navy")},
	{"rgb:0/0/80", N_("dark blue")},
	{"rgb:33/33/99", N_("purple #2")},
	{"rgb:33/33/33", N_("very dark gray")},


	{"rgb:80/0/0", N_("dark red")},
	{"rgb:FF/66/0", N_("red-orange")},
	{"rgb:80/80/0", N_("gold")},
	{"rgb:0/80/0", N_("dark green")},
	{"rgb:0/80/80", N_("dull blue")},
	{"rgb:0/0/FF", N_("blue")},
	{"rgb:66/66/99", N_("dull purple")},
	{"rgb:80/80/80", N_("dark grey")},


	{"rgb:FF/0/0", N_("red")},
	{"rgb:FF/99/0", N_("orange")},
	{"rgb:99/CC/0", N_("lime")},
	{"rgb:33/99/66", N_("dull green")},
	{"rgb:33/CC/CC",N_("dull blue #2")},
	{"rgb:33/66/FF", N_("sky blue #2")},
	{"rgb:80/0/80", N_("purple")},
	{"rgb:96/96/96", N_("gray")},


	{"rgb:FF/0/FF", N_("magenta")},
	{"rgb:FF/CC/0", N_("bright orange")},
	{"rgb:FF/FF/0", N_("yellow")},
	{"rgb:0/FF/0", N_("green")},
	{"rgb:0/FF/FF", N_("cyan")},
	{"rgb:0/CC/FF", N_("bright blue")},
	{"rgb:99/33/66", N_("red purple")},
	{"rgb:c0/c0/c0", N_("light grey")},


	{"rgb:FF/99/CC", N_("pink")},
	{"rgb:FF/CC/99", N_("light orange")},
	{"rgb:FF/FF/99", N_("light yellow")},
	{"rgb:CC/FF/CC", N_("light green")},
	{"rgb:CC/FF/FF", N_("light cyan")},
	{"rgb:99/CC/FF", N_("light blue")},
	{"rgb:CC/99/FF", N_("light purple")},
	{"rgb:FF/FF/FF", N_("white")},

	/* Disable these for now, they are mostly repeats */
	{NULL, NULL},

	{"rgb:99/99/FF", N_("purplish blue")},
	{"rgb:99/33/66", N_("red purple")},
	{"rgb:FF/FF/CC", N_("light yellow")},
	{"rgb:CC/FF/FF", N_("light blue")},
	{"rgb:66/0/66", N_("dark purple")},
	{"rgb:FF/80/80", N_("pink")},
	{"rgb:0/66/CC", N_("sky blue")},
	{"rgb:CC/CC/FF", N_("light purple")},

	{"rgb:0/0/80", N_("dark blue")},
	{"rgb:FF/0/FF", N_("magenta")},
	{"rgb:FF/FF/0", N_("yellow")},
	{"rgb:0/FF/FF", N_("cyan")},
	{"rgb:80/0/80", N_("purple")},
	{"rgb:80/0/0", N_("dark red")},
	{"rgb:0/80/80", N_("dull blue")},
	{"rgb:0/0/FF", N_("blue")},

	{NULL, NULL}
};

/* 
 * Default constructor. Pass an XPM icon and a label for what this dialog represents(or NULL)
 */
GtkWidget *
color_combo_new (char **icon, char const * const no_color_label)
{
	return color_combo_new_with_vals (icon, no_color_label, 8, 5, default_colors);
}

/*
 * Set the color combo to some pre-defined GdkColor
 */
void
color_combo_select_color (ColorCombo *cc, GdkColor *color)
{
	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));
	g_return_if_fail (cc->items != NULL);
	/* g_return_if_fail (color != NULL) */

	set_color (cc, color, COLOR_COPY);
	gnome_canvas_item_set (
		cc->preview_color_item,
		"fill_color_gdk", color,
		NULL);
}

/*
 * Resets color in ColorCombo cc to its default state
 */
void
color_combo_select_clear (ColorCombo *cc)
{
	GdkColor *color;

	g_return_if_fail (cc != NULL);
	g_return_if_fail (IS_COLOR_COMBO (cc));
	g_return_if_fail (cc->items != NULL);

	gtk_object_get (
		GTK_OBJECT (cc->items [0]),
		"fill_color_gdk", &color,
		NULL);

	color_combo_select_color (cc, color);
	g_free (color);
}


