/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook-format-toolbar.c: Format toolbar implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-1999 Miguel de Icaza.
 * (C) 2000-2002 Jody Goldberg
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-format-toolbar.h"

#include "gui-util.h"
#include "dialogs.h"
#include "selection.h"
#include "global-gnome-font.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-style.h"
#include "cell.h"
#include "application.h"
#include "commands.h"
#include "format.h"
#include "formats.h"
#include "style-color.h"
#include "style-border.h"
#include "ranges.h"
#include "mstyle.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"

#include <gal/widgets/gtk-combo-text.h>
#include <gal/widgets/widget-color-combo.h>
#include <gal/widgets/widget-pixmap-combo.h>
#include <libgnome/gnome-i18n.h>

#define TOOLBAR_FONT_BUTTON_INDEX		2
#define TOOLBAR_BOLD_BUTTON_INDEX		3
#define TOOLBAR_ITALIC_BUTTON_INDEX		4
#define TOOLBAR_UNDERLINE_BUTTON_INDEX		5
#define TOOLBAR_ALIGN_LEFT_BUTTON_INDEX		6
#define TOOLBAR_ALIGN_CENTER_BUTTON_INDEX	7
#define TOOLBAR_ALIGN_RIGHT_BUTTON_INDEX	8
#define TOOLBAR_CENTER_ACROSS_SELECTION_INDEX	9

static void
workbook_format_halign_feedback_set (WorkbookControlGUI *wbcg,
				     StyleHAlignFlags h_align);

static void
set_selection_halign (WorkbookControlGUI *wbcg, StyleHAlignFlags halign)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	WorkbookView	*wb_view;
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *style;

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	application_clipboard_unant ();

	/* This is a toggle button.  If we are already enabled
	 * then revert to general
	 */
	wb_view = wb_control_view (wbc);
	style = wb_view->current_format;
	if (mstyle_get_align_h (style) == halign)
		halign = HALIGN_GENERAL;

	style = mstyle_new ();
	mstyle_set_align_h (style, halign);
	workbook_format_halign_feedback_set (wbcg, halign);

	cmd_format (wbc, sheet, style, NULL,
		    _("Set Horizontal Alignment"));
}

static void
cb_align_left (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	set_selection_halign (wbcg, HALIGN_LEFT);
}

static void
cb_align_right (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	set_selection_halign (wbcg, HALIGN_RIGHT);
}

static void
cb_align_center (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	set_selection_halign (wbcg, HALIGN_CENTER);
}

static void
cb_center_across_selection (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	set_selection_halign (wbcg, HALIGN_CENTER_ACROSS_SELECTION);
}

static void
cb_merge_cells (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GSList *range_list = selection_get_ranges (sheet, FALSE);
	cmd_merge_cells (wbc, sheet, range_list);
	range_fragment_free (range_list);
}

static void
cb_unmerge_cells (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GSList *range_list = selection_get_ranges (sheet, FALSE);
	cmd_unmerge_cells (wbc, sheet, range_list);
	range_fragment_free (range_list);
}

/*
 * change_selection_font
 * @wbcg:  The workbook to operate on
 * @bold:         TRUE to toggle, FALSE to leave unchanged
 * @italic:       TRUE to toggle, FALSE to leave unchanged
 * @underline:    TRUE to toggle, FALSE to leave unchanged
 * strikethrough: TRUE to toggle, FALSE to leave unchanged
 */
static void
change_selection_font (WorkbookControlGUI *wbcg,
		       gboolean bold,		gboolean italic,
		       gboolean underline,	gboolean strikethrough)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *new_style, *current_style;

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	application_clipboard_unant ();

	new_style = mstyle_new ();
	current_style = sheet_style_get (sheet,
		sheet->edit_pos.col,
		sheet->edit_pos.row);

	if (bold)
		mstyle_set_font_bold (new_style,
			!mstyle_get_font_bold (current_style));

	if (italic)
		mstyle_set_font_italic (new_style,
			!mstyle_get_font_italic (current_style));

	if (underline)
		mstyle_set_font_uline (new_style,
			(mstyle_get_font_uline (current_style) == UNDERLINE_NONE)
			? UNDERLINE_SINGLE : UNDERLINE_NONE);

	if (strikethrough)
		mstyle_set_font_strike (new_style,
					!mstyle_get_font_strike (current_style));

	if (bold || italic || underline || strikethrough)
		cmd_format (wbc, sheet, new_style, NULL,
			    _("Set Font Style"));
	else
		mstyle_unref (new_style);
}

