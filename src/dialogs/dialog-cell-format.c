/**
 * dialog-cell-format.c:  Implements a dialog to format cells.
 *
 * Author:
 *  Jody Goldberg <jgoldberg@home.com>
 *
 **/

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "sheet.h"
#include "color.h"
#include "dialogs.h"
#include "utils-dialog.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-dashed-canvas-line.h"
#include "gnumeric-sheet.h"
#include "selection.h"
#include "ranges.h"
#include "format.h"
#include "formats.h"
#include "pattern.h"
#include "mstyle.h"

#define GLADE_FILE "cell-format.glade"

/* The order corresponds to the border_buttons name list
 * in dialog_cell_format_impl */
typedef enum
{
	BORDER_TOP,	BORDER_BOTTOM,
	BORDER_LEFT,	BORDER_RIGHT,
	BORDER_REV_DIAG,BORDER_DIAG,

	/* These are special.
	 * They are logical rather than actual borders, however, they
	 * require extra lines to be drawn so they need to be here.
	 */
	BORDER_HORIZ, BORDER_VERT,

	BORDER_EDGE_MAX
} BorderLocations;

/* The order corresponds to border_preset_buttons */
typedef enum
{
	BORDER_PRESET_NONE,
	BORDER_PRESET_OUTLINE,
	BORDER_PRESET_INSIDE,

	BORDER_PRESET_MAX
} BorderPresets;

/* The available format widgets */
typedef enum
{
    F_GENERAL,		F_DECIMAL_BOX,	F_SEPERATOR, 
    F_SYMBOL_LABEL,	F_SYMBOL,	F_DELETE,
    F_ENTRY,		F_LIST_SCROLL,	F_LIST,
    F_TEXT,		F_DECIMAL_SPIN,	F_NEGATIVE,
    F_MAX_WIDGET
} FormatWidget;

struct _FormatState;
typedef struct
{
	struct _FormatState *state;
	int cur_index;
	GtkToggleButton *current_pattern;
	GtkToggleButton *default_button;
	void (*draw_preview) (struct _FormatState *);
} PatternPicker;

typedef struct
{
	struct _FormatState *state;
	GdkColor	 *auto_color;
	GtkToggleButton  *custom, *autob;
	GnomeColorPicker *picker;
	GtkSignalFunc	  preview_update;

	gboolean	  is_auto;
	guint		  rgba;
	guint		  r, g, b;
} ColorPicker;

typedef struct
{
	struct _FormatState *state;
	GtkToggleButton  *button;
	StyleBorderType	  pattern_index;
	gboolean	  is_selected;	/* Is it selected */
	BorderLocations   index;
	guint		  rgba;
	gboolean	  is_set;	/* Has the element been changed */
} BorderPicker;

typedef struct _FormatState
{
	GladeXML	*gui;
	GnomePropertyBox*dialog;
	gint		 page_signal;

	Sheet		*sheet;
	MStyle		*style, *result;

	gboolean	 is_multi;	/* single cell or multiple ranges */
	gboolean	 enable_edit;

	struct
	{
		GnomeCanvas	*canvas;
		GtkBox		*box;
		GtkWidget	*widget[F_MAX_WIDGET];

		gchar 		*spec;
		gint		 current_type;
		int		 num_decimals;
		int		 negative_format;
		gboolean	 use_seperator;
	} format;
	struct
	{
		GtkCheckButton	*wrap;
	} align;
	struct
	{
		FontSelector	*selector;
		ColorPicker	 color;
	} font;
	struct
	{
		GnomeCanvas	*canvas;
		GtkButton 	*preset[BORDER_PRESET_MAX];
		GnomeCanvasItem	*back;
		GnomeCanvasItem *lines[12];

		BorderPicker	 edge[BORDER_EDGE_MAX];
		ColorPicker	 color;
		PatternPicker	 pattern;
	} border;
	struct
	{
		GnomeCanvas	*canvas;
		GnomeCanvasItem	*back;
		GnomeCanvasItem	*pattern_item;

		ColorPicker	 back_color, pattern_color;
		PatternPicker	 pattern;
	} back;
} FormatState;

/*****************************************************************************/
/* Some utility routines shared by all pages */

/*
 * A utility routine to help mark the attributes as being changed
 * VERY stupid for now.
 */
static void
fmt_dialog_changed (FormatState *state)
{
	/* Catch all the pseudo-events that take place while initializing */
	if (state->enable_edit)
		gnome_property_box_changed (state->dialog);
}

/* Default to the 'Format' page but remember which page we were on between
 * invocations */
static int fmt_dialog_page = 0;

/*
 * Callback routine to help remember which format tab was selected
 * between dialog invocations.
 */
static void
cb_page_select (GtkNotebook *notebook, GtkNotebookPage *page,
		gint page_num, gpointer user_data)
{
	fmt_dialog_page = page_num;
}

static void
cb_notebook_destroy (GtkObject *obj, FormatState *state)
{
	gtk_signal_disconnect (obj, state->page_signal);
}

/*
 * Callback routine to give radio button like behaviour to the
 * set of toggle buttons used for line & background patterns.
 */
static void
cb_toggle_changed (GtkToggleButton *button, PatternPicker *picker)
{
	if (gtk_toggle_button_get_active (button) &&
	    picker->current_pattern != button) {
		gtk_toggle_button_set_active(picker->current_pattern, FALSE);
		picker->current_pattern = button;
		picker->cur_index =
				GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (button), "index"));
		if (picker->draw_preview)
			picker->draw_preview (picker->state);
	}
}

/*
 * Setup routine to associate images with toggle buttons
 * and to adjust the relief so it looks nice.
 */
static void
setup_pattern_button (GladeXML  *gui,
		      char const * const name,
		      PatternPicker *picker,
		      gboolean const flag,
		      int const index,
		      gboolean select)
{
	GtkWidget * tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkButton *button = GTK_BUTTON (tmp);
		if (flag) {
			GtkWidget * image = gnumeric_load_image(name);
			if (image != NULL)
				gtk_container_add(GTK_CONTAINER (tmp), image);
		}

		if (picker->current_pattern == NULL) {
			picker->default_button = GTK_TOGGLE_BUTTON (button);
			picker->current_pattern = picker->default_button;
			picker->cur_index = index;
		}

		gtk_button_set_relief (button, GTK_RELIEF_NONE);
		gtk_signal_connect (GTK_OBJECT (button), "toggled",
				    GTK_SIGNAL_FUNC (cb_toggle_changed),
				    picker);
		gtk_object_set_data (GTK_OBJECT (button), "index", 
				     GINT_TO_POINTER (index));

		/* Set the state AFTER the signal to get things redrawn correctly */
		if (select) {
			picker->cur_index = index;
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
						      TRUE);
		}
	} else
		g_warning ("CellFormat : Unexpected missing glade widget");
}

static void
cb_custom_color_selected (GtkObject *obj, ColorPicker *state)
{
	/* The color picker was clicked.  Toggle the custom radio button */
	gtk_toggle_button_set_active (state->custom, TRUE);
}

