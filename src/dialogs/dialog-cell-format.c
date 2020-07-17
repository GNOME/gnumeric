/*
 * dialog-cell-format.c:  Implements a dialog to format cells.
 *
 * Authors:
 *  Jody Goldberg <jody@gnome.org>
 *  Almer S. Tigelaar <almer@gnome.org>
 *  Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
  **/

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <style-color.h>
#include <gui-util.h>
#include <selection.h>
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
#include <input-msg.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <mathfunc.h>
#include <preview-grid.h>
#include <widgets/gnm-dashed-canvas-line.h>
#include <widgets/gnm-format-sel.h>
#include <style-conditions.h>

#include <goffice/goffice.h>
#include <goffice/canvas/goc-canvas.h>
#include <goffice/canvas/goc-item.h>
#include <goffice/canvas/goc-rectangle.h>

#include <string.h>

#define CELL_FORMAT_KEY "cell-format-dialog"

#if 0
static struct {
	char const *Cname;
	GnmUnderline ut;
} const underline_types[] = {
	/* xgettext: This refers to a "none underline" */
	{ NC_("underline", "None"), UNDERLINE_NONE },
	{ NC_("underline", "Single"), UNDERLINE_SINGLE },
	{ NC_("underline", "Double"), UNDERLINE_DOUBLE },
	/* xgettext: This refers to a "single low underline" */
	{ NC_("underline", "Single Low"), UNDERLINE_SINGLE_LOW },
	/* xgettext: This refers to a "double low underline" */
	{ NC_("underline", "Double Low"), UNDERLINE_DOUBLE_LOW }
};
#endif

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
	GtkBuilder	*gui;
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
		GOFontSel	*selector;
		GtkWidget       *underline_picker;
		GnmUnderline     underline;
	} font;
	struct {
		GocCanvas	*canvas;
		GtkButton	*preset[BORDER_PRESET_MAX];
		GocItem		*back;
		GocItem		*lines[20];

		BorderPicker	 edge[GNM_STYLE_BORDER_EDGE_MAX];
		ColorPicker      color;
		guint		 rgba;
		gboolean         is_auto_color;
		PatternPicker	 pattern;
	} border;
	struct {
		GocCanvas	*canvas;
		GnmPreviewGrid     *grid;
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
		GtkGrid       *criteria_grid;
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
	struct {
		gboolean is_selector;
		GtkWindow *w;
		gpointer closure;
	} style_selector;
} FormatState;

enum {
	CONDITIONS_RANGE,
	CONDITIONS_COND,
	CONDITIONS_NUM_COLUMNS
};



/*****************************************************************************/
/* Some utility routines shared by all pages */

/*
 * A utility routine to help mark the attributes as being changed
 * VERY stupid for now.
 */
static void
fmt_dialog_changed (FormatState *state)
{
	GOFormatSel *gfs;
	GOFormat const *fmt;
	gboolean ok;

	if (!state->enable_edit)
		return;

	gfs = GO_FORMAT_SEL (state->format_sel);
	fmt = go_format_sel_get_fmt (gfs);
	ok = !go_format_is_invalid (fmt);

	gtk_widget_set_sensitive (state->apply_button, ok);
	gtk_widget_set_sensitive (state->ok_button, ok);
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
		G_GNUC_UNUSED GtkWidget *page,
		gint page_num,
		G_GNUC_UNUSED	gpointer user_data)
{
	fmt_dialog_page = page_num;
}

