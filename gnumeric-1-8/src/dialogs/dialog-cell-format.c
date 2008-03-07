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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <style-color.h>
#include <gui-util.h>
#include <selection.h>
#include <str.h>
#include <ranges.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <gnm-format.h>
#include <pattern.h>
#include <position.h>
#include <mstyle.h>
#include <application.h>
#include <validation.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <commands.h>
#include <mathfunc.h>
#include <preview-grid.h>
#include <widgets/gnumeric-expr-entry.h>
#include <widgets/widget-font-selector.h>
#include <widgets/gnumeric-dashed-canvas-line.h>
#include <widgets/gnm-format-sel.h>
#include <goffice/gtk/go-combo-color.h>
#include <goffice/gtk/go-combo-box.h>
#include <goffice/gtk/go-combo-text.h>
#include <goffice/gtk/go-rotation-sel.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-util.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <glade/glade.h>
#include <gtk/gtklabel.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkcelllayout.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktable.h>
#include <gtk/gtkicontheme.h>
#include <gtk/gtkbox.h>

#include <math.h>
#include <string.h>

static struct {
	char const *Cname;
	GnmUnderline ut;
} const underline_types[] = {
	{ N_("None"), UNDERLINE_NONE },
	{ N_("Single"), UNDERLINE_SINGLE },
	{ N_("Double"), UNDERLINE_DOUBLE }
};

/* The order corresponds to border_preset_buttons */
typedef enum
{
	BORDER_PRESET_NONE,
	BORDER_PRESET_OUTLINE,
	BORDER_PRESET_INSIDE,

	BORDER_PRESET_MAX
} BorderPresets;

struct _FormatState;

typedef struct {
	struct _FormatState *state;
	int cur_index;
	GtkToggleButton *current_pattern;
	GtkToggleButton *default_button;
	void (*draw_preview) (struct _FormatState *);
} PatternPicker;

typedef struct {
	struct _FormatState *state;

	GtkWidget        *combo;
	GCallback	  preview_update;
} ColorPicker;

typedef struct {
	struct _FormatState *state;
	GtkToggleButton  *button;
	GnmStyleBorderType	  pattern_index;
	gboolean	  is_selected;	/* Is it selected */
	GnmStyleBorderLocation   index;
	guint		  rgba;
	gboolean          is_auto_color;
	gboolean	  is_set;	/* Has the element been changed */
} BorderPicker;

typedef struct {
	GtkLabel	*name;
	GnmExprEntry	*entry;
} ExprEntry;

typedef struct _FormatState {
	GladeXML	*gui;
	WBCGtk	*wbcg;
	GtkDialog	*dialog;
	GtkNotebook	*notebook;
	GtkWidget	*apply_button;
	GtkWidget	*ok_button;

	Sheet		*sheet;
	SheetView	*sv;
	GnmValue	*value;
	unsigned int	 conflicts;
	GnmStyle	*style, *result;
	GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX];

	int		 selection_mask;
	gboolean	 enable_edit;

	GtkWidget *	format_sel;

	struct {
		GtkCheckButton	*wrap;
		GtkSpinButton	*indent_button;
		GtkWidget	*indent_label;
		int		 indent;
		GORotationSel	*rotation;
	} align;
	struct {
		FontSelector	*selector;
		GtkToggleButton	*superscript, *subscript;
		ColorPicker      color;
	} font;
	struct {
		FooCanvas	*canvas;
		GtkButton	*preset[BORDER_PRESET_MAX];
		FooCanvasItem	*back;
		FooCanvasItem *lines[20];

		BorderPicker	 edge[GNM_STYLE_BORDER_EDGE_MAX];
		ColorPicker      color;
		guint		 rgba;
		gboolean         is_auto_color;
		PatternPicker	 pattern;
	} border;
	struct {
		FooCanvas	*canvas;
		PreviewGrid     *grid;
		GnmStyle        *style;

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
		GtkComboBox  *constraint_type;
		GtkLabel       *operator_label;
		GtkComboBox  *op;
		ExprEntry	expr0, expr1;
		GtkToggleButton *allow_blank;
		GtkToggleButton *use_dropdown;

		struct {
			GtkLabel      *action_label;
			GtkLabel      *title_label;
			GtkLabel      *msg_label;
			GtkComboBox *action;
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
setup_pattern_button (GdkScreen *screen,
		      GladeXML  *gui,
		      char const *const name,
		      PatternPicker *picker,
		      gboolean const flag,
		      int const index,
		      int const select_index,
		      unsigned size)
{
	GtkWidget *tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkButton *button = GTK_BUTTON (tmp);
		if (flag) {
			GdkPixbuf *pixbuf = gtk_icon_theme_load_icon (
				gtk_icon_theme_get_for_screen (screen),
				name, size, 0, NULL);
			GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);
			g_object_unref (pixbuf);
			gtk_widget_show (image);
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
		g_warning ("CellFormat: Unexpected missing glade widget");
}

static void
setup_color_pickers (FormatState *state,
		     ColorPicker *picker,
		     char const *color_group,
		     char const *container,
		     char const *label,
		     char const *default_caption,
		     char const *caption,
		     GCallback preview_update,
		     GnmStyleElement e)
{
	GtkWidget *combo, *w, *frame;
	GOColorGroup *cg;
	GnmColor *mcolor = NULL;
	GnmColor *def_sc = NULL;

	switch (e) {
	case MSTYLE_COLOR_PATTERN:
		if (0 == (state->conflicts & (1 << MSTYLE_COLOR_PATTERN)))
			mcolor = gnm_style_get_pattern_color (state->style);

	case MSTYLE_BORDER_TOP:	/* MSTYLE_BORDER_TOP is abused as representing all borders. */
		def_sc = sheet_style_get_auto_pattern_color (state->sheet);
		break;
	case MSTYLE_FONT_COLOR:
		if (0 == (state->conflicts & (1 << MSTYLE_FONT_COLOR)))
			mcolor = gnm_style_get_font_color (state->style);
		def_sc = style_color_auto_font ();
		break;
	case MSTYLE_COLOR_BACK:
		if (0 == (state->conflicts & (1 << MSTYLE_COLOR_BACK)))
			mcolor = gnm_style_get_back_color (state->style);
		def_sc = style_color_auto_back ();
		break;
	default:
		g_warning ("Unhandled style element!");
	}
	cg = go_color_group_fetch (color_group,
		 wb_control_view (WORKBOOK_CONTROL (state->wbcg)));
	combo = go_combo_color_new (NULL, default_caption, 
		def_sc ? GDK_TO_UINT (def_sc->gdk_color) : RGBA_BLACK, cg);
	go_combo_box_set_title (GO_COMBO_BOX (combo), caption);

	/* Connect to the sample canvas and redraw it */
	g_signal_connect (G_OBJECT (combo),
		"color_changed",
		G_CALLBACK (preview_update), state);

	go_combo_color_set_color_gdk (GO_COMBO_COLOR (combo),
		(mcolor && !mcolor->is_auto) ? &mcolor->gdk_color : NULL);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	gtk_container_add (GTK_CONTAINER (frame), combo);

	w = glade_xml_get_widget (state->gui, container);
	gtk_box_pack_start (GTK_BOX (w), frame, FALSE, FALSE, 0);
	gtk_widget_show_all (frame);

	w = glade_xml_get_widget (state->gui, label);
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), combo);

	if (def_sc)
		style_color_unref (def_sc);

	if (picker != NULL) {
		picker->combo          = combo;
		picker->preview_update = preview_update;
	}
}