static void
cb_auto_color_selected (GtkObject *obj, ColorPicker *state)
{
	/* TODO TODO TODO : Some day we need to properly support 'Auto' colors.
	 *                  We should calculate them on the fly rather than hard coding
	 *                  in the initialization.
	 */

	if ((state->is_auto = gtk_toggle_button_get_active (state->autob))) {
		/* The auto radio was clicked.  Reset the color in the picker */
		gnome_color_picker_set_i16 (state->picker,
					    state->auto_color->red,
					    state->auto_color->green,
					    state->auto_color->blue,
					    0xffff);

		state->preview_update (state->picker,
				       state->auto_color->red,
				       state->auto_color->green,
				       state->auto_color->blue,
				       0, state->state);
	}
}

static void
setup_color_pickers (GladeXML	 *gui,
		     char const  * const picker_name,
		     char const  * const custom_radio_name,
		     char const  * const auto_name,
		     ColorPicker *color_state,
		     FormatState *state,
		     GdkColor	 *auto_color,
		     GtkSignalFunc preview_update,
		     MStyleElementType const e,
		     MStyle	 *mstyle)
{
	StyleColor *mcolor = NULL;

	GtkWidget *tmp = glade_xml_get_widget (gui, picker_name);
	g_return_if_fail (tmp && NULL != (color_state->picker = GNOME_COLOR_PICKER (tmp)));

	tmp = glade_xml_get_widget (gui, custom_radio_name);
	g_return_if_fail (tmp && NULL != (color_state->custom = GTK_TOGGLE_BUTTON (tmp)));

	tmp = glade_xml_get_widget (gui, auto_name);
	g_return_if_fail (tmp && NULL != (color_state->autob = GTK_TOGGLE_BUTTON (tmp)));

	color_state->auto_color = auto_color;
	color_state->preview_update = preview_update;
	color_state->state = state;
	color_state->is_auto = TRUE;

	gtk_signal_connect (GTK_OBJECT (color_state->picker), "clicked",
			    GTK_SIGNAL_FUNC (cb_custom_color_selected),
			    color_state);
	gtk_signal_connect (GTK_OBJECT (color_state->autob), "clicked",
			    GTK_SIGNAL_FUNC (cb_auto_color_selected),
			    color_state);

	/* Toggle the auto button to initialize the color to Auto */
	gtk_toggle_button_set_active (color_state->autob, FALSE);
	gtk_toggle_button_set_active (color_state->autob, TRUE);

	/* Connect to the sample canvas and redraw it */
	gtk_signal_connect (GTK_OBJECT (color_state->picker), "color_set",
			    preview_update, state);


	if (e != MSTYLE_ELEMENT_UNSET && mstyle_is_element_set (mstyle, e))
		mcolor = mstyle_get_color (mstyle, e);

	if (mcolor != NULL) {
		gnome_color_picker_set_i16 (color_state->picker,
					    mcolor->red, mcolor->green,
					    mcolor->blue, 0xffff);
		gtk_toggle_button_set_active (color_state->custom, TRUE);
		(*preview_update) (state,
				   mcolor->red,
				   mcolor->green,
				   mcolor->blue,
				   0xffff, state);
	}

}

static StyleColor *
picker_style_color (ColorPicker const *c)
{
	return style_color_new (c->r, c->g, c->b);
}

/*
 * Utility routine to load an image and insert it into a
 * button of the same name.
 */
static GtkWidget *
init_button_image (GladeXML *gui, char const * const name)
{
	GtkWidget *tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkWidget * image = gnumeric_load_image(name);
		if (image != NULL)
			gtk_container_add (GTK_CONTAINER (tmp), image);
	}

	return tmp;
}

/*****************************************************************************/

/* TODO : Add preview */
static void
draw_format_preview (FormatState *state)
{
	/* The first time through lets initialize */
	if (state->format.canvas == NULL) {
		state->format.canvas =
		    GNOME_CANVAS (glade_xml_get_widget (state->gui, "format_sample"));
	}
}

static void
fillin_negative_samples (FormatState *state, int const page)
{
	static char const * const decimals = "098765432109876543210987654321";
	char const * const sep = state->format.use_seperator
	    ? format_get_thousand () : "";
	int const n = 30 - state->format.num_decimals;

	char const * const decimal =
	    (state->format.num_decimals > 0)
	    ? format_get_decimal () : "";

	char const * prefix = "";
	char const * const *formats;
	GtkCList *cl;
	char buf[50];
	int i;

	g_return_if_fail (page == 1 || page == 2);

	cl = GTK_CLIST (state->format.widget[F_NEGATIVE]);
	if (page == 1) {
		/* TODO : Do these need translation too ?? */
		static char const * const number_formats[4] = {
		    "%s-3%s210%s%s",
		    "%s3%s210%s%s",
		    "%s(3%s210%s%s)",
		    "%s(3%s210%s%s)"
		};
		prefix = "";
		formats = number_formats;
	} else {
		static char const * const currency_formats[4] = {
		    "-%s3%s210%s%s",
		    "%s3%s210%s%s",
		    "-%s3%s210%s%s",
		    "-%s3%s210%s%s"
		};
		prefix = "$"; /* FIXME : Add real currency list support */
		formats = currency_formats;
	}

	for (i = 4; --i >= 0 ; ) {
		sprintf (buf, formats[i], prefix, sep, decimal, decimals + n);
		gtk_clist_set_text (cl, i, 0, buf);
	}

	draw_format_preview (state);
}

static void
cb_decimals_changed (GtkEditable *editable, FormatState *state)
{
	int const page = state->format.current_type;

	state->format.num_decimals =
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (editable));

	if (page == 1 || page == 2)
		fillin_negative_samples (state, page);
}

static void
cb_seperator_toggle (GtkObject *obj, FormatState *state)
{
	state->format.use_seperator = 
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (obj));
	fillin_negative_samples (state, 1);
}

static int
fm_dialog_init_fmt_list (GtkCList *cl, char const * const *formats,
			 char const * const cur_format,
			 int select, int *count)
{
	int j;

	for (j = 0; formats [j]; ++j) {
		gchar *t [1];

		t [0] = _(formats [j]);
		gtk_clist_append (cl, t);

		/* CHECK : Do we really want to be case insensitive ? */
		if (!g_strcasecmp (formats[j], cur_format))
			select = j + *count;
	}

	*count += j;
	return select;
}

