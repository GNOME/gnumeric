/*
 * WidgetColorCombo: A color selector combo box
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 */
#include <config.h>
#include <gnome.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-canvas.h>
#include "color.h"
#include "gtk-combo-box.h"
#include "widget-color-combo.h"

#define COLOR_PREVIEW_WIDTH 15
#define COLOR_PREVIEW_HEIGHT 15

enum {
	CHANGED,
	LAST_SIGNAL
};

typedef struct {
	char *color;	/* rgb color or otherwise - eg. "rgb:FF/FF/FF" */
	char *name;	/* english name - eg. "white" */
} ColorNamePair;

struct _ColorCombo {
	GtkComboBox     combo_box;

	/*
	 * Canvas where we display
	 */
	GtkWidget       *preview_button;
	GnomeCanvas     *preview_canvas;
	GnomeCanvasItem *preview_color_item;

	GtkWidget       *color_table;
	GtkTooltips     *tool_tip;

	/*
	 * Array of colors
	 */
	GnomeCanvasItem **items;

        /*
	 * Current color
	 */
        GdkColor *current;

	/* The (potententially NULL) default color */
        GdkColor *default_color;

	/* Is there a custom color ? */
	gboolean custom_color_allocated;
        GdkColor custom_color;
        /*
	 * Position of the last possible position
	 * for custom colors in **items
	 * (i.e. custom colors go from items[custom_color_pos]
	 *  to items[total - 1])
	 *
	 * If custom_color_pos == -1, there is no room for custom colors
	 */
        int custom_color_pos;
        /*
	 * Number of default colors in **items
	 */
	int total;

};
typedef struct {
	GnomeCanvasClass parent_class;

	/* Signals emited by this widget */
	void (* changed) (ColorCombo *color_combo, GdkColor *color);
} ColorComboClass;

static gint color_combo_signals [LAST_SIGNAL] = { 0, };

static GtkObjectClass *color_combo_parent_class;

typedef enum { COLOR_COPY, COLOR_NO_COPY } colcopy;

static void
set_color (ColorCombo *cc, GdkColor *color, colcopy mode)
{
	GdkColor *outline_color;

	if (cc->current)
		g_free (cc->current);

	if (color != NULL && mode == COLOR_COPY) {
		cc->current = g_new (GdkColor, 1);
		memcpy (cc->current, color, sizeof (GdkColor));
	} else
		cc->current = color;

	/* If the new colour is NULL use the default */
	if (color == NULL)
		color = cc->default_color;

	/* If the new and the default are NULL draw an outline */
	outline_color = (color) ? color : &gs_dark_gray;

	gnome_canvas_item_set (cc->preview_color_item,
			       "fill_color_gdk", color,
			       "outline_color_gdk", outline_color,
			       NULL);
}

static void
color_combo_finalize (GtkObject *object)
{
	ColorCombo *cc = COLOR_COMBO (object);

	if (cc->current) {
		g_free (cc->current);
		cc->current = NULL;
	}
	if (cc->tool_tip) {
		gtk_object_unref (GTK_OBJECT (cc->tool_tip));
		cc->tool_tip = NULL;
	}

	if (cc->custom_color_allocated) {
	        gdk_colormap_free_colors (gdk_imlib_get_colormap (), &cc->custom_color, 1);
		cc->custom_color_allocated = FALSE;
	}

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

	gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

static void
preview_clicked (GtkWidget *button, ColorCombo *cc)
{
        emit_change (cc);
}

/*
 * NoColour/Auto button was pressed.
 * emit a signal with 'NULL' color.
 */
static void
cb_nocolor_clicked (GtkWidget *button, ColorCombo *cc)
{
	set_color (cc, NULL, COLOR_NO_COPY);
        emit_change (cc);
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
 * A new custom color was selected. Change the colors of the
 * custom color row to reflect this new choice
 *
 * The new custom color is the one describbed by cc->custom_color, and
 * should have been previously allocated
 */
static void
cust_color_row_shift (ColorCombo *cc)
{
        int index;
	GnomeCanvasItem *item;
	GnomeCanvasItem *next_item;
	GdkColor         *outline_color;

        /* Make sure a color was allocated, and that there is room
	 *  in the combo box
	 */
        if (!cc->custom_color_allocated || cc->custom_color_pos == -1)
		return;

	for (index = cc->custom_color_pos; index < cc->total - 1; index++) {
		GdkColor *color;
		GdkColor *outline;
		item = cc->items [index];
		next_item = cc->items [index + 1];

		gtk_object_get ( GTK_OBJECT (next_item),
				 "fill_color_gdk", &color,
				 "outline_color_gdk", &outline,
				 NULL);
		gnome_canvas_item_set ( item,
					"fill_color_gdk", color,
					"outline_color_gdk", outline,
					NULL);
	}

	item = cc->items [cc->total - 1];
	outline_color = &(cc->custom_color);
	gnome_canvas_item_set (item,
			       "fill_color_gdk", &(cc->custom_color),
			       "outline_color_gdk", &outline_color,
			       NULL);
	return;
}

/*
 * The custom color box was clicked. Find out its value and emit it
 */
static void
cust_color_set (GtkWidget  *color_picker, guint r, guint g, guint b, guint a,
		ColorCombo *cc)
{
	if (cc->custom_color_allocated) {
	        gdk_colormap_free_colors (gdk_imlib_get_colormap (),
					  &cc->custom_color, 1);
		cc->custom_color_allocated = FALSE;
	}

	cc->custom_color.red   = (gushort)r;
	cc->custom_color.green = (gushort)g;
	cc->custom_color.blue  = (gushort)b;

	if (!gdk_colormap_alloc_color (gdk_imlib_get_colormap (),
				       &cc->custom_color,
				       TRUE, TRUE))
	        return;

	cc->custom_color_allocated = TRUE;
	set_color   (cc, &cc->custom_color, COLOR_COPY);
	cust_color_row_shift (cc);
	emit_change (cc);
}

static void
cust_color_clicked (GtkWidget *widget, ColorCombo *cc)
{
        gtk_combo_box_popup_hide (GTK_COMBO_BOX (cc));
}

/*
 * Create the individual color buttons
 *
 * Utility function
 */
static GnomeCanvasItem *
color_table_button_new(ColorCombo *cc, GtkTable* table, GtkTooltips *tool_tip, ColorNamePair* color_name, gint col, gint row, int data)
{
        GtkWidget *button;
	GtkWidget *canvas;
	GnomeCanvasItem *item;

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

	gtk_widget_push_visual (gdk_imlib_get_visual ());
	gtk_widget_push_colormap (gdk_imlib_get_colormap ());
	canvas = gnome_canvas_new ();
	gtk_widget_pop_colormap ();
	gtk_widget_pop_visual ();

	gtk_widget_set_usize (canvas, COLOR_PREVIEW_WIDTH, COLOR_PREVIEW_HEIGHT);
	gtk_container_add (GTK_CONTAINER (button), canvas);

	item  = gnome_canvas_item_new (
				       GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (canvas))),
				       gnome_canvas_rect_get_type (),
				       "x1", 0.0,
				       "y1", 0.0,
				       "x2", (double) COLOR_PREVIEW_WIDTH,
				       "y2", (double) COLOR_PREVIEW_HEIGHT,
				       "fill_color", color_name->color,
				       NULL);

	gtk_tooltips_set_tip (tool_tip, button, _(color_name->name),
			      "Private+Unused");

	gtk_table_attach (table, button,
			  col, col+1, row, row+1, GTK_FILL, GTK_FILL, 1, 1);

	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC(color_clicked), cc);
	gtk_object_set_user_data (GTK_OBJECT (button),
				  GINT_TO_POINTER (data));
	return item;
}



