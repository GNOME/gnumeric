/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-cell-format.c:  Implements a dialog to format cells.
 *
 * Authors:
 *  Jody Goldberg <jody@gnome.org>
 *  Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
  **/

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <style-color.h>
#include <widgets/widget-font-selector.h>
#include <widgets/gnumeric-dashed-canvas-line.h>
#include <widgets/gnumeric-combo-text.h>
#include <gui-util.h>
#include <selection.h>
#include <str.h>
#include <ranges.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <format.h>
#include <formats.h>
#include <pattern.h>
#include <position.h>
#include <mstyle.h>
#include <application.h>
#include <validation.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <commands.h>
#include <mathfunc.h>
#include <widgets/gnumeric-expr-entry.h>

#include <libart_lgpl/art_alphagamma.h>
#include <libart_lgpl/art_pixbuf.h>
#include <libart_lgpl/art_rgb_pixbuf_affine.h>
#include <glade/glade.h>
#include <pango/pangoft2.h>
#include <gal/widgets/widget-color-combo.h>

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
    F_GENERAL,		F_DECIMAL_BOX,	F_SEPARATOR,
    F_SYMBOL_LABEL,	F_SYMBOL,	F_DELETE,
    F_ENTRY,		F_LIST_SCROLL,	F_LIST,
    F_TEXT,		F_DECIMAL_SPIN,	F_NEGATIVE_SCROLL,
    F_NEGATIVE,         F_MAX_WIDGET
} FormatWidget;

/* The maximum number of chars in the formatting sample */
#define FORMAT_PREVIEW_MAX 40

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

	GtkWidget        *combo;
       	GCallback	  preview_update;
} ColorPicker;

typedef struct
{
	struct _FormatState *state;
	GtkToggleButton  *button;
	StyleBorderType	  pattern_index;
	gboolean	  is_selected;	/* Is it selected */
	StyleBorderLocation   index;
	guint		  rgba;
	gboolean          is_auto_color;
	gboolean	  is_set;	/* Has the element been changed */
} BorderPicker;

typedef struct {
	GtkLabel          *name;
	GnumericExprEntry *entry;
} ExprEntry;
typedef struct _FormatState
{
	GladeXML	*gui;
	WorkbookControlGUI	*wbcg;
	GtkDialog	*dialog;
	GtkNotebook	*notebook;
	GtkWidget	*apply_button;
	GtkWidget	*ok_button;

	Sheet		*sheet;
	SheetView	*sv;
	Value		*value;
	MStyle		*style, *result;
	StyleBorder *borders[STYLE_BORDER_EDGE_MAX];

	int	 	 selection_mask;
	gboolean	 enable_edit;

	struct {
		GtkLabel	*preview;
		GtkBox		*box;
		GtkWidget	*widget[F_MAX_WIDGET];
		struct {
			GtkTreeView	 *view;
			GtkListStore	 *model;
			GtkTreeSelection *selection;
		} negative_types;
		struct {
			GtkTreeView	 *view;
			GtkListStore	 *model;
			GtkTreeSelection *selection;
		} formats;

		StyleFormat	*spec;
		gint		 current_type;
		int		 num_decimals;
		int		 negative_format;
		int		 currency_index;
		gboolean	 use_separator;
	} format;
	struct {
		GtkCheckButton	*wrap;
		GtkSpinButton	*indent_button;
		GtkWidget	*indent_label;
		int		 indent;

		GtkSpinButton	*rotate_spinner;
		GnomeCanvasItem *rotate_marks[13];
		GnomeCanvasItem *line;
		GnomeCanvasItem *text;
		int		 rot_width, rot_height;
		int		 rotation;
		gulong		 motion_handle;
	} align;
	struct {
		FontSelector	*selector;
		ColorPicker      color;
		GtkCheckButton	*strikethrough;
	} font;
	struct {
		GnomeCanvas	*canvas;
		GtkButton 	*preset[BORDER_PRESET_MAX];
		GnomeCanvasItem	*back;
		GnomeCanvasItem *lines[20];

		BorderPicker	 edge[STYLE_BORDER_EDGE_MAX];
		ColorPicker      color;
		guint		 rgba;
		gboolean         is_auto_color;
		PatternPicker	 pattern;
	} border;
	struct {
		FooCanvas	*canvas;
		PreviewGrid     *grid;
		MStyle          *style;

		ColorPicker	 back_color;
		ColorPicker	 pattern_color;
		PatternPicker	 pattern;
	} back;
	struct {
		GtkCheckButton *hidden, *locked, *sheet_protected;

		gboolean	 sheet_protected_changed;
		gboolean	 sheet_protected_value;
	} protection;
	struct {
		GtkTable       *criteria_table;
		GtkOptionMenu  *constraint_type;
		GtkLabel       *operator_label;
		GtkOptionMenu  *op;
		ExprEntry	expr0, expr1;
		GtkToggleButton *allow_blank;
		GtkToggleButton *use_dropdown;

		struct {
			GtkLabel      *action_label;
			GtkLabel      *title_label;
			GtkLabel      *msg_label;
			GtkOptionMenu *action;
			GtkEntry      *title;
			GtkTextView   *msg;
			GtkImage      *image;
		} error;
		gboolean changed;
		int      valid;
	} validation;
	struct {
		GtkToggleButton *flag;

		GtkLabel        *title_label;
		GtkLabel        *msg_label;
		GtkEntry        *title;
		GtkTextView     *msg;
	} input_msg;

	void (*dialog_changed) (gpointer user_data);
	gpointer	dialog_changed_user_data;
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
	if (!state->enable_edit)
		return;

	if (state->dialog_changed)
		state->dialog_changed (state->dialog_changed_user_data);
	else
		gtk_widget_set_sensitive (state->apply_button, TRUE);
}

/* Default to the 'Format' page but remember which page we were on between
 * invocations */
static FormatDialogPosition_t fmt_dialog_page = FD_NUMBER;

#if 0
/* The last currency selected */
static int fmt_dialog_currency = 0;
#endif

/*
 * Callback routine to help remember which format tab was selected
 * between dialog invocations.
 */
static void
cb_page_select (G_GNUC_UNUSED GtkNotebook *notebook,
		G_GNUC_UNUSED GtkNotebookPage *page,
		gint page_num,
		G_GNUC_UNUSED	gpointer user_data)
{
	fmt_dialog_page = page_num;
}

static void
cb_notebook_destroy (GtkObject *obj, gpointer page_sig_ptr)
{
	g_signal_handler_disconnect (obj, GPOINTER_TO_UINT (page_sig_ptr));
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
		gtk_toggle_button_set_active (picker->current_pattern, FALSE);
		picker->current_pattern = button;
		picker->cur_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));
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
		      int const select_index)
{
	GtkWidget *tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkButton *button = GTK_BUTTON (tmp);
		if (flag) {
			GtkWidget * image = gnumeric_load_image (name);
			if (image != NULL)
				gtk_container_add (GTK_CONTAINER (tmp), image);
		}

		if (picker->current_pattern == NULL) {
			picker->default_button = GTK_TOGGLE_BUTTON (button);
			picker->current_pattern = picker->default_button;
			picker->cur_index = index;
		}

		gtk_button_set_relief (button, GTK_RELIEF_NONE);
		g_signal_connect (G_OBJECT (button),
			"toggled",
			G_CALLBACK (cb_toggle_changed), picker);
		g_object_set_data (G_OBJECT (button), "index",
				   GINT_TO_POINTER (index));

		/* Set the state AFTER the signal to get things redrawn correctly */
		if (index == select_index) {
			picker->cur_index = index;
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
						      TRUE);
		}
	} else
		g_warning ("CellFormat : Unexpected missing glade widget");
}

static void
setup_color_pickers (ColorPicker *picker,
	             char const * const color_group,
	             char const * const container,
		     char const * const default_caption,
		     char const * const caption,
		     FormatState *state,
		     GCallback preview_update,
		     MStyleElementType const e,
		     MStyle	 *mstyle)
{
	GtkWidget *combo, *box, *frame;
	ColorGroup *cg;
	StyleColor *mcolor = NULL;
	StyleColor *def_sc = NULL;
	GdkColor *def_gc;

	/* MSTYLE_ELEMENT_UNSET is abused as representing borders. */
	if (e != MSTYLE_ELEMENT_UNSET &&
	    !mstyle_is_element_conflict (mstyle, e))
		mcolor = mstyle_get_color (mstyle, e);

	switch (e) {
	case MSTYLE_COLOR_PATTERN:
	case MSTYLE_ELEMENT_UNSET: /* This is used for borders */
		def_sc = sheet_style_get_auto_pattern_color (state->sheet);
		break;
	case MSTYLE_COLOR_FORE:
		def_sc = style_color_auto_font ();
		break;
	case MSTYLE_COLOR_BACK:
		def_sc = style_color_auto_back ();
		break;
	default:
		g_warning ("Unhandled mstyle element!");
	}
	cg = color_group_fetch
		(color_group,
		 wb_control_view (WORKBOOK_CONTROL (state->wbcg)));

	def_gc = def_sc ? &def_sc->color : &gs_black;

	combo = color_combo_new (NULL, default_caption, def_gc, cg);
	g_signal_connect (G_OBJECT (combo),
		"color_changed",
		G_CALLBACK (preview_update), state);
	/* We don't need the functionality the button provides */
	gtk_widget_set_sensitive (COLOR_COMBO (combo)->preview_button, FALSE);
	/* FIXME: Should we disable the focus? Line 793 workbook-format-toolbar.c */
	gtk_combo_box_set_title (GTK_COMBO_BOX (combo), caption);

	/* Connect to the sample canvas and redraw it */

	picker->combo          = combo;
	picker->preview_update = preview_update;

	if (mcolor && !mcolor->is_auto)
		color_combo_set_color (COLOR_COMBO (combo), &mcolor->color);
	else
		color_combo_set_color_to_default (COLOR_COMBO (combo));

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	gtk_container_add (GTK_CONTAINER (frame), combo);

	box = glade_xml_get_widget (state->gui, container);
	gtk_box_pack_start (GTK_BOX (box), frame, FALSE, FALSE, 0);
	gtk_widget_show_all (frame);

	if (def_sc)
		style_color_unref (def_sc);
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
		GtkWidget * image = gnumeric_load_image (name);
		if (image != NULL)
			gtk_container_add (GTK_CONTAINER (tmp), image);
	}

	return tmp;
}

/*****************************************************************************/

static void
generate_format (FormatState *state)
{
	FormatFamily const page = state->format.current_type;
	GString		*new_format = g_string_new ("");

	/* It is a strange idea not to reuse FormatCharacteristics
	 in this file */
	/* So build one to pass to the style_format_... function */
	FormatCharacteristics format;

	format.thousands_sep = state->format.use_separator;
	format.num_decimals = state->format.num_decimals;
	format.negative_fmt = state->format.negative_format;
	format.currency_symbol_index = state->format.currency_index;
	format.list_element = 0; /* Don't need this one. */
	format.date_has_days = FALSE;
	format.date_has_months = FALSE;

	/* Update the format based on the current selections and page */
	switch (page) {
	case FMT_GENERAL :
	case FMT_TEXT :
		g_string_append (new_format, cell_formats[page][0]);
		break;

	case FMT_NUMBER :
		/* Make sure no currency is selected */
		format.currency_symbol_index = 0;

	case FMT_CURRENCY :
		style_format_number(new_format, &format);
		break;

	case FMT_ACCOUNT :
		style_format_account(new_format, &format);
		break;

	case FMT_PERCENT :
		style_format_percent(new_format, &format);
		break;

	case FMT_SCIENCE :
		style_format_science(new_format, &format);
		break;

	default :
		break;
	};

	if (new_format->len > 0) {
		char *tmp = style_format_str_as_XL (new_format->str, TRUE);
		gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]),
				    tmp);
		g_free (tmp);
	}

	g_string_free (new_format, TRUE);
}