static void
fmt_dialog_enable_widgets (FormatState *state, int page)
{
	static FormatWidget contents[12][6] =
	{
		/* General */
		{ F_GENERAL, F_MAX_WIDGET },
		/* Number */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SEPERATOR, F_NEGATIVE, F_MAX_WIDGET },
		/* Currency */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SYMBOL_LABEL, F_SYMBOL, F_NEGATIVE, F_MAX_WIDGET },
		/* Accounting */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SYMBOL_LABEL, F_SYMBOL, F_MAX_WIDGET },
		/* Date */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Time */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Percentage */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_MAX_WIDGET },
		/* Fraction */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Scientific */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_MAX_WIDGET },
		/* Text */
		{ F_TEXT, F_MAX_WIDGET },
		/* Special */
		{ F_MAX_WIDGET },
		/* Custom */
		{ F_ENTRY, F_LIST_SCROLL, F_LIST, F_DELETE, F_MAX_WIDGET },
	};

	int const old_page = state->format.current_type;
	int i, count = 0;
	FormatWidget tmp;

	/* Hide widgets from old page */
	if (old_page >= 0)
		for (i = 0; (tmp = contents[old_page][i]) != F_MAX_WIDGET ; ++i)
			gtk_widget_hide (state->format.widget[tmp]);

	/* Set the default format if appropriate */
	/* FIXME : Not correct.  This should be set ONLY if things do not match */
	switch (page) {
	case 0: case 3: case 6: case 7: case 8: case 9:
	{
		char const * const new_format = cell_formats [0][0];
		gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]),
				    new_format);
	}
	break;

	case 1 : case 2 : /* Are handled by fillin_negative */
	case 4 : case 5 : /* get filled in with their lists */

	default :
		break;
	};

	state->format.current_type = page;
	for (i = 0; (tmp = contents[page][i]) != F_MAX_WIDGET ; ++i) {
		GtkWidget *w = state->format.widget[tmp];
		gtk_widget_show (w);

		/* The sample is always the 1st widget */
		gtk_box_reorder_child (state->format.box, w, i+1);

		if (tmp == F_LIST) {
			GtkCList *cl = GTK_CLIST (w);
			int select = -1, start = 0, end = -1;
			switch (page) {
			case 4: case 5: case 7:
				start = end = page;
				break;

			case 11:
				/* TODO Allow for REAL custom formats */
				start = 0; end = 8;
				break;

			default :
				g_assert_not_reached ();
			};

			gtk_clist_freeze (cl);
			gtk_clist_clear (cl);
			gtk_clist_set_auto_sort (cl, FALSE);

			for (; start <= end ; ++start)
				select = fm_dialog_init_fmt_list (cl,
						cell_formats [start],
						state->format.spec,
						select, &count);

			/* If this is the custom page and the format has
			 * not been found append it */
			if  (page == 11 && select == -1) {
				gchar *dummy[1];
				dummy[0] = state->format.spec;
				select = gtk_clist_append (cl, dummy);
			}
			gtk_clist_thaw (cl);
			if (select < 0)
				select = 0;
			gtk_clist_select_row (cl, select, 0);
			if (!gtk_clist_row_is_visible (cl, select))
				gtk_clist_moveto (cl, select, 0, 0.5, 0.);
		} else if (tmp == F_NEGATIVE)
			fillin_negative_samples (state, page);
	}
}

/*
 * Callback routine to manage the relationship between the number
 * formating radio buttons and the widgets required for each mode.
 */
static void
cb_format_changed (GtkObject *obj, FormatState *state)
{
	GtkToggleButton *button = GTK_TOGGLE_BUTTON (obj);
	if (gtk_toggle_button_get_active (button))
		fmt_dialog_enable_widgets ( state,
			GPOINTER_TO_INT (gtk_object_get_data (obj, "index")));
}

static void
cb_format_entry (GtkEditable *w, FormatState *state)
{
	state->format.spec = gtk_entry_get_text (GTK_ENTRY (w));
	mstyle_set_format (state->result, state->format.spec);
	fmt_dialog_changed (state);
}

static void
cb_format_list_select (GtkCList *clist, gint row, gint column,
		       GdkEventButton *event, FormatState *state)
{
	gchar *text;
	gtk_clist_get_text (clist, row, column, &text);
	gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]), text);
}

static void
fmt_dialog_init_format_page (FormatState *state)
{
	static char const * const format_buttons[] = {
	    "format_general",	"format_number",
	    "format_currency",	"format_accounting",
	    "format_date",	"format_time",
	    "format_percentage","format_fraction",
	    "format_scientific","format_text",
	    "format_special",	"format_custom",
	    NULL
	};

	/* The various format widgets */
	static char const * const widget_names[] =
	{
		"format_general_label",	"format_decimal_box",
		"format_seperator",	"format_symbol_label",
		"format_symbol_select",	"format_delete",
		"format_entry",		"format_list_scroll",
		"format_list",		"format_text_label",
		"format_number_decimals", "format_negatives",
		NULL
	};

	GtkWidget *tmp;
	GtkCList *cl;
	char const * name;
	int i, j, page;

	FormatCharacteristics info;

	/* Get the current format */
	StyleFormat *format = NULL;
	if (mstyle_is_element_set (state->style, MSTYLE_FORMAT))
		format = mstyle_get_format (state->style);

	state->format.canvas = NULL;
	state->format.spec = format->format;

	/* The handlers will set the format family later.  -1 flags that
	 * all widgets are already hidden. */
	state->format.current_type = -1;

	/* Attempt to extract general parameters from the current format */
	if ((page = cell_format_classify (state->format.spec, &info)) >= 0) {
		state->format.num_decimals = info.num_decimals;
		state->format.use_seperator = info.thousands_sep;
		state->format.negative_format = info.negative_fmt;
	} else
	{
		/* Default to custom */
		page = 11;

		state->format.num_decimals = 2;
		state->format.negative_format = 0;
		state->format.use_seperator = FALSE;
	}

	state->format.box = GTK_BOX (glade_xml_get_widget (state->gui, "format_box"));

	/* Collect all the required format widgets and hide them */
	for (i = 0; (name = widget_names[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);

		g_return_if_fail (tmp != NULL);

		gtk_widget_hide (tmp);
		state->format.widget[i] = tmp;
	}

	/* setup the red elements of the negative list box */
	cl = GTK_CLIST (state->format.widget[F_NEGATIVE]);
	if (cl != NULL) {
		gchar *dummy[1] = { "321" };
		GtkStyle *style;

		/* stick in some place holders */
		for (j = 4; --j >= 0 ;)
		    gtk_clist_append  (cl, dummy);

		/* Make the 2nd and 4th elements red */
		gtk_widget_ensure_style (GTK_WIDGET (cl));
		style = gtk_widget_get_style (GTK_WIDGET (cl));
		style = gtk_style_copy (style);
		style->fg[GTK_STATE_NORMAL] = gs_red;
		style->fg[GTK_STATE_ACTIVE] = gs_red;
		style->fg[GTK_STATE_PRELIGHT] = gs_red;
		gtk_clist_set_cell_style (cl, 1, 0, style);
		gtk_clist_set_cell_style (cl, 3, 0, style);
		gtk_style_unref (style);

		gtk_clist_select_row (cl, state->format.negative_format, 0);
	}

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->format.widget[F_DECIMAL_SPIN]),
				   state->format.num_decimals);

	/* Catch changes to the spin box */
	(void) gtk_signal_connect (
		GTK_OBJECT (state->format.widget[F_DECIMAL_SPIN]),
		"changed", GTK_SIGNAL_FUNC (cb_decimals_changed),
		state);

	/* Catch <return> in the spin box */
	gnome_dialog_editable_enters (
		GNOME_DIALOG (state->dialog),
		GTK_EDITABLE (state->format.widget[F_DECIMAL_SPIN]));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->format.widget[F_SEPERATOR]),
				      state->format.use_seperator);

	/* Setup special handlers for : Numbers */
	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_SEPERATOR]),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_seperator_toggle),
			    state);

	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_LIST]),
			    "select-row",
			    GTK_SIGNAL_FUNC (cb_format_list_select),
			    state);