/*
 * Utility routine to load an image and insert it into a
 * button of the same name.
 */
static GtkWidget *
init_button_image (GladeXML *gui, char const *name)
{
	GtkWidget *tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GdkScreen *screen = gtk_widget_get_screen (tmp);
		GdkPixbuf *pixbuf = gtk_icon_theme_load_icon (
			gtk_icon_theme_get_for_screen (screen),
			name, 16, 0, NULL);
		GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);
		g_object_unref (pixbuf);
		gtk_widget_show (image);
		gtk_container_add (GTK_CONTAINER (tmp), image);
	}

	return tmp;
}

/*****************************************************************************/

static void
cb_number_format_changed (G_GNUC_UNUSED GtkWidget *widget,
			  const char *fmt,
			  FormatState *state)
{
	gboolean changed = FALSE;
	g_return_if_fail (state != NULL);

	if (!state->enable_edit)
		return;

	if (fmt) {
		GOFormat *format = go_format_new_from_XL (fmt);
		gnm_style_set_format (state->result, format);
		go_format_unref (format);
		changed =  TRUE;
	}

	if (changed)
		fmt_dialog_changed (state);
}

static void
fmt_dialog_init_format_page (FormatState *state)
{
	GOFormatSel *gfs;

	state->format_sel = gnm_format_sel_new ();
	gfs = GO_FORMAT_SEL (state->format_sel);

	gtk_notebook_prepend_page (GTK_NOTEBOOK (state->notebook),
				   state->format_sel,
				   gtk_label_new (_("Number")));
	gtk_widget_show (GTK_WIDGET (gfs));

	if (0 == (state->conflicts & (1 << MSTYLE_FORMAT)))
		go_format_sel_set_style_format (gfs,
			gnm_style_get_format (state->style));
	if (state->value)
		gnm_format_sel_set_value (gfs, state->value);
	go_format_sel_set_dateconv (gfs,
		workbook_date_conv (state->sheet->workbook));
	go_format_sel_editable_enters (gfs, GTK_WINDOW (state->dialog));

	g_signal_connect (G_OBJECT (state->format_sel), "format_changed",
		G_CALLBACK (cb_number_format_changed), state);
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
			gnm_style_set_indent (state->result, val);
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
		GnmHAlign const new_h =
			GPOINTER_TO_INT (g_object_get_data (
				G_OBJECT (button), "align"));
		gboolean const supports_indent =
			(new_h == HALIGN_LEFT || new_h == HALIGN_RIGHT);
		gnm_style_set_align_h (state->result, new_h);
		gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_button),
					  supports_indent);
		gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_label),
					  supports_indent);
		/* TODO: Should we 0 the indent ? */
		fmt_dialog_changed (state);
	}
}

static void
cb_align_v_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	if (state->enable_edit) {
		gnm_style_set_align_v (
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
		gnm_style_set_wrap_text (state->result,
				      gtk_toggle_button_get_active (button));
		fmt_dialog_changed (state);
	}
}

static void
fmt_dialog_init_align_radio (char const *const name,
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
cb_rotation_changed (GORotationSel *grs, int angle, FormatState *state)
{
	if (angle < 0)
		angle += 360;
	gnm_style_set_rotation (state->result, angle);
	fmt_dialog_changed (state);
}

static void
fmt_dialog_init_align_page (FormatState *state)
{
	static struct {
		char const *const	name;
		GnmHAlign	align;
	} const h_buttons[] = {
		{ "halign_left",	HALIGN_LEFT },
		{ "halign_center",	HALIGN_CENTER },
		{ "halign_right",	HALIGN_RIGHT },
		{ "halign_general",	HALIGN_GENERAL },
		{ "halign_justify",	HALIGN_JUSTIFY },
		{ "halign_fill",	HALIGN_FILL },
		{ "halign_center_across_selection",	HALIGN_CENTER_ACROSS_SELECTION },
		{ "halign_distributed",			HALIGN_DISTRIBUTED },
		{ NULL }
	};
	static struct {
		char const *const	name;
		GnmVAlign	align;
	} const v_buttons[] = {
		{ "valign_top", VALIGN_TOP },
		{ "valign_center", VALIGN_CENTER },
		{ "valign_bottom", VALIGN_BOTTOM },
		{ "valign_justify", VALIGN_JUSTIFY },
		{ "valign_distributed", VALIGN_DISTRIBUTED },
		{ NULL }
	};

	GtkWidget *w;
	gboolean wrap = FALSE;
	GnmHAlign    h = HALIGN_GENERAL;
	GnmVAlign    v = VALIGN_CENTER;
	char const *name;
	int i, r;

	if (0 == (state->conflicts & (1 << MSTYLE_ALIGN_H)))
		h = gnm_style_get_align_h (state->style);
	if (0 == (state->conflicts & (1 << MSTYLE_ALIGN_V)))
		v = gnm_style_get_align_v (state->style);

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
	if (0 == (state->conflicts & (1 << MSTYLE_WRAP_TEXT)))
		wrap = gnm_style_get_wrap_text (state->style);

	w = glade_xml_get_widget (state->gui, "align_wrap");
	state->align.wrap = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), wrap);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_align_wrap_toggle), state);

	if (0 == (state->conflicts & (1 << MSTYLE_INDENT)) ||
	    (h != HALIGN_LEFT && h != HALIGN_RIGHT))
		state->align.indent = 0;
	else
		state->align.indent = gnm_style_get_indent (state->style);

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
		"value-changed",
		G_CALLBACK (cb_indent_changed), state);

	/* Catch <return> in the spin box */
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (w));

	/* setup the rotation canvas */
	if (0 == (state->conflicts & (1 << MSTYLE_ROTATION))) {
		r = gnm_style_get_rotation (state->style);
		if (r > 180)
			r -= 360;
	} else
		r = 0;
	state->align.rotation = (GORotationSel *)
		glade_xml_get_widget (state->gui, "rotation_selector");
	go_rotation_sel_set_rotation (state->align.rotation, r);
	g_signal_connect (G_OBJECT (state->align.rotation), "rotation-changed",
		G_CALLBACK (cb_rotation_changed), state);
}

/*****************************************************************************/