/*
 * Creates the color table
 */
static GtkWidget *
color_table_setup (ColorCombo *cc, char const * const no_color_label, int ncols, int nrows, ColorNamePair *color_names)
{
	GtkWidget *nocolor_button;
	GtkWidget *cust_label;
	GtkWidget *cust_color;
	GtkWidget *table;
	GtkTooltips *tool_tip;
	int total, row, col;

	table = gtk_table_new (ncols, nrows, 0);

	if (no_color_label != NULL) {
	        nocolor_button = gtk_button_new_with_label (no_color_label);

		gtk_table_attach (GTK_TABLE (table), nocolor_button,
				  0, ncols, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (nocolor_button), "clicked",
				    GTK_SIGNAL_FUNC(cb_nocolor_clicked), cc);
	}

	cc->tool_tip = tool_tip = gtk_tooltips_new();
	cc->custom_color_pos = -1;
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
				ColorNamePair color_name  = {"rgb:0/0/0", N_("custom")};
				row++;
				if (col == 0 || row < nrows) {
					/* Add a full row for custom colors */
					for (col = 0; col < ncols; col++) {
						/* Have we set custom pos yet ? */
						if (cc->custom_color_pos == -1) {
							cc->custom_color_pos = total;
						}
						cc->items[total] =
							color_table_button_new(cc,
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

			cc->items[total] =
				color_table_button_new(cc,
						       GTK_TABLE (table),
						       GTK_TOOLTIPS (tool_tip),
						       &(color_names [pos]),
						       col,
						       row + 1,
						       total);
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
		"width_pixels", 1,
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

	/* Start with the default color */
	set_color (cc, NULL, COLOR_NO_COPY);
}

/*
 * More verbose constructor. Allows for specifying the rows, columns, and
 * Colors this box will contain
 *
 * Note that if after placing all of the color_names there remains an entire
 * row available then a row of custum colors (initialized to black) is added
 *
 */
static GtkWidget *
color_combo_new_with_vals (char **icon, char const * const no_color_label,
			   int ncols, int nrows, ColorNamePair *color_names,
			   GdkColor *default_color)
{
	ColorCombo *cc;

	g_return_val_if_fail (icon != NULL, NULL);
	g_return_val_if_fail (color_names != NULL, NULL);

	cc = gtk_type_new (color_combo_get_type ());

        cc->default_color = default_color;
	cc->current = NULL;
	cc->custom_color_allocated = FALSE;

	color_combo_construct (cc, icon, no_color_label, ncols, nrows, color_names);

	return GTK_WIDGET (cc);
}

/*
 * this list of colors should match the Excel 2000 list of colors
 */
static ColorNamePair default_color_set [] = {
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
 * Default constructor. Pass an XPM icon and an optional label for
 * the no/auto color button.
 */
GtkWidget *
color_combo_new (char **icon, char const * const no_color_label,
		 GdkColor *default_color)
{
        /* specify 6 rows to allow for a row of custom colors */
	return color_combo_new_with_vals (icon, no_color_label, 8, 6, default_color_set,
					  default_color);
}