#if 0
	/* TODO */
	/* Setup special handler for : Currency */
	"_symbol"
	/* Setup special handler for : Accounting */
	"_symbol"
#endif

	/* Setup special handler for Custom */
	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_ENTRY]),
			    "changed", GTK_SIGNAL_FUNC(cb_format_entry),
			    state);
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (state->format.widget[F_ENTRY]));
	
	/* Setup format buttons to toggle between the format pages */
	for (i = 0; (name = format_buttons[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);
		if (tmp == NULL)
			continue;

		gtk_object_set_data (GTK_OBJECT (tmp), "index", 
				     GINT_TO_POINTER (i));
		gtk_signal_connect (GTK_OBJECT (tmp), "toggled",
				    GTK_SIGNAL_FUNC (cb_format_changed),
				    state);

		if (i == page)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp), TRUE);
	}
}

/*****************************************************************************/

static void
cb_align_h_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	mstyle_set_align_v (
		state->style,
		GPOINTER_TO_INT (gtk_object_get_data (
		GTK_OBJECT (button), "align")));
	fmt_dialog_changed (state);
}

static void
cb_align_v_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	mstyle_set_align_v (
		state->style,
		GPOINTER_TO_INT (gtk_object_get_data (
		GTK_OBJECT (button), "align")));
	fmt_dialog_changed (state);
}

static void
cb_align_wrap_toggle (GtkToggleButton *button, FormatState *state)
{
	mstyle_set_fit_in_cell (state->result,
				gtk_toggle_button_get_active (button));
	fmt_dialog_changed (state);
}

static void
fmt_dialog_init_align_radio (char const * const name,
			     int const val, int const target,
			     FormatState *state,
			     GtkSignalFunc handler)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
	if (tmp != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp),
					      val == target);
		gtk_object_set_data (GTK_OBJECT (tmp), "align", 
				     GINT_TO_POINTER (val));
		gtk_signal_connect (GTK_OBJECT (tmp),
				    "toggled", handler,
				    state);
	}
}

static void
fmt_dialog_init_align_page (FormatState *state)
{
	static struct
	{
		char const * const	name;
		StyleHAlignFlags	align;
	} const h_buttons[] =
	{
	    { "halign_left",	HALIGN_LEFT },
	    { "halign_center",	HALIGN_CENTER },
	    { "halign_right",	HALIGN_RIGHT },
	    { "halign_general",	HALIGN_GENERAL },
	    { "halign_justify",	HALIGN_JUSTIFY },
	    { "halign_fill",	HALIGN_FILL },
	    { NULL }
	};
	static struct
	{
		char const * const	name;
		StyleVAlignFlags	align;
	} const v_buttons[] =
	{
	    { "valign_top", VALIGN_TOP },
	    { "valign_center", VALIGN_CENTER },
	    { "valign_bottom", VALIGN_BOTTOM },
	    { "valign_justify", VALIGN_JUSTIFY },
	    { NULL }
	};

	gboolean wrap = FALSE;
	StyleHAlignFlags    h = HALIGN_GENERAL;
	StyleVAlignFlags    v = VALIGN_CENTER;
	char const *name;
	int i;

	if (mstyle_is_element_set (state->style, MSTYLE_ALIGN_H))
		h = mstyle_get_align_h (state->style);
	if (mstyle_is_element_set (state->style, MSTYLE_ALIGN_V))
		v = mstyle_get_align_v (state->style);

	/* Setup the horizontal buttons */
	for (i = 0; (name = h_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, h_buttons[i].align,
					     h, state,
					     GTK_SIGNAL_FUNC (cb_align_h_toggle));

	/* Setup the vertical buttons */
	for (i = 0; (name = v_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, v_buttons[i].align,
					     v, state,
					     GTK_SIGNAL_FUNC (cb_align_v_toggle));

	/* Setup the wrap button, and assign the current value */
	if (mstyle_is_element_set (state->style, MSTYLE_FIT_IN_CELL))
		wrap = mstyle_get_fit_in_cell (state->style);

	state->align.wrap =
	    GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "align_wrap"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->align.wrap),
				      wrap);
	gtk_signal_connect (GTK_OBJECT (state->align.wrap), "toggled",
			    GTK_SIGNAL_FUNC (cb_align_wrap_toggle),
			    state);
}

/*****************************************************************************/

/*
 * A callback to set the font color.
 * It is called whenever the color picker changes value.
 */
static void
cb_font_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		       FormatState *state)
{
	GtkStyle *style;
	GdkColor col;
	state->font.color.r = col.red   = r;
	state->font.color.g = col.green = g;
	state->font.color.b = col.blue  = b;

	style = gtk_style_copy (state->font.selector->font_preview->style);
	style->fg[GTK_STATE_NORMAL] = col;
	style->fg[GTK_STATE_ACTIVE] = col;
	style->fg[GTK_STATE_PRELIGHT] = col;
	style->fg[GTK_STATE_SELECTED] = col;
	gtk_widget_set_style (state->font.selector->font_preview, style);
	gtk_style_unref (style);

	mstyle_set_color (state->result, MSTYLE_COLOR_FORE,
			  picker_style_color (&state->font.color));
	fmt_dialog_changed (state);
}

static void
cb_font_changed (GtkWidget *widget, GtkStyle *previous_style, FormatState *state)
{
	FontSelector *font_sel;
	GnomeDisplayFont *gnome_display_font;
	GnomeFont *gnome_font;
	char *family_name;
	double height;

	g_return_if_fail (state != NULL);
	font_sel = state->font.selector;
	g_return_if_fail (font_sel != NULL);
	gnome_display_font = font_sel->display_font;

	if (!gnome_display_font)
		return;

	gnome_font = gnome_display_font->gnome_font;
	family_name = gnome_font->fontmap_entry->familyname;
	height = gnome_display_font->gnome_font->size;

	mstyle_set_font_name   (state->result, family_name);
	mstyle_set_font_size   (state->result, gnome_font->size);
	mstyle_set_font_bold   (state->result,
				gnome_font->fontmap_entry->weight_code >=
				GNOME_FONT_BOLD);
	mstyle_set_font_italic (state->result, gnome_font->fontmap_entry->italic);

	fmt_dialog_changed (state);
}