static void
cb_font_changed (G_GNUC_UNUSED GtkWidget *widget,
		 GnmStyle *style, FormatState *state)
{
	static GnmStyleElement const font_types[] = {
		MSTYLE_FONT_NAME,
		MSTYLE_FONT_SIZE,
		MSTYLE_FONT_BOLD,
		MSTYLE_FONT_ITALIC,
		MSTYLE_FONT_UNDERLINE,
		MSTYLE_FONT_STRIKETHROUGH,
		MSTYLE_FONT_SCRIPT,
		MSTYLE_FONT_COLOR
	};
	int i;
	static int const num_font_types = G_N_ELEMENTS (font_types);

	gboolean changed = FALSE;
	g_return_if_fail (state != NULL);

	if (!state->enable_edit)
		return;

	for (i = 0 ; i < num_font_types; i++) {
		GnmStyleElement const t = font_types[i];
		if (gnm_style_is_element_set (style, t)) {
			gnm_style_merge_element (state->result, style, t);
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
cb_font_preview_color (G_GNUC_UNUSED GOComboColor *combo,
		       GOColor c,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       gboolean is_default, FormatState *state)
{
	GnmColor *col;

	if (!state->enable_edit)
		return;

	col = is_default
	       ? style_color_auto_font ()
	       : style_color_new_go (c);
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
static void
cb_font_script_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		GOFontScript script = GO_FONT_SCRIPT_STANDARD;
		if (gtk_toggle_button_get_active (button)) {
			state->enable_edit = FALSE;
			if (button == state->font.superscript) {
				script = GO_FONT_SCRIPT_SUPER;
				gtk_toggle_button_set_active (state->font.subscript, FALSE);
			} else {
				script = GO_FONT_SCRIPT_SUB;
				gtk_toggle_button_set_active (state->font.superscript, FALSE);
			}
			state->enable_edit = TRUE;
		}
		font_selector_set_script (state->font.selector, script);
	}
}

static gboolean
cb_font_underline_changed (G_GNUC_UNUSED GtkWidget *ct,
			   char *new_text, FormatState *state)
{
	GnmUnderline res = UNDERLINE_NONE;
	int i;

	/* ignore the clear while assigning a new value */
	if (!state->enable_edit || new_text == NULL || *new_text == '\0')
		return FALSE;

	for (i = G_N_ELEMENTS (underline_types); i-- > 0; )
		if (go_utf8_collate_casefold (new_text, _(underline_types[i].Cname)) == 0) {
			res = underline_types[i].ut;
			break;
		}

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
	GOFontScript script = GO_FONT_SCRIPT_STANDARD;
	int i;

	g_return_if_fail (container != NULL);
	g_return_if_fail (uline != NULL);
	g_return_if_fail (strike != NULL);

	/* TODO: How to insert the font box in the right place initially */
	gtk_widget_show (tmp);
	gtk_box_pack_start (GTK_BOX (container), tmp, TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (container), tmp, 0);

	font_selector_editable_enters (font_widget, GTK_WINDOW (state->dialog));

	state->font.selector = FONT_SELECTOR (font_widget);

	font_selector_set_value (state->font.selector, state->value);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_NAME)))
		font_selector_set_name (state->font.selector,
					gnm_style_get_font_name (state->style));

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_BOLD)) &&
	    0 == (state->conflicts & (1 << MSTYLE_FONT_ITALIC)))
		font_selector_set_style (state->font.selector,
					 gnm_style_get_font_bold (state->style),
					 gnm_style_get_font_italic (state->style));
	if (0 == (state->conflicts & (1 << MSTYLE_FONT_SIZE)))
		font_selector_set_points (state->font.selector,
					  gnm_style_get_font_size (state->style));

	for (i = 0; i < (int)G_N_ELEMENTS (underline_types); i++)
		go_combo_text_add_item	(GO_COMBO_TEXT (uline), _(underline_types[i].Cname));
	if (0 == (state->conflicts & (1 << MSTYLE_FONT_UNDERLINE))) {
		GnmUnderline ut = gnm_style_get_font_uline (state->style);
		uline_str = _(underline_types[ut].Cname);
		font_selector_set_underline (state->font.selector, ut);
	} else
		uline_str = "";
	go_combo_text_set_text	(GO_COMBO_TEXT (uline), uline_str,
		GO_COMBO_TEXT_FROM_TOP);
	g_signal_connect (G_OBJECT (uline),
		"entry_changed",
		G_CALLBACK (cb_font_underline_changed), state);
	gtk_widget_show_all (uline);

	tmp = glade_xml_get_widget (state->gui, "underline_label");
	gtk_label_set_mnemonic_widget (GTK_LABEL (tmp), uline);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_STRIKETHROUGH)))
		strikethrough = gnm_style_get_font_strike (state->style);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (strike), strikethrough);
	font_selector_set_strike (state->font.selector, strikethrough);
	g_signal_connect (G_OBJECT (strike),
		"toggled",
		G_CALLBACK (cb_font_strike_toggle), state);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_SCRIPT)))
		script = gnm_style_get_font_script (state->style);
	font_selector_set_script (state->font.selector, script);
	if (NULL != (tmp = glade_xml_get_widget (state->gui, "superscript_button"))) {
		state->font.superscript = GTK_TOGGLE_BUTTON (tmp);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp),
			script == GO_FONT_SCRIPT_SUPER);
		g_signal_connect (G_OBJECT (tmp), "toggled",
			G_CALLBACK (cb_font_script_toggle), state);
	}
	if (NULL != (tmp =  glade_xml_get_widget (state->gui, "subscript_button"))) {
		state->font.subscript = GTK_TOGGLE_BUTTON (tmp);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp),
			script == GO_FONT_SCRIPT_SUB);
		g_signal_connect (G_OBJECT (tmp), "toggled",
			G_CALLBACK (cb_font_script_toggle), state);
	}

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_COLOR)))
		font_selector_set_color (
			state->font.selector,
			style_color_ref (gnm_style_get_font_color (state->style)));

	g_signal_connect (G_OBJECT (font_widget),
		"font_changed",
		G_CALLBACK (cb_font_changed), state);
}

/*****************************************************************************/

static void
back_style_changed (FormatState *state)
{
	g_return_if_fail (state->back.style != NULL);

	fmt_dialog_changed (state);

	if (state->enable_edit) {
		gnm_style_merge_element (state->result, state->back.style, MSTYLE_PATTERN);
		gnm_style_merge_element (state->result, state->back.style, MSTYLE_COLOR_BACK);
		gnm_style_merge_element (state->result, state->back.style, MSTYLE_COLOR_PATTERN);
		foo_canvas_item_set (FOO_CANVAS_ITEM (state->back.grid),
			"default-style",	state->back.style,
			NULL);
	}
}

static void
cb_back_preview_color (G_GNUC_UNUSED GOComboColor *combo,
		       GOColor c,
		       G_GNUC_UNUSED gboolean is_custom,
		       G_GNUC_UNUSED gboolean by_user,
		       gboolean is_default,
		       FormatState *state)
{
	GnmColor *sc;

	g_return_if_fail (c);

	if (is_default) {
		sc = style_color_auto_back ();
		gnm_style_set_pattern (state->back.style, 0);
	} else {
		sc = style_color_new_go (c);
		gnm_style_set_pattern (state->back.style, state->back.pattern.cur_index);
	}

	gnm_style_set_back_color (state->back.style, sc);
	back_style_changed (state);
}