static void
cb_notebook_destroy (GtkWidget *nb, gpointer page_sig_ptr)
{
	g_signal_handler_disconnect (nb, GPOINTER_TO_SIZE (page_sig_ptr));
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
		      GtkBuilder  *gui,
		      char const *const name,
		      PatternPicker *picker,
		      gboolean do_image,
		      gboolean from_icon,
		      int const index,
		      int const select_index,
		      unsigned size)
{
	GtkWidget *tmp = go_gtk_builder_get_widget (gui, name);
	if (tmp != NULL) {
		GtkButton *button = GTK_BUTTON (tmp);
		if (do_image) {
			char *res = g_strconcat ("/org/gnumeric/gnumeric/images/", name, ".png", NULL);
			GtkWidget *image;
			if (from_icon)
				image = gtk_image_new_from_icon_name (name, GTK_ICON_SIZE_DIALOG);
			else {
				/* gtk_image_new_from_resource() is unable to load pixdata with gdk-pixbuf >= 2.36.1
				 * because it uses the gdk_pixbuf_loader API and the pixdata module has been removed
				 * because of a security issue. See #776004. */
				GdkPixbuf *pixbuf = gdk_pixbuf_new_from_resource (res, NULL);
				image = gtk_image_new_from_pixbuf (pixbuf);
				g_object_unref (pixbuf);
			}
			g_free (res);
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
		g_warning ("CellFormat: Unexpected missing widget");
}

static void
setup_color_pickers (FormatState *state,
		     ColorPicker *picker,
		     char const *color_group,
		     char const *placeholder,
		     char const *label,
		     char const *default_caption,
		     char const *caption,
		     GCallback preview_update,
		     GnmStyleElement e,
                     gboolean allow_alpha)
{
	GtkWidget *combo, *w, *frame;
	GOColorGroup *cg;
	GnmColor *mcolor = NULL;
	GnmColor *def_sc = NULL;

	switch (e) {
	case MSTYLE_COLOR_PATTERN:
		if (0 == (state->conflicts & (1 << MSTYLE_COLOR_PATTERN)))
			mcolor = gnm_style_get_pattern_color (state->style);

		/* fallthrough */

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
	cg = go_color_group_fetch (color_group, NULL);
	combo = go_combo_color_new (NULL, default_caption,
		def_sc ? def_sc->go_color : GO_COLOR_BLACK, cg);
	g_object_unref (cg);
	go_combo_box_set_title (GO_COMBO_BOX (combo), caption);

	/* Connect to the sample canvas and redraw it */
	g_signal_connect (G_OBJECT (combo),
			  "color_changed",
			  G_CALLBACK (preview_update), state);

	if (mcolor && !mcolor->is_auto)
		go_combo_color_set_color (GO_COMBO_COLOR (combo),
					  mcolor->go_color);
	else
		go_combo_color_set_color_to_default (GO_COMBO_COLOR (combo));

	if (allow_alpha)
		go_combo_color_set_allow_alpha (GO_COMBO_COLOR (combo), TRUE);
	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	gtk_container_add (GTK_CONTAINER (frame), combo);
	gtk_widget_show_all (frame);

	w = go_gtk_builder_get_widget (state->gui, placeholder);
	go_gtk_widget_replace (w, frame);

	w = go_gtk_builder_get_widget (state->gui, label);
	gtk_label_set_mnemonic_widget (GTK_LABEL (w), combo);

	style_color_unref (def_sc);

	if (picker != NULL) {
		picker->combo          = combo;
		picker->preview_update = preview_update;
	}
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
	GODateConventions const *date_conv = sheet_date_conv (state->sheet);

	state->format_sel = gnm_format_sel_new ();
	gfs = GO_FORMAT_SEL (state->format_sel);

	gtk_notebook_prepend_page (GTK_NOTEBOOK (state->notebook),
				   state->format_sel,
				   gtk_label_new (_("Number")));
	gtk_widget_show (GTK_WIDGET (gfs));

	if (0 == (state->conflicts & (1 << MSTYLE_FORMAT))) {
		GOFormat const *fmt = gnm_style_get_format (state->style);
		go_format_sel_set_style_format (gfs, fmt);
	}
	go_format_sel_set_dateconv (gfs, date_conv);
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
			(new_h == GNM_HALIGN_LEFT || new_h == GNM_HALIGN_RIGHT);
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
	GtkWidget *tmp = go_gtk_builder_get_widget (state->gui, name);
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
cb_rotation_changed (G_GNUC_UNUSED GORotationSel *grs, int angle, FormatState *state)
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
		{ "halign_left",	GNM_HALIGN_LEFT },
		{ "halign_center",	GNM_HALIGN_CENTER },
		{ "halign_right",	GNM_HALIGN_RIGHT },
		{ "halign_general",	GNM_HALIGN_GENERAL },
		{ "halign_justify",	GNM_HALIGN_JUSTIFY },
		{ "halign_fill",	GNM_HALIGN_FILL },
		{ "halign_center_across_selection",	GNM_HALIGN_CENTER_ACROSS_SELECTION },
		{ "halign_distributed",			GNM_HALIGN_DISTRIBUTED },
		{ NULL, 0}
	};
	static struct {
		char const *const	name;
		GnmVAlign	align;
	} const v_buttons[] = {
		{ "valign_top", GNM_VALIGN_TOP },
		{ "valign_center", GNM_VALIGN_CENTER },
		{ "valign_bottom", GNM_VALIGN_BOTTOM },
		{ "valign_justify", GNM_VALIGN_JUSTIFY },
		{ "valign_distributed", GNM_VALIGN_DISTRIBUTED },
		{ NULL, 0}
	};

	GtkWidget *w;
	gboolean wrap = FALSE;
	GnmHAlign    h = GNM_HALIGN_GENERAL;
	GnmVAlign    v = GNM_VALIGN_CENTER;
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

	w = go_gtk_builder_get_widget (state->gui, "align_wrap");
	state->align.wrap = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), wrap);
	g_signal_connect (G_OBJECT (w),
			  "toggled",
			  G_CALLBACK (cb_align_wrap_toggle), state);

	if (0 != (state->conflicts & (1 << MSTYLE_INDENT)) ||
	    (h != GNM_HALIGN_LEFT && h != GNM_HALIGN_RIGHT))
		state->align.indent = 0;
	else
		state->align.indent = gnm_style_get_indent (state->style);

	state->align.indent_label =
		go_gtk_builder_get_widget (state->gui, "halign_indent_label");
	w = go_gtk_builder_get_widget (state->gui, "halign_indent");
	state->align.indent_button = GTK_SPIN_BUTTON (w);
	gtk_spin_button_set_value (state->align.indent_button, state->align.indent);
	gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_button),
				  (h == GNM_HALIGN_LEFT || h == GNM_HALIGN_RIGHT));
	gtk_widget_set_sensitive (GTK_WIDGET (state->align.indent_label),
				  (h == GNM_HALIGN_LEFT || h == GNM_HALIGN_RIGHT));

	/* Catch changes to the spin box */
	g_signal_connect (G_OBJECT (w),
			  "value-changed",
			  G_CALLBACK (cb_indent_changed), state);

	/* Catch <return> in the spin box */
	gnm_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (w));

	/* setup the rotation canvas */
	if (0 == (state->conflicts & (1 << MSTYLE_ROTATION))) {
		r = gnm_style_get_rotation (state->style);
		if (r > 180)
			r -= 360;
	} else
		r = 0;
	state->align.rotation = (GORotationSel *) go_rotation_sel_new ();
	go_rotation_sel_set_rotation (state->align.rotation, r);
	g_signal_connect (G_OBJECT (state->align.rotation), "rotation-changed",
			  G_CALLBACK (cb_rotation_changed), state);
	go_gtk_widget_replace (go_gtk_builder_get_widget (state->gui, "rotation_placeholder"),
			 GTK_WIDGET (state->align.rotation));
}

/*****************************************************************************/