/**
 * cb_font_name
 *
 * @wbcg:  workboook
 *
 * Pop up cell format dialog at font page. Used from font select toolbar
 * button, which is displayed in vertical mode instead of font name / font
 * size controls.
 */
static void
cb_font_name (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_cell_format (wbcg, wb_control_cur_sheet (wbc), FD_FONT);
}

static void
cb_font_bold (GtkToggleButton *t, WorkbookControlGUI *wbcg)
{
	change_selection_font (wbcg, TRUE, FALSE, FALSE, FALSE);
}

static void
cb_font_italic (GtkToggleButton *t, WorkbookControlGUI *wbcg)
{
	change_selection_font (wbcg, FALSE, TRUE, FALSE, FALSE);
}

static void
cb_font_underline (GtkToggleButton *t, WorkbookControlGUI *wbcg)
{
	change_selection_font (wbcg, FALSE, FALSE, TRUE, FALSE);
}

#if 0
static void
strikethrough_cmd (GtkToggleButton *t, WorkbookControlGUI *wbcg)
{
	change_selection_font (wbcg, FALSE, FALSE, FALSE, TRUE);
}
#endif

static void
change_font_in_selection_cmd (GtkWidget *caller, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	if (sheet != NULL) {
		const char *font_name = gtk_entry_get_text (GTK_ENTRY(caller));
		MStyle *mstyle;

		mstyle = mstyle_new ();
		mstyle_set_font_name (mstyle, font_name);
		cmd_format (wbc, sheet, mstyle, NULL,
			    _("Set Font"));

		/* Restore the focus to the sheet */
		wbcg_focus_cur_scg (wbcg);
	}
}

static void
change_font_size_in_selection_cmd (GtkEntry *entry, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *mstyle;
	double size;

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	/* Make 1pt a minimum size for fonts */
	size = atof (gtk_entry_get_text (entry));
	if (size < 1.0) {
		gtk_entry_set_text (entry, "10");
		return;
	}

	mstyle = mstyle_new ();
	mstyle_set_font_size (mstyle, size);

	cmd_format (wbc, sheet, mstyle, NULL,
		    _("Set Font Size"));

	/* Restore the focus to the sheet */
	wbcg_focus_cur_scg (wbcg);
}

static void
apply_number_format (WorkbookControlGUI *wbcg,
		     char const *translated_format, char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *mstyle = mstyle_new ();

	mstyle_set_format_text (mstyle, translated_format);
	cmd_format (wbc, sheet, mstyle, NULL, descriptor);
}

static void
cb_format_as_money (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	apply_number_format (wbcg, cell_formats [FMT_ACCOUNT] [2],
			     _("Format as Money"));
}

static void
cb_format_as_percent (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	apply_number_format (wbcg, "0.00%", _("Format as Percentage"));
}

static void
cb_format_with_thousands (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	apply_number_format (wbcg, cell_formats [FMT_NUMBER][2],
			     _("Format with thousands separator"));
}

static void
modify_format (WorkbookControlGUI *wbcg,
	       char *(*format_modify_fn) (StyleFormat const *format),
	       char const *descriptor)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	WorkbookView const *wbv;
	char *new_fmt;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	new_fmt = (*format_modify_fn) (mstyle_get_format (wbv->current_format));
	if (new_fmt != NULL) {
		MStyle *style = mstyle_new ();
		mstyle_set_format_text (style, new_fmt);
		cmd_format (wbc, sheet, style, NULL, descriptor);
		g_free (new_fmt);
	}
}