static void
cb_pattern_preview_color (G_GNUC_UNUSED GOComboColor *combo,
			  GOColor c,
			  G_GNUC_UNUSED gboolean is_custom,
			  G_GNUC_UNUSED gboolean by_user,
			  gboolean is_default, FormatState *state)
{
	GnmColor *col = is_default
			   ? sheet_style_get_auto_pattern_color (state->sheet)
			   : style_color_new_go (c);

	gnm_style_set_pattern_color (state->back.style, col);

	back_style_changed (state);
}

static void
draw_pattern_selected (FormatState *state)
{
	gnm_style_set_pattern (state->back.style, state->back.pattern.cur_index);
	back_style_changed (state);
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
		"render-gridlines",	FALSE,
		"default-col-width",	w,
		"default-row-height",	h,
		"default-style",	state->back.style,
		NULL));
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
	GnmStyleBorderLocation	const	location;
} const line_info[] =
{
	/*
	state 1 = single cell;
	state 2 = multi vert, single horiz (A1:A2);
	state 3 = single vert, multi horiz (A1:B1);
	state 4 = multi vertical & multi horizontal
	*/

	/* 1, 2, 3, 4 */
	{ { L, T, R, T }, 0xf, GNM_STYLE_BORDER_TOP },
	{ { L, B, R, B }, 0xf, GNM_STYLE_BORDER_BOTTOM },
	{ { L, T, L, B }, 0xf, GNM_STYLE_BORDER_LEFT },
	{ { R, T, R, B }, 0xf, GNM_STYLE_BORDER_RIGHT },

	/* Only for state 2 & 4 */
	{ { L, H, R, H }, 0xa, GNM_STYLE_BORDER_HORIZ },

	/* Only for state 3 & 4 */
	{ { V, T, V, B }, 0xc, GNM_STYLE_BORDER_VERT },

	/* Only for state 1 & 4 */
	{ { L, T, R, B }, 0x9, GNM_STYLE_BORDER_REV_DIAG },
	{ { L, B, R, T }, 0x9, GNM_STYLE_BORDER_DIAG},

	/* Only for state 2 */
	{ { L, T, R, H }, 0x2, GNM_STYLE_BORDER_REV_DIAG },
	{ { L, H, R, B }, 0x2, GNM_STYLE_BORDER_REV_DIAG },
	{ { L, H, R, T }, 0x2, GNM_STYLE_BORDER_DIAG },
	{ { L, B, R, H }, 0x2, GNM_STYLE_BORDER_DIAG },

	/* Only for state 3 */
	{ { L, T, V, B }, 0x4, GNM_STYLE_BORDER_REV_DIAG },
	{ { V, T, R, B }, 0x4, GNM_STYLE_BORDER_REV_DIAG },
	{ { L, B, V, T }, 0x4, GNM_STYLE_BORDER_DIAG },
	{ { V, B, R, T }, 0x4, GNM_STYLE_BORDER_DIAG },

	/* Only for state 4 */
	{ { L, H, V, B }, 0x8, GNM_STYLE_BORDER_REV_DIAG },
	{ { V, T, R, H }, 0x8, GNM_STYLE_BORDER_REV_DIAG },
	{ { L, H, V, T }, 0x8, GNM_STYLE_BORDER_DIAG },
	{ { V, B, R, H }, 0x8, GNM_STYLE_BORDER_DIAG },

	{ { 0., 0., 0., 0. }, 0, 0 }
};