static void
draw_format_preview (FormatState *state, gboolean regen_format)
{
	gchar		*preview;
	StyleFormat	*sf = NULL;

	if (regen_format)
		generate_format (state);

	/* Nothing to sample. */
	if (state->value == NULL)
		return;

	if (mstyle_is_element_set (state->result, MSTYLE_FORMAT))
		sf = mstyle_get_format (state->result);
	else if (!mstyle_is_element_conflict (state->style, MSTYLE_FORMAT))
		sf = mstyle_get_format (state->style);

	if (sf == NULL || state->value == NULL)
		return;

	if (style_format_is_general (sf) &&
	    VALUE_FMT (state->value) != NULL)
		sf = VALUE_FMT (state->value);

	preview = format_value (sf, state->value, NULL, -1,
			workbook_date_conv (state->sheet->workbook));
	if (strlen (preview) > FORMAT_PREVIEW_MAX)
		strcpy (&preview[FORMAT_PREVIEW_MAX - 5], " ...");

	gtk_label_set_text (state->format.preview, preview);
	g_free (preview);
}

static void
fillin_negative_samples (FormatState *state)
{
	static char const * const decimals = "098765432109876543210987654321";
	static char const * const formats[4] = {
		"-%s%s3%s210%s%s%s%s",
		"%s%s3%s210%s%s%s%s",
		"(%s%s3%s210%s%s%s%s)",
		"(%s%s3%s210%s%s%s%s)"
	};
	int const n = 30 - state->format.num_decimals;

	int const page = state->format.current_type;
	char const *space_b = "", *currency_b;
	char const *space_a = "", *currency_a;
	char decimal[2] = { '\0', '\0' } ;
	char thousand_sep[2] = { '\0', '\0' } ;
	char buf[50];
	int i;
	GtkTreeIter  iter;
	GtkTreePath *path;

	g_return_if_fail (page == 1 || page == 2);
	g_return_if_fail (state->format.num_decimals <= 30);

	if (state->format.use_separator)
		thousand_sep[0] = format_get_thousand ();
	if (state->format.num_decimals > 0)
		decimal[0] = format_get_decimal ();

	if (page == 2) {
		currency_b = (const gchar *)currency_symbols[state->format.currency_index].symbol;
		/*
		 * FIXME : This should be better hidden.
		 * Ideally the render would do this for us.
		 */
		if (currency_b[0] == '[' && currency_b[1] == '$') {
			char const *end = strchr (currency_b+2, '-');
			if (end == NULL)
				end = strchr (currency_b+2, ']');
			currency_b = g_strndup (currency_b+2, end-currency_b-2);
		} else
			currency_b = g_strdup (currency_b);

		if (currency_symbols[state->format.currency_index].has_space)
			space_b = " ";

		if (!currency_symbols[state->format.currency_index].precedes) {
			currency_a = currency_b;
			currency_b = "";
			space_a = space_b;
			space_b = "";
		} else {
			currency_a = "";
		}
	} else
		currency_a = currency_b = "";

	gtk_list_store_clear (state->format.negative_types.model);

	for (i = 0 ; i < 4; i++) {
		gtk_list_store_append (state->format.negative_types.model, &iter);
		sprintf (buf, formats[i], currency_b, space_b, thousand_sep, decimal, decimals + n, space_a, currency_a);
		gtk_list_store_set (state->format.negative_types.model, &iter,
			0,	i,
			1,  buf,
			2, (i % 2) ? "red" : NULL,
			-1);
	}

	/* If non empty then free the string */
	if (*currency_a)
		g_free ((char*)currency_a);
	if (*currency_b)
		g_free ((char*)currency_b);

	path = gtk_tree_path_new ();
	gtk_tree_path_append_index (path, state->format.negative_format);
	gtk_tree_selection_select_path (state->format.negative_types.selection, path);
	gtk_tree_path_free (path);
}

static void
cb_decimals_changed (GtkEditable *editable, FormatState *state)
{
	int const page = state->format.current_type;

	state->format.num_decimals =
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (editable));

	if (page == 1 || page == 2)
		fillin_negative_samples (state);

	draw_format_preview (state, TRUE);
}

static void
cb_separator_toggle (GtkObject *obj, FormatState *state)
{
	state->format.use_separator =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (obj));
	fillin_negative_samples (state);

	draw_format_preview (state, TRUE);
}

static void
fmt_dialog_init_fmt_list (FormatState *state, char const * const *formats,
			  GtkTreeIter *select)
{
	GtkTreeIter iter;
	char *fmt;
	char const *cur_fmt = state->format.spec->format;

	for (; *formats; formats++) {
		gtk_list_store_append (state->format.formats.model, &iter);
		fmt = style_format_str_as_XL (*formats, TRUE);
		gtk_list_store_set (state->format.formats.model, &iter,
			0, fmt, -1);
		g_free (fmt);

		if (!strcmp (*formats, cur_fmt))
			*select = iter;
	}
}

static void
fmt_dialog_enable_widgets (FormatState *state, int page)
{
	static FormatWidget const contents[12][8] = {
		/* General */
		{ F_GENERAL, F_MAX_WIDGET },
		/* Number */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SEPARATOR,
		  F_NEGATIVE_SCROLL, F_NEGATIVE, F_MAX_WIDGET },
		/* Currency */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SEPARATOR, F_SYMBOL_LABEL, F_SYMBOL,
		  F_NEGATIVE_SCROLL, F_NEGATIVE, F_MAX_WIDGET },
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
		{ F_ENTRY, F_LIST_SCROLL, F_LIST, F_DELETE, F_MAX_WIDGET }
	};

	int const old_page = state->format.current_type;
	int i;
	FormatWidget tmp;

	/* Hide widgets from old page */
	if (old_page >= 0)
		for (i = 0; (tmp = contents[old_page][i]) != F_MAX_WIDGET ; ++i)
			gtk_widget_hide (state->format.widget[tmp]);

	/* Set the default format if appropriate */
	if (page == FMT_GENERAL || page == FMT_ACCOUNT || page == FMT_FRACTION || page == FMT_TEXT) {
		FormatCharacteristics info;
		int list_elem = 0;
		char *tmp;
		if (page == cell_format_classify (state->format.spec, &info))
			list_elem = info.list_element;

		tmp = style_format_str_as_XL (cell_formats[page][list_elem], TRUE);
		gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]), tmp);
		g_free (tmp);
	}

	state->format.current_type = page;
	for (i = 0; (tmp = contents[page][i]) != F_MAX_WIDGET ; ++i) {
		GtkWidget *w = state->format.widget[tmp];
		gtk_widget_show (w);

		if (tmp == F_LIST) {
			int start = 0, end = -1;
			GtkTreeIter select;

			switch (page) {
			case 4: case 5: case 7:
				start = end = page;
				break;

			case 11:
				start = 0; end = 8;
				break;

			default :
				g_assert_not_reached ();
			};

			select.stamp = 0;
			gtk_list_store_clear (state->format.formats.model);
			for (; start <= end ; ++start)
				fmt_dialog_init_fmt_list (state,
					cell_formats[start], &select);

			/* If this is the custom page and the format has
			 * not been found append it */
			/* TODO We should add the list of other custom formats created.
			 *      It should be easy.  All that is needed is a way to differentiate
			 *      the std formats and the custom formats in the StyleFormat hash.
			 */
			if  (page == 11 && select.stamp == 0) {
				char *tmp = style_format_as_XL (state->format.spec, TRUE);
				gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]), tmp);
				g_free (tmp);
			} else if (select.stamp == 0)
				gtk_tree_model_get_iter_first (
					GTK_TREE_MODEL (state->format.formats.model),
					&select);

			if (select.stamp != 0)
				gtk_tree_selection_select_iter (
					state->format.formats.selection, &select);
		} else if (tmp == F_NEGATIVE)
			fillin_negative_samples (state);
		else if (tmp == F_DECIMAL_SPIN)
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->format.widget[F_DECIMAL_SPIN]),
				state->format.num_decimals);
		else if (tmp == F_SEPARATOR)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->format.widget[F_SEPARATOR]),
				state->format.use_separator);
	}

#if 0
	if ((cl = GTK_CLIST (state->format.widget[F_LIST])) != NULL)
		gnumeric_clist_make_selection_visible (cl);
#endif

	draw_format_preview (state, TRUE);
}

/*
 * Callback routine to manage the relationship between the number
 * formating radio buttons and the widgets required for each mode.
 */
static void
cb_format_class_changed (GtkToggleButton *button, FormatState *state)
{
	if (gtk_toggle_button_get_active (button))
		fmt_dialog_enable_widgets (state, GPOINTER_TO_INT (
			g_object_get_data (G_OBJECT (button), "index")));
}

static void
cb_format_entry_changed (GtkEditable *w, FormatState *state)
{
	char *fmt;
	if (!state->enable_edit)
		return;

	fmt = style_format_delocalize (gtk_entry_get_text (GTK_ENTRY (w)));
	if (strcmp (state->format.spec->format, fmt)) {
		style_format_unref (state->format.spec);
		state->format.spec = style_format_new_XL (fmt, FALSE);
		mstyle_set_format_text (state->result, fmt);
		fmt_dialog_changed (state);
		draw_format_preview (state, FALSE);
	} else
		g_free (fmt);
}

static void
cb_format_list_select (GtkTreeSelection *selection, FormatState *state)
{
	GtkTreeIter iter;
	gchar *text;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->format.formats.model),
		&iter, 0, &text, -1);
	gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]), text);
}

static gboolean
cb_format_currency_select (G_GNUC_UNUSED GtkWidget *ct,
			   char *new_text, FormatState *state)
{
	int i;

	/* ignore the clear while assigning a new value */
	if (!state->enable_edit || new_text == NULL || *new_text == '\0')
		return FALSE;

	for (i = 0; currency_symbols[i].symbol != NULL ; ++i)
		if (!strcmp (_(currency_symbols[i].description), new_text)) {
			state->format.currency_index = i;
			break;
		}

	if (state->format.current_type == 1 || state->format.current_type == 2)
		fillin_negative_samples (state);
	draw_format_preview (state, TRUE);

	return TRUE;
}

static void
cb_format_negative_form_selected (GtkTreeSelection *selection, FormatState *state)
{
	GtkTreeIter iter;
	int type;

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (state->format.negative_types.model),
		&iter, 0, &type, -1);
	state->format.negative_format = type;
	draw_format_preview (state, TRUE);
}