static void
cb_format_inc_precision (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	modify_format (wbcg, &format_add_decimal, _("Increase precision"));
}

static void
cb_format_dec_precision (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	modify_format (wbcg, &format_remove_decimal, _("Decrease precision"));
}

static void
cb_format_inc_indent (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	WorkbookView const *wbv;
	int i;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	i = mstyle_get_indent (wbv->current_format);
	if (i < 20) {
		MStyle *style = mstyle_new ();

		if (HALIGN_LEFT != mstyle_get_align_h (wbv->current_format))
			mstyle_set_align_h (style, HALIGN_LEFT);
		mstyle_set_indent (style, i+1);
		cmd_format (wbc, sheet, style, NULL,
			    _("Increase Indent"));
	}
}

static void
cb_format_dec_indent (GtkWidget *ignore, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	WorkbookView const *wbv;
	int i;

	wbv = wb_control_view (wbc);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->current_format != NULL);

	i = mstyle_get_indent (wbv->current_format);
	if (i > 0) {
		MStyle *style = mstyle_new ();

		mstyle_set_indent (style, i-1);
		cmd_format (wbc, sheet, style, NULL,
			    _("Decrease Indent"));
	}
}

#ifndef ENABLE_BONOBO
static GnomeUIInfo workbook_format_toolbar [] = {
	/* Placeholder: font selector */
        /* Placeholder: size selector */

	/* Button to replace font and size selectors in vertical mode */
	GNOMEUIINFO_ITEM_STOCK (
		N_("Select font"), N_("Font selector"),
		&cb_font_name, "Font"),
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Bold"), N_("Sets the bold font"),
		&cb_font_bold, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_BOLD,
		'b', GDK_CONTROL_MASK
	},

	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Italic"), N_("Makes the font italic"),
		&cb_font_italic, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_ITALIC,
		'i', GDK_CONTROL_MASK
	},

	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Underline"), N_("Underlines the font"),
		&cb_font_underline, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_UNDERLINE,
		'u', GDK_CONTROL_MASK
	},

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Left align"), N_("Left justifies the cell contents"),
		&cb_align_left, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_ALIGN_LEFT },
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Center"), N_("Centers the cell contents"),
		&cb_align_center, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_ALIGN_CENTER },
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Right align"), N_("Right justifies the cell contents"),
		&cb_align_right, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_ALIGN_RIGHT },
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Center across selection"), N_("Center across selection"),
		&cb_center_across_selection, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Gnumeric_CenterAcrossSelection" },
	GNOMEUIINFO_ITEM_STOCK (
		N_("Merge"), N_("Merge a range of cells"),
		&cb_merge_cells, "Gnumeric_MergeCells"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Split"), N_("Split merged ranges of cells"),
		&cb_unmerge_cells, "Gnumeric_SplitCells"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Money format"), N_("Sets the format of the selected cells to monetary"),
		cb_format_as_money, "Gnumeric_FormatAsMoney"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Percent"), N_("Sets the format of the selected cells to percentage"),
		cb_format_as_percent, "Gnumeric_FormatAsPercent"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Thousand separator"), N_("Sets the format of the selected cells to include a thousands separator"),
		cb_format_with_thousands, "Gnumeric_FormatThousandSeparator"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Add decimals"), N_("Increases the number of decimals displayed"),
		cb_format_inc_precision, "Gnumeric_FormatAddPrecision"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Remove decimals"), N_("Decreases the number of decimals displayed"),
		cb_format_dec_precision, "Gnumeric_FormatRemovePrecision"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Decrease Indent"), N_("Aligns the contents to the left and decreases the indent"),
		cb_format_dec_indent, GNOME_STOCK_PIXMAP_TEXT_UNINDENT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Increase Indent"), N_("Aligns the contents to the left and increases the indent"),
		cb_format_inc_indent, GNOME_STOCK_PIXMAP_TEXT_INDENT),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_END
};
#else
static BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("FontSelect",              &cb_font_name),
	BONOBO_UI_UNSAFE_VERB ("FontBold",		  &cb_font_bold),
	BONOBO_UI_UNSAFE_VERB ("FontItalic",		  &cb_font_italic),
	BONOBO_UI_UNSAFE_VERB ("FontUnderline",	   	  &cb_font_underline),
	BONOBO_UI_UNSAFE_VERB ("AlignLeft",		  &cb_align_left),
	BONOBO_UI_UNSAFE_VERB ("AlignCenter",		  &cb_align_center),
	BONOBO_UI_UNSAFE_VERB ("AlignRight",		  &cb_align_right),
	BONOBO_UI_UNSAFE_VERB ("CenterAcrossSelection",	  &cb_center_across_selection),
	BONOBO_UI_UNSAFE_VERB ("FormatMergeCells",	  &cb_merge_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatUnmergeCells",	  &cb_unmerge_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAsMoney",	          &cb_format_as_money),
	BONOBO_UI_UNSAFE_VERB ("FormatAsPercent",	  &cb_format_as_percent),
	BONOBO_UI_UNSAFE_VERB ("FormatWithThousands",	  &cb_format_with_thousands),
	BONOBO_UI_UNSAFE_VERB ("FormatIncreasePrecision", &cb_format_inc_precision),
	BONOBO_UI_UNSAFE_VERB ("FormatDecreasePrecision", &cb_format_dec_precision),
	BONOBO_UI_UNSAFE_VERB ("FormatIncreaseIndent",	  &cb_format_inc_indent),
	BONOBO_UI_UNSAFE_VERB ("FormatDecreaseIndent",	  &cb_format_dec_indent),
	BONOBO_UI_VERB_END
};
#endif