/* Manually insert the font selector, and setup signals */
static void
fmt_dialog_init_font_page (FormatState *state)
{
	GtkWidget *tmp = font_selector_new ();
	FontSelector *font_widget = FONT_SELECTOR (tmp);
	GtkWidget *container = glade_xml_get_widget (state->gui, "font_box");

	g_return_if_fail (container != NULL);

	/* TODO : How to insert the font box in the right place initially */
	gtk_widget_show (tmp);
	gtk_box_pack_start (GTK_BOX (container), tmp, TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (container), tmp, 0);

	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_name_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_style_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_size_entry));

	/* When the font preview changes flag we know the style has changed.
	 * This catches color and font changes
	 */
	gtk_signal_connect (GTK_OBJECT (font_widget->font_preview),
			    "style_set",
			    GTK_SIGNAL_FUNC (cb_font_changed), state);

	state->font.selector = FONT_SELECTOR (font_widget);

	/* Init the font selector with the current font */
	font_selector_set (state->font.selector,
			   mstyle_get_font_name (state->style),
			   mstyle_get_font_bold (state->style),
			   mstyle_get_font_italic (state->style),
			   mstyle_get_font_size (state->style));
}

/*****************************************************************************/

static void
draw_pattern_preview (FormatState *state)
{
	/* The first time through lets initialize */
	if (state->back.canvas == NULL) {
		state->back.canvas =
		    GNOME_CANVAS (glade_xml_get_widget (state->gui, "back_sample"));
	}

	fmt_dialog_changed (state);

	/* If background is auto (none) : then remove any patterns or backgrounds */
	if (state->back.back_color.is_auto) {
		if (state->back.back != NULL) {
			gtk_object_destroy (GTK_OBJECT (state->back.back));
			state->back.back = NULL;
		}
		if (state->back.pattern_item != NULL) {
			gtk_object_destroy (GTK_OBJECT (state->back.pattern_item));
			state->back.pattern_item = NULL;

			/* This will recursively call draw_pattern_preview */
			gtk_toggle_button_set_active (state->back.pattern.default_button,
						      TRUE);
			/* This will recursively call draw_pattern_preview */
			gtk_toggle_button_set_active (state->back.pattern_color.autob,
						      TRUE);
			return;
		}

		if (state->enable_edit) {
			/* We can clear the background by specifying a pattern of 0 */
			mstyle_set_pattern (state->result, 0);

			/* Clear the colours just in case (The actual colours are irrelevant */
			mstyle_set_color (state->result, MSTYLE_COLOR_BACK,
					  style_color_new (0xffff, 0xffff, 0xffff));

			mstyle_set_color (state->result, MSTYLE_COLOR_PATTERN,
					  style_color_new (0x0, 0x0, 0x0));
		}

	/* BE careful just in case the initialization failed */
	} else if (state->back.canvas != NULL) {
		GnomeCanvasGroup *group =
			GNOME_CANVAS_GROUP (gnome_canvas_root (state->back.canvas));

		/* Create the background if necessary */
		if (state->back.back == NULL) {
			state->back.back = GNOME_CANVAS_ITEM (
				gnome_canvas_item_new (
					group,
					gnome_canvas_rect_get_type (),
					"x1", 0.,	"y1", 0.,
					"x2", 90.,	"y2", 50.,
					"width_pixels", (int) 5,
				       "fill_color_rgba",
				       state->back.back_color.rgba,
					NULL));
		} else
			gnome_canvas_item_set (
				GNOME_CANVAS_ITEM (state->back.back),
				"fill_color_rgba", state->back.back_color.rgba,
				NULL);

		if (state->enable_edit) {
			mstyle_set_pattern (state->result, state->back.pattern.cur_index);
			mstyle_set_color (state->result, MSTYLE_COLOR_BACK,
					  picker_style_color (&state->back.back_color));

			if (state->back.pattern.cur_index > 1)
				mstyle_set_color (state->result, MSTYLE_COLOR_PATTERN,
						  picker_style_color (&state->back.pattern_color));
		}

		/* If there is no pattern don't draw the overlay */
		if (state->back.pattern.cur_index == 0) {
			if (state->back.pattern_item != NULL) {
				gtk_object_destroy (GTK_OBJECT (state->back.pattern_item));
				state->back.pattern_item = NULL;
			}
			return;
		}

		/* Create the pattern if necessary */
		if (state->back.pattern_item == NULL) {
			state->back.pattern_item = GNOME_CANVAS_ITEM (
				gnome_canvas_item_new (
					group,
					gnome_canvas_rect_get_type (),
					"x1", 0.,	"y1", 0.,
					"x2", 90.,	"y2", 50.,
					"width_pixels", (int) 5,
					"fill_color_rgba",
					state->back.pattern_color.rgba,
					NULL));
		} else
			gnome_canvas_item_set (
				GNOME_CANVAS_ITEM (state->back.pattern_item),
				"fill_color_rgba", state->back.pattern_color.rgba,
				NULL);

		gnome_canvas_item_set (
			GNOME_CANVAS_ITEM (state->back.pattern_item),
			"fill_stipple",
			gnumeric_pattern_get_stipple (state->back.pattern.cur_index),
			NULL);
	}
}

static void
cb_back_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		       FormatState *state)
{
	state->back.back_color.r = r;
	state->back.back_color.g = g;
	state->back.back_color.b = b;
	state->back.back_color.rgba =
		GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, a>>8);
	draw_pattern_preview (state);
}

static void
cb_pattern_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
			  FormatState *state)
{
	state->back.pattern_color.r = r;
	state->back.pattern_color.g = g;
	state->back.pattern_color.b = b;
	state->back.pattern_color.rgba =
		GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, a>>8);
	draw_pattern_preview (state);
}

static void
cb_custom_back_selected (GtkObject *obj, FormatState *state)
{
	draw_pattern_preview (state);
}

static void
draw_pattern_selected (FormatState *state)
{
	/* If a pattern was selected switch to custom color.
	 * The color is already set to the default, but we need to
	 * differentiate, default and none
	 */
	if (state->back.pattern.cur_index > 0)
		gtk_toggle_button_set_active (state->back.back_color.custom, TRUE);
	draw_pattern_preview (state);
}

/*****************************************************************************/

#define L 10.	/* Left */
#define R 140.	/* Right */
#define T 10.	/* Top */
#define B 90.	/* Bottom */
#define H 50.	/* Horizontal Middle */
#define V 75.	/* Vertical Middle */

static struct
{
	double const		points[4];
	gboolean const		is_single;
	BorderLocations	const	location;
} const line_info[12] =
{
	{ { L, T, R, T }, TRUE, BORDER_TOP },
	{ { L, B, R, B }, TRUE, BORDER_BOTTOM },
	{ { L, T, L, B }, TRUE, BORDER_LEFT },
	{ { R, T, R, B }, TRUE, BORDER_RIGHT },
	{ { L, T, R, B }, TRUE, BORDER_REV_DIAG },
	{ { L, B, R, T }, TRUE, BORDER_DIAG},

	{ { L, H, R, H }, FALSE, BORDER_HORIZ },
	{ { L, H, V, B }, FALSE, BORDER_REV_DIAG },
	{ { V, T, R, H }, FALSE, BORDER_REV_DIAG },

	{ { V, T, V, B }, FALSE, BORDER_VERT },
	{ { V, T, L, H }, FALSE, BORDER_DIAG },
	{ { R, H, V, B }, FALSE, BORDER_DIAG }
};