static void
cb_font_changed (G_GNUC_UNUSED GtkWidget *widget,
		 PangoAttrList *attrs, FormatState *state)
{
	PangoAttrIterator *aiter;
	const PangoAttribute *attr;
	GnmStyle *res = state->result;
	GOFontScript script = GO_FONT_SCRIPT_STANDARD;
	gboolean has_script_attr = FALSE;
	GnmColor *c;

	gboolean changed = FALSE;
	g_return_if_fail (state != NULL);

	if (!state->enable_edit)
		return;

	aiter = pango_attr_list_get_iterator (attrs);

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_FAMILY);
	if (attr) {
		const char *s = ((PangoAttrString*)attr)->value;
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_NAME) ||
		    !g_str_equal (s, gnm_style_get_font_name (res))) {
			changed = TRUE;
			gnm_style_set_font_name (res, s);
		}
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_SIZE);
	if (attr) {
		int i = ((PangoAttrInt*)attr)->value;
		double d = i / (double)PANGO_SCALE;
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_SIZE) ||
		    d != gnm_style_get_font_size (res)) {
			changed = TRUE;
			gnm_style_set_font_size (res, d);
		}
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_WEIGHT);
	if (attr) {
		int i = ((PangoAttrInt*)attr)->value;
		gboolean b = (i >= PANGO_WEIGHT_BOLD);
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_BOLD) ||
		    b != gnm_style_get_font_bold (res)) {
			changed = TRUE;
			gnm_style_set_font_bold (res, b);
		}
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_STYLE);
	if (attr) {
		int i = ((PangoAttrInt*)attr)->value;
		gboolean b = (i != PANGO_STYLE_NORMAL);
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_ITALIC) ||
		    b != gnm_style_get_font_italic (res)) {
			changed = TRUE;
			gnm_style_set_font_italic (res, b);
		}
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_UNDERLINE);
	if (attr) {
		/* Underline is special: we go beyond what pango has */
		GnmUnderline u = state->font.underline;
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_UNDERLINE) ||
		    u != gnm_style_get_font_uline (res)) {
			changed = TRUE;
			gnm_style_set_font_uline (res, u);
		}
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_STRIKETHROUGH);
	if (attr) {
		int i = ((PangoAttrInt*)attr)->value;
		gboolean b = (i != 0);
		if (!gnm_style_is_element_set (res, MSTYLE_FONT_STRIKETHROUGH) ||
		    b != gnm_style_get_font_strike (res)) {
			changed = TRUE;
			gnm_style_set_font_strike (res, b);
		}
	}

	attr = pango_attr_iterator_get (aiter, go_pango_attr_subscript_get_attr_type ());
	if (attr) {
		has_script_attr = TRUE;
		if (((GOPangoAttrSubscript*)attr)->val)
			script = GO_FONT_SCRIPT_SUB;
	}
	attr = pango_attr_iterator_get (aiter, go_pango_attr_superscript_get_attr_type ());
	if (attr) {
		has_script_attr = TRUE;
		if (((GOPangoAttrSuperscript*)attr)->val)
			script = GO_FONT_SCRIPT_SUPER;
	}
	if (has_script_attr &&
	    (!gnm_style_is_element_set (res, MSTYLE_FONT_SCRIPT) ||
	     script != gnm_style_get_font_script (res))) {
		changed = TRUE;
		gnm_style_set_font_script (res, script);
	}

	attr = pango_attr_iterator_get (aiter, PANGO_ATTR_FOREGROUND);
	c = attr
		? gnm_color_new_pango (&((PangoAttrColor*)attr)->color)
		: style_color_auto_font ();
	if (!gnm_style_is_element_set (res, MSTYLE_FONT_COLOR) ||
	    !style_color_equal (c, gnm_style_get_font_color (res))) {
		changed = TRUE;
		gnm_style_set_font_color (res, c);
	} else
		style_color_unref (c);

	pango_attr_iterator_destroy (aiter);

	if (changed)
		fmt_dialog_changed (state);
}

static void
change_font_attr (FormatState *state, PangoAttribute *attr)
{
	GOFontSel *gfs = state->font.selector;
	PangoAttrList *attrs = pango_attr_list_copy
		(go_font_sel_get_sample_attributes (gfs));
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_change (attrs, attr);
	go_font_sel_set_sample_attributes (gfs, attrs);
	cb_font_changed (NULL, attrs, state);
	pango_attr_list_unref (attrs);
}

static void
set_font_underline (FormatState *state, GnmUnderline uline)
{
	PangoUnderline pu = gnm_translate_underline_to_pango (uline);
	GOOptionMenu *om = GO_OPTION_MENU (state->font.underline_picker);
	GtkMenuShell *ms = GTK_MENU_SHELL (go_option_menu_get_menu (om));
	GList *children, *l;

	if (uline != state->font.underline) {
		state->font.underline = uline;
		change_font_attr (state, pango_attr_underline_new (pu));
	}

	children = gtk_container_get_children (GTK_CONTAINER (ms));
	for (l = children; l; l = l->next) {
		GtkMenuItem *item = GTK_MENU_ITEM (l->data);
		GnmUnderline u = GPOINTER_TO_INT
			(g_object_get_data (G_OBJECT (item), "value"));
		if (u == uline)
			go_option_menu_select_item (om, item);
	}
	g_list_free (children);
}

static void
cb_underline_changed (GOOptionMenu *om, FormatState *state)
{
	GtkWidget *selected = go_option_menu_get_history (om);
	GnmUnderline u;

	if (!selected)
		return;

	u = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (selected), "value"));
	set_font_underline (state, u);
}


