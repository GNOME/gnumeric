/*
 * color-palette.c : A color selector palette
 * 
 * This code was extracted from widget-color-combo.c
 *   written by Miguel de Icaza (miguel@kernel.org) and
 *   Dom Lachowicz (dominicl@seas.upenn.edu). The extracted
 *   code was re-packaged into a separate object by
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *
 */
#include <config.h>
#include <gnome.h>
#include <libgnomeui/gnome-canvas.h>
#include "color.h"
#include "color-group.h"
#include "color-palette.h"

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

struct _ColorPalette {
	GtkVBox          vbox;
	GtkTooltips      *tool_tip;
	GnomeColorPicker *picker;
	/*
	 * Array of colors
	 */
	GnomeCanvasItem **items;
	/* The (potententially NULL) default color */
        GdkColor *default_color;
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

	/* The color group to which we belong */
	ColorGroup *color_group;
};

typedef struct {
	GtkVBoxClass parent_class;
	
	/* Signals emited by this widget */
	void (* changed) (ColorPalette *color_palette, GdkColor *color, gboolean custom);
} ColorPaletteClass;

static gint color_palette_signals [LAST_SIGNAL] = { 0, };

static GtkObjectClass *color_palette_parent_class;

#define make_color(P,COL) (((COL) != NULL) ? (COL) : ((P) ? ((P)->default_color) : NULL))

static void
color_palette_finalize (GtkObject *object)
{
	ColorPalette *P = COLOR_PALETTE (object);
	
	if (P->tool_tip) {
		gtk_object_unref (GTK_OBJECT (P->tool_tip));
		P->tool_tip = NULL;
	}
	
	g_free (P->items);

	if (P->color_group) {
		gtk_object_unref(GTK_OBJECT (P->color_group));
		P->color_group = NULL;
	}
	
	(*color_palette_parent_class->finalize) (object);
}


typedef void (*GtkSignal_NONE__POINTER_BOOL) (GtkObject * object,
					      gpointer arg1,
					      gboolean arg2,
					      gpointer user_data);
static void 
marshal_NONE__POINTER_BOOL (GtkObject * object,
			    GtkSignalFunc func,
			    gpointer func_data,
			    GtkArg * args)
{
	GtkSignal_NONE__POINTER_BOOL rfunc;
	rfunc = (GtkSignal_NONE__POINTER_BOOL) func;
	(*rfunc) (object,
		  GTK_VALUE_POINTER (args[0]),
		  GTK_VALUE_BOOL    (args[1]),
		  func_data);
}


static void
color_palette_class_init (GtkObjectClass *object_class)
{
	object_class->finalize = color_palette_finalize;
	
	color_palette_parent_class = gtk_type_class (gtk_vbox_get_type ());
	
	color_palette_signals [CHANGED] =
		gtk_signal_new (
			"changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (ColorPaletteClass, changed),
			marshal_NONE__POINTER_BOOL,
			GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_BOOL);
	
	gtk_object_class_add_signals (object_class, color_palette_signals, LAST_SIGNAL);
}

GtkType
color_palette_get_type (void)
{
	static GtkType type = 0;
	
	if (!type){
		GtkTypeInfo info = {
			"ColorPalette",
			sizeof (ColorPalette),
			sizeof (ColorPaletteClass),
			(GtkClassInitFunc) color_palette_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};
		
		type = gtk_type_unique (gtk_vbox_get_type (), &info);
	}
	
	return type;
}

/*
 * Fires signal "changed" with color as its param
 *
 * prop
 */
static void
emit_change (ColorPalette *P, GdkColor *color, gboolean custom, gboolean tell_group)
{
	if( tell_group == TRUE )
		color_group_add_color( P->color_group, color, custom);
  	gtk_signal_emit (
		GTK_OBJECT (P), color_palette_signals [CHANGED], color, custom);
	
}


/*
 * Add the new custom color as the first custom color in the custom color rows
 * and shift all of the others 'one step down'
 *
 * Also take care of setting up the GnomeColorPicker 'display'
 */