static void
cb_fore_color_changed (ColorCombo *combo, GdkColor *c,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *mstyle;

	/* Color was set programatically, bail out */
	if (!by_user)
		return;

	mstyle = mstyle_new ();
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE,
			  (c != NULL)
			  ? style_color_new (c->red, c->green, c->blue)
			  : style_color_black () /* FIXME: add auto colours ? */);

	/* Change the color for all views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (wbc), view, control,
				  if (control != wbc) color_combo_set_color (
					  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->fore_color), c););

	cmd_format (wbc, sheet, mstyle, NULL,
		    _("Set Foreground Color"));
}

static void
cb_back_color_changed (ColorCombo *combo, GdkColor *c,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	MStyle *mstyle;

	/* Color was set programatically, bail out */
	if (!by_user)
		return;

	mstyle = mstyle_new ();
	if (c != NULL) {
		/* We need to have a pattern of at least solid to draw a background colour */
		if (!mstyle_is_element_set  (mstyle, MSTYLE_PATTERN) ||
		    mstyle_get_pattern (mstyle) < 1)
			mstyle_set_pattern (mstyle, 1);

		mstyle_set_color (mstyle, MSTYLE_COLOR_BACK,
				  style_color_new (c->red, c->green, c->blue));
	} else
		/* Set background to NONE */
		mstyle_set_pattern (mstyle, 0);

	/* Change the color for all views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (wbc), view, control,
				  if (control != wbc) color_combo_set_color (
					  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->back_color), c););

	cmd_format (wbc, sheet, mstyle, NULL,
		    _("Set Background Color"));
}

/*
 * Removes the GTK_CAN_FOCUS flag from a container and its children.
 */
static void
disable_focus (GtkWidget *base, void *closure)
{
	if (GTK_IS_CONTAINER (base))
		gtk_container_foreach (GTK_CONTAINER (base), disable_focus, NULL);
	GTK_WIDGET_UNSET_FLAGS (base, GTK_CAN_FOCUS);
}

#ifndef ENABLE_BONOBO
/*
 * Some toolbar items are too damn wide to put into the toolbar
 * if it is vertical.
 */