/* Manually insert the font selector, and setup signals */
static void
fmt_dialog_init_font_page (FormatState *state)
{
	GOColorGroup *cg;
	GtkWidget *font_widget;
	gboolean strikethrough = FALSE;
	GOFontScript script = GO_FONT_SCRIPT_STANDARD;
	GODateConventions const *date_conv = sheet_date_conv (state->sheet);
	GnmColor *mcolor = NULL;
	GnmColor *def_sc;
	GtkWidget *up;

	up = state->font.underline_picker = go_option_menu_build
		(C_("underline", "None"), UNDERLINE_NONE,
		 C_("underline", "Single"), UNDERLINE_SINGLE,
		 C_("underline", "Double"), UNDERLINE_DOUBLE,
		 C_("underline", "Single Low"), UNDERLINE_SINGLE_LOW,
		 C_("underline", "Double Low"), UNDERLINE_DOUBLE_LOW,
		 NULL);
	g_signal_connect (up,
			  "changed", G_CALLBACK (cb_underline_changed), state);
	def_sc = style_color_auto_font ();
	cg = go_color_group_fetch ("fore_color_group", NULL);
	font_widget = g_object_new (GO_TYPE_FONT_SEL,
				    "show-style", TRUE,
				    "show-color", TRUE,
				    "color-unset-text", _("Automatic"),
				    "color-group", cg,
				    "color-default", def_sc->go_color,
				    "show-underline", TRUE,
				    "underline-picker", up,
				    "show-script", TRUE,
				    "show-strikethrough", TRUE,
				    "vexpand", TRUE,
				    "hexpand", TRUE,
				    NULL);
	g_object_unref (cg);
	style_color_unref (def_sc);
	state->font.selector = GO_FONT_SEL (font_widget);
	g_object_unref (up);

	gtk_widget_show (font_widget);
	gtk_container_add (GTK_CONTAINER (go_gtk_builder_get_widget (state->gui, "font_sel_placeholder")),
			   font_widget);

	go_font_sel_editable_enters (state->font.selector,
				     GTK_WINDOW (state->dialog));

	if (state->value) {
		char *s = format_value (NULL, state->value, -1, date_conv);
		go_font_sel_set_sample_text (state->font.selector, s);
		g_free (s);
	}

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_NAME))) {
		const char *family = gnm_style_get_font_name (state->style);
		go_font_sel_set_family (state->font.selector, family);
	}

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_BOLD)) &&
	    0 == (state->conflicts & (1 << MSTYLE_FONT_ITALIC))) {
		gboolean is_bold = gnm_style_get_font_bold (state->style);
		gboolean is_italic = gnm_style_get_font_italic (state->style);

		go_font_sel_set_style
			(state->font.selector,
			 is_bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
			 is_italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	}

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_SIZE))) {
		double pts = gnm_style_get_font_size (state->style);
		go_font_sel_set_size (state->font.selector,
				      pts * PANGO_SCALE);
	}

	state->font.underline = UNDERLINE_NONE;
	if (0 == (state->conflicts & (1 << MSTYLE_FONT_UNDERLINE))) {
		GnmUnderline ut = gnm_style_get_font_uline (state->style);
		set_font_underline (state, ut);
	}

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_COLOR)))
		mcolor = gnm_style_get_font_color (state->style);
	go_font_sel_set_color (state->font.selector,
			       mcolor ? mcolor->go_color : GO_COLOR_BLACK,
			       !mcolor || mcolor->is_auto);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_STRIKETHROUGH)))
		strikethrough = gnm_style_get_font_strike (state->style);
	go_font_sel_set_strikethrough (state->font.selector, strikethrough);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_SCRIPT)))
		script = gnm_style_get_font_script (state->style);
	go_font_sel_set_script (state->font.selector, script);

	if (0 == (state->conflicts & (1 << MSTYLE_FONT_COLOR)))
		change_font_attr
			(state,
			 go_color_to_pango (gnm_style_get_font_color (state->style)->go_color,
					    TRUE));

	g_signal_connect (G_OBJECT (state->font.selector),
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
		goc_item_set (GOC_ITEM (state->back.grid),
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
		sc = gnm_color_new_go (c);
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
		: gnm_color_new_go (c);

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

	widget = g_object_new (GOC_TYPE_CANVAS, NULL);
	state->back.canvas = GOC_CANVAS (widget);
	gtk_widget_set_size_request (widget, w, h);

	widget = go_gtk_builder_get_widget (state->gui, "back_sample_frame");
	gtk_container_add (GTK_CONTAINER (widget),
			   GTK_WIDGET (state->back.canvas));
	gtk_widget_show_all (widget);

	state->back.grid = GNM_PREVIEW_GRID (goc_item_new (
						 goc_canvas_get_root (state->back.canvas),
						 gnm_preview_grid_get_type (),
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
} const line_info[] = {
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
		guint8 const a = (guint8) (edge->rgba >>  0);
		color = gnm_color_new_rgba8 (r, g, b, a);
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
				go_styled_object_get_style (
					GO_STYLED_OBJECT (state->border.lines[i]))->line.color = edge->rgba;
		}
	}
	if ((int)edge->pattern_index != state->border.pattern.cur_index) {
		edge->pattern_index = state->border.pattern.cur_index;
		for (i = 0; line_info[i].states != 0 ; ++i ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL) {
				gnm_dashed_canvas_line_set_dash_index (
					GNM_DASHED_CANVAS_LINE (state->border.lines[i]),
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
	BorderPicker *edge;
	GnmStyleBorderLocation which;

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
			which = GNM_STYLE_BORDER_LEFT;
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
	int i, j, k;

	/* The first time through lets initialize */
	if (state->border.canvas == NULL) {
		GocGroup  *group;
		GocPoints *points;
		GOStyle *style;

		state->border.canvas = GOC_CANVAS (g_object_new (GOC_TYPE_CANVAS, NULL));
		gtk_widget_show (GTK_WIDGET (state->border.canvas));
		gtk_widget_set_size_request (GTK_WIDGET (state->border.canvas),
					     150, 100);
		go_gtk_widget_replace (go_gtk_builder_get_widget (state->gui, "border_sample_placeholder"),
				 GTK_WIDGET (state->border.canvas));
		group = GOC_GROUP (goc_canvas_get_root (state->border.canvas));

		g_signal_connect (G_OBJECT (state->border.canvas),
				  "button-press-event",
				  G_CALLBACK (border_event), state);

		state->border.back = goc_item_new (group,
						   GOC_TYPE_RECTANGLE,
						   "x", L-10.,		"y", T-10.,
						   "width", R-L+20.,	"height", B-T+20.,
						   NULL);
		style = go_styled_object_get_style (GO_STYLED_OBJECT (state->border.back));
		style->line.dash_type = GO_LINE_NONE;

		/* Draw the corners */
		points = goc_points_new (3);

		for (i = 0; i < 12 ; ++i) {
			if (i >= 8) {
				if (!(state->selection_mask & 0xa))
					continue;
			} else if (i >= 4) {
				if (!(state->selection_mask & 0xc))
					continue;
			}

			for (j = 3, k = 5 ; --j >= 0 ;) {
				points->points[j].y = corners[i][k--] + .5;
				points->points[j].x = corners[i][k--] + .5;
			}


			style = go_styled_object_get_style (GO_STYLED_OBJECT (
								    goc_item_new (group,
										  goc_polyline_get_type (),
										  "points",	points,
										  NULL)));
			style->line.color = GO_COLOR_FROM_RGBA (0xa1, 0xa1, 0xa1, 0xff); /* gray63 */
			style->line.width = 0.;
		}
		goc_points_unref (points);

		for (i = 0; line_info[i].states != 0 ; ++i ) {
			if (line_info[i].states & state->selection_mask) {
				BorderPicker const *p =
					& state->border.edge[line_info[i].location];
				state->border.lines[i] =
					goc_item_new (group,
						      gnm_dashed_canvas_line_get_type (),
					              "x0", line_info[i].points[0],
					              "y0", line_info[i].points[1],
					              "x1", line_info[i].points[2],
					              "y1", line_info[i].points[3],
						      NULL);
				style = go_styled_object_get_style (GO_STYLED_OBJECT (state->border.lines[i]));
				style->line.color = p->rgba;
				gnm_dashed_canvas_line_set_dash_index (
					GNM_DASHED_CANVAS_LINE (state->border.lines[i]),
					p->pattern_index);
			} else
				state->border.lines[i] = NULL;
		}
	}

	for (i = 0; i < GNM_STYLE_BORDER_EDGE_MAX; ++i) {
		BorderPicker const *border = &state->border.edge[i];
		int j;

		for (j = 0; line_info[j].states != 0 ; ++j) {
			if ((int)line_info[j].location == i &&
			    state->border.lines[j] != NULL)
				goc_item_set_visible (state->border.lines[j],
						      border->is_selected);
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
		state->border.edge[i].rgba = c->go_color;
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
	w = go_gtk_builder_get_widget (state->gui, "protection_locked");
	state->protection.locked = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
			  "toggled",
			  G_CALLBACK (cb_protection_locked_toggle), state);

	flag = (state->conflicts & (1 << MSTYLE_CONTENTS_HIDDEN))
		? FALSE : gnm_style_get_contents_hidden (state->style);
	w = go_gtk_builder_get_widget (state->gui, "protection_hidden");
	state->protection.hidden = GTK_CHECK_BUTTON (w);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), flag);
	g_signal_connect (G_OBJECT (w),
			  "toggled",
			  G_CALLBACK (cb_protection_hidden_toggle), state);

	state->protection.sheet_protected_changed = FALSE;
	flag = state->sheet->is_protected;
	w = go_gtk_builder_get_widget (state->gui, "protection_sheet_protected");
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

	if (type != GNM_VALIDATION_TYPE_ANY) {
		ValidationStyle style = gtk_combo_box_get_active (state->validation.error.action);
		ValidationOp    op    = gtk_combo_box_get_active (state->validation.op);
		char *title = gtk_editable_get_chars (GTK_EDITABLE (state->validation.error.title), 0, -1);
		char *msg   = gnm_textview_get_text (state->validation.error.msg);
		GnmExprTop const *texpr0 =
			validation_entry_to_expr (state->sheet,
						  state->validation.expr0.entry);
		GnmExprTop const *texpr1 = NULL;

		if (texpr0 != NULL) {
			if (type == GNM_VALIDATION_TYPE_CUSTOM || type == GNM_VALIDATION_TYPE_IN_LIST) {
				state->validation.valid = 1;
				op = GNM_VALIDATION_OP_NONE;
			} else if (op == GNM_VALIDATION_OP_BETWEEN || op == GNM_VALIDATION_OP_NOT_BETWEEN) {
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
				 gnm_validation_new
				 (style, type, op,
				  state->sheet,
				  title, msg,
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

static struct {
	const char *text;
	const char *icon_name;
} validation_error_actions[] = {
	{
		N_("None          (silently accept invalid input)"),
		NULL
	},
	{
		N_("Stop            (never allow invalid input)"),
		"dialog-error"
	},
	{
		N_("Warning     (accept/discard invalid input)"),
		"dialog-warning"
	},
	{
		N_("Information (allow invalid input)"),
		"dialog-information"
	}
};

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
		char const *icon_name = validation_error_actions[index].icon_name;
		gtk_image_set_from_icon_name (state->validation.error.image,
					      icon_name, GTK_ICON_SIZE_DIALOG);
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
	case GNM_VALIDATION_TYPE_IN_LIST:		msg0 = _("Source"); break;
	case GNM_VALIDATION_TYPE_CUSTOM:		msg0 = _("Criteria"); break;

	case GNM_VALIDATION_TYPE_AS_INT:
	case GNM_VALIDATION_TYPE_AS_NUMBER:
	case GNM_VALIDATION_TYPE_AS_DATE:
	case GNM_VALIDATION_TYPE_AS_TIME:
	case GNM_VALIDATION_TYPE_TEXT_LENGTH: {
		ValidationOp const op = gtk_combo_box_get_active (
			state->validation.op);
		has_operators = TRUE;
		switch (op) {
		case GNM_VALIDATION_OP_NONE:
			break;
		case GNM_VALIDATION_OP_BETWEEN:
		case GNM_VALIDATION_OP_NOT_BETWEEN:
			msg0 = _("Min:");
			msg1 = _("Max:");
			break;
		case GNM_VALIDATION_OP_EQUAL:
		case GNM_VALIDATION_OP_NOT_EQUAL:
			msg0 = _("Value:");
			break;
		case GNM_VALIDATION_OP_GT:
		case GNM_VALIDATION_OP_GTE:
			msg0 =_("Min:");
			break;
		case GNM_VALIDATION_OP_LT:
		case GNM_VALIDATION_OP_LTE:
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
				  type != GNM_VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.error.action),
				  type != GNM_VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.allow_blank),
				  type != GNM_VALIDATION_TYPE_ANY);
	gtk_widget_set_sensitive (GTK_WIDGET (state->validation.use_dropdown),
				  type == GNM_VALIDATION_TYPE_IN_LIST);

	validation_rebuild_validation (state);
}

static void
cb_validation_changed (G_GNUC_UNUSED GtkEntry *ignored,
		       FormatState *state)
{
	if (state->enable_edit)
		state->validation.changed = TRUE;
}

static void
fmt_dialog_init_validation_expr_entry (FormatState *state, ExprEntry *entry,
				       char const *name, int i)
{
	entry->name  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, name));
	entry->entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gtk_grid_attach (state->validation.criteria_grid,
		GTK_WIDGET (entry->entry), 1, 3+i, 3, 1);
	gtk_widget_show (GTK_WIDGET (entry->entry));
	gnm_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (entry->entry));
	gnm_expr_entry_set_flags (entry->entry, GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL, GNM_EE_MASK);
	g_signal_connect (G_OBJECT (entry->entry),
		"changed",
		G_CALLBACK (cb_validation_changed), state);
}

static void
cb_validation_rebuild (G_GNUC_UNUSED void *ignored,
		       FormatState *state)
{
	validation_rebuild_validation (state);
}

static void
build_validation_error_combo (FormatState *state, GtkComboBox *box)
{
	GtkListStore *store;
	GtkCellRenderer *renderer;
	unsigned ui;

	store = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	gtk_combo_box_set_model (box, GTK_TREE_MODEL (store));

	for (ui = 0; ui < G_N_ELEMENTS (validation_error_actions); ui++) {
		const char *icon_name = validation_error_actions[ui].icon_name;
		GdkPixbuf *pixbuf = icon_name
			? go_gtk_widget_render_icon_pixbuf (GTK_WIDGET (wbcg_toplevel (state->wbcg)),
							    icon_name, GTK_ICON_SIZE_MENU)
			: NULL;
		GtkTreeIter iter;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    0, pixbuf,
				    1, _(validation_error_actions[ui].text),
				    -1);
		if (pixbuf)
			g_object_unref (pixbuf);
	}

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

	g_object_unref (store);
}

static void
fmt_dialog_init_validation_page (FormatState *state)
{
	GnmValidation const *v = NULL;
	g_return_if_fail (state != NULL);

	/* Setup widgets */
	state->validation.changed	  = FALSE;
	state->validation.valid		  = 1;
	state->validation.criteria_grid  = GTK_GRID          (go_gtk_builder_get_widget (state->gui, "validation-grid"));
	state->validation.constraint_type = GTK_COMBO_BOX    (go_gtk_builder_get_widget (state->gui, "validation_constraint_type"));
	gtk_combo_box_set_active (state->validation.constraint_type, 0);
	state->validation.operator_label  = GTK_LABEL          (go_gtk_builder_get_widget (state->gui, "validation_operator_label"));
	state->validation.op		     = GTK_COMBO_BOX    (go_gtk_builder_get_widget (state->gui, "validation_operator"));
	gtk_combo_box_set_active (state->validation.op, 0);
	state->validation.allow_blank	     = GTK_TOGGLE_BUTTON(go_gtk_builder_get_widget (state->gui, "validation_ignore_blank"));
	state->validation.use_dropdown       = GTK_TOGGLE_BUTTON(go_gtk_builder_get_widget (state->gui, "validation_in_dropdown"));
	state->validation.error.action_label = GTK_LABEL       (go_gtk_builder_get_widget (state->gui, "validation_error_action_label"));
	state->validation.error.title_label  = GTK_LABEL       (go_gtk_builder_get_widget (state->gui, "validation_error_title_label"));
	state->validation.error.msg_label    = GTK_LABEL       (go_gtk_builder_get_widget (state->gui, "validation_error_msg_label"));
	state->validation.error.action       = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "validation_error_action"));
	build_validation_error_combo (state, state->validation.error.action);
	gtk_combo_box_set_active (state->validation.error.action, 0);
	state->validation.error.title        = GTK_ENTRY       (go_gtk_builder_get_widget (state->gui, "validation_error_title"));
	state->validation.error.msg          = GTK_TEXT_VIEW   (go_gtk_builder_get_widget (state->gui, "validation_error_msg"));
	state->validation.error.image        = GTK_IMAGE       (go_gtk_builder_get_widget (state->gui, "validation_error_image"));

	gnm_editable_enters (
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
		GnmParsePos pp;

		gtk_combo_box_set_active (state->validation.error.action, v->style);
		gtk_combo_box_set_active (state->validation.constraint_type, v->type);
		gtk_combo_box_set_active (state->validation.op, v->op);

		gtk_entry_set_text (GTK_ENTRY (state->validation.error.title),
			(v->title != NULL) ? v->title->str : "");
		if (v->msg != NULL)
			gnm_textview_set_text (GTK_TEXT_VIEW (state->validation.error.msg),
				v->msg->str);
		gtk_toggle_button_set_active (state->validation.allow_blank,  v->allow_blank);
		gtk_toggle_button_set_active (state->validation.use_dropdown, v->use_dropdown);

		parse_pos_init (&pp, state->sheet->workbook, state->sheet,
			state->sv->edit_pos.col, state->sv->edit_pos.row);
		gnm_expr_entry_load_from_expr (state->validation.expr0.entry,
					       v->deps[0].base.texpr, &pp);
		gnm_expr_entry_load_from_expr (state->validation.expr1.entry,
					       v->deps[1].base.texpr, &pp);
	}

	cb_validation_sensitivity (NULL, state);
	cb_validation_error_action_changed (NULL, state);
}