static GnmBorder *
border_get_mstyle (FormatState const *state, GnmStyleBorderLocation const loc)
{
	BorderPicker const *edge = & state->border.edge[loc];
	GnmColor *color;
	/* Don't set borders that have not been changed */
	if (!edge->is_set)
		return NULL;

	if (!edge->is_selected)
		return gnm_style_border_ref (gnm_style_border_none ());

	if (edge->is_auto_color) {
		color = sheet_style_get_auto_pattern_color (state->sheet);
	} else {
		guint8 const r = (guint8) (edge->rgba >> 24);
		guint8 const g = (guint8) (edge->rgba >> 16);
		guint8 const b = (guint8) (edge->rgba >>  8);
		color = style_color_new_i8 (r, g, b);
	}
	return gnm_style_border_fetch
		(state->border.edge[loc].pattern_index, color,
		 gnm_style_border_get_orientation (loc));
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
				foo_canvas_item_set (
					FOO_CANVAS_ITEM (state->border.lines[i]),
					"fill-color-rgba", edge->rgba,
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
	GnmStyleBorderLocation	 which = GNM_STYLE_BORDER_LEFT;

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
	if (x <= L+5.)		which = GNM_STYLE_BORDER_LEFT;
	else if (y <= T+5.)	which = GNM_STYLE_BORDER_TOP;
	else if (y >= B-5.)	which = GNM_STYLE_BORDER_BOTTOM;
	else if (x >= R-5.)	which = GNM_STYLE_BORDER_RIGHT;
	else switch (state->selection_mask) {
	case 1:
		if ((x < V) == (y < H))
			which = GNM_STYLE_BORDER_REV_DIAG;
		else
			which = GNM_STYLE_BORDER_DIAG;
		break;
	case 2:
		if (H-5. < y  && y < H+5.)
			which = GNM_STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the top */
			if (y > H) y -= H-10.;

			if ((x < V) == (y < H/2.))
				which = GNM_STYLE_BORDER_REV_DIAG;
			else
				which = GNM_STYLE_BORDER_DIAG;
		}
		break;
	case 4:
		if (V-5. < x  && x < V+5.)
			which = GNM_STYLE_BORDER_VERT;
		else {
			/* Map everything back to the left */
			if (x > V) x -= V-10.;

			if ((x < V/2.) == (y < H))
				which = GNM_STYLE_BORDER_REV_DIAG;
			else
				which = GNM_STYLE_BORDER_DIAG;
		}
		break;
	case 8:
		if (V-5. < x  && x < V+5.)
			which = GNM_STYLE_BORDER_VERT;
		else if (H-5. < y  && y < H+5.)
			which = GNM_STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the 1st quadrant */
			if (x > V) x -= V-10.;
			if (y > H) y -= H-10.;

			if ((x < V/2.) == (y < H/2.))
				which = GNM_STYLE_BORDER_REV_DIAG;
			else
				which = GNM_STYLE_BORDER_DIAG;
		}
		break;

	default:
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
		FooCanvasGroup  *group;
		FooCanvasPoints *points;

		state->border.canvas = FOO_CANVAS (foo_canvas_new ());
		gtk_widget_show (GTK_WIDGET (state->border.canvas));
		gtk_widget_set_size_request (GTK_WIDGET (state->border.canvas),
					     150, 100);
		gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (state->gui, "border_sample_container")),
				   GTK_WIDGET (state->border.canvas));
		group = FOO_CANVAS_GROUP (foo_canvas_root (state->border.canvas));

		g_signal_connect (G_OBJECT (state->border.canvas),
			"button-press-event",
			G_CALLBACK (border_event), state);

		state->border.back = foo_canvas_item_new (group,
			FOO_TYPE_CANVAS_RECT,
			"x1", L-10.,	"y1", T-10.,
			"x2", R+10.,	"y2", B+10.,
			"width-pixels", (int) 0,
			"fill-color",	"white",
			NULL);

		/* Draw the corners */
		points = foo_canvas_points_new (3);

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

			foo_canvas_item_new (group,
					       foo_canvas_line_get_type (),
					       "width-pixels",	(int) 0,
					       "fill-color",	"gray63",
					       "points",	points,
					       NULL);
		}
		foo_canvas_points_free (points);

		points = foo_canvas_points_new (2);
		for (i = 0; line_info[i].states != 0 ; ++i ) {
			for (j = 4; --j >= 0 ; )
				points->coords[j] = line_info[i].points[j];

			if (line_info[i].states & state->selection_mask) {
				BorderPicker const *p =
				    & state->border.edge[line_info[i].location];
				state->border.lines[i] =
					foo_canvas_item_new (group,
							       gnumeric_dashed_canvas_line_get_type (),
							       "fill-color-rgba", p->rgba,
							       "points",	  points,
							       NULL);
				gnumeric_dashed_canvas_line_set_dash_index (
					GNUMERIC_DASHED_CANVAS_LINE (state->border.lines[i]),
					p->pattern_index);
			} else
				state->border.lines[i] = NULL;
		}
		foo_canvas_points_free (points);
	}

	for (i = 0; i < GNM_STYLE_BORDER_EDGE_MAX; ++i) {
		BorderPicker *border = &state->border.edge[i];
		void (*func)(FooCanvasItem *item) = border->is_selected
			? &foo_canvas_item_show : &foo_canvas_item_hide;

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
	GnmStyleBorderLocation i, last;

	if (state->border.preset[BORDER_PRESET_NONE] == btn) {
		i = GNM_STYLE_BORDER_TOP;
		last = GNM_STYLE_BORDER_VERT;
		target_state = FALSE;
	} else if (state->border.preset[BORDER_PRESET_OUTLINE] == btn) {
		i = GNM_STYLE_BORDER_TOP;
		last = GNM_STYLE_BORDER_RIGHT;
		target_state = TRUE;
	} else if (state->border.preset[BORDER_PRESET_INSIDE] == btn) {
		i = GNM_STYLE_BORDER_HORIZ;
		last = GNM_STYLE_BORDER_VERT;
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
cb_border_color (G_GNUC_UNUSED GOComboColor *combo,
		 GOColor c,
		 G_GNUC_UNUSED gboolean is_custom,
		 G_GNUC_UNUSED gboolean by_user,
		 gboolean is_default, FormatState *state)
{
	state->border.rgba = c;
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
	GnmStyleBorderLocation     t;
	GnmBorder const *res;
} check_border_closure_t;

/*
 * Initialize the fields of a BorderPicker, connect signals and
 * hide if needed.
 */
static void
init_border_button (FormatState *state, GnmStyleBorderLocation const i,
		    GtkWidget *button,
		    GnmBorder const * const border)
{
	if (border == NULL) {
		state->border.edge[i].rgba = 0;
		state->border.edge[i].is_auto_color = TRUE;
		state->border.edge[i].pattern_index = GNM_STYLE_BORDER_INCONSISTENT;
		state->border.edge[i].is_selected = TRUE;
	} else {
		GnmColor const *c = border->color;
		state->border.edge[i].rgba = FOO_CANVAS_COLOR (
			c->gdk_color.red >> 8,
			c->gdk_color.green >> 8,
			c->gdk_color.blue >> 8);
		state->border.edge[i].is_auto_color = c->is_auto;
		state->border.edge[i].pattern_index = border->line_type;
		state->border.edge[i].is_selected = (border->line_type != GNM_STYLE_BORDER_NONE);
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

	if ((i == GNM_STYLE_BORDER_HORIZ && !(state->selection_mask & 0xa)) ||
	    (i == GNM_STYLE_BORDER_VERT  && !(state->selection_mask & 0xc)))
		gtk_widget_hide (button);
}

/*****************************************************************************/

static void
cb_protection_locked_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		gnm_style_set_contents_locked (state->result,
			gtk_toggle_button_get_active (button));
		fmt_dialog_changed (state);
	}
}

static void
cb_protection_hidden_toggle (GtkToggleButton *button, FormatState *state)
{
	if (state->enable_edit) {
		gnm_style_set_contents_hidden (state->result,
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

	flag = (state->conflicts & (1 << MSTYLE_CONTENTS_LOCKED))
		? FALSE : gnm_style_get_contents_locked (state->style);
	w = glade_xml_get_widget (state->gui, "protection_locked");
	state->protection.locked = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_locked_toggle), state);

	flag = (state->conflicts & (1 << MSTYLE_CONTENTS_HIDDEN))
		? FALSE : gnm_style_get_contents_hidden (state->style);
	w = glade_xml_get_widget (state->gui, "protection_hidden");
	state->protection.hidden = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_hidden_toggle), state);

	state->protection.sheet_protected_changed = FALSE;
	flag = state->sheet->is_protected; 
	w = glade_xml_get_widget (state->gui, "protection_sheet_protected");
	state->protection.sheet_protected = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_protection_sheet_protected_toggle), state);
}

/*****************************************************************************/

static GnmExprTop const *
validation_entry_to_expr (Sheet *sheet, GnmExprEntry *gee)
{
	GnmParsePos pp;
	parse_pos_init_sheet (&pp, sheet);
	return gnm_expr_entry_parse (gee, &pp, NULL, FALSE, GNM_EXPR_PARSE_DEFAULT);
}

static void
validation_rebuild_validation (FormatState *state)
{
	ValidationType	type;

	if (!state->enable_edit)
		return;

	state->validation.changed = FALSE;
	type = gtk_combo_box_get_active (
		state->validation.constraint_type);

	if (type != VALIDATION_TYPE_ANY) {
		ValidationStyle style = gtk_combo_box_get_active (state->validation.error.action);
		ValidationOp    op    = gtk_combo_box_get_active (state->validation.op);
		char *title = gtk_editable_get_chars (GTK_EDITABLE (state->validation.error.title), 0, -1);
		char *msg   = gnumeric_textview_get_text (state->validation.error.msg);
		GnmExprTop const *texpr0 =
			validation_entry_to_expr (state->sheet,
						  state->validation.expr0.entry);
		GnmExprTop const *texpr1 = NULL;

		if (texpr0 != NULL) {
			if (type == VALIDATION_TYPE_CUSTOM || type == VALIDATION_TYPE_IN_LIST) {
				state->validation.valid = 1;
				op = VALIDATION_OP_NONE;
			} else if (op == VALIDATION_OP_BETWEEN || op == VALIDATION_OP_NOT_BETWEEN) {
				texpr1 = validation_entry_to_expr (state->sheet,
								   state->validation.expr1.entry);
				if (texpr1 != NULL)
					state->validation.valid = 2;
				else {
					state->validation.valid = -2;
					gnm_expr_top_unref (texpr0);
				}
			} else
				state->validation.valid = 1;
		} else
			state->validation.valid = -1;

		if (state->validation.valid > 0) {
			gboolean allow_blank = gtk_toggle_button_get_active (state->validation.allow_blank);
			gboolean use_dropdown = gtk_toggle_button_get_active (state->validation.use_dropdown);
			gnm_style_set_validation
				(state->result,
				 validation_new
				 (style, type, op, title, msg,
				  texpr0,
				  texpr1,
				  allow_blank,
				  use_dropdown));
		}

		g_free (msg);
		g_free (title);
	} else
		gnm_style_set_validation (state->result, NULL);
	fmt_dialog_changed (state);
}