static gint
funny_currency_order (gconstpointer _a, gconstpointer _b)
{
	char const *a = (char const *)_a;
	char const *b = (char const *)_b;

	/* One letter versions?  */
	gboolean a1 = (a[0] && *(g_utf8_next_char(a)) == '\0');
	gboolean b1 = (b[0] && *(g_utf8_next_char(b)) == '\0');

	if (a1) {
		if (b1) {
			return strcmp (a, b);
		} else {
			return -1;
		}
	} else {
		if (b1) {
			return +1;
		} else {
			return strcmp (a, b);
		}
	}
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
	static char const * const widget_names[] = {
		"format_general_label",	"format_decimal_box",
		"format_separator",	"format_symbol_label",
		"format_symbol_select",	"format_delete",
		"format_entry",
		"format_list_scroll",	"format_list",
		"format_text_label",	"format_number_decimals",
		"format_negatives_scroll", "format_negatives",
		NULL
	};

	GtkWidget *tmp;
	GtkTreeViewColumn *column;
	GnmComboText *combo;
	char const *name;
	int i, page;
	FormatCharacteristics info;
	StyleFormat *fmt;

	/* Get the current format */
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FORMAT))
		fmt = mstyle_get_format (state->style);
	else
		fmt = style_format_general ();

	if (style_format_is_general (fmt) &&
	    state->value != NULL && VALUE_FMT (state->value) != NULL)
		fmt = VALUE_FMT (state->value);

	state->format.preview = NULL;
	state->format.spec = fmt;
	style_format_ref (fmt);

	/* The handlers will set the format family later.  -1 flags that
	 * all widgets are already hidden. */
	state->format.current_type = -1;

	/* Attempt to extract general parameters from the current format */
	if ((page = cell_format_classify (fmt, &info)) < 0)
		page = 11; /* Default to custom */

	/* Even if the format was not recognized it has set intelligent defaults */
	state->format.use_separator = info.thousands_sep;
	state->format.num_decimals = info.num_decimals;
	state->format.negative_format = info.negative_fmt;
	state->format.currency_index = info.currency_symbol_index;

	state->format.box = GTK_BOX (glade_xml_get_widget (state->gui, "format_box"));
	state->format.preview = GTK_LABEL (glade_xml_get_widget (state->gui, "format_sample"));

	/* Collect all the required format widgets and hide them */
	for (i = 0; (name = widget_names[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);

		g_return_if_fail (tmp != NULL);

		gtk_widget_hide (tmp);
		state->format.widget[i] = tmp;
	}

	/* setup the structure of the negative type list */
	state->format.negative_types.model = gtk_list_store_new (3, 
		G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING); 
	state->format.negative_types.view =
		GTK_TREE_VIEW (state->format.widget[F_NEGATIVE]);
	gtk_tree_view_set_model (state->format.negative_types.view,
		GTK_TREE_MODEL (state->format.negative_types.model));
	column = gtk_tree_view_column_new_with_attributes (_("Negative Number Format"),
			gtk_cell_renderer_text_new (),
			"text",		1, 
			"foreground",	2,
			NULL);
	gtk_tree_view_append_column (state->format.negative_types.view, column);
	state->format.negative_types.selection =
		gtk_tree_view_get_selection (state->format.negative_types.view);
	gtk_tree_selection_set_mode (state->format.negative_types.selection,
				     GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (state->format.negative_types.selection),
		"changed",
		G_CALLBACK (cb_format_negative_form_selected), state);

	/* Catch changes to the spin box */
	g_signal_connect (G_OBJECT (state->format.widget[F_DECIMAL_SPIN]),
		"changed",
		G_CALLBACK (cb_decimals_changed), state);

	/* Catch <return> in the spin box */
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->format.widget[F_DECIMAL_SPIN]));

	/* Setup special handlers for : Numbers */
	g_signal_connect (G_OBJECT (state->format.widget[F_SEPARATOR]),
		"toggled",
		G_CALLBACK (cb_separator_toggle), state);

	/* setup custom format list */
	state->format.formats.model = gtk_list_store_new (1, 
		G_TYPE_STRING); 
	state->format.formats.view =
		GTK_TREE_VIEW (state->format.widget[F_LIST]);
	gtk_tree_view_set_model (state->format.formats.view,
		GTK_TREE_MODEL (state->format.formats.model));
	column = gtk_tree_view_column_new_with_attributes (_("Number Formats"),
			gtk_cell_renderer_text_new (),
			"text",		0, 
			NULL);
	gtk_tree_view_append_column (state->format.formats.view, column);
	state->format.formats.selection =
		gtk_tree_view_get_selection (state->format.formats.view);
	gtk_tree_selection_set_mode (state->format.formats.selection,
				     GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (state->format.formats.selection),
		"changed",
		G_CALLBACK (cb_format_list_select), state);

	/* Setup handler Currency & Accounting currency symbols */
	combo = GNM_COMBO_TEXT (state->format.widget[F_SYMBOL]);
	if (combo != NULL) {
		GList *ptr, *l = NULL;

		for (i = 0; currency_symbols[i].symbol != NULL ; ++i)
			l = g_list_append (l, _((const gchar *)currency_symbols[i].description));
		l = g_list_sort (l, funny_currency_order);

		for (ptr = l; ptr != NULL ; ptr = ptr->next)
			gnm_combo_text_add_item	(combo, ptr->data);
		g_list_free (l);
		gnm_combo_text_set_text	 (combo,
			_((const gchar *)currency_symbols[state->format.currency_index].description),
			GNM_COMBO_TEXT_FROM_TOP);

		g_signal_connect (G_OBJECT (combo),
			"entry_changed",
			G_CALLBACK (cb_format_currency_select), state);
	}

	/* Setup special handler for Custom */
	g_signal_connect (G_OBJECT (state->format.widget[F_ENTRY]),
		"changed",
		G_CALLBACK (cb_format_entry_changed), state);
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->format.widget[F_ENTRY]));

	/* Setup format buttons to toggle between the format pages */
	for (i = 0; (name = format_buttons[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);
		if (tmp == NULL)
			continue;
		g_object_set_data (G_OBJECT (tmp), "index",
				   GINT_TO_POINTER (i));

		if (i == page) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp), TRUE);
			cb_format_class_changed (GTK_TOGGLE_BUTTON (tmp), state);
		}

		g_signal_connect (G_OBJECT (tmp),
			"toggled",
			G_CALLBACK (cb_format_class_changed), state);

	}
}

/*****************************************************************************/

static void
cb_indent_changed (GtkEditable *editable, FormatState *state)
{
	if (state->enable_edit) {
		GtkSpinButton *sb = GTK_SPIN_BUTTON (editable);
		int val = gtk_spin_button_get_value_as_int (sb);

		if (state->align.indent != val) {
			state->align.indent = val;
			mstyle_set_indent (state->result, val);
			fmt_dialog_changed (state);
		}
	}
}

static void
cb_align_h_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	if (state->enable_edit) {
		StyleHAlignFlags const new_h =
			GPOINTER_TO_INT (g_object_get_data (
				G_OBJECT (button), "align"));
		gboolean const supports_indent =
			(new_h == HALIGN_LEFT || new_h == HALIGN_RIGHT);
		mstyle_set_align_h (state->result, new_h);
		gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_button),
					  supports_indent);
		gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_label),
					  supports_indent);
		/* TODO : Should we 0 the indent ? */
		fmt_dialog_changed (state);
	}
}

static void
cb_align_v_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	if (state->enable_edit) {
		mstyle_set_align_v (
			state->result,
			GPOINTER_TO_INT (g_object_get_data (
			G_OBJECT (button), "align")));
		fmt_dialog_changed (state);
	}
}

static void
cb_align_wrap_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		mstyle_set_wrap_text (state->result,
				      gtk_toggle_button_get_active (button));
		fmt_dialog_changed (state);
	}
}

static void
fmt_dialog_init_align_radio (char const * const name,
			     int const val, int const target,
			     FormatState *state,
			     GCallback handler)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
	if (tmp != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp),
					      val == target);
		g_object_set_data (G_OBJECT (tmp), "align",
				     GINT_TO_POINTER (val));
		g_signal_connect (G_OBJECT (tmp),
			"toggled",
			handler, state);
	}
}

static void
cb_rotate_changed (GtkEditable *editable, FormatState *state)
{
	double res[6], trans[6];
	char const *colour;
	int i;

	if (editable != NULL && state->enable_edit) {
		GtkSpinButton *sb = GTK_SPIN_BUTTON (editable);
		int val = gtk_spin_button_get_value_as_int (sb);

		if (state->align.rotation != val) {
			state->align.rotation = val;
			/* mstyle_set_indent (state->result, val); */
			fmt_dialog_changed (state);
		}
	}

	for (i = 0 ; i <= 12 ; i++)
		if (state->align.rotate_marks [i] != NULL) {
			colour = (state->align.rotation == (i-6)*15) ? "green" : "black";
			gnome_canvas_item_set (state->align.rotate_marks [i],
					       "fill_color",	colour,
					       NULL);
		}
	if (state->align.line != NULL) {
		GnomeCanvasPoints *points = gnome_canvas_points_new (2);
		double rad = state->align.rotation * M_PIgnum / 180.;
		points->coords [0] =  15 + cos (rad) * state->align.rot_width;
		points->coords [1] = 100 - sin (rad) * state->align.rot_width;
		points->coords [2] =  15 + cos (rad) * 72.;
		points->coords [3] = 100 - sin (rad) * 72.;
		gnome_canvas_item_set (state->align.line,
				       "points", points,
				       NULL);
		gnome_canvas_points_free (points);
	}

	art_affine_translate (trans, 0., -state->align.rot_height/2);
	art_affine_rotate (res, -state->align.rotation);
	art_affine_multiply (res, trans, res);
	art_affine_translate (trans, 15., 100.);
	art_affine_multiply (res, res, trans);
	gnome_canvas_item_affine_absolute (state->align.text, res);
}

static void
cb_rotate_canvas_realize (GnomeCanvas *canvas, FormatState *state)
{
	GnomeCanvasItem *item;
	GnomeCanvasGroup  *group;
	double rad, x, y, size;
	int i, bx, by;
	gint w, h;
	GdkPixbuf *pixbuf;
	FT_Bitmap ft_bitmap;
	PangoLayout *layout;
	PangoContext *context;
	PangoAttrList	*attrs;
	PangoAttribute  *attr;
	guint8 const *ps;
	guint8 *pd;

	GtkStyle  *style = gtk_style_copy (GTK_WIDGET (canvas)->style);
	style->bg [GTK_STATE_NORMAL] = gs_white;
	gtk_widget_set_style (GTK_WIDGET (canvas), style);
	gtk_style_unref (style);

	gnome_canvas_set_scroll_region (canvas, 0, 0, 100, 200);
	gnome_canvas_scroll_to (canvas, 0, 0);

	group = GNOME_CANVAS_GROUP (gnome_canvas_root (canvas));
	for (i = 0 ; i <= 12 ; i++) {
		rad = (i-6) * M_PIgnum / 12.;
		x = 15 + cos (rad) * 80.;
		y = 100 - sin (rad) * 80.;
		size = (i % 3) ? 3 : 4;
		item = gnome_canvas_item_new (group,
			GNOME_TYPE_CANVAS_ELLIPSE,
			"x1", x-size,	"y1", y-size,
			"x2", x+size,	"y2", y+size,
			"width_pixels", (int) 1,
			"outline_color","black",
			"fill_color",	"black",
			NULL);
		state->align.rotate_marks [i] = item;
	}
	state->align.line = gnome_canvas_item_new (group,
		GNOME_TYPE_CANVAS_LINE,
		"fill_color",	"black",
		"width_units",	2.,
		NULL);
	state->align.text = gnome_canvas_item_new (group,
		GNOME_TYPE_CANVAS_PIXBUF,
		NULL);

	context = pango_ft2_get_context (
		application_display_dpi_get (TRUE),
		application_display_dpi_get (FALSE));
	layout = pango_layout_new (context);
	pango_layout_set_font_description (layout, 
		pango_context_get_font_description (gnumeric_default_font->pango.context));
	pango_layout_set_text (layout, _("Text"), -1);
 	attrs = pango_attr_list_new ();
	pango_layout_set_attributes (layout, attrs);
	attr = pango_attr_scale_new (1.3);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert (attrs, attr);

	pango_layout_get_pixel_size (layout, &w, &h);

	if (w == 0 || h == 0)
		return;
	ft_bitmap.rows         = h;
	ft_bitmap.width        = w;
	ft_bitmap.pitch        = (w+3) & ~3;
	ft_bitmap.buffer       = g_malloc0 (ft_bitmap.rows * ft_bitmap.pitch);
	ft_bitmap.num_grays    = 256;
	ft_bitmap.pixel_mode   = ft_pixel_mode_grays;
	ft_bitmap.palette_mode = 0;
	ft_bitmap.palette      = NULL;

	pango_ft2_render_layout (&ft_bitmap, layout, 0, 0);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
				     ft_bitmap.width, ft_bitmap.rows);
	for (by = 0; by < ft_bitmap.rows; by++) {
	        pd = gdk_pixbuf_get_pixels (pixbuf)
			+ by * gdk_pixbuf_get_rowstride (pixbuf);
		ps = ft_bitmap.buffer + by * ft_bitmap.pitch;
		for (bx = 0; bx < ft_bitmap.width; bx++) {
			*pd++ = 0;
		        *pd++ = 0;
			*pd++ = 0;
			*pd++ = *ps++;
		}
	}

	gnome_canvas_item_set (state->align.text,
		"pixbuf",	pixbuf,
		NULL);
	g_object_unref (G_OBJECT (pixbuf));
	state->align.rot_width  = w;
	state->align.rot_height = h;
	cb_rotate_changed (NULL, state);
}