/*****************************************************************************/

static void
input_msg_rebuild_input_msg (FormatState *state)
{
	GnmInputMsg *im;
	char *msg = gnm_textview_get_text (state->input_msg.msg);
	char const *title = gtk_entry_get_text (state->input_msg.title);

	im = gnm_input_msg_new	(msg, title);
	g_free (msg);
	gnm_style_set_input_msg (state->result, im);
	fmt_dialog_changed (state);
}

static void
cb_input_msg_rebuild (G_GNUC_UNUSED void *ignored,
		      FormatState *state)
{
	input_msg_rebuild_input_msg (state);
}

static void
cb_input_msg_flag_toggled (GtkToggleButton *button, FormatState *state)
{
	gboolean flag = gtk_toggle_button_get_active (button);

	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.title_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.msg_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg.msg), flag);

	if (state->enable_edit) {
		if (flag)
			input_msg_rebuild_input_msg (state);
		else
			gnm_style_set_input_msg (state->result, NULL);
		fmt_dialog_changed (state);
	}
}

static void
fmt_dialog_init_input_msg_page (FormatState *state)
{
	GnmInputMsg const *im = NULL;

	g_return_if_fail (state != NULL);

	/* Setup widgets */
	state->input_msg.flag        = GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "input_msg_flag"));
	state->input_msg.title_label = GTK_LABEL         (go_gtk_builder_get_widget (state->gui, "input_msg_title_label"));
	state->input_msg.msg_label   = GTK_LABEL         (go_gtk_builder_get_widget (state->gui, "input_msg_msg_label"));
	state->input_msg.title       = GTK_ENTRY         (go_gtk_builder_get_widget (state->gui, "input_msg_title"));
	state->input_msg.msg         = GTK_TEXT_VIEW     (go_gtk_builder_get_widget (state->gui, "input_msg_msg"));

	/* Initialize */
	if (0 == (state->conflicts & (1 << MSTYLE_INPUT_MSG)))
		im = gnm_style_get_input_msg (state->style);
	if (im) {
		gtk_entry_set_text (state->input_msg.title,
				    gnm_input_msg_get_title (im));
		gnm_textview_set_text (state->input_msg.msg,
					    gnm_input_msg_get_msg (im));
	}
	gtk_toggle_button_set_active (state->input_msg.flag, im != NULL);

	gnm_editable_enters (
		GTK_WINDOW (state->dialog),
		GTK_WIDGET (state->input_msg.title));

	g_signal_connect (G_OBJECT (state->input_msg.flag),
		"toggled",
		G_CALLBACK (cb_input_msg_flag_toggled), state);
	g_signal_connect (G_OBJECT (state->input_msg.title),
		"changed",
		G_CALLBACK (cb_input_msg_rebuild), state);
	g_signal_connect (G_OBJECT (gtk_text_view_get_buffer (state->input_msg.msg)),
		"changed",
		G_CALLBACK (cb_input_msg_rebuild), state);

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
		int i;

		/* We need to make sure the right sheet is active */
		/* since we are acting on the current selection   */
		/* validation may have switched sheets.           */

		wb_control_sheet_focus (GNM_WBC (state->wbcg),
					state->sheet);

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


		if (state->style_selector.is_selector) {
			GnmStyle *style = gnm_style_dup (state->style);
			for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_DIAG; i++) {
				GnmBorder *b = border_get_mstyle (state, i);
				if (b)
					gnm_style_set_border
						(state->result,
						 MSTYLE_BORDER_TOP +
						 (int)(i - GNM_STYLE_BORDER_TOP),
						 b);
			}
			gnm_style_merge (style, state->result);
			dialog_cell_format_style_added
				(state->style_selector.closure,
				 style);
			gnm_style_unref (state->result);
		} else {
			GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX];
			for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
				borders[i] = border_get_mstyle (state, i);
			cmd_selection_format (GNM_WBC (state->wbcg),
					      state->result, borders, NULL);
		}
		/* state->result got absorbed.  */
		/* Get a fresh style to accumulate results in */
		state->result = gnm_style_new ();
		sheet_update (state->sheet);


		gtk_widget_set_sensitive (state->apply_button, FALSE);
	}

	if (btn != state->apply_button)
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