static void
cb_validation_error_action_changed (G_GNUC_UNUSED GtkMenuShell *ignored,
				       FormatState *state)
{
	int index = gtk_combo_box_get_active (state->validation.error.action);
	gboolean const flag = (index > 0) &&
		(gtk_combo_box_get_active (state->validation.constraint_type) > 0);

	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.title_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.msg_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.msg), flag);

	if (flag) {
		char const *s = NULL;

		switch (index) {
		case 1: s = GTK_STOCK_DIALOG_ERROR;	break;
		case 2: s = GTK_STOCK_DIALOG_WARNING;	break;
		case 3: s = GTK_STOCK_DIALOG_INFO;	break;
		default:
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
	ValidationType const type = gtk_combo_box_get_active (
		state->validation.constraint_type);

	switch (type) {
	case VALIDATION_TYPE_IN_LIST:		msg0 = _("Source"); break;
	case VALIDATION_TYPE_CUSTOM:		msg0 = _("Criteria"); break;

	case VALIDATION_TYPE_AS_INT:
	case VALIDATION_TYPE_AS_NUMBER:
	case VALIDATION_TYPE_AS_DATE:
	case VALIDATION_TYPE_AS_TIME:
	case VALIDATION_TYPE_TEXT_LENGTH: {
		ValidationOp const op = gtk_combo_box_get_active (
			state->validation.op);
		has_operators = TRUE;
		switch (op) {
		case VALIDATION_OP_NONE:
			break;
		case VALIDATION_OP_BETWEEN:
		case VALIDATION_OP_NOT_BETWEEN:
			msg0 = _("Min:");
			msg1 = _("Max:");
			break;
		case VALIDATION_OP_EQUAL:
		case VALIDATION_OP_NOT_EQUAL:
			msg0 = _("Value:");
			break;
		case VALIDATION_OP_GT:
		case VALIDATION_OP_GTE:
			msg0 =_("Min:");
			break;
		case VALIDATION_OP_LT:
		case VALIDATION_OP_LTE:
			msg0 = _("Max:");
			break;
		default:
			g_warning ("Unknown operator index %d", (int)op);
		}
		break;
	}
	default:
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
	entry->name  = GTK_LABEL (glade_xml_get_widget (state->gui, name));
	entry->entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gtk_table_attach (state->validation.criteria_table,
		GTK_WIDGET (entry->entry),
		 1, 2, 2+i, 3+i, GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show (GTK_WIDGET (entry->entry));
	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (entry->entry));
	g_signal_connect (G_OBJECT (entry->entry),
		"changed",
		G_CALLBACK (cb_validation_changed), state);
	gnm_expr_entry_set_flags (entry->entry, GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);

}

static void
cb_validation_rebuild (G_GNUC_UNUSED void *ignored,
		       FormatState *state)
{
	validation_rebuild_validation (state);
}

static void
build_validation_error_combo (GtkComboBox *box)
{
	GdkPixbuf *pixbuf;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;

	store = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	gtk_combo_box_set_model (box, GTK_TREE_MODEL (store));

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
				1, _("None          (silently accept invalid input)"),
				-1);

	pixbuf = gtk_widget_render_icon (GTK_WIDGET (box), GTK_STOCK_STOP,
										 GTK_ICON_SIZE_BUTTON, NULL);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
				0, pixbuf,
				1, _("Stop            (never allow invalid input)"),
				-1);

	pixbuf = gtk_widget_render_icon (GTK_WIDGET (box), GTK_STOCK_DIALOG_WARNING,
										 GTK_ICON_SIZE_BUTTON, NULL);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
				0, pixbuf,
				1, _("Warning     (accept/discard invalid input)"),
				-1);

	pixbuf = gtk_widget_render_icon (GTK_WIDGET (box), GTK_STOCK_DIALOG_INFO,
										 GTK_ICON_SIZE_BUTTON, NULL);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
				0, pixbuf,
				1, _("Information (allow invalid input)"),
				-1);

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (box),
								renderer,
								FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (box), renderer,
									"pixbuf", 0,
									NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (box),
								renderer,
								TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (box), renderer,
									"text", 1,
									NULL);
}

static void
fmt_dialog_init_validation_page (FormatState *state)
{
	GnmValidation const *v = NULL;
	g_return_if_fail (state != NULL);

	/* Setup widgets */
	state->validation.changed	  = FALSE;
	state->validation.valid		  = 1;
	state->validation.criteria_table  = GTK_TABLE          (glade_xml_get_widget (state->gui, "validation_criteria_table"));
	state->validation.constraint_type = GTK_COMBO_BOX    (glade_xml_get_widget (state->gui, "validation_constraint_type"));
	gtk_combo_box_set_active (state->validation.constraint_type, 0);
	state->validation.operator_label  = GTK_LABEL          (glade_xml_get_widget (state->gui, "validation_operator_label"));
	state->validation.op		     = GTK_COMBO_BOX    (glade_xml_get_widget (state->gui, "validation_operator"));
	gtk_combo_box_set_active (state->validation.op, 0);
	state->validation.allow_blank	     = GTK_TOGGLE_BUTTON(glade_xml_get_widget (state->gui, "validation_ignore_blank"));
	state->validation.use_dropdown       = GTK_TOGGLE_BUTTON(glade_xml_get_widget (state->gui, "validation_in_dropdown"));
	state->validation.error.action_label = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_action_label"));
	state->validation.error.title_label  = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_title_label"));
	state->validation.error.msg_label    = GTK_LABEL       (glade_xml_get_widget (state->gui, "validation_error_msg_label"));
	state->validation.error.action       = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "validation_error_action"));
	build_validation_error_combo (state->validation.error.action);
	gtk_combo_box_set_active (state->validation.error.action, 0);
	state->validation.error.title        = GTK_ENTRY       (glade_xml_get_widget (state->gui, "validation_error_title"));
	state->validation.error.msg          = GTK_TEXT_VIEW   (glade_xml_get_widget (state->gui, "validation_error_msg"));
	state->validation.error.image        = GTK_IMAGE       (glade_xml_get_widget (state->gui, "validation_error_image"));

	gnumeric_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->validation.error.title));

	g_signal_connect (state->validation.constraint_type,
		"changed",
		G_CALLBACK (cb_validation_sensitivity), state);
	g_signal_connect (state->validation.op,
		"changed",
		G_CALLBACK (cb_validation_sensitivity), state);
	g_signal_connect (state->validation.error.action,
		"changed",
		G_CALLBACK (cb_validation_error_action_changed), state);

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
	if (0 == (state->conflicts & (1 << MSTYLE_VALIDATION)))
		v = gnm_style_get_validation (state->style);
	if (v != NULL) {
		GnmValidation const *v = gnm_style_get_validation (state->style);
		GnmParsePos pp;

		gtk_combo_box_set_active (state->validation.error.action, v->style);
		gtk_combo_box_set_active (state->validation.constraint_type, v->type);
		gtk_combo_box_set_active (state->validation.op, v->op);

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
			v->texpr[0], &pp);
		gnm_expr_entry_load_from_expr (state->validation.expr1.entry,
			v->texpr[1], &pp);
	}

	cb_validation_sensitivity (NULL, state);
	cb_validation_error_action_changed (NULL, state);
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
#if 0
	static GnmStyleBorderLocation const bmap_ltr[] = {
		GNM_STYLE_BORDER_TOP,	GNM_STYLE_BORDER_BOTTOM,
		GNM_STYLE_BORDER_LEFT,	GNM_STYLE_BORDER_RIGHT,
		GNM_STYLE_BORDER_REV_DIAG,	GNM_STYLE_BORDER_DIAG,
		GNM_STYLE_BORDER_HORIZ,	GNM_STYLE_BORDER_VERT
	};
	static GnmStyleBorderLocation const bmap_rtl[] = {
		GNM_STYLE_BORDER_TOP,	GNM_STYLE_BORDER_BOTTOM,
		/* reverse */
		GNM_STYLE_BORDER_RIGHT,	GNM_STYLE_BORDER_LEFT,
		/* reverse */
		GNM_STYLE_BORDER_DIAG,	GNM_STYLE_BORDER_REV_DIAG,
		GNM_STYLE_BORDER_HORIZ,	GNM_STYLE_BORDER_VERT
	};
	GnmStyleBorderLocation const *bmap = bmap_ltr;

	if (NULL != state->sheet && state->sheet->text_is_rtl)
		bmap = bmap_rtl;