static void
color_palette_change_custom_color ( ColorPalette *P, GdkColor *new)
{
	int index;
	GnomeCanvasItem *item;
	GnomeCanvasItem *next_item;

	g_return_if_fail (P != NULL);
	g_return_if_fail (new != NULL);
	g_return_if_fail (P->picker);

	/* make sure there is room */
	if (P->custom_color_pos == -1)
		return;
	
	for (index = P->custom_color_pos; index < P->total - 1; index++) {
		GdkColor *color;
		GdkColor *outline;
		item = P->items [index];
		next_item = P->items [index + 1];
		
		gtk_object_get ( GTK_OBJECT (next_item),
				 "fill_color_gdk", &color,
				 "outline_color_gdk", &outline,
				 NULL);
		gnome_canvas_item_set ( item,
					"fill_color_gdk", color,
					"outline_color_gdk", outline,
					NULL);
	}
	item = P->items [index];
	gnome_canvas_item_set ( item,
				"fill_color_gdk", new,
				"outline_color_gdk", new,
				NULL);
	gnome_color_picker_set_i16 ( P->picker,
				     new->red,
				     new->green,
				     new->blue,
				     0);
	return;
}

/*
 * The custom color box was clicked. Find out its value and emit it
 * And add it to the custom color row
 */
static void
cust_color_set (GtkWidget  *color_picker, guint r, guint g, guint b, guint a,
		ColorPalette *P)
{
	GdkColor *c_color;
	
	c_color =  g_new (GdkColor, 1);
	
	c_color->red   = (gushort)r;
	c_color->green = (gushort)g;
	c_color->blue  = (gushort)b;
	
	color_alloc_gdk (c_color);
	emit_change (P, c_color, TRUE, TRUE);
}

/*
 * NoColour/Auto button was pressed.
 * emit a signal with 'NULL' color.
 */
static void
cb_nocolor_clicked (GtkWidget *button, ColorPalette *P)
{
        emit_change (P, P->default_color, FALSE, TRUE);
}

/*
 * Something in our table was clicked. Find out what and emit it
 */
static void
color_clicked (GtkWidget *button, ColorPalette *P)
{
	int              index;
	GnomeCanvasItem *item;
	GdkColor        *gdk_color;
	
	index = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (button)));
	item  = P->items [index];
	
	gtk_object_get (
		GTK_OBJECT (item),
		"fill_color_gdk", &gdk_color,
		NULL);
	
	emit_change (P, gdk_color, FALSE, TRUE);
}

/*
  The color group sent the 'color_changed' signal
*/
static void
cb_group_color_change (GtkObject *cg, GdkColor * color, gboolean custom, ColorPalette *P)
{
	if (custom == TRUE) {
		GdkColor *new;
		
		new = make_color (P, color);
		color_palette_change_custom_color (P, new);
	}
	/* 
	   We set the last parameter to FALSE in order to avoid telling the 
	   group something it already knows (and causing a loop)
	 */
	emit_change(P, color, custom, FALSE);
}


/*
 * Create the individual color buttons
 *
 * Utility function
 */
static GnomeCanvasItem *
color_palette_button_new(ColorPalette *P, GtkTable* table, 
			 GtkTooltips *tool_tip, ColorNamePair* color_name, 
			 gint col, gint row, int data)
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
	
	item  = gnome_canvas_item_new (GNOME_CANVAS_GROUP (gnome_canvas_root 
							   (GNOME_CANVAS (canvas))),
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
			    GTK_SIGNAL_FUNC(color_clicked), P);
	gtk_object_set_user_data (GTK_OBJECT (button),
				  GINT_TO_POINTER (data));
	return item;
}

/*
  gets history information from the group
*/
static void
custom_color_history_setup(ColorPalette *P)
{
	GdkColor *color;
	
	g_return_if_fail (P != NULL);
	g_return_if_fail (P->color_group != NULL);
	
	for ( color = color_group_oldest_color (P->color_group);
	      color != NULL ;
	      color = color_group_previous_color(P->color_group)) 
		color_palette_change_custom_color (P , color);
	
	return;
}



/*
 * Creates the color table
 */