static void
workbook_format_toolbar_orient (GtkToolbar *toolbar,
				GtkOrientation dir,
				gpointer closure)
{
	WorkbookControlGUI *wbcg = closure;
	GtkWidget *font_button;

	font_button = gnumeric_toolbar_get_widget (toolbar,
						   TOOLBAR_FONT_BUTTON_INDEX);

	if (dir == GTK_ORIENTATION_HORIZONTAL) {
		gtk_widget_show (wbcg->font_name_selector);
		gtk_widget_show (wbcg->font_size_selector);
		gtk_widget_hide (font_button);
	} else {
		gtk_widget_hide (wbcg->font_name_selector);
		gtk_widget_hide (wbcg->font_size_selector);
		gtk_widget_show (font_button);
	}
}
#endif


/****************************************************************************/
/* Border combo box */

static PixmapComboElement border_combo_info[] =
{
    { N_("Left"),			gnm_border_left,		11 },
    { N_("Clear Borders"),		gnm_border_none,		12 },
    { N_("Right"),			gnm_border_right,		13 },

    { N_("All Borders"),		gnm_border_all,			21 },
    { N_("Outside Borders"),		gnm_border_outside,		22 },
    { N_("Thick Outside Borders"),	gnm_border_thick_outside,	23 },

    { N_("Bottom"),			gnm_border_bottom,		31 },
    { N_("Double Bottom"),		gnm_border_double_bottom,	32 },
    { N_("Thick Bottom"),		gnm_border_thick_bottom,	33 },

    { N_("Top and Bottom"),		gnm_border_top_n_bottom,	41 },
    { N_("Top and Double Bottom"),	gnm_border_top_n_double_bottom,	42 },
    { N_("Top and Thick Bottom"),	gnm_border_top_n_thick_bottom,	43 },

    { NULL, NULL}
};

static void
cb_border_changed (PixmapCombo *pixmap_combo, int index, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	StyleBorder *borders [STYLE_BORDER_EDGE_MAX];
	int i;

	/* Init the list */
	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
		borders [i] = NULL;

	switch (index) {
	case 11 : /* left */
		borders [STYLE_BORDER_LEFT] =
		    style_border_fetch (STYLE_BORDER_THIN, style_color_black (),
					style_border_get_orientation (MSTYLE_BORDER_LEFT));
		break;

	case 12 : /* none */
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			borders [i] = style_border_ref (style_border_none ());
		break;

	case 13 : /* right */
		borders [STYLE_BORDER_RIGHT] =
		    style_border_fetch (STYLE_BORDER_THIN, style_color_black (),
					style_border_get_orientation (MSTYLE_BORDER_RIGHT));
		break;

	case 21 : /* all */
		for (i = STYLE_BORDER_HORIZ; i <= STYLE_BORDER_VERT; ++i)
			borders [i] = style_border_fetch (STYLE_BORDER_THIN,
				style_color_black (),
				style_border_get_orientation (i));
		/* fall through */

	case 22 : /* outside */
		for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; ++i)
			borders [i] = style_border_fetch (STYLE_BORDER_THIN,
				style_color_black (),
				style_border_get_orientation (i));
		break;

	case 23 : /* thick_outside */
		for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; ++i)
			borders [i] =
			    style_border_fetch (STYLE_BORDER_THICK, style_color_black (),
						style_border_get_orientation (i));
		break;

	case 41 : /* top_n_bottom */
	case 42 : /* top_n_double_bottom */
	case 43 : /* top_n_thick_bottom */
		borders [STYLE_BORDER_TOP] =
		    style_border_fetch (STYLE_BORDER_THIN, style_color_black (),
					style_border_get_orientation (STYLE_BORDER_TOP));
	    /* Fall through */

	case 31 : /* bottom */
	case 32 : /* double_bottom */
	case 33 : /* thick_bottom */
	{
		int const tmp = index % 10;
		StyleBorderType const t =
		    (tmp == 1) ? STYLE_BORDER_THIN :
		    (tmp == 2) ? STYLE_BORDER_DOUBLE
		    : STYLE_BORDER_THICK;

		borders [STYLE_BORDER_BOTTOM] =
		    style_border_fetch (t, style_color_black (),
					style_border_get_orientation (STYLE_BORDER_BOTTOM));
		break;
	}

	default :
		g_warning ("Unknown border preset selected (%d)", index);
		return;
	}

	cmd_format (WORKBOOK_CONTROL (wbcg), sheet, NULL, borders,
		    _("Set Borders"));
}