#endif

	if (btn == state->apply_button || btn == state->ok_button) {
		GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX];
		int i;

		if (state->validation.changed)
			validation_rebuild_validation (state);

		if (state->validation.valid < 0) {
			if (go_gtk_query_yes_no (
				    GTK_WINDOW (state->dialog),
				    FALSE,
				    _ ("The validation criteria are unusable. Disable validation?")))
			{
				gtk_combo_box_set_active (state->validation.constraint_type, 0);
				cb_validation_sensitivity (NULL, state);
			} else {
				gtk_notebook_set_current_page (state->notebook, FD_VALIDATION);

				if (state->validation.valid == -1)
					gnm_expr_entry_grab_focus (state->validation.expr0.entry, FALSE);
				else
					gnm_expr_entry_grab_focus (state->validation.expr1.entry, FALSE);
				return;
			}
		}

		if (state->protection.sheet_protected_changed) {
			state->sheet->is_protected = state->protection.sheet_protected_value;
			state->protection.sheet_protected_changed = FALSE;
		}

		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			borders[i] = border_get_mstyle (state, i);

		cmd_selection_format (WORKBOOK_CONTROL (state->wbcg),
			state->result, borders, NULL);
		/* state->result got absorbed.  */
		/* Get a fresh style to accumulate results in */
		state->result = gnm_style_new ();
		sheet_update (state->sheet);


		gtk_widget_set_sensitive (state->apply_button, FALSE);
	}

	if (btn != state->apply_button)
		gtk_object_destroy (GTK_OBJECT (state->dialog));
}