static MStyleBorder *
border_get_mstyle (FormatState const *state, BorderLocations const loc)
{
	BorderPicker const * edge = & state->border.edge[loc];
	int const r = (edge->rgba >> 24) & 0xff;
	int const g = (edge->rgba >> 16) & 0xff;
	int const b = (edge->rgba >>  8) & 0xff;
	StyleColor *color =
	    style_color_new ((r << 8)|r, (g << 8)|g, (b << 8)|b);
		
	/* Don't set borders that have not been changed */
	if (!edge->is_set)
		return NULL;

	if (!edge->is_selected)
		return style_border_ref (style_border_none ());

	return style_border_fetch (state->border.edge[loc].pattern_index, color,
				   style_border_get_orientation (loc + MSTYLE_BORDER_TOP));
}

/* See if either the color or pattern for any segment has changed and
 * apply the change to all of the lines that make up the segment.
 */
static gboolean
border_format_has_changed (FormatState *state, BorderPicker *edge)
{
	int i;
	gboolean changed = FALSE;

	edge->is_set = TRUE;
	if (edge->rgba != state->border.color.rgba) {
		edge->rgba = state->border.color.rgba;

		for (i = 12; --i >= 0 ; ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL)
				gnome_canvas_item_set (
					GNOME_CANVAS_ITEM (state->border.lines[i]),
					"fill_color_rgba", edge->rgba,
					NULL);
		}
		changed = TRUE;
	}
	if (edge->pattern_index != state->border.pattern.cur_index) {
		edge->pattern_index = state->border.pattern.cur_index;
		for (i = 12; --i >= 0 ; ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL) {
				gnumeric_dashed_canvas_line_set_dash_index (
					GNUMERIC_DASHED_CANVAS_LINE (state->border.lines[i]),
					edge->pattern_index);
			}
		}
		changed = TRUE;
	}

	return changed;
}

/*
 * Map canvas x.y coords to a border type */
static gboolean
border_event (GtkWidget *widget, GdkEventButton *event, FormatState *state)
{
	double x = event->x;
	double y = event->y;
	BorderLocations	which;

	if (event->button != 1)
		return FALSE;

	/* If we receive a double or triple translate them into single clicks */
	if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
	{
		GdkEventType type = event->type;
		event->type = GDK_BUTTON_PRESS;
		border_event (widget, event, state);
		if (event->type == GDK_3BUTTON_PRESS)
			border_event (widget, event, state);
		event->type = type;
	}

	if (x <= L+5.)		which = BORDER_LEFT;
	else if (y <= T+5.)	which = BORDER_TOP;
	else if (y >= B-5.)	which = BORDER_BOTTOM;
	else if (x >= R-5.)	which = BORDER_RIGHT;
	else if (state->is_multi) {
		if (V-5. < x  && x < V+5.)
			which = BORDER_VERT;
		else if (H-5. < y  && y < H+5.)
			which = BORDER_HORIZ;
		else {
			/* Map everything back to the 1sr quadrant */
			if (x > V) x -= V-10.;
			if (y > H) y -= H-10.;

			if ((x < V/2.) == (y < H/2.))
				which = BORDER_REV_DIAG;
			else
				which = BORDER_DIAG;
		}
	} else
	{
		if ((x < V) == (y < H))
			which = BORDER_REV_DIAG;
		else
			which = BORDER_DIAG;
	}

	{
		BorderPicker *edge = &state->border.edge[which];
		if (!border_format_has_changed (state, edge) ||
		    !edge->is_selected)
			gtk_toggle_button_set_active (edge->button, !edge->is_selected);
	}
	return TRUE;
}

static void
draw_border_preview (FormatState *state)
{
	static double const corners[12][6] = 
	{
	    { T-5., T, L, T, L, T-5. },
	    { R+5., T, R, T, R, T-5 },
	    { T-5., B, L, B, L, B+5. },
	    { R+5., B, R, B, R, B+5. },

	    { V-5., T-1., V, T-1., V, T-5. },
	    { V+5., T-1., V, T-1., V, T-5. },

	    { V-5., B+1., V, B+1., V, B+5. },
	    { V+5., B+1., V, B+1., V, B+5. },

	    { L-1., H-5., L-1., H, L-5., H },
	    { L-1., H+5., L-1., H, L-5., H },

	    { R+1., H-5., R+1., H, R+5., H },
	    { R+1., H+5., R+1., H, R+5., H }
	};
	int i, j;

	/* The first time through lets initialize */
	if (state->border.canvas == NULL) {
		GnomeCanvasGroup  *group;
		GnomeCanvasPoints *points;

		state->border.canvas =
			GNOME_CANVAS (glade_xml_get_widget (state->gui, "border_sample"));
		group = GNOME_CANVAS_GROUP (gnome_canvas_root (state->border.canvas));

		gtk_signal_connect (GTK_OBJECT (state->border.canvas),
				    "button-press-event", GTK_SIGNAL_FUNC (border_event),
				    state);

		state->border.back = GNOME_CANVAS_ITEM (
			gnome_canvas_item_new ( group,
						gnome_canvas_rect_get_type (),
						"x1", L-10.,	"y1", T-10.,
						"x2", R+10.,	"y2", B+10.,
						"width_pixels", (int) 0,
						"fill_color",	"white",
						NULL));

		/* Draw the corners */
		points = gnome_canvas_points_new (3);

		i = (state->is_multi) ? 12 : 4;
		for (; --i >= 0 ; ) {
			for (j = 6 ; --j >= 0 ;)
				points->coords [j] = corners[i][j];

			gnome_canvas_item_new (group,
					       gnome_canvas_line_get_type (),
					       "width_pixels",	(int) 0,
					       "fill_color",	"gray63",
					       "points",	points,
					       NULL);
		}
		gnome_canvas_points_free (points);

		points = gnome_canvas_points_new (2);
		for (i = 12; --i >= 0 ; ) {
			for (j = 4; --j >= 0 ; )
				points->coords [j] = line_info[i].points[j];

			if (line_info[i].is_single || state->is_multi) {
				BorderPicker const * p =
				    & state->border.edge[line_info[i].location];
				state->border.lines[i] =
					gnome_canvas_item_new (group,
							       gnumeric_dashed_canvas_line_get_type (),
							       "fill_color_rgba", p->rgba,
							       "points",	  points,
							       NULL);
				gnumeric_dashed_canvas_line_set_dash_index (
					GNUMERIC_DASHED_CANVAS_LINE (state->border.lines[i]),
					p->pattern_index);
			} else
				state->border.lines[i] = NULL;
		    }
		gnome_canvas_points_free (points);
	}

	for (i = 0; i < BORDER_EDGE_MAX; ++i) {
		BorderPicker *border = &state->border.edge[i];
		void (*func)(GnomeCanvasItem *item) = border->is_selected
			? &gnome_canvas_item_show : &gnome_canvas_item_hide;

		for (j = 12; --j >= 0 ; ) {
			if (line_info[j].location == i &&
			    state->border.lines[j] != NULL)
				(*func) (state->border.lines[j]);
		}
	}

	fmt_dialog_changed (state);
}