void
workbook_create_format_toolbar (WorkbookControlGUI *wbcg)
{
	GtkWidget *fontsel, *fontsize, *entry;
	GtkWidget *border_combo, *back_combo, *fore_combo;
	ColorGroup *cg;

	GList *l;
	int i, len;

#ifndef ENABLE_BONOBO
	GtkWidget *font_button;
	GtkWidget *toolbar = gnumeric_toolbar_new (wbcg,
		workbook_format_toolbar, "FormatToolbar", 2, 0, 0);
#else
	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);
#endif

	/* font name selector */
	fontsel = wbcg->font_name_selector = gtk_combo_text_new (TRUE);
	entry = GTK_COMBO_TEXT (fontsel)->entry;
	g_signal_connect (G_OBJECT (entry),
		"activate",
		G_CALLBACK (change_font_in_selection_cmd), wbcg);
	gtk_combo_box_set_title (GTK_COMBO_BOX (fontsel), _("Font"));
	gtk_container_set_border_width (GTK_CONTAINER (fontsel), 0);

	len = 0;
	for (l = gnumeric_font_family_list; l; l = l->next) {
		if (l->data) {	/* Don't include empty fonts in list. */
			int tmp = gdk_string_measure (gtk_style_get_font (entry->style),
						      l->data);
			if (tmp > len)
				len = tmp;
			gtk_combo_text_add_item(GTK_COMBO_TEXT (fontsel),
						l->data, l->data);
		}
	}
	gtk_widget_set_usize (entry, len, 0);

	/* font size selector */
	fontsize = wbcg->font_size_selector = gtk_combo_text_new (TRUE);
	entry = GTK_COMBO_TEXT (fontsize)->entry;
	g_signal_connect (G_OBJECT (entry),
		"activate",
		G_CALLBACK (change_font_size_in_selection_cmd), wbcg);
	gtk_combo_box_set_title (GTK_COMBO_BOX (fontsize), _("Size"));
	for (i = 0; gnumeric_point_sizes [i] != 0; i++) {
		char buffer [12];
		g_snprintf (buffer, sizeof(buffer),
			    "%d", gnumeric_point_sizes [i]);
		gtk_combo_text_add_item(GTK_COMBO_TEXT (fontsize), buffer, buffer);
	}
	gtk_widget_set_usize (entry,
		gdk_string_measure (gtk_style_get_font (entry->style), "888"), 0);

	/* Border combo box */
	border_combo = pixmap_combo_new (border_combo_info, 3, 4);

	/* default to none */
	pixmap_combo_select_pixmap (PIXMAP_COMBO (border_combo), 1);
	g_signal_connect (G_OBJECT (border_combo),
		"changed",
		G_CALLBACK (cb_border_changed), wbcg);
	disable_focus (border_combo, NULL);
	gtk_combo_box_set_title (GTK_COMBO_BOX (border_combo),
				 _("Borders"));

	/* Create the background colour combo box */
	cg = color_group_fetch ("back_color_group", wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg->back_color = back_combo = color_combo_new (
		gdk_pixbuf_new_from_inline (-1, gnm_bucket, FALSE, NULL),
		_("Clear Background"),
		/* Draw an outline for the default */
		NULL, cg);
	g_signal_connect (G_OBJECT (back_combo),
		"color_changed",
		G_CALLBACK (cb_back_color_changed), wbcg);
	disable_focus (back_combo, NULL);
	gtk_combo_box_set_title (GTK_COMBO_BOX (back_combo),
				 _("Background"));

	/* Sync the color of the background color combo with the other views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (WORKBOOK_CONTROL (wbcg)), view, control,
				  if (control != WORKBOOK_CONTROL (wbcg)) {
					  GdkColor *color = color_combo_get_color (
						  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->back_color), NULL);
					  if (color) {
						  color_combo_set_color (
							  COLOR_COMBO (wbcg->back_color), color);
						  gdk_color_free (color);
						  break;
					  }
				  });

	/* Create the font colour combo box.  */
	cg = color_group_fetch ("fore_color_group", wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg->fore_color = fore_combo = color_combo_new (
		gdk_pixbuf_new_from_inline (-1, gnm_font, FALSE, NULL),
		_("Automatic"), /* Draw black for the default */
		&gs_black, cg);
	g_signal_connect (G_OBJECT (fore_combo),
		"color_changed",
		G_CALLBACK (cb_fore_color_changed), wbcg);
	disable_focus (fore_combo, NULL);
	gtk_combo_box_set_title (GTK_COMBO_BOX (fore_combo),
				 _("Foreground"));

	/* Sync the color of the font color combo with the other views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (WORKBOOK_CONTROL (wbcg)), view, control,
				  if (control != WORKBOOK_CONTROL (wbcg)) {
					  GdkColor *color = color_combo_get_color (
						  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->fore_color), NULL);
					  if (color) {
						  color_combo_set_color (
							  COLOR_COMBO (wbcg->fore_color), color);
						  gdk_color_free (color);
						  break;
					  }
				  });

#ifdef ENABLE_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, fontsel, "/FormatToolbar/FontName");
	gnumeric_inject_widget_into_bonoboui (wbcg, fontsize, "/FormatToolbar/FontSize");
	gnumeric_inject_widget_into_bonoboui (wbcg, border_combo, "/FormatToolbar/BorderSelector");
	gnumeric_inject_widget_into_bonoboui (wbcg, back_combo, "/FormatToolbar/BackgroundColor");
	gnumeric_inject_widget_into_bonoboui (wbcg, fore_combo, "/FormatToolbar/ForegroundColor");
#else
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (toolbar), fontsel, _("Font selector"), NULL, 0);
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (toolbar), fontsize, _("Font Size"), NULL, 1);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		border_combo, _("Borders"), NULL);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		back_combo, _("Background"), NULL);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		fore_combo, _("Foreground"), NULL);

	/* Hide font selector button - only shown in vertical mode */
	font_button = gnumeric_toolbar_get_widget (GTK_TOOLBAR (toolbar),
						   TOOLBAR_FONT_BUTTON_INDEX);
	gtk_widget_hide (font_button);

	/* Handle orientation changes so that we can hide wide widgets */
	g_signal_connect (G_OBJECT(toolbar),
		"orientation-changed",
		G_CALLBACK (&workbook_format_toolbar_orient), wbcg);

	gtk_widget_show (toolbar);
	wbcg->format_toolbar = toolbar;
#endif
}

#ifdef ENABLE_BONOBO
static void
workbook_format_toolbutton_update (WorkbookControlGUI *wbcg,
				   char const *path, gboolean state)
{
	const gchar *new_val = state ? "1" : "0";

	/* Ick,  This should be in bonobo */
	gchar *old_val = bonobo_ui_component_get_prop (wbcg->uic, path,
						       "state", NULL);
	gboolean same = (old_val != NULL && !strcmp (new_val, old_val));
	g_free (old_val);
	if (same)
		return;

	if (wbcg_ui_update_begin (wbcg)) {
		bonobo_ui_component_set_prop (wbcg->uic, path, "state", new_val,
					      NULL);
		wbcg_ui_update_end (wbcg);
	}
}

static void
workbook_format_halign_feedback_set (WorkbookControlGUI *wbcg,
				     StyleHAlignFlags h_align)
{
	workbook_format_toolbutton_update (wbcg, "/commands/AlignLeft",
					   h_align == HALIGN_LEFT);
	workbook_format_toolbutton_update (wbcg, "/commands/AlignCenter",
					   h_align == HALIGN_CENTER);
	workbook_format_toolbutton_update (wbcg, "/commands/AlignRight",
					   h_align == HALIGN_RIGHT);
	workbook_format_toolbutton_update (wbcg, "/commands/CenterAcrossSelection",
					   h_align == HALIGN_CENTER_ACROSS_SELECTION);
}
#else
static void
workbook_format_toolbutton_update (WorkbookControlGUI *wbcg,
				   GtkToolbar *toolbar,
				   int const button_index,
				   gboolean const state)
{
	GtkWidget *w = gnumeric_toolbar_get_widget (toolbar, button_index);
	GtkToggleButton *tb = GTK_TOGGLE_BUTTON (w);

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_toggle_button_set_active (tb, state);
		wbcg_ui_update_end (wbcg);
	}
}