static void
set_rot_from_point (FormatState *state, GnomeCanvas *canvas, double x, double y)
{
	gnome_canvas_window_to_world (canvas, x, y, &x, &y);
	x -= 15.;	if (x < 0.) x = 0.;
	y -= 100.;
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->align.rotate_spinner),
		-gnumeric_fake_round (atan (y/x) * 180 / M_PIgnum));
}

static gboolean
cb_rotate_motion_notify_event (GnomeCanvas *canvas, GdkEventMotion *event,
			       FormatState *state)
{
	set_rot_from_point (state, canvas, event->x, event->y);
	return TRUE;
}

static gboolean
cb_rotate_canvas_button (GnomeCanvas *canvas, GdkEventButton *event,
			 FormatState *state)
{
	if (event->type == GDK_BUTTON_PRESS) {
		set_rot_from_point (state, canvas, event->x, event->y);
		if (state->align.motion_handle == 0) {
			gdk_pointer_grab (canvas->layout.bin_window, FALSE,
					  GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
					  NULL, NULL, event->time);

			state->align.motion_handle = g_signal_connect (G_OBJECT (canvas),
				"motion_notify_event",
				G_CALLBACK (cb_rotate_motion_notify_event), state);
		}
		return TRUE;
	} else if (event->type == GDK_BUTTON_RELEASE) {
		if (state->align.motion_handle != 0) {
			gdk_pointer_ungrab (event->time);
			g_signal_handler_disconnect (canvas, state->align.motion_handle);
			state->align.motion_handle = 0;
		}
		return TRUE;
	} else
		return FALSE;
}

static void
fmt_dialog_init_align_page (FormatState *state)
{
	static struct {
		char const * const	name;
		StyleHAlignFlags	align;
	} const h_buttons[] = {
	    { "halign_left",	HALIGN_LEFT },
	    { "halign_center",	HALIGN_CENTER },
	    { "halign_right",	HALIGN_RIGHT },
	    { "halign_general",	HALIGN_GENERAL },
	    { "halign_justify",	HALIGN_JUSTIFY },
	    { "halign_fill",	HALIGN_FILL },
	    { "halign_center_across_selection",	HALIGN_CENTER_ACROSS_SELECTION },
	    { NULL }
	};
	static struct {
		char const * const	name;
		StyleVAlignFlags	align;
	} const v_buttons[] = {
	    { "valign_top", VALIGN_TOP },
	    { "valign_center", VALIGN_CENTER },
	    { "valign_bottom", VALIGN_BOTTOM },
	    { "valign_justify", VALIGN_JUSTIFY },
	    { NULL }
	};

	GtkWidget *w;
	gboolean wrap = FALSE;
	StyleHAlignFlags    h = HALIGN_GENERAL;
	StyleVAlignFlags    v = VALIGN_CENTER;
	char const *name;
	int i;

	if (!mstyle_is_element_conflict (state->style, MSTYLE_ALIGN_H))
		h = mstyle_get_align_h (state->style);
	if (!mstyle_is_element_conflict (state->style, MSTYLE_ALIGN_V))
		v = mstyle_get_align_v (state->style);

	/* Setup the horizontal buttons */
	for (i = 0; (name = h_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, h_buttons[i].align,
					     h, state,
					     G_CALLBACK (cb_align_h_toggle));

	/* Setup the vertical buttons */
	for (i = 0; (name = v_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, v_buttons[i].align,
					     v, state,
					     G_CALLBACK (cb_align_v_toggle));

	/* Setup the wrap button, and assign the current value */
	if (!mstyle_is_element_conflict (state->style, MSTYLE_WRAP_TEXT))
		wrap = mstyle_get_wrap_text (state->style);

	w = glade_xml_get_widget (state->gui, "align_wrap");
	state->align.wrap = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), wrap);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_align_wrap_toggle), state);

	if (mstyle_is_element_conflict (state->style, MSTYLE_ALIGN_V) &&
	    (h != HALIGN_LEFT && h != HALIGN_RIGHT))
		state->align.indent = 0;
	else
		state->align.indent = mstyle_get_indent (state->style);

	state->align.indent_label =
		glade_xml_get_widget (state->gui, "halign_indent_label");
	w = glade_xml_get_widget (state->gui, "halign_indent");
	state->align.indent_button = GTK_SPIN_BUTTON (w);
	gtk_spin_button_set_value (state->align.indent_button, state->align.indent);
	gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_button),
				  (h == HALIGN_LEFT || h == HALIGN_RIGHT));
	gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_label),
				  (h == HALIGN_LEFT || h == HALIGN_RIGHT));

	/* Catch changes to the spin box */
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_indent_changed), state);

	/* Catch <return> in the spin box */
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (w));

	/* setup the rotation canvas */
	state->align.rotation = 0;
	memset (state->align.rotate_marks, 0,
		sizeof (state->align.rotate_marks));
	w = glade_xml_get_widget (state->gui, "rotate_spinner");
	state->align.rotate_spinner = GTK_SPIN_BUTTON (w);
	g_signal_connect (G_OBJECT (w),
		"changed",
		G_CALLBACK (cb_rotate_changed), state);

	state->align.motion_handle = 0;
	w = glade_xml_get_widget (state->gui, "rotate_canvas");
	g_signal_connect (G_OBJECT (w),
		"realize",
		G_CALLBACK (cb_rotate_canvas_realize), state);
	g_signal_connect (G_OBJECT (w),
		"button_press_event",
		G_CALLBACK (cb_rotate_canvas_button), state);
	g_signal_connect (G_OBJECT (w),
		"button_release_event",
		G_CALLBACK (cb_rotate_canvas_button), state);
	gtk_spin_button_set_value (state->align.rotate_spinner,
				   state->align.rotation);
}

/*****************************************************************************/

static void
cb_font_changed (G_GNUC_UNUSED GtkWidget *widget,
		 MStyle *mstyle, FormatState *state)
{
	static MStyleElementType const font_types[] = {
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_SIZE,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
		MSTYLE_FONT_UNDERLINE,
		MSTYLE_FONT_STRIKETHROUGH,
		MSTYLE_COLOR_FORE
	};
	int i;
	static int const num_font_types = sizeof (font_types) /
		sizeof (MStyleElementType);

	gboolean changed = FALSE;
	g_return_if_fail (state != NULL);

	if (!state->enable_edit)
		return;

	for (i = 0 ; i < num_font_types; i++) {
		MStyleElementType const t = font_types[i];
		if (mstyle_is_element_set (mstyle, t)) {
			mstyle_replace_element (mstyle, state->result, t);
			changed = TRUE;
		}
	}

	if (changed)
		fmt_dialog_changed (state);
}

/*
 * A callback to set the font color.
 * It is called whenever the color combo changes value.
 */
static void
cb_font_preview_color (G_GNUC_UNUSED ColorCombo *combo,
		       GdkColor *c,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       gboolean is_default, FormatState *state)
{
	StyleColor *col;

	if (!state->enable_edit)
		return;

	col = (is_default
	       ? style_color_auto_font ()
	       : style_color_new (c->red, c->green, c->blue));
	font_selector_set_color (state->font.selector, col);
}

static void
cb_font_strike_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		font_selector_set_strike (
			state->font.selector,
			gtk_toggle_button_get_active (button));
	}
}

static gboolean
cb_font_underline_changed (G_GNUC_UNUSED GtkWidget *ct,
			   char *new_text, FormatState *state)
{
	StyleUnderlineType res = UNDERLINE_NONE;

	/* ignore the clear while assigning a new value */
	if (!state->enable_edit || new_text == NULL || *new_text == '\0')
		return FALSE;

	/* There must be a better way than this */
	if (!g_ascii_strcasecmp (new_text, _("Single")))
		res = UNDERLINE_SINGLE;
	else if (!g_ascii_strcasecmp (new_text, _("Double")))
		res = UNDERLINE_DOUBLE;
	else if (g_ascii_strcasecmp (new_text, _("None")))
		g_warning ("Invalid underline style '%s', assuming NONE", new_text);

	font_selector_set_underline (state->font.selector, res);
	return TRUE;
}

/* Manually insert the font selector, and setup signals */
static void
fmt_dialog_init_font_page (FormatState *state)
{
	GtkWidget *tmp = font_selector_new ();
	FontSelector *font_widget = FONT_SELECTOR (tmp);
	GtkWidget *container = glade_xml_get_widget (state->gui, "font_box");
	GtkWidget *uline = glade_xml_get_widget (state->gui, "underline_combo");
	char const *uline_str;
	GtkWidget *strike = glade_xml_get_widget (state->gui, "strikethrough_button");
	gboolean   strikethrough = FALSE;

	g_return_if_fail (container != NULL);
	g_return_if_fail (uline != NULL);
	g_return_if_fail (strike != NULL);

	/* TODO : How to insert the font box in the right place initially */
	gtk_widget_show (tmp);
	gtk_box_pack_start (GTK_BOX (container), tmp, TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (container), tmp, 0);

	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (font_widget->font_name_entry));
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (font_widget->font_style_entry));
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (font_widget->font_size_entry));

	state->font.selector = FONT_SELECTOR (font_widget);

	font_selector_set_value (state->font.selector, state->value);

	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_NAME))
		font_selector_set_name (state->font.selector,
					mstyle_get_font_name (state->style));

	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_BOLD) &&
	    !mstyle_is_element_conflict (state->style, MSTYLE_FONT_ITALIC))
		font_selector_set_style (state->font.selector,
					 mstyle_get_font_bold (state->style),
					 mstyle_get_font_italic (state->style));
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_SIZE))
		font_selector_set_points (state->font.selector,
					  mstyle_get_font_size (state->style));

	gnm_combo_text_add_item	(GNM_COMBO_TEXT (uline), _("None"));
	gnm_combo_text_add_item	(GNM_COMBO_TEXT (uline), _("Single"));
	gnm_combo_text_add_item	(GNM_COMBO_TEXT (uline), _("Double"));
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_UNDERLINE)) {
		switch (mstyle_get_font_uline (state->style)) {
		default :
		case UNDERLINE_NONE   : uline_str = _("None"); break;
		case UNDERLINE_SINGLE : uline_str = _("Single"); break;
		case UNDERLINE_DOUBLE : uline_str = _("Double"); break;
		};
		font_selector_set_underline (state->font.selector,
			mstyle_get_font_uline (state->style));
	} else
		uline_str = "";
	gnm_combo_text_set_text	(GNM_COMBO_TEXT (uline), uline_str,
		GNM_COMBO_TEXT_FROM_TOP);
	g_signal_connect (G_OBJECT (uline),
		"entry_changed",
		G_CALLBACK (cb_font_underline_changed), state);
	gtk_widget_show_all (uline);

	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_STRIKETHROUGH))
		strikethrough = mstyle_get_font_strike (state->style);

	state->font.strikethrough = GTK_CHECK_BUTTON (strike);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (strike), strikethrough);
	font_selector_set_strike (state->font.selector, strikethrough);

	g_signal_connect (G_OBJECT (strike),
		"toggled",
		G_CALLBACK (cb_font_strike_toggle), state);

	if (!mstyle_is_element_conflict (state->style, MSTYLE_COLOR_FORE))
		font_selector_set_color (
			state->font.selector,
			style_color_ref (mstyle_get_color (state->style, MSTYLE_COLOR_FORE)));

	g_signal_connect (G_OBJECT (font_widget),
		"font_changed",
		G_CALLBACK (cb_font_changed), state);
}