static GtkWidget *
color_palette_setup (ColorPalette *P, 
		     char const * const no_color_label, 
		     int ncols, int nrows, 
		     ColorNamePair *color_names)
{
	GtkWidget *nocolor_button;
	GtkWidget *cust_label;
	GtkWidget *table;
	GtkTooltips *tool_tip;
	int total, row, col;

	table = gtk_table_new (ncols, nrows, FALSE);


	if (no_color_label != NULL) {
	        nocolor_button = gtk_button_new_with_label (no_color_label);

		gtk_table_attach (GTK_TABLE (table), nocolor_button,
				  0, ncols, 0, 1, GTK_FILL | GTK_EXPAND, 0, 0, 0);
		gtk_signal_connect (GTK_OBJECT (nocolor_button), "clicked",
				    GTK_SIGNAL_FUNC(cb_nocolor_clicked), P);
	}

	P->tool_tip = tool_tip = gtk_tooltips_new();
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
				ColorNamePair color_name  = {"rgb:0/0/0", N_("custom")};
				row++;
				if (col == 0 || row < nrows) {
					/* Add a full row for custom colors */
					for (col = 0; col < ncols; col++) {
						/* Have we set custom pos yet ? */
						if (P->custom_color_pos == -1) {
							P->custom_color_pos = total;
						}
						P->items[total] =
							color_palette_button_new(P,
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
			
			P->items[total] =
				color_palette_button_new(P,
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
	cust_label = gtk_label_new ( _("Custom Color:"));
	gtk_table_attach (GTK_TABLE (table), cust_label, 0, ncols - 3 ,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	/*
	  Keep a poier to the picker so that we can update it's color
	  to keep it in synch with that of other members of the group
	*/
	P->picker = GNOME_COLOR_PICKER (gnome_color_picker_new ());
	gnome_color_picker_set_title (P->picker, _("Choose Custom Color"));
	gtk_table_attach (GTK_TABLE (table), GTK_WIDGET (P->picker), ncols - 3, ncols,
			  row + 1, row + 2, GTK_FILL | GTK_EXPAND, 0, 0, 0);
	gtk_signal_connect (GTK_OBJECT (P->picker), "color_set",
			    GTK_SIGNAL_FUNC (cust_color_set), P);
	return table;
}

GdkColor *
color_palette_get_current_color(ColorPalette *P)
{
	g_return_val_if_fail (P != NULL, NULL);
	g_return_val_if_fail (IS_COLOR_GROUP (P->color_group), NULL);

	return color_group_get_current_color(P->color_group);
}

GtkWidget *
color_palette_get_color_picker (ColorPalette *P)
{
	g_return_val_if_fail (IS_COLOR_PALETTE (P), NULL);

	return GTK_WIDGET (P->picker);
}


/*
 * Where the actual construction goes on
 */
static void
color_palette_construct (ColorPalette *P,
			 char const * const no_color_label,
			 int ncols, int nrows, ColorNamePair *color_names)
{
	GtkWidget * table;
	g_return_if_fail (P != NULL);
	g_return_if_fail (IS_COLOR_PALETTE (P));
	g_return_if_fail (color_names != NULL);
	
	P->items = g_malloc (sizeof (GnomeCanvasItem *) * ncols * nrows);
	
	/*
	 * Our table selector
	 */
	table = color_palette_setup (P, no_color_label, ncols, 
				     nrows, color_names);
	gtk_container_add (GTK_CONTAINER(P), table);
}




/*
 * More verbose constructor. Allows for specifying the rows, columns, and
 * Colors this palette will contain
 *
 * Note that if after placing all of the color_names there remains an entire
 * row available then a row of custum colors (initialized to black) is added
 *
 */
static GtkWidget*
color_palette_new_with_vals ( char const * const no_color_label,
			      int ncols, int nrows, ColorNamePair *color_names,
			      GdkColor *default_color, 
			      gchar *group_name){
	ColorPalette *P;
	
	g_return_val_if_fail (color_names != NULL, NULL);
	
	P = gtk_type_new (color_palette_get_type ());
	
	P->default_color = default_color;
	P->color_group = COLOR_GROUP (color_group_new_named(group_name));

	color_palette_construct (P, no_color_label, ncols, nrows, color_names);
	custom_color_history_setup(P);
	
	gtk_signal_connect( GTK_OBJECT(P->color_group), "color_change",
			    GTK_SIGNAL_FUNC (cb_group_color_change),
			    P);
	
	return GTK_WIDGET (P);
}


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
 * Default constructor. Pass an optional label for
 * the no/auto color button.
 *
 */
GtkWidget*
color_palette_new (  char const * const no_color_label,
		     GdkColor *default_color, gchar *group_name) {
	/* specify 6 rows to allow for a row of custom colors */
	return color_palette_new_with_vals (no_color_label,
					    8, 6, 
					    default_color_set, default_color,
					    group_name);
}