static void
workbook_format_halign_feedback_set (WorkbookControlGUI *wbcg,
				     StyleHAlignFlags h_align)
{
	GtkToolbar *toolbar = GTK_TOOLBAR (wbcg->format_toolbar);

	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_ALIGN_LEFT_BUTTON_INDEX,
					   h_align == HALIGN_LEFT);
	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_ALIGN_CENTER_BUTTON_INDEX,
					   h_align == HALIGN_CENTER);
	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_ALIGN_RIGHT_BUTTON_INDEX,
					   h_align == HALIGN_RIGHT);
	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_CENTER_ACROSS_SELECTION_INDEX,
					   h_align == HALIGN_CENTER_ACROSS_SELECTION);
}
#endif

/*
 * Updates the edit control state: bold, italic, font name and font size
 */
void
workbook_feedback_set (WorkbookControlGUI *wbcg)
{
#ifndef ENABLE_BONOBO
	GtkToolbar *toolbar;
#endif
	MStyle 		*style;
	GtkComboText    *fontsel;
	GtkComboText    *fontsize;
	char             size_str [40];
	WorkbookView	*wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (wb_view != NULL);

	style = wb_view->current_format;

	g_return_if_fail (style != NULL);
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_BOLD));
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC));
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE));
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_ALIGN_H));
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_SIZE));
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_NAME));