/*****************************************************************************/

static MStyle *
cb_pattern_preview_get_cell_style (G_GNUC_UNUSED PreviewGrid *pg,
				   G_GNUC_UNUSED int row,
				   G_GNUC_UNUSED int col,
				   MStyle *style)
{
	mstyle_ref (style);
	return style;
}

static void
draw_pattern_preview (FormatState *state)
{
	g_return_if_fail (state->back.style != NULL);

	fmt_dialog_changed (state);

	if (state->enable_edit) {
		mstyle_replace_element (state->back.style, state->result, MSTYLE_PATTERN);
		mstyle_replace_element (state->back.style, state->result, MSTYLE_COLOR_BACK);
		mstyle_replace_element (state->back.style, state->result, MSTYLE_COLOR_PATTERN);
	}

	foo_canvas_request_redraw (state->back.canvas,
		-2, -2, INT_MAX/2, INT_MAX/2);
}

static void
cb_back_preview_color (G_GNUC_UNUSED ColorCombo *combo,
		       GdkColor *c,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       gboolean is_default,
		       FormatState *state)
{
	StyleColor *sc;

	g_return_if_fail (c);

	if (is_default) {
		sc = style_color_auto_back ();
		mstyle_set_pattern (state->back.style, 0);
	} else {
		sc = style_color_new (c->red, c->green, c->blue);
		mstyle_set_pattern (state->back.style, state->back.pattern.cur_index);
	}

	mstyle_set_color (state->back.style, MSTYLE_COLOR_BACK, sc);
	draw_pattern_preview (state);
}

static void
cb_pattern_preview_color (G_GNUC_UNUSED ColorCombo *combo,
			  GdkColor *c,
			  G_GNUC_UNUSED gboolean is_custom,
			  G_GNUC_UNUSED gboolean by_user,
			  gboolean is_default, FormatState *state)
{
	StyleColor *col = (is_default
			   ? sheet_style_get_auto_pattern_color (state->sheet)
			   : style_color_new (c->red, c->green, c->blue));

	mstyle_set_color (state->back.style, MSTYLE_COLOR_PATTERN, col);

	draw_pattern_preview (state);
}

static void
draw_pattern_selected (FormatState *state)
{
	mstyle_set_pattern (state->back.style, state->back.pattern.cur_index);
	draw_pattern_preview (state);
}

static void
fmt_dialog_init_background_page (FormatState *state)
{
	GtkWidget *widget;
	int w = 120;
	int h = 60;

	widget = foo_canvas_new ();
	state->back.canvas = FOO_CANVAS (widget);
	gtk_widget_set_size_request (widget, w, h);
	foo_canvas_set_scroll_region (state->back.canvas, -1, -1, w, h);

	widget = glade_xml_get_widget (state->gui, "back_sample_frame");
	gtk_container_add (GTK_CONTAINER (widget),
		GTK_WIDGET (state->back.canvas));
	gtk_widget_show_all (widget);

	state->back.grid = PREVIEW_GRID (foo_canvas_item_new (
		foo_canvas_root (state->back.canvas),
		preview_grid_get_type (),
		"RenderGridlines", FALSE,
		"DefaultColWidth", w,
		"DefaultRowHeight", h,
		NULL));
	g_signal_connect (G_OBJECT (state->back.grid),
		"get_cell_style",
		G_CALLBACK (cb_pattern_preview_get_cell_style), state->back.style);
}

/*****************************************************************************/

/*
 * This is self-evident.
 * I stared at it for 15 minutes before realizing that it's self-evident,
 * but it is. - jon_kare
 *
 * @points:   x, y coordinates for the endpoints of the line segment.
 * @states:   A bitmap of states the coordinates are valid for.
 * @location: Location.
 */
#define L 10.	/* Left */
#define R 140.	/* Right */
#define T 10.	/* Top */
#define B 90.	/* Bottom */
#define H 50.	/* Horizontal Middle */
#define V 75.	/* Vertical Middle */

static struct
{
	double const			points[4];
	int const			states;
	StyleBorderLocation	const	location;
} const line_info[] =
{
	/*
	state 1 = single cell;
	state 2 = multi vert, single horiz (A1:A2);
	state 3 = single vert, multi horiz (A1:B1);
	state 4 = multi vertical & multi horizontal
	*/

	/* 1, 2, 3, 4 */
	{ { L, T, R, T }, 0xf, STYLE_BORDER_TOP },
	{ { L, B, R, B }, 0xf, STYLE_BORDER_BOTTOM },
	{ { L, T, L, B }, 0xf, STYLE_BORDER_LEFT },
	{ { R, T, R, B }, 0xf, STYLE_BORDER_RIGHT },

	/* Only for state 2 & 4 */
	{ { L, H, R, H }, 0xa, STYLE_BORDER_HORIZ },

	/* Only for state 3 & 4 */
	{ { V, T, V, B }, 0xc, STYLE_BORDER_VERT },

	/* Only for state 1 & 4 */
	{ { L, T, R, B }, 0x9, STYLE_BORDER_REV_DIAG },
	{ { L, B, R, T }, 0x9, STYLE_BORDER_DIAG},

	/* Only for state 2 */
	{ { L, T, R, H }, 0x2, STYLE_BORDER_REV_DIAG },
	{ { L, H, R, B }, 0x2, STYLE_BORDER_REV_DIAG },
	{ { L, H, R, T }, 0x2, STYLE_BORDER_DIAG },
	{ { L, B, R, H }, 0x2, STYLE_BORDER_DIAG },

	/* Only for state 3 */
	{ { L, T, V, B }, 0x4, STYLE_BORDER_REV_DIAG },
	{ { V, T, R, B }, 0x4, STYLE_BORDER_REV_DIAG },
	{ { L, B, V, T }, 0x4, STYLE_BORDER_DIAG },
	{ { V, B, R, T }, 0x4, STYLE_BORDER_DIAG },

	/* Only for state 4 */
	{ { L, H, V, B }, 0x8, STYLE_BORDER_REV_DIAG },
	{ { V, T, R, H }, 0x8, STYLE_BORDER_REV_DIAG },
	{ { L, H, V, T }, 0x8, STYLE_BORDER_DIAG },
	{ { V, B, R, H }, 0x8, STYLE_BORDER_DIAG },

	{ { 0., 0., 0., 0. }, 0, 0 }
};