static void
cb_border_preset_clicked (GtkButton *btn, FormatState *state)
{
	gboolean target_state;
	BorderLocations i, last;

	if (state->border.preset[BORDER_PRESET_NONE] == btn) {
		i = BORDER_TOP;
		last = BORDER_VERT;
		target_state = FALSE;
	} else if (state->border.preset[BORDER_PRESET_OUTLINE] == btn) {
		i = BORDER_TOP;
		last = BORDER_RIGHT;
		target_state = TRUE;
	} else if (state->border.preset[BORDER_PRESET_INSIDE] == btn) {
		i = BORDER_HORIZ;
		last = BORDER_VERT;
		target_state = TRUE;
	} else {
		g_warning ("Unknown border preset button");
		return;
	}

	/* If we are turning things on, TOGGLE the states to
	 * capture the current pattern and color */
	for (; i <= last; ++i) {
		gtk_toggle_button_set_active (
			state->border.edge[i].button,
			FALSE);

		if (target_state)
			gtk_toggle_button_set_active (
				state->border.edge[i].button,
				TRUE);
	}
}

/*
 * Callback routine to update the border preview when a button is clicked
 */
static void
cb_border_toggle (GtkToggleButton *button, BorderPicker *picker)
{
	picker->is_selected = gtk_toggle_button_get_active (button);

	/* If the format has changed and we were just toggled off,
	 * turn ourselves back on.
	 */
	if (border_format_has_changed (picker->state, picker) &&
	    !picker->is_selected)
		gtk_toggle_button_set_active (button, TRUE);
	else 
		/* Update the preview lines and enable/disable them */
		draw_border_preview (picker->state);
}

static void
cb_border_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		 FormatState *state)
{
	state->border.color.rgba = GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, a>>8);
}

#undef L
#undef R
#undef T
#undef B
#undef H
#undef V

/*
 * Initialize the fields of a BorderPicker, connect signals and
 * hide if needed.
 */
static void
init_border_button (FormatState *state, BorderLocations const i,
		    GtkWidget *button, gboolean const hide)
{
	g_return_if_fail (button != NULL);

	/* TODO : get this information from the selection */
	state->border.edge[i].rgba = 0;
	state->border.edge[i].pattern_index = BORDER_NONE /* BORDER_INCONSISTENT */;
	state->border.edge[i].is_selected = FALSE;

	state->border.edge[i].state = state;
	state->border.edge[i].index = i;
	state->border.edge[i].button = GTK_TOGGLE_BUTTON (button);
	state->border.edge[i].is_set = FALSE;

	gtk_toggle_button_set_active (state->border.edge[i].button,
				      state->border.edge[i].is_selected);

	gtk_signal_connect (GTK_OBJECT (button), "toggled",
			    GTK_SIGNAL_FUNC (cb_border_toggle),
			    &state->border.edge[i]);

	if (!state->is_multi && hide)
		gtk_widget_hide (button);
}

/*****************************************************************************/

/* Handler for the apply button */
static void
cb_fmt_dialog_dialog_apply (GtkObject *w, int page, FormatState *state)
{
	if (page != -1)
		return;

	cell_freeze_redraws ();
	
	sheet_selection_apply_style (state->sheet, state->result);

	if (mstyle_is_element_set (state->result, MSTYLE_FONT_SIZE))        
		sheet_selection_height_update (state->sheet);     

	sheet_selection_set_border  (state->sheet,
				     border_get_mstyle (state, BORDER_TOP),
				     border_get_mstyle (state, BORDER_BOTTOM),
				     border_get_mstyle (state, BORDER_LEFT),
				     border_get_mstyle (state, BORDER_RIGHT),
				     border_get_mstyle (state, BORDER_REV_DIAG),
				     border_get_mstyle (state, BORDER_DIAG),
				     border_get_mstyle (state, BORDER_HORIZ),
				     border_get_mstyle (state, BORDER_VERT));

	cell_thaw_redraws ();

	/* Get a fresh style to accumulate results in */
	state->result = mstyle_new ();
}

static void
fmt_dialog_impl (Sheet *sheet, MStyle *mstyle, GladeXML  *gui, gboolean is_multi)
{
	static GnomeHelpMenuEntry help_ref = { "gnumeric", "formatting.html" };

	static struct
	{
		char const * const name;
		StyleBorderType const pattern;
	} const line_pattern_buttons[] = {
	    { "line_pattern_thin", BORDER_THIN },

	    { "line_pattern_none", BORDER_NONE },
	    { "line_pattern_medium_dash_dot_dot", BORDER_MEDIUM_DASH_DOT_DOT },

	    { "line_pattern_hair", BORDER_HAIR },
	    { "line_pattern_slant", BORDER_SLANTED_DASH_DOT },

	    { "line_pattern_dotted", BORDER_DOTTED },
	    { "line_pattern_medium_dash_dot", BORDER_MEDIUM_DASH_DOT },

	    { "line_pattern_dash_dot_dot", BORDER_DASH_DOT_DOT },
	    { "line_pattern_medium_dash", BORDER_MEDIUM_DASH },

	    { "line_pattern_dash_dot", BORDER_DASH_DOT },
	    { "line_pattern_medium", BORDER_MEDIUM },

	    { "line_pattern_dashed", BORDER_DASHED },
	    { "line_pattern_thick", BORDER_THICK },

	    /* Thin will display here, but we need to put it first to make it
	     * the default */
	    { "line_pattern_double", BORDER_DOUBLE },

	    { NULL },
	};
	static char const * const pattern_buttons[] = {
	    "gp_solid", "gp_75grey", "gp_50grey",
	    "gp_25grey", "gp_125grey", "gp_625grey",
	    "gp_horiz",
	    "gp_vert",
	    "gp_diag",
	    "gp_rev_diag",
	    "gp_diag_cross",
	    "gp_thick_diag_cross",
	    "gp_thin_horiz",
	    "gp_thin_vert",
	    "gp_thin_rev_diag",
	    "gp_thin_diag",
	    "gp_thin_horiz_cross",
	    "gp_thin_diag_cross",
	    NULL
	};

	/* The order corresponds to the BorderLocation enum */
	static char const * const border_buttons[] = {
	    "top_border",	"bottom_border",
	    "left_border",	"right_border",
	    "rev_diag_border",	"diag_border",
	    "inside_horiz_border", "inside_vert_border",
	    NULL
	};

	/* The order corresponds to BorderPresets */
	static char const * const border_preset_buttons[] = {
	    "no_border", "outline_border", "inside_border",
	    NULL
	};

	FormatState state;
	int i, res, selected;
	char const *name;
	gboolean has_back;

	GtkWidget *dialog = glade_xml_get_widget (gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	/* Make the dialog a child of the application so that it will iconify */
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (sheet->workbook->toplevel));

	/* Initialize */
	state.gui			= gui;
	state.dialog			= GNOME_PROPERTY_BOX (dialog);
	state.sheet			= sheet;
	state.style			= mstyle;
	state.result			= mstyle_new ();
	state.is_multi			= is_multi;
	state.enable_edit		= FALSE;  /* Enable below */

	state.border.canvas	= NULL;
	state.border.pattern.cur_index	= 0;

	state.back.canvas	= NULL;
	state.back.back		= NULL;
	state.back.pattern_item	= NULL;
	state.back.pattern.cur_index	= 0;

	/* Select the same page the last invocation used */
	gtk_notebook_set_page (
		GTK_NOTEBOOK (GNOME_PROPERTY_BOX (dialog)->notebook),
		fmt_dialog_page);
	state.page_signal = gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"switch_page", GTK_SIGNAL_FUNC (cb_page_select),
		NULL);
	gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"destroy", GTK_SIGNAL_FUNC (cb_notebook_destroy),
		&state);

	fmt_dialog_init_format_page (&state);
	fmt_dialog_init_align_page (&state);
	fmt_dialog_init_font_page (&state);

	/* Setup border line pattern buttons & select the 1st button */
	state.border.pattern.draw_preview = NULL;
	state.border.pattern.current_pattern = NULL;
	state.border.pattern.state = &state;
	for (i = 0; (name = line_pattern_buttons[i].name) != NULL; ++i)
		setup_pattern_button (gui, name, &state.border.pattern,
				      i != 1, /* No image for None */
				      line_pattern_buttons[i].pattern,
				      FALSE); /* don't select */

	/* Set the default line pattern to THIN (the 1st element of line_pattern_buttons).
	 * This can not come from the style.  It is a UI element not a display item */
	gtk_toggle_button_set_active (state.border.pattern.default_button, TRUE);