#ifndef ENABLE_BONOBO
	toolbar = GTK_TOOLBAR (wbcg->format_toolbar);
	g_return_if_fail (toolbar);

	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_BOLD_BUTTON_INDEX,
					   mstyle_get_font_bold (style));
	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_ITALIC_BUTTON_INDEX,
					   mstyle_get_font_italic (style));
	workbook_format_toolbutton_update (wbcg, toolbar,
					   TOOLBAR_UNDERLINE_BUTTON_INDEX,
					   mstyle_get_font_uline (style) == UNDERLINE_SINGLE);
#else
	workbook_format_toolbutton_update (wbcg, "/commands/FontBold",
					   mstyle_get_font_bold (style));
	workbook_format_toolbutton_update (wbcg, "/commands/FontItalic",
					   mstyle_get_font_italic (style));
	workbook_format_toolbutton_update (wbcg, "/commands/FontUnderline",
					   mstyle_get_font_uline (style) == UNDERLINE_SINGLE);
#endif

	workbook_format_halign_feedback_set (wbcg, mstyle_get_align_h (style));

	fontsize = GTK_COMBO_TEXT (wbcg->font_size_selector);
	g_snprintf (size_str, sizeof(size_str), "%d", (int)mstyle_get_font_size (style));
	if (wbcg_ui_update_begin (wbcg)) {
		gtk_combo_text_set_text (fontsize, size_str);
		wbcg_ui_update_end (wbcg);
	}

	fontsel = GTK_COMBO_TEXT (wbcg->font_name_selector);
	if (wbcg_ui_update_begin (wbcg)) {
		gtk_combo_text_set_text (fontsel, mstyle_get_font_name (style));
		wbcg_ui_update_end (wbcg);
	}
}