static StyleBorder *
border_get_mstyle (FormatState const *state, StyleBorderLocation const loc)
{
	BorderPicker const * edge = & state->border.edge[loc];
	StyleColor *color;
	/* Don't set borders that have not been changed */
	if (!edge->is_set)
		return NULL;

	if (!edge->is_selected)
		return style_border_ref (style_border_none ());

	if (edge->is_auto_color) {
		color = sheet_style_get_auto_pattern_color (state->sheet);
	} else {
		guint8 const r = (guint8) (edge->rgba >> 24);
		guint8 const g = (guint8) (edge->rgba >> 16);
		guint8 const b = (guint8) (edge->rgba >>  8);
		color = style_color_new_i8 (r, g, b);
	}
	return style_border_fetch
		(state->border.edge[loc].pattern_index, color,
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
	if (edge->is_auto_color) {
		if (!state->border.is_auto_color) {
			edge->is_auto_color = state->border.is_auto_color;
			changed = TRUE;
		}
	} else if (edge->rgba != state->border.rgba)
		changed = TRUE;

	if (edge->rgba != state->border.rgba) {
		edge->rgba = state->border.rgba;

		for (i = 0; line_info[i].states != 0 ; ++i ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL)
				gnome_canvas_item_set (
					GNOME_CANVAS_ITEM (state->border.lines[i]),
					"fill_color_rgba", edge->rgba,
					NULL);
		}
	}
	if ((int)edge->pattern_index != state->border.pattern.cur_index) {
		edge->pattern_index = state->border.pattern.cur_index;
		for (i = 0; line_info[i].states != 0 ; ++i ) {
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
 * Map canvas x.y coords to a border type
 * Handle all of the various permutations of lines
 */
static gboolean
border_event (GtkWidget *widget, GdkEventButton *event, FormatState *state)
{
	double x = event->x;
	double y = event->y;
	BorderPicker		*edge;

	/* Crap!  This variable is always initialized.
	 * However, the compiler is confused and thinks it is not
	 * so we are forced to pick a random irrelevant value.
	 */
	StyleBorderLocation	 which = STYLE_BORDER_LEFT;

	if (event->button != 1)
		return FALSE;

	/* If we receive a double or triple translate them into single clicks */
	if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS) {
		GdkEventType type = event->type;
		event->type = GDK_BUTTON_PRESS;
		border_event (widget, event, state);
		if (event->type == GDK_3BUTTON_PRESS)
			border_event (widget, event, state);
		event->type = type;
	}

	/* The edges are always there */
	if (x <= L+5.)		which = STYLE_BORDER_LEFT;
	else if (y <= T+5.)	which = STYLE_BORDER_TOP;
	else if (y >= B-5.)	which = STYLE_BORDER_BOTTOM;
	else if (x >= R-5.)	which = STYLE_BORDER_RIGHT;
	else switch (state->selection_mask) {
	case 1 :
		if ((x < V) == (y < H))
			which = STYLE_BORDER_REV_DIAG;
		else
			which = STYLE_BORDER_DIAG;
		break;
	case 2 :
		if (H-5. < y  && y < H+5.)
			which = STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the top */
			if (y > H) y -= H-10.;

			if ((x < V) == (y < H/2.))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;
	case 4 :
		if (V-5. < x  && x < V+5.)
			which = STYLE_BORDER_VERT;
		else {
			/* Map everything back to the left */
			if (x > V) x -= V-10.;

			if ((x < V/2.) == (y < H))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;
	case 8 :
		if (V-5. < x  && x < V+5.)
			which = STYLE_BORDER_VERT;
		else if (H-5. < y  && y < H+5.)
			which = STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the 1st quadrant */
			if (x > V) x -= V-10.;
			if (y > H) y -= H-10.;

			if ((x < V/2.) == (y < H/2.))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;

	default :
		g_assert_not_reached ();
	}

	edge = &state->border.edge[which];
	if (!border_format_has_changed (state, edge) || !edge->is_selected)
		gtk_toggle_button_set_active (edge->button,
					      !edge->is_selected);
	else
		fmt_dialog_changed (state);

	return TRUE;
}

static void
draw_border_preview (FormatState *state)
{
	static double const corners[12][6] = {
	    { L-5., T, L, T, L, T-5. },
	    { R+5., T, R, T, R, T-5 },
	    { L-5., B, L, B, L, B+5. },
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

		g_signal_connect (G_OBJECT (state->border.canvas),
			"button-press-event",
			G_CALLBACK (border_event), state);

		state->border.back = gnome_canvas_item_new (group,
			GNOME_TYPE_CANVAS_RECT,
			"x1", L-10.,	"y1", T-10.,
			"x2", R+10.,	"y2", B+10.,
			"width_pixels", (int) 0,
			"fill_color",	"white",
			NULL);

		/* Draw the corners */
		points = gnome_canvas_points_new (3);

		for (i = 0; i < 12 ; ++i) {
			if (i >= 8) {
				if (!(state->selection_mask & 0xa))
					continue;
			} else if (i >= 4) {
				if (!(state->selection_mask & 0xc))
					continue;
			}

			for (j = 6 ; --j >= 0 ;)
				points->coords[j] = corners[i][j];

			gnome_canvas_item_new (group,
					       gnome_canvas_line_get_type (),
					       "width_pixels",	(int) 0,
					       "fill_color",	"gray63",
					       "points",	points,
					       NULL);
		}
		gnome_canvas_points_free (points);

		points = gnome_canvas_points_new (2);
		for (i = 0; line_info[i].states != 0 ; ++i ) {
			for (j = 4; --j >= 0 ; )
				points->coords[j] = line_info[i].points[j];

			if (line_info[i].states & state->selection_mask) {
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

	for (i = 0; i < STYLE_BORDER_EDGE_MAX; ++i) {
		BorderPicker *border = &state->border.edge[i];
		void (*func)(GnomeCanvasItem *item) = border->is_selected
			? &gnome_canvas_item_show : &gnome_canvas_item_hide;

		for (j = 0; line_info[j].states != 0 ; ++j) {
			if ((int)line_info[j].location == i &&
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
	StyleBorderLocation i, last;

	if (state->border.preset[BORDER_PRESET_NONE] == btn) {
		i = STYLE_BORDER_TOP;
		last = STYLE_BORDER_VERT;
		target_state = FALSE;
	} else if (state->border.preset[BORDER_PRESET_OUTLINE] == btn) {
		i = STYLE_BORDER_TOP;
		last = STYLE_BORDER_RIGHT;
		target_state = TRUE;
	} else if (state->border.preset[BORDER_PRESET_INSIDE] == btn) {
		i = STYLE_BORDER_HORIZ;
		last = STYLE_BORDER_VERT;
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
		else if (gtk_toggle_button_get_active (
				state->border.edge[i].button))
			/* Turn off damn it !
			 * we really want things off not just to pick up
			 * the new colours.
			 */
			gtk_toggle_button_set_active (
				state->border.edge[i].button,
				FALSE);
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
cb_border_color (G_GNUC_UNUSED ColorCombo *combo,
		 GdkColor *c,
		 G_GNUC_UNUSED gboolean is_custom,
		 G_GNUC_UNUSED gboolean by_user,
		 gboolean is_default, FormatState *state)
{
	state->border.rgba =
		GNOME_CANVAS_COLOR (c->red>>8, c->green>>8, c->blue>>8);
	state->border.is_auto_color = is_default;
}

#undef L
#undef R
#undef T
#undef B
#undef H
#undef V

typedef struct
{
	StyleBorderLocation     t;
	StyleBorder const *res;
} check_border_closure_t;

/*
 * Initialize the fields of a BorderPicker, connect signals and
 * hide if needed.
 */
static void
init_border_button (FormatState *state, StyleBorderLocation const i,
		    GtkWidget *button,
		    StyleBorder const * const border)
{
	if (border == NULL) {
		state->border.edge[i].rgba = 0;
		state->border.edge[i].is_auto_color = TRUE;
		state->border.edge[i].pattern_index = STYLE_BORDER_INCONSISTENT;
		state->border.edge[i].is_selected = TRUE;
	} else {
		StyleColor const * c = border->color;
		state->border.edge[i].rgba =
		    GNOME_CANVAS_COLOR (c->red>>8, c->green>>8, c->blue>>8);
		state->border.edge[i].is_auto_color = c->is_auto;
		state->border.edge[i].pattern_index = border->line_type;
		state->border.edge[i].is_selected = (border->line_type != STYLE_BORDER_NONE);
	}

	state->border.edge[i].state = state;
	state->border.edge[i].index = i;
	state->border.edge[i].button = GTK_TOGGLE_BUTTON (button);
	state->border.edge[i].is_set = FALSE;

	g_return_if_fail (button != NULL);

	gtk_toggle_button_set_active (state->border.edge[i].button,
				      state->border.edge[i].is_selected);

	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_border_toggle), &state->border.edge[i]);

	if ((i == STYLE_BORDER_HORIZ && !(state->selection_mask & 0xa)) ||
	    (i == STYLE_BORDER_VERT  && !(state->selection_mask & 0xc)))
		gtk_widget_hide (button);
}

/*****************************************************************************/

static void
cb_protection_locked_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		mstyle_set_content_locked (state->result,
			gtk_toggle_button_get_active (button));
		fmt_dialog_changed (state);
	}
}

static void
cb_protection_hidden_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		mstyle_set_content_hidden (state->result,
			gtk_toggle_button_get_active (button));
		fmt_dialog_changed (state);
	}
}

static void
cb_protection_sheet_protected_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		state->protection.sheet_protected_value =
			gtk_toggle_button_get_active (button);
		state->protection.sheet_protected_changed = TRUE;
		fmt_dialog_changed (state);
	}
}

static void
fmt_dialog_init_protection_page (FormatState *state)
{
	GtkWidget *w;
	gboolean flag = FALSE;

	flag = mstyle_is_element_conflict (state->style, MSTYLE_CONTENT_LOCKED)
		? FALSE : mstyle_get_content_locked (state->style);
	w = glade_xml_get_widget (state->gui, "protection_locked");
	state->protection.locked = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_locked_toggle), state);

	flag = mstyle_is_element_conflict (state->style, MSTYLE_CONTENT_HIDDEN)
		? FALSE : mstyle_get_content_hidden (state->style);
	w = glade_xml_get_widget (state->gui, "protection_hidden");
	state->protection.hidden = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_hidden_toggle), state);

	state->protection.sheet_protected_changed = FALSE;
	flag = wb_control_view (WORKBOOK_CONTROL (state->wbcg))->is_protected;
	w = glade_xml_get_widget (state->gui, "protection_sheet_protected");
	state->protection.sheet_protected = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_sheet_protected_toggle), state);
}

/*****************************************************************************/

static GnmExpr const *
validation_entry_to_expr (Sheet *sheet, GnumericExprEntry *gee)
{
	ParsePos pp;
	parse_pos_init (&pp, sheet->workbook, sheet, 0, 0);
	return gnm_expr_entry_parse (gee, &pp, NULL, FALSE);
}

static void
validation_rebuild_validation (FormatState *state)
{
	ValidationType	type;

	if (!state->enable_edit)
		return;

	state->validation.changed = FALSE;
	type = gtk_option_menu_get_history (
		state->validation.constraint_type);

	if (type != VALIDATION_TYPE_ANY) {
		ValidationStyle style = gtk_option_menu_get_history (state->validation.error.action);
		ValidationOp    op    = gtk_option_menu_get_history (state->validation.op);
		char *title = gtk_editable_get_chars (GTK_EDITABLE (state->validation.error.title), 0, -1);
		char *msg   = gnumeric_textview_get_text (state->validation.error.msg);
		GnmExpr const *expr0 = validation_entry_to_expr (state->sheet,
								 state->validation.expr0.entry);
		GnmExpr const *expr1 = NULL;



		if (expr0 != NULL) {
			if (op == VALIDATION_OP_BETWEEN || op == VALIDATION_OP_NOT_BETWEEN) {
				expr1 = validation_entry_to_expr (state->sheet, state->validation.expr1.entry);
				if (expr1 != NULL)
					state->validation.valid = 2;
				else {
					state->validation.valid = -2;
					gnm_expr_unref (expr0);
				}
			} else
				state->validation.valid = 1;
		} else
			state->validation.valid = -1;

		if (state->validation.valid > 0) {
			mstyle_set_validation (state->result,
					       validation_new (style, type, op, title, msg,
							       expr0, expr1,
							       gtk_toggle_button_get_active (state->validation.allow_blank),
							       gtk_toggle_button_get_active (state->validation.use_dropdown)));
		}

		g_free (msg);
		g_free (title);
	} else
		mstyle_set_validation (state->result, NULL);
	fmt_dialog_changed (state);
}

static void
cb_validation_error_action_deactivate (G_GNUC_UNUSED GtkMenuShell *ignored,
				       FormatState *state)
{
	int index = gtk_option_menu_get_history (state->validation.error.action);
	gboolean const flag = (index > 0) &&
		(gtk_option_menu_get_history (state->validation.constraint_type) > 0);

	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.title_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.msg_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.msg), flag);

	if (flag) {
		char const *s = NULL;

		switch (index) {
		case 1 : s = GTK_STOCK_DIALOG_ERROR;	break;
		case 2 : s = GTK_STOCK_DIALOG_WARNING;	break;
		case 3 : s = GTK_STOCK_DIALOG_INFO;	break;
		default :
			 g_warning ("Unknown validation style");
			 return;
		}

	     	if (s != NULL)
			gtk_image_set_from_stock (state->validation.error.image,
						  s, GTK_ICON_SIZE_MENU);
		gtk_widget_show (GTK_WIDGET (state->validation.error.image));
	} else
		gtk_widget_hide (GTK_WIDGET (state->validation.error.image));

	validation_rebuild_validation (state);
}

static void
cb_validation_sensitivity (G_GNUC_UNUSED GtkMenuShell *ignored,
			   FormatState *state)
{
	gboolean has_operators = FALSE;
	char const *msg0 = "";
	char const *msg1 = "";
	ValidationType const type = gtk_option_menu_get_history (
		state->validation.constraint_type);

	switch (type) {
	case VALIDATION_TYPE_IN_LIST :		msg0 = _("Source"); break;
	case VALIDATION_TYPE_CUSTOM :		msg0 = _("Criteria"); break;

	case VALIDATION_TYPE_AS_INT :
	case VALIDATION_TYPE_AS_NUMBER :
	case VALIDATION_TYPE_AS_DATE :
	case VALIDATION_TYPE_AS_TIME :
	case VALIDATION_TYPE_TEXT_LENGTH : {
		ValidationOp const op = gtk_option_menu_get_history (
			state->validation.op);
		has_operators = TRUE;
		switch (op) {
		case VALIDATION_OP_BETWEEN : case VALIDATION_OP_NOT_BETWEEN :
			msg0 = _("Min :");
			msg1 = _("Max :");
			break;
		case VALIDATION_OP_EQUAL : case VALIDATION_OP_NOT_EQUAL :
			msg0 = _("Value :");
			break;
		case VALIDATION_OP_GT : case VALIDATION_OP_GTE :
			msg0 =_("Min :");
			break;
		case VALIDATION_OP_LT : case VALIDATION_OP_LTE :
			msg0 = _("Max :");
			break;
		default :
			g_warning ("Unknown operator index");
		}
		break;
	}
	default :
		break;
	}

	gtk_label_set_text (state->validation.expr0.name, msg0);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.expr0.name),  *msg0 != '\0');
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.expr0.entry), *msg0 != '\0');

	gtk_label_set_text (state->validation.expr1.name, msg1);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.expr1.name),  *msg1 != '\0');
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.expr1.entry), *msg1 != '\0');

	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.op),
		has_operators);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.operator_label),
		has_operators);

	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.action_label),
		type != VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.action),
		type != VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.allow_blank),
		type != VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.use_dropdown),
		type == VALIDATION_TYPE_IN_LIST);

	validation_rebuild_validation (state);
}