#define COLOR_SUPPORT(v, n, style_element, auto_color, func) \
	setup_color_pickers (gui, #n "_picker", #n "_custom", #n "_auto",\
			     &state.v, &state, auto_color, GTK_SIGNAL_FUNC (func),\
			     style_element, mstyle)

	COLOR_SUPPORT (font.color, font_color, MSTYLE_COLOR_FORE,
		       &gs_black, cb_font_preview_color);

	/* FIXME : If all the border colors are the same return that color */
	COLOR_SUPPORT (border.color, border_color, MSTYLE_ELEMENT_UNSET,
		       &gs_black, cb_border_color);

	COLOR_SUPPORT (back.back_color, back_color, MSTYLE_COLOR_BACK,
		       &gs_white, cb_back_preview_color);
	COLOR_SUPPORT (back.pattern_color, pattern_color, MSTYLE_COLOR_PATTERN,
		       &gs_black, cb_pattern_preview_color);

	/* The background color selector is special.  There is a difference
	 * between auto (None) and the default custom which is white.
	 */
	gtk_signal_connect (GTK_OBJECT (state.back.back_color.custom), "clicked",
			    GTK_SIGNAL_FUNC (cb_custom_back_selected),
			    &state);

	/* Setup the border images */
	for (i = 0; (name = border_buttons[i]) != NULL; ++i) {
		GtkWidget * tmp = init_button_image (gui, name);
		if (tmp != NULL)
			init_border_button (&state, i, tmp, i >= BORDER_HORIZ);
	}

	/* Get the current background
	 * A pattern of 0 is has no background.
	 * A pattern of 1 is a solid background
	 * All others have 2 colours and a stipple
	 */
	has_back = FALSE;
	selected = 1;
	if (mstyle_is_element_set (mstyle, MSTYLE_PATTERN)) {
		selected = mstyle_get_pattern (mstyle);
		has_back = (selected != 0);
	}

	/* Setup pattern buttons & select the current pattern (or the 1st
	 * if none is selected)
	 * NOTE : This must be done AFTER the colour has been setup to
	 * avoid having it erased by initialization.
	 */
	state.back.pattern.draw_preview = &draw_pattern_selected;
	state.back.pattern.current_pattern = NULL;
	state.back.pattern.state = &state;
	for (i = 0; (name = pattern_buttons[i]) != NULL; ++i)
		setup_pattern_button (gui, name, &state.back.pattern, TRUE,
				      i+1, /* Pattern #s start at 1 */
				      i+1 == selected);

	/* If the pattern is 0 indicating no background colour
	 * Set background to No colour.  This will set states correctly.
	 */
	if (!has_back)
		gtk_toggle_button_set_active (state.back.back_color.autob,
					      TRUE);

	/* Setup the images in the border presets */
	for (i = 0; (name = border_preset_buttons[i]) != NULL; ++i) {
		GtkWidget * tmp = init_button_image (gui, name);
		if (tmp != NULL) {
			state.border.preset[i] = GTK_BUTTON (tmp);
			gtk_signal_connect (GTK_OBJECT (tmp), "clicked",
					    GTK_SIGNAL_FUNC (cb_border_preset_clicked),
					    &state);
			if (!state.is_multi && i == BORDER_PRESET_INSIDE)
				gtk_widget_hide (tmp);
		}
	}

	/* Draw the border preview */
	draw_border_preview (&state);

	/* Setup help */
	gtk_signal_connect (GTK_OBJECT (dialog), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);

	/* Handle apply */
	gtk_signal_connect (GTK_OBJECT (dialog), "apply",
			    GTK_SIGNAL_FUNC (cb_fmt_dialog_dialog_apply), &state);

	/* Ok, edit events from now on are real */
	state.enable_edit = TRUE;

	/* Bring up the dialog, and run it until someone hits ok or cancel */
	while ((res = gnome_dialog_run (GNOME_DIALOG (dialog))) > 0)
		;
}

/* Wrapper to ensure the libglade object gets removed on error */
void
dialog_cell_format (Workbook *wb, Sheet *sheet)
{
	Range const *selection;
	GladeXML    *gui;
	MStyle      *mstyle;
	gboolean     is_multi;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	selection = selection_first_range (sheet, TRUE);
	is_multi = g_list_length (sheet->selections) != 1 ||
	    !range_is_singleton (selection);

	mstyle = sheet_style_compute (sheet,
				      selection->start.col,
				      selection->start.row);
	fmt_dialog_impl (sheet, mstyle, gui, is_multi);
	
	gtk_object_unref (GTK_OBJECT (gui));
	mstyle_unref (mstyle);
}

/*
 * TODO 
 *
 * Translation to/from an MStyle.
 * 	- border from
 *
 * Formats 
 * 	- regexps to recognize parameterized formats (ie percent 4 decimals)
 *      - Generate formats from the dialogs.
 *      - Add the preview for the 1st upper-left cell.
 *
 * Borders
 * 	- Double lines for borders
 * 	- Add the 'text' elements in the preview
 * 	- Handle indeterminant.
 *
 * How to show ambiguities when applying to a range ?
 *
 * Wishlist
 * 	- Some undo capabilities in the dialog.
 * 	- How to distinguish between auto & custom colors.
 */