/* Handler for destroy */
static void
cb_fmt_dialog_dialog_destroy (FormatState *state)
{
	gnm_style_unref (state->back.style);
	gnm_style_unref (state->style);
	gnm_style_unref (state->result);
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

	if (strcmp (name, "number_box") == 0) {
		go_format_sel_set_focus (GO_FORMAT_SEL (s->format_sel));
		return;
	} else if (strcmp (name, "alignment_box") == 0)
	      focus_widget = glade_xml_get_widget (s->gui, "halign_left");
	else if (strcmp (name, "font_box") == 0)
	      focus_widget = GTK_WIDGET (s->font.selector);
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
		char const *const name;
		GnmStyleBorderType const pattern;
	} const line_pattern_buttons[] = {
		{ "line_pattern_none", GNM_STYLE_BORDER_NONE },
		{ "line_pattern_medium_dash_dot_dot", GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT },

		{ "line_pattern_hair", GNM_STYLE_BORDER_HAIR },
		{ "line_pattern_slant", GNM_STYLE_BORDER_SLANTED_DASH_DOT },

		{ "line_pattern_dotted", GNM_STYLE_BORDER_DOTTED },
		{ "line_pattern_medium_dash_dot", GNM_STYLE_BORDER_MEDIUM_DASH_DOT },

		{ "line_pattern_dash_dot_dot", GNM_STYLE_BORDER_DASH_DOT_DOT },
		{ "line_pattern_medium_dash", GNM_STYLE_BORDER_MEDIUM_DASH },

		{ "line_pattern_dash_dot", GNM_STYLE_BORDER_DASH_DOT },
		{ "line_pattern_medium", GNM_STYLE_BORDER_MEDIUM },

		{ "line_pattern_dashed", GNM_STYLE_BORDER_DASHED },
		{ "line_pattern_thick", GNM_STYLE_BORDER_THICK },

		{ "line_pattern_thin", GNM_STYLE_BORDER_THIN },
		{ "line_pattern_double", GNM_STYLE_BORDER_DOUBLE },

		{ NULL }
	};
	static char const *const pattern_buttons[] = {
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
	static char const *const border_buttons[] = {
		"top_border",	"bottom_border",
		"left_border",	"right_border",
		"rev_diag_border",	"diag_border",
		"inside_horiz_border", "inside_vert_border",
		NULL
	};

	/* The order corresponds to BorderPresets */
	static char const *const border_preset_buttons[] = {
		"no_border", "outline_border", "inside_border",
		NULL
	};

	int page_signal;
	int i, selected;
	char const *name;
	gboolean has_back;
	GdkColor *default_border_color;
	int default_border_style = GNM_STYLE_BORDER_THIN;

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
	state->back.style             = gnm_style_new_default ();
	state->back.pattern.cur_index = 0;

	fmt_dialog_init_format_page (state);
	fmt_dialog_init_align_page (state);
	fmt_dialog_init_font_page (state);
	fmt_dialog_init_background_page (state);
	fmt_dialog_init_protection_page (state);
	fmt_dialog_init_validation_page (state);
	fmt_dialog_init_input_msg_page (state);

	default_border_color = &GTK_WIDGET (state->dialog)->style->black;

	if (pageno == FD_CURRENT)
		pageno = fmt_dialog_page;
	gtk_notebook_set_current_page (state->notebook, pageno);

	page_signal = g_signal_connect (G_OBJECT (state->notebook),
		"switch_page",
		G_CALLBACK (cb_page_select), NULL);
	g_signal_connect (G_OBJECT (state->notebook),
		"destroy",
		G_CALLBACK (cb_notebook_destroy), GINT_TO_POINTER (page_signal));

	/* Setup border line pattern buttons & select the 1st button */
	for (i = MSTYLE_BORDER_TOP; i < MSTYLE_BORDER_DIAGONAL; i++) {
		GnmBorder const *border = gnm_style_get_border (state->style, i);
		if (!gnm_style_border_is_blank (border)) {
			default_border_color = &border->color->gdk_color;
			default_border_style = border->line_type;
			break;
		}
	}

	state->border.pattern.draw_preview = NULL;
	state->border.pattern.current_pattern = NULL;
	state->border.pattern.state = state;
	state->border.rgba = FOO_CANVAS_COLOR (
		default_border_color->red   >> 8,
		default_border_color->green >> 8,
		default_border_color->blue  >> 8);
	for (i = 0; (name = line_pattern_buttons[i].name) != NULL; ++i)
		setup_pattern_button (gtk_widget_get_screen (GTK_WIDGET (state->dialog)),
				      state->gui, name, &state->border.pattern,
				      i != 0, /* No image for None */
				      line_pattern_buttons[i].pattern,
				      default_border_style, 54);

	setup_color_pickers (state, &state->border.color,	"border_color_group",
			     "border_color_hbox",		"border_color_label",
			     _("Automatic"),			_("Border"),
			     G_CALLBACK (cb_border_color),	MSTYLE_BORDER_TOP);
	setup_color_pickers (state, NULL,			"fore_color_group",
			     "font_color_hbox",			"font_color_label",
			     _("Automatic"),			_("Foreground"),
			     G_CALLBACK (cb_font_preview_color), MSTYLE_FONT_COLOR);
	setup_color_pickers (state, &state->back.back_color,	"back_color_group",
			     "back_color_hbox",			"back_color_label",
			     _("Clear Background"),		_("Background"),
			     G_CALLBACK (cb_back_preview_color), MSTYLE_COLOR_BACK);
	setup_color_pickers (state, &state->back.pattern_color, "pattern_color_group",
			     "pattern_color_hbox",		"pattern_color_label",
			     _("Automatic"),			_("Pattern"),
			     G_CALLBACK (cb_pattern_preview_color), MSTYLE_COLOR_PATTERN);

	/* Setup the border images */
	for (i = 0; (name = border_buttons[i]) != NULL; ++i) {
		tmp = init_button_image (state->gui, name);
		if (tmp != NULL) {
			init_border_button (state, i, tmp,
					    state->borders[i]);
			gnm_style_border_unref (state->borders[i]);
		}
	}

	/* Get the current background
	 * A pattern of 0 is has no background.
	 * A pattern of 1 is a solid background
	 * All others have 2 colours and a stipple
	 */
	has_back = FALSE;
	selected = 1;
	if (0 == (state->conflicts & (1 << MSTYLE_PATTERN))) {
		selected = gnm_style_get_pattern (state->style);
		has_back = (selected != 0);
	}

	/* Setup pattern buttons & select the current pattern (or the 1st
	 * if none is selected)
	 * NOTE: This must be done AFTER the colour has been setup to
	 * avoid having it erased by initialization.
	 */
	state->back.pattern.draw_preview = &draw_pattern_selected;
	state->back.pattern.current_pattern = NULL;
	state->back.pattern.state = state;
	for (i = 0; (name = pattern_buttons[i]) != NULL; ++i)
		setup_pattern_button (gtk_widget_get_screen (GTK_WIDGET (state->dialog)),
				      state->gui, name,
				      &state->back.pattern, TRUE,
				      i+1, /* Pattern #s start at 1 */
				      selected, 16);

	/* If the pattern is 0 indicating no background colour
	 * Set background to No colour.  This will set states correctly.
	 */
	if (!has_back)
		go_combo_color_set_color_to_default (GO_COMBO_COLOR (state->back.back_color.combo));

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
		GNUMERIC_HELP_LINK_CELL_FORMAT);

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	state->apply_button = glade_xml_get_widget (state->gui, "applybutton");
	gtk_widget_set_sensitive (state->apply_button, FALSE);
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	tmp = glade_xml_get_widget (state->gui, "cancelbutton");
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
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_fmt_dialog_dialog_destroy);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));
}

static GnmValue *
cb_check_cell_format (GnmCellIter const *iter, gpointer user)
{
	FormatState *state = user;
	GnmValue const *value = iter->cell->value;
	GOFormat const *common = gnm_style_get_format (state->style);

	if (!value || !VALUE_FMT (value))
		return NULL;

	if (go_format_eq (common, VALUE_FMT (value)))
		return NULL;

	if (go_format_is_general (common)) {
		gnm_style_set_format (state->style, VALUE_FMT (value));
		return NULL;
	} else {
		state->conflicts |= MSTYLE_FORMAT;
		return VALUE_TERMINATE;
	}
}

static gboolean
fmt_dialog_selection_type (SheetView *sv,
			   GnmRange const *range,
			   gpointer user_data)
{
	FormatState *state = user_data;
	GSList *merged = gnm_sheet_merge_get_overlap (sv->sheet, range);
	GnmRange r = *range;
	gboolean allow_multi =
		merged == NULL ||
		merged->next != NULL ||
		!range_equal ((GnmRange *)merged->data, range);
	g_slist_free (merged);

	/* allow_multi == FALSE && !is_singleton (range) means that we are in
	 * an merge cell, use only the top left */
	if (r.start.col != r.end.col)
	{
		if (allow_multi)
			state->selection_mask |= 2;
		else
			r.end.col = r.start.col;
	}
	if (range->start.row != range->end.row)
	{
		if (allow_multi)
			state->selection_mask |= 1;
		else
			r.end.row = r.start.row;
	}

	state->conflicts = sheet_style_find_conflicts (state->sheet, &r,
		&(state->style), state->borders);

	if ((state->conflicts & MSTYLE_FORMAT) == 0 &&
	    go_format_is_general (gnm_style_get_format (state->style))) {
		sheet_foreach_cell_in_range (state->sheet, CELL_ITER_IGNORE_BLANK,
					     r.start.col, r.start.row,
					     r.end.col,	  r.end.row,
					     cb_check_cell_format,
					     state);
	}

	return TRUE;
}

void
dialog_cell_format (WBCGtk *wbcg, FormatDialogPosition_t pageno)
{
	GladeXML     *gui;
	GnmCell	     *edit_cell;
	FormatState  *state;

	g_return_if_fail (wbcg != NULL);

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"cell-format.glade", NULL, NULL);
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

	state->value		= (edit_cell != NULL) ? edit_cell->value : NULL;
	state->style		= NULL;
	state->result		= gnm_style_new ();
	state->selection_mask	= 0;
	state->dialog_changed	= NULL;
	state->dialog_changed_user_data = NULL;

	(void) sv_selection_foreach (state->sv,
		fmt_dialog_selection_type, state);
	state->selection_mask	= 1 << state->selection_mask;

	fmt_dialog_impl (state, pageno);
}

/*
 * TODO
 *
 * Borders
 *	- Add the 'text' elements in the preview
 *
 * Wishlist
 *	- Some undo capabilities in the dialog.
 *	- How to distinguish between auto & custom colors on extraction from styles.
 */