static void
cb_validation_changed (G_GNUC_UNUSED GtkEntry *ignored,
		       FormatState *state)
{
	state->validation.changed = TRUE;
}

static void
fmt_dialog_init_validation_expr_entry (FormatState *state, ExprEntry *entry,
				       char const *name, int i)
{
	GnumericExprEntryFlags flags = GNM_EE_ABS_ROW | GNM_EE_ABS_COL | GNM_EE_SHEET_OPTIONAL;

	entry->name  = GTK_LABEL (glade_xml_get_widget (state->gui, name));
	entry->entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gtk_table_attach (state->validation.criteria_table,
		GTK_WIDGET (entry->entry),
		1, 3, 2+i, 3+i, GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (entry->entry));
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (entry->entry));
	gnm_expr_entry_set_scg (entry->entry, wbcg_cur_scg (state->wbcg));
	g_signal_connect (G_OBJECT (entry->entry),
		"changed",
		G_CALLBACK (cb_validation_changed), state);
	gnm_expr_entry_set_flags (entry->entry, flags, flags | GNM_EE_SINGLE_RANGE);
}

static void
cb_validation_rebuild (G_GNUC_UNUSED void *ignored,
		       FormatState *state)
{
	validation_rebuild_validation (state);
}

static void
fmt_dialog_init_validation_page (FormatState *state)
{
	Validation const *v = NULL;
	g_return_if_fail (state != NULL);

	/* Setup widgets */
	state->validation.changed	  = FALSE;
	state->validation.valid 	  = 1;
	state->validation.criteria_table  = GTK_TABLE          (glade_xml_get_widget (state->gui, "validation_criteria_table"));
	state->validation.constraint_type = GTK_OPTION_MENU    (glade_xml_get_widget (state->gui, "validation_constraint_type"));
	state->validation.operator_label  = GTK_LABEL          (glade_xml_get_widget (state->gui, "validation_operator_label"));
	state->validation.op        	     = GTK_OPTION_MENU    (glade_xml_get_widget (state->gui, "validation_operator"));
	state->validation.allow_blank	     = GTK_TOGGLE_BUTTON(glade_xml_get_widget (state->gui, "validation_ignore_blank"));
	state->validation.use_dropdown       = GTK_TOGGLE_BUTTON(glade_xml_get_widget (state->gui, "validation_in_dropdown"));
	state->validation.error.action_label = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_action_label"));
	state->validation.error.title_label  = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_title_label"));
	state->validation.error.msg_label    = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_msg_label"));
	state->validation.error.action       = GTK_OPTION_MENU (glade_xml_get_widget (state->gui, "validation_error_action"));
	state->validation.error.title        = GTK_ENTRY       (glade_xml_get_widget (state->gui, "validation_error_title"));
	state->validation.error.msg          = GTK_TEXT_VIEW   (glade_xml_get_widget (state->gui, "validation_error_msg"));
	state->validation.error.image        = GTK_IMAGE       (glade_xml_get_widget (state->gui, "validation_error_image"));

	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->validation.error.title));

	g_signal_connect (G_OBJECT (gtk_option_menu_get_menu (state->validation.constraint_type)),
		"deactivate",
		G_CALLBACK (cb_validation_sensitivity), state);
	g_signal_connect (G_OBJECT (gtk_option_menu_get_menu (state->validation.op)),
		"deactivate",
		G_CALLBACK (cb_validation_sensitivity), state);
	g_signal_connect (G_OBJECT (gtk_option_menu_get_menu (state->validation.error.action)),
		"deactivate",
		G_CALLBACK (cb_validation_error_action_deactivate), state);

	fmt_dialog_init_validation_expr_entry (state, &state->validation.expr0, "validation_expr0_name", 0);
	fmt_dialog_init_validation_expr_entry (state, &state->validation.expr1, "validation_expr1_name", 1);

	g_signal_connect (G_OBJECT (state->validation.allow_blank),
		"toggled",
		G_CALLBACK (cb_validation_rebuild), state);
	g_signal_connect (G_OBJECT (state->validation.use_dropdown),
		"toggled",
		G_CALLBACK (cb_validation_rebuild), state);
	g_signal_connect (G_OBJECT (state->validation.error.title),
		"changed",
		G_CALLBACK (cb_validation_rebuild), state);
	g_signal_connect (G_OBJECT (gtk_text_view_get_buffer (state->validation.error.msg)),
		"changed",
		G_CALLBACK (cb_validation_rebuild), state);

	/* Initialize */
	if (!mstyle_is_element_conflict (state->style, MSTYLE_VALIDATION))
		v = mstyle_get_validation (state->style);
	if (v != NULL) {
		Validation const *v = mstyle_get_validation (state->style);
		ParsePos pp;

		gtk_option_menu_set_history (state->validation.error.action, v->style);
		gtk_option_menu_set_history (state->validation.constraint_type, v->type);
		gtk_option_menu_set_history (state->validation.op, v->op);

		gtk_entry_set_text (GTK_ENTRY (state->validation.error.title),
			(v->title != NULL) ? v->title->str : "");
		if (v->msg != NULL)
			gnumeric_textview_set_text (GTK_TEXT_VIEW (state->validation.error.msg),
				v->msg->str);
		gtk_toggle_button_set_active (state->validation.allow_blank,  v->allow_blank);
		gtk_toggle_button_set_active (state->validation.use_dropdown, v->use_dropdown);

		parse_pos_init (&pp, state->sheet->workbook, state->sheet,
			state->sv->edit_pos.col, state->sv->edit_pos.row);
		gnm_expr_entry_load_from_expr (state->validation.expr0.entry,
			v->expr[0], &pp);
		gnm_expr_entry_load_from_expr (state->validation.expr1.entry,
			v->expr[1], &pp);
	}

	cb_validation_sensitivity (NULL, state);
	cb_validation_error_action_deactivate (NULL, state);
}

/*****************************************************************************/

static void
cb_input_msg_flag_toggled (GtkToggleButton *button, FormatState *state)
{
	gboolean flag = gtk_toggle_button_get_active (button);

	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.title_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.msg_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.msg), flag);
}

static void
fmt_dialog_init_input_msg_page (FormatState *state)
{
	g_return_if_fail (state != NULL);

	/*
	 * NOTE: This should be removed when the feature
	 * is implemented.
	 */
#if 1
	gtk_notebook_remove_page (state->notebook, 7);
	return;
#endif

	/* Setup widgets */
	state->input_msg.flag        = GTK_TOGGLE_BUTTON (glade_xml_get_widget (state->gui, "input_msg_flag"));
	state->input_msg.title_label = GTK_LABEL         (glade_xml_get_widget (state->gui, "input_msg_title_label"));
	state->input_msg.msg_label   = GTK_LABEL         (glade_xml_get_widget (state->gui, "input_msg_msg_label"));
	state->input_msg.title       = GTK_ENTRY         (glade_xml_get_widget (state->gui, "input_msg_title"));
	state->input_msg.msg         = GTK_TEXT_VIEW     (glade_xml_get_widget (state->gui, "input_msg_msg"));

	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->input_msg.title));
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->input_msg.msg));

	g_signal_connect (G_OBJECT (state->input_msg.flag),
		"toggled",
		G_CALLBACK (cb_input_msg_flag_toggled), state);

	/* Initialize */
	cb_input_msg_flag_toggled (state->input_msg.flag, state);
}

/*****************************************************************************/

/* button handlers */
static void
cb_fmt_dialog_dialog_buttons (GtkWidget *btn, FormatState *state)
{
	if (btn == state->apply_button || btn == state->ok_button) {
		StyleBorder *borders[STYLE_BORDER_EDGE_MAX];
		int i;

		if (state->validation.changed)
			validation_rebuild_validation (state);

		if (state->validation.valid < 0) {
			if (gnumeric_dialog_question_yes_no (state->wbcg,
							     _ ("The validation criteria are unusable. Disable validation?"), FALSE))
			{
				gtk_option_menu_set_history (state->validation.constraint_type, 0);
				cb_validation_sensitivity (NULL, state);
			} else {
				gtk_notebook_set_page (state->notebook, FD_VALIDATION);

				if (state->validation.valid == -1)
					gnm_expr_entry_grab_focus (state->validation.expr0.entry, FALSE);
				else
					gnm_expr_entry_grab_focus (state->validation.expr1.entry, FALSE);
				return;
			}
		}

		if (state->protection.sheet_protected_changed) {
			WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (state->wbcg));
			wbv->is_protected = state->protection.sheet_protected_value;
			wb_view_prefs_update (wbv);
			state->protection.sheet_protected_changed = FALSE;
		}

		mstyle_ref (state->result);

		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			borders[i] = border_get_mstyle (state, i);

		cmd_selection_format (WORKBOOK_CONTROL (state->wbcg),
			    state->result, borders, NULL);

		mstyle_unref (state->result);
		sheet_update (state->sheet);

		/* Get a fresh style to accumulate results in */
		state->result = mstyle_new ();

		gtk_widget_set_sensitive (state->apply_button, FALSE);
	}

	if (btn != state->apply_button)
		gtk_object_destroy (GTK_OBJECT (state->dialog));
}