/* Handler for destroy */
static void
cb_fmt_dialog_dialog_destroy (FormatState *state)
{
	gnm_style_unref (state->back.style);
	gnm_style_unref (state->style);
	gnm_style_unref (state->result);
	g_object_unref (state->gui);
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

static void
cb_dialog_destroy (GtkDialog *dialog)
{
	g_object_set_data (G_OBJECT (dialog), "state", NULL);
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
	      focus_widget = go_gtk_builder_get_widget (s->gui, "halign_left");
	else if (strcmp (name, "font_box") == 0)
	      focus_widget = GTK_WIDGET (s->font.selector);
	else if (strcmp (name, "border_box") == 0)
	      focus_widget = go_gtk_builder_get_widget (s->gui, "gnumeric-format-border-outline");
	else if (strcmp (name, "background_box") == 0)
	      focus_widget = go_gtk_builder_get_widget (s->gui, "back_color_auto");
	else if (strcmp (name, "protection_box") == 0)
	      focus_widget = GTK_WIDGET (s->protection.locked);
	else
		focus_widget = NULL;

	if (focus_widget &&
	    gtk_widget_get_can_focus (focus_widget) &&
	    gtk_widget_is_sensitive (focus_widget))
		gtk_widget_grab_focus (focus_widget);
}

static void
fmt_dialog_impl (FormatState *state, FormatDialogPosition_t pageno, gint pages)
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

		{ NULL, 0}
	};
	static char const *const pattern_buttons[] = {
		"gnumeric-pattern-solid", "gnumeric-pattern-75grey", "gnumeric-pattern-50grey",
		"gnumeric-pattern-25grey", "gnumeric-pattern-125grey", "gnumeric-pattern-625grey",

		"gnumeric-pattern-horiz",
		"gnumeric-pattern-vert",
		"gnumeric-pattern-diag",
		"gnumeric-pattern-rev-diag",
		"gnumeric-pattern-diag-cross",
		"gnumeric-pattern-thick-diag-cross",

		"gnumeric-pattern-thin-horiz",
		"gnumeric-pattern-thin-vert",
		"gnumeric-pattern-thin-rev-diag",
		"gnumeric-pattern-thin-diag",
		"gnumeric-pattern-thin-horiz-cross",
		"gnumeric-pattern-thin-diag-cross",

		"gnumeric-pattern-small-circle",
		"gnumeric-pattern-semi-circle",
		"gnumeric-pattern-thatch",
		"gnumeric-pattern-large-circles",
		"gnumeric-pattern-bricks",
		"gnumeric-pattern-foreground-solid",

		NULL
	};

	/* The order corresponds to the BorderLocation enum */
	static char const *const border_buttons[] = {
		"gnumeric-format-border-top",	"gnumeric-format-border-bottom",
		"gnumeric-format-border-left",	"gnumeric-format-border-right",
		"gnumeric-format-border-rev-diag",	"gnumeric-format-border-diag",
		"gnumeric-format-border-inside-horiz", "gnumeric-format-border-inside-vert",
		NULL
	};

	/* The order corresponds to BorderPresets */
	static char const *const border_preset_buttons[] = {
		"gnumeric-format-border-no", "gnumeric-format-border-outline", "gnumeric-format-border-inside",
		NULL
	};

	gulong page_signal;
	int i, selected;
	char const *name;
	gboolean has_back;
	GOColor default_border_color;
	int default_border_style = GNM_STYLE_BORDER_THIN;
	GtkStyleContext *ctxt;
	GdkRGBA bc;

	GtkWidget *tmp, *dialog = go_gtk_builder_get_widget (state->gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Format Cells"));

	/* Initialize */
	state->dialog	   = GTK_DIALOG (dialog);
	state->notebook	   = GTK_NOTEBOOK (go_gtk_builder_get_widget (state->gui, "notebook"));

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

	ctxt = gtk_widget_get_style_context (GTK_WIDGET (state->dialog));
	gtk_style_context_get_border_color (ctxt, GTK_STATE_FLAG_NORMAL, &bc);
	default_border_color = GO_COLOR_FROM_GDK_RGBA (bc);

	if (pageno == FD_CURRENT)
		pageno = fmt_dialog_page;
	gtk_notebook_set_current_page (state->notebook, pageno);

	page_signal = g_signal_connect (G_OBJECT (state->notebook),
		"switch_page",
		G_CALLBACK (cb_page_select), NULL);
	g_signal_connect (G_OBJECT (state->notebook),
		"destroy",
		G_CALLBACK (cb_notebook_destroy), GSIZE_TO_POINTER (page_signal));

	/* Setup border line pattern buttons & select the 1st button */
	for (i = MSTYLE_BORDER_TOP; i < MSTYLE_BORDER_DIAGONAL; i++) {
		GnmBorder const *border = gnm_style_get_border (state->style, i);
		if (!gnm_style_border_is_blank (border)) {
			default_border_color = border->color->go_color;
			default_border_style = border->line_type;
			break;
		}
	}

	state->border.pattern.draw_preview = NULL;
	state->border.pattern.current_pattern = NULL;
	state->border.pattern.state = state;
	state->border.rgba = default_border_color;
	for (i = 0; (name = line_pattern_buttons[i].name) != NULL; ++i)
		setup_pattern_button (gtk_widget_get_screen (GTK_WIDGET (state->dialog)),
				      state->gui, name, &state->border.pattern,
				      i != 0, /* No image for None */
				      FALSE,
				      line_pattern_buttons[i].pattern,
				      default_border_style, 54);

	setup_color_pickers (state, &state->border.color,	"border_color_group",
			     "border_color_placeholder",	"border_color_label",
			     _("Automatic"),			_("Border"),
			     G_CALLBACK (cb_border_color),	MSTYLE_BORDER_TOP, FALSE);
	setup_color_pickers (state, &state->back.back_color,	"back_color_group",
			     "background_color_placeholder",	"back_color_label",
			     _("Clear Background"),		_("Background"),
			     G_CALLBACK (cb_back_preview_color), MSTYLE_COLOR_BACK, FALSE);
	setup_color_pickers (state, &state->back.pattern_color, "pattern_color_group",
			     "pattern_color_placeholder",	"pattern_color_label",
			     _("Automatic"),			_("Pattern"),
			     G_CALLBACK (cb_pattern_preview_color), MSTYLE_COLOR_PATTERN, FALSE);

	/* Setup the border images */
	for (i = 0; (name = border_buttons[i]) != NULL; ++i) {
		GtkWidget *tmp = go_gtk_builder_get_widget (state->gui, name);
		if (tmp != NULL) {
			init_border_button (state, i, tmp,
					    state->borders[i]);
			gnm_style_border_unref (state->borders[i]);
			state->borders[i] = NULL;
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
				      &state->back.pattern, TRUE, TRUE,
				      i+1, /* Pattern #s start at 1 */
				      selected, 16);

	/* If the pattern is 0 indicating no background colour
	 * Set background to No colour.  This will set states correctly.
	 */
	if (!has_back)
		go_combo_color_set_color_to_default (GO_COMBO_COLOR (state->back.back_color.combo));

	/* Setup the images in the border presets */
	for (i = 0; (name = border_preset_buttons[i]) != NULL; ++i) {
		GtkWidget *tmp = go_gtk_builder_get_widget (state->gui, name);
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

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "helpbutton"),
		GNUMERIC_HELP_LINK_CELL_FORMAT);

	state->ok_button = go_gtk_builder_get_widget (state->gui, "okbutton");
	gtk_widget_set_sensitive (state->ok_button, FALSE);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	state->apply_button = go_gtk_builder_get_widget (state->gui, "applybutton");
	gtk_widget_set_sensitive (state->apply_button, FALSE);
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);
	tmp = go_gtk_builder_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (tmp),
		"clicked",
		G_CALLBACK (cb_fmt_dialog_dialog_buttons), state);

	set_initial_focus (state);
	gtk_notebook_set_scrollable (state->notebook, TRUE);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog), state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

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

	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_fmt_dialog_dialog_destroy);
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (cb_dialog_destroy), NULL);

	if (pages > 0)
		for (i = 0; i <= FD_LAST; i++) {
			GtkWidget *widget = gtk_notebook_get_nth_page
				(state->notebook, i);
			if (widget != NULL && !((1<<i) & pages))
				gtk_widget_hide (widget);
		}

	// If we have conflicts then allow setting General right away
	if (state->conflicts & (1 << MSTYLE_FORMAT))
		cb_number_format_changed (NULL,
					  go_format_as_XL (go_format_general ()),
					  state);

	gnm_restore_window_geometry (GTK_WINDOW (state->dialog),
					  CELL_FORMAT_KEY);
}