/* Handler for destroy */
static void
cb_fmt_dialog_dialog_destroy (FormatState *state)
{
	wbcg_edit_detach_guru (state->wbcg);
	style_format_unref (state->format.spec);
	mstyle_unref (state->back.style);
	mstyle_unref (state->style);
	mstyle_unref (state->result);
	g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

/* Handler for expr-entry's focus.
 *
 * NOTE: This will only become useful once the
 *       cell format dialog is made non-modal
 */
static void
cb_fmt_dialog_set_focus (G_GNUC_UNUSED GtkWidget *window,
			 G_GNUC_UNUSED GtkWidget *focus_widget,
			 FormatState *state)
{
	if (state->validation.changed)
		validation_rebuild_validation (state);
}

/* Set initial focus */
static void
set_initial_focus (FormatState *s)
{
	GtkWidget *focus_widget = NULL, *pagew;
	gchar const *name;

	pagew = gtk_notebook_get_nth_page (s->notebook, fmt_dialog_page);
	name = gtk_widget_get_name (pagew);

	if (strcmp (name, "number_box") == 0)
		focus_widget = glade_xml_get_widget (s->gui, "format_general");
	else if (strcmp (name, "alignment_box") == 0)
	      focus_widget = glade_xml_get_widget (s->gui, "halign_left");
	else if (strcmp (name, "font_box") == 0)
	      focus_widget = GTK_WIDGET (s->font.selector->font_size_entry);
	else if (strcmp (name, "border_box") == 0)
	      focus_widget = glade_xml_get_widget (s->gui, "outline_border");
	else if (strcmp (name, "background_box") == 0)
	      focus_widget = glade_xml_get_widget (s->gui, "back_color_auto");
	else if (strcmp (name, "protection_box") == 0)
	      focus_widget = GTK_WIDGET (s->protection.locked);
	else
		focus_widget = NULL;

	if (focus_widget
	    && GTK_WIDGET_CAN_FOCUS (focus_widget)
	    && GTK_WIDGET_IS_SENSITIVE (focus_widget))
		gtk_widget_grab_focus (focus_widget);
}

static void
fmt_dialog_impl (FormatState *state, FormatDialogPosition_t pageno)
{
	static struct {
		char const * const name;
		StyleBorderType const pattern;
	} const line_pattern_buttons[] = {
		{ "line_pattern_none", STYLE_BORDER_NONE },
		{ "line_pattern_medium_dash_dot_dot", STYLE_BORDER_MEDIUM_DASH_DOT_DOT },

		{ "line_pattern_hair", STYLE_BORDER_HAIR },
		{ "line_pattern_slant", STYLE_BORDER_SLANTED_DASH_DOT },

		{ "line_pattern_dotted", STYLE_BORDER_DOTTED },
		{ "line_pattern_medium_dash_dot", STYLE_BORDER_MEDIUM_DASH_DOT },

		{ "line_pattern_dash_dot_dot", STYLE_BORDER_DASH_DOT_DOT },
		{ "line_pattern_medium_dash", STYLE_BORDER_MEDIUM_DASH },

		{ "line_pattern_dash_dot", STYLE_BORDER_DASH_DOT },
		{ "line_pattern_medium", STYLE_BORDER_MEDIUM },

		{ "line_pattern_dashed", STYLE_BORDER_DASHED },
		{ "line_pattern_thick", STYLE_BORDER_THICK },

		{ "line_pattern_thin", STYLE_BORDER_THIN },
		{ "line_pattern_double", STYLE_BORDER_DOUBLE },

		{ NULL }
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

		"gp_small_circle",
		"gp_semi_circle",
		"gp_thatch",
		"gp_large_circles",
		"gp_bricks",
		"gp_foreground_solid",

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

	int page_signal;
	int i, selected;
	char const *name;
	gboolean has_back;
	GdkColor *default_border_color = &gs_black;
	int default_border_style = STYLE_BORDER_THIN;

	GtkWidget *tmp, *dialog = glade_xml_get_widget (state->gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Format Cells"));

	/* Initialize */
	state->dialog	   = GTK_DIALOG (dialog);
	state->notebook	   = GTK_NOTEBOOK (glade_xml_get_widget (state->gui, "notebook"));
	state->enable_edit = FALSE;  /* Enable below */

	state->border.canvas	= NULL;
	state->border.pattern.cur_index	= 0;

	state->back.canvas	= NULL;
	state->back.grid        = NULL;
	state->back.style             = mstyle_new_default ();
	state->back.pattern.cur_index = 0;

	if (pageno == FD_CURRENT)
		pageno = fmt_dialog_page;
	gtk_notebook_set_page (state->notebook, pageno);

	page_signal = g_signal_connect (G_OBJECT (state->notebook),
		"switch_page",
		G_CALLBACK (cb_page_select), NULL);
	g_signal_connect (G_OBJECT (state->notebook),
		"destroy",
		G_CALLBACK (cb_notebook_destroy), GINT_TO_POINTER (page_signal));

	fmt_dialog_init_format_page (state);
	fmt_dialog_init_align_page (state);
	fmt_dialog_init_font_page (state);
	fmt_dialog_init_background_page (state);
	fmt_dialog_init_protection_page (state);
	fmt_dialog_init_validation_page (state);
	fmt_dialog_init_input_msg_page (state);

	/* Setup border line pattern buttons & select the 1st button */
	for (i = MSTYLE_BORDER_TOP; i < MSTYLE_BORDER_DIAGONAL; i++) {
		StyleBorder const *border = mstyle_get_border (state->style, i);
		if (!style_border_is_blank (border)) {
			default_border_color = &border->color->color;
			default_border_style = border->line_type;
			break;
		}
	}

	state->border.pattern.draw_preview = NULL;
	state->border.pattern.current_pattern = NULL;
	state->border.pattern.state = state;
	state->border.rgba = GNOME_CANVAS_COLOR (
		default_border_color->red   >> 8,
		default_border_color->green >> 8,
		default_border_color->blue  >> 8);
	for (i = 0; (name = line_pattern_buttons[i].name) != NULL; ++i)
		setup_pattern_button (state->gui, name, &state->border.pattern,
			i != 0, /* No image for None */
			line_pattern_buttons[i].pattern,
			default_border_style);

	setup_color_pickers (&state->border.color, "border_color_group",
			     "border_color_hbox",
			     _("Automatic"), _("Border"), state,
			     G_CALLBACK (cb_border_color),
			     MSTYLE_ELEMENT_UNSET, state->style);
	setup_color_pickers (&state->font.color, "fore_color_group",
			     "font_color_hbox",
			     _("Automatic"), _("Foreground"), state,
			     G_CALLBACK (cb_font_preview_color),
			     MSTYLE_COLOR_FORE, state->style);
	setup_color_pickers (&state->back.back_color, "back_color_group",
			     "back_color_hbox",
			     _("Clear Background"), _("Background"), state,
			     G_CALLBACK (cb_back_preview_color),
			     MSTYLE_COLOR_BACK, state->style);
	setup_color_pickers (&state->back.pattern_color, "pattern_color_group",
			     "pattern_color_hbox",
			     _("Automatic"), _("Pattern"), state,
			     G_CALLBACK (cb_pattern_preview_color),
			     MSTYLE_COLOR_PATTERN, state->style);

	/* Setup the border images */
	for (i = 0; (name = border_buttons[i]) != NULL; ++i) {
		tmp = init_button_image (state->gui, name);
		if (tmp != NULL) {
			init_border_button (state, i, tmp,
					    state->borders[i]);
			style_border_unref (state->borders[i]);
		}
	}

	/* Get the current background
	 * A pattern of 0 is has no background.
	 * A pattern of 1 is a solid background
	 * All others have 2 colours and a stipple
	 */
	has_back = FALSE;
	selected = 1;
	if (!mstyle_is_element_conflict (state->style, MSTYLE_PATTERN)) {
		selected = mstyle_get_pattern (state->style);
		has_back = (selected != 0);
	}

	/* Setup pattern buttons & select the current pattern (or the 1st
	 * if none is selected)
	 * NOTE : This must be done AFTER the colour has been setup to
	 * avoid having it erased by initialization.
	 */
	state->back.pattern.draw_preview = &draw_pattern_selected;
	state->back.pattern.current_pattern = NULL;
	state->back.pattern.state = state;
	for (i = 0; (name = pattern_buttons[i]) != NULL; ++i)
		setup_pattern_button (state->gui, name,
				      &state->back.pattern, TRUE,
				      i+1, /* Pattern #s start at 1 */
				      selected);

	/* If the pattern is 0 indicating no background colour
	 * Set background to No colour.  This will set states correctly.
	 */
	if (!has_back)
		color_combo_set_color_to_default (COLOR_COMBO (state->back.back_color.combo));

	/* Setup the images in the border presets */
	for (i = 0; (name = border_preset_buttons[i]) != NULL; ++i) {
		tmp = init_button_image (state->gui, name);
		if (tmp != NULL) {
			state->border.preset[i] = GTK_BUTTON (tmp);
			g_signal_connect (G_OBJECT (tmp),
				"clicked",
				G_CALLBACK (cb_border_preset_clicked), state);
			if (i == BORDER_PRESET_INSIDE && state->selection_mask != 0x8)
				gtk_widget_hide (tmp);
		}
	}

	draw_border_preview (state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "helpbutton"),
		"formatting.html");

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	state->apply_button = glade_xml_get_widget (state->gui, "applybutton");
	gtk_widget_set_sensitive (state->apply_button, FALSE);
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	tmp = glade_xml_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (tmp),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);

	set_initial_focus (state);
	gtk_notebook_set_scrollable (state->notebook, TRUE);

	/* Ok, edit events from now on are real */
	state->enable_edit = TRUE;

	g_signal_connect (G_OBJECT (dialog),
		"set-focus",
		G_CALLBACK (cb_fmt_dialog_set_focus), state);
	/* We could now make it modeless, and arguably should do so. We must
	 * then track the selection: styles should be applied to the current
	 * selection.
	 * There are some UI issues to discuss before we do this, though. Most
	 * important:
	 * - will users be confused?
	 * And on a different level:
	 * - should the preselected style in the dialog change when another
	 *   cell is selected? May be, but then we can't first make a style,
	 *   then move around and apply it to different cells.
	 */

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_fmt_dialog_dialog_destroy);
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));
}

static gboolean
fmt_dialog_selection_type (SheetView *sv,
			   Range const *range,
			   gpointer user_data)
{
	FormatState *state = user_data;
	GSList *merged = sheet_merge_get_overlap (sv->sheet, range);
	gboolean allow_multi =
		merged == NULL ||
		merged->next != NULL ||
		!range_equal ((Range *)merged->data, range);
	g_slist_free (merged);

	if (allow_multi) {
		if (range->start.col != range->end.col)
			state->selection_mask |= 2;
		if (range->start.row != range->end.row)
			state->selection_mask |= 1;
	}

	sheet_style_get_uniform (state->sheet, range,
				 &(state->style), state->borders);
	return TRUE;
}

void
dialog_cell_format (WorkbookControlGUI *wbcg, FormatDialogPosition_t pageno)
{
	GladeXML     *gui;
	Cell	     *edit_cell;
	FormatState  *state;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "cell-format.glade");
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (FormatState, 1);
	state->wbcg	= wbcg;
	state->gui	= gui;
	state->sv	= wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet	= sv_sheet (state->sv);

	edit_cell = sheet_cell_get (state->sheet,
				    state->sv->edit_pos.col,
				    state->sv->edit_pos.row);

	state->value	        = (edit_cell != NULL) ? edit_cell->value : NULL;
	state->style		= NULL;
	state->result		= mstyle_new ();
	state->selection_mask	= 0;
	state->dialog_changed	= NULL;
	state->dialog_changed_user_data = NULL;

	(void) selection_foreach_range (state->sv, TRUE,
					fmt_dialog_selection_type,
					state);
	state->selection_mask	= 1 << state->selection_mask;

	fmt_dialog_impl (state, pageno);
}

#if 0
GtkWidget *
dialog_cell_number_fmt (WorkbookControlGUI *wbcg, Value *sample_val)
{
	GladeXML     *gui;
	FormatState  *state;
	GtkWidget    *res;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	gui = gnumeric_glade_xml_new (wbcg, "cell-format.glade");
        if (gui == NULL)
                return NULL;

	/* Initialize */
	state = g_new (FormatState, 1);
	state->wbcg		= wbcg;
	state->gui		= NULL;
	state->sheet		= NULL;
	state->value		= sample_val;
	state->style		= mstyle_new (); /* FIXME : this should be passed in */
	state->result		= mstyle_new ();
	state->selection_mask	= 0;
	state->dialog_changed	= NULL;		/* FIXME : These should be passed in */
	state->dialog_changed_user_data = NULL;

	fmt_dialog_init_format_page (state);

	res = glade_xml_get_widget (state->gui, "number_box");
	g_signal_connect (G_OBJECT (res),
		"destroy",
		G_CALLBACK (cb_fmt_dialog_dialog_destroy), state);

	return res;
}
#endif

/*
 * TODO
 *
 * Borders
 * 	- Add the 'text' elements in the preview
 *
 * Wishlist
 * 	- Some undo capabilities in the dialog.
 * 	- How to distinguish between auto & custom colors on extraction from styles.
 */