static GnmValue *
cb_check_cell_format (GnmCellIter const *iter, gpointer user)
{
	FormatState *state = user;
	GnmValue const *value = iter->cell->value;
	GOFormat const *common = gnm_style_get_format (state->style);
	GOFormat const *fmt = value ? VALUE_FMT (value) : NULL;

	if (!fmt ||
	    go_format_is_markup (fmt) ||
	    go_format_eq (common, fmt))
		return NULL;

	if (go_format_is_general (common)) {
		gnm_style_set_format (state->style, fmt);
		return NULL;
	} else {
		state->conflicts |= (1 << MSTYLE_FORMAT);
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

	if ((state->conflicts & (1 << MSTYLE_FORMAT)) == 0 &&
	    go_format_is_general (gnm_style_get_format (state->style))) {
		sheet_foreach_cell_in_range (state->sheet,
					     CELL_ITER_IGNORE_BLANK,
					     &r,
					     cb_check_cell_format,
					     state);
	}

	return TRUE;
}

static FormatState *
dialog_cell_format_init (WBCGtk *wbcg)
{
	GtkBuilder     *gui;
	GnmCell	     *edit_cell;
	FormatState  *state;

	gui = gnm_gtk_builder_load ("res:ui/cell-format.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return NULL;

	/* Initialize */
	state = g_new (FormatState, 1);
	state->wbcg	= wbcg;
	state->gui	= gui;
	state->sv	= wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet	= sv_sheet (state->sv);

	edit_cell = sheet_cell_get (state->sheet,
				    state->sv->edit_pos.col,
				    state->sv->edit_pos.row);

	state->value		= (edit_cell != NULL) ? edit_cell->value : NULL;
	state->style		= NULL;
	state->result		= gnm_style_new ();
	state->selection_mask	= 0;

	(void) sv_selection_foreach (state->sv,
		fmt_dialog_selection_type, state);
	state->selection_mask	= 1 << state->selection_mask;

	return state;
}

void
dialog_cell_format (WBCGtk *wbcg, FormatDialogPosition_t pageno, gint pages)
{
	FormatState  *state;

	g_return_if_fail (wbcg != NULL);

	state = dialog_cell_format_init (wbcg);

        if (state == NULL)
                return;

	state->style_selector.is_selector = FALSE;
	state->style_selector.w = NULL;
	state->style_selector.closure = NULL;

	if (pages == 0) {
		int i;
		for (i = FD_NUMBER; i <= FD_PROTECTION; i++)
			pages |= (1 << i);
	}

	fmt_dialog_impl (state, pageno, pages);

	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));
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

/**
 * dialog_cell_format_select_style:
 *
 * Returns: (transfer floating): a #GtkDialog.
 */
GtkDialog *
dialog_cell_format_select_style (WBCGtk *wbcg, gint pages,
				 GtkWindow *w,
				 GnmStyle *style, gpointer closure)
{
	FormatState  *state;

	g_return_val_if_fail (wbcg != NULL, NULL);
	state = dialog_cell_format_init (wbcg);
	g_return_val_if_fail (state != NULL, NULL);

	state->style_selector.is_selector = TRUE;
	state->style_selector.w = w;
	state->style_selector.closure = closure;
	state->selection_mask	= 1;

	if (style) {
		gnm_style_unref (state->style);
		state->style = style;
		state->conflicts = 0;
	}

	fmt_dialog_impl (state, FD_BACKGROUND, pages);

	gtk_widget_hide (state->apply_button);

	go_gtk_nonmodal_dialog (w, GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));

	return state->dialog;
}
