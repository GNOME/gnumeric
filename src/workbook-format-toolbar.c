/*
 * workbook-format-toolbar.c: Format toolbar implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1998, 1999 Miguel de Icaza.
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"
#include "selection.h"
#include "workbook-format-toolbar.h"
#include "global-gnome-font.h"
#include "widgets/widget-color-combo.h"
#include "widgets/gnumeric-toolbar.h"
#include "workbook-private.h"
#include "format.h"

/*
 * Pixmaps
 */
#include "pixmaps/money.xpm"
#include "pixmaps/percent.xpm"
#include "pixmaps/thousands.xpm"
#include "pixmaps/add_decimals.xpm"
#include "pixmaps/remove_decimals.xpm"
#include "pixmaps/font.xpm"
#include "pixmaps/bucket.xpm"

static const char *money_format   = "Default Money Format:$#,##0_);($#,##0)";
static const char *percent_format = "Default Percent Format:0.00%";

static void
set_selection_halign (Workbook *wb, StyleHAlignFlags align)
{
	MStyle *mstyle;
	Sheet  *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_unant (sheet);

	mstyle = mstyle_new ();
	mstyle_set_align_h (mstyle, align);
	
	sheet_selection_apply_style (sheet, mstyle);
}

static void
left_align_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_LEFT);
}

static void
right_align_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_RIGHT);
}

static void
center_cmd (GtkWidget *widget, Workbook *wb)
{
	set_selection_halign (wb, HALIGN_CENTER);
}

/*
 * change_selection_font
 * @wb:  The workbook to operate on
 * @bold: -1 to leave unchanged, 0 to clear, 1 to set
 * @italic: -1 to leave unchanged, 0 to clear, 1 to set
 *
 */
static void
change_selection_font (Workbook *wb, int bold, int italic)
{
	MStyle *mstyle;
	Sheet  *sheet;

	sheet = workbook_get_current_sheet (wb);
	sheet_selection_unant (sheet);

	if (bold >= 0) {
		mstyle = mstyle_new ();
		mstyle_set_font_bold (mstyle, bold);
		sheet_selection_apply_style (sheet, mstyle);
	}

	if (italic >= 0) {
		mstyle = mstyle_new ();
		mstyle_set_font_italic (mstyle, italic);
		sheet_selection_apply_style (sheet, mstyle);
	}
}

static void
bold_cmd (GtkToggleButton *t, Workbook *wb)
{
	change_selection_font (wb, t->active, -1);
}

static void
italic_cmd (GtkToggleButton *t, Workbook *wb)
{
	change_selection_font (wb, -1, t->active);
}

static void
change_font_in_selection_cmd (GtkMenuItem *item, Workbook *wb)
{
	Sheet *sheet;
	const char *font_name = gtk_object_get_user_data (GTK_OBJECT (item));
	MStyle *mstyle;

	wb->priv->current_font_name = font_name;
	
	sheet = workbook_get_current_sheet (wb);

	mstyle = mstyle_new ();
	mstyle_set_font_name (mstyle, font_name);
	sheet_selection_apply_style (sheet, mstyle);
}

static void
change_font_size_in_selection_cmd (GtkEntry *entry, Workbook *wb)
{
	Sheet  *sheet = workbook_get_current_sheet (wb);
	MStyle *mstyle;
	double size;

	size = atof (gtk_entry_get_text (entry));
	if (size < 0.0) {
		gtk_entry_set_text (entry, "12");
		return;
	}

	mstyle = mstyle_new ();
	mstyle_set_font_size (mstyle, size);
	
	sheet_selection_apply_style (sheet, mstyle);
	sheet_selection_height_update (sheet, size);
	workbook_focus_current_sheet (sheet->workbook);
}

static void
do_sheet_selection_apply_style (Sheet *sheet, const char *format)
{
	const char   *real_format = strchr (_(format), ':');
	MStyle *mstyle;

	if (real_format)
		real_format++;
	else
		return;

	mstyle = mstyle_new ();
	mstyle_set_format (mstyle, real_format);

	sheet_selection_apply_style (sheet, mstyle);
}

static void
workbook_cmd_format_as_money (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	
	do_sheet_selection_apply_style (sheet, _(money_format));
}

static void
workbook_cmd_format_as_percent (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = workbook_get_current_sheet (wb);
	
	do_sheet_selection_apply_style (sheet, _(percent_format));
}

/*
 * The routines that modify the format of a cell using the
 * helper routines in format.c.
 */
typedef char *(*format_modify_fn) (const char *format);
	
static Value *
modify_cell_format (Sheet *sheet, int col, int row, Cell *cell, void *closure)
{
	MStyle *mstyle = sheet_style_compute (sheet, col, row);
	StyleFormat *sf = mstyle_get_format (mstyle);
	format_modify_fn modify_format = closure;
	char *new_fmt;
		
	new_fmt = (*modify_format) (sf->format);
	if (new_fmt == NULL)
		return NULL;

	cell_set_format (cell, new_fmt);
	g_free (new_fmt);
	mstyle_unref (mstyle);
	return NULL;
}

static void
modify_cell_region (Sheet *sheet,
		    int start_col, int start_row,
		    int end_col,   int end_row,
		    void *closure)
{
	sheet_cell_foreach_range (
		sheet, TRUE,
		start_col, start_row, end_col, end_row,
		modify_cell_format, closure);
}

/*
 * This is sort of broken, it just operates on the
 * existing cells, rather than the empty spots.
 * To do: think if it is worth doing.
 *
 * Ie, the user could set and set styles, and work on them
 * with no visible cell.  Does it matter?
 */
static void
do_modify_format (Workbook *wb, format_modify_fn modify_fn)
{
	Sheet *sheet = workbook_get_current_sheet (wb);

	selection_apply (sheet, modify_cell_region, FALSE, modify_fn);
	sheet_set_dirty (sheet, TRUE);
}

static void
workbook_cmd_format_add_thousands (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_add_thousand);
}

static void
workbook_cmd_format_add_decimals (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_add_decimal);
}

static void
workbook_cmd_format_remove_decimals (GtkWidget *widget, Workbook *wb)
{
	do_modify_format (wb, format_remove_decimal);
}

static GnomeUIInfo workbook_format_toolbar [] = {
	/* Placeholder: font selector */
        /* Placeholder: size selector */
	
	{ GNOME_APP_UI_TOGGLEITEM, N_("Bold"), N_("Sets the bold font"),
	  bold_cmd, NULL, NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_BOLD },

	{ GNOME_APP_UI_TOGGLEITEM, N_("Italic"), N_("Makes the font italic"),
	  italic_cmd, NULL, NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_PIXMAP_TEXT_ITALIC },

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Left align"), N_("Left justifies the cell contents"),
		left_align_cmd, GNOME_STOCK_PIXMAP_ALIGN_LEFT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Center"), N_("Centers the cell contents"),
		center_cmd, GNOME_STOCK_PIXMAP_ALIGN_CENTER),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Right align"), N_("Right justifies the cell contents"),
		right_align_cmd, GNOME_STOCK_PIXMAP_ALIGN_RIGHT),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_DATA (
		N_("Money format"), N_("Sets the format of the selected cells to monetary"),
		workbook_cmd_format_as_money, NULL, money_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Percent"), N_("Sets the format of the selected cells to percentage"),
		workbook_cmd_format_as_percent, NULL, percent_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Thousand separator"), N_("Sets the format of the selected cells to include a thousands separator"),
		workbook_cmd_format_add_thousands, NULL, thousands_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Add decimals"), N_("Increases the number of decimal numbers displayed"),
		workbook_cmd_format_add_decimals, NULL, add_decimals_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Remove decimals"), N_("Decreases the number of decimal numbers displayed"),
		workbook_cmd_format_remove_decimals, NULL, remove_decimals_xpm),

	GNOMEUIINFO_END
};

static void
fore_color_changed (ColorCombo *cc, GdkColor *color, int color_index, Workbook *wb)
{
	Sheet  *sheet  = workbook_get_current_sheet (wb);
	MStyle *mstyle = mstyle_new ();

	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, 
			  style_color_new (color->red, color->green, color->blue));

	sheet_selection_apply_style (sheet, mstyle);
}

static void
back_color_changed (ColorCombo *cc, GdkColor *color, int color_index, Workbook *wb)
{
	Sheet  *sheet = workbook_get_current_sheet (wb);
	MStyle *mstyle = mstyle_new ();

	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK, 
			  style_color_new (color->red, color->green, color->blue));

	sheet_selection_apply_style (sheet, mstyle);
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

GtkWidget *
workbook_create_format_toolbar (Workbook *wb)
{
	GtkWidget *menu, *item, *toolbar;
	const char *name = "FormatToolbar";
	GList *l;
	int len;
	
	toolbar = gnumeric_toolbar_new (
		workbook_format_toolbar, wb);

	gnome_app_add_toolbar (
		GNOME_APP (wb->toplevel),
		GTK_TOOLBAR (toolbar),
		name,
		GNOME_DOCK_ITEM_BEH_NORMAL,
		GNOME_DOCK_TOP, 2, 0, 0);

	/*
	 * Create a font name selector
	 */
	wb->priv->option_menu = gtk_option_menu_new ();
	gtk_container_set_border_width (GTK_CONTAINER (wb->priv->option_menu), 0);
	menu = gtk_menu_new ();

	/* An empty item for the case of no font that applies */
	item = gtk_menu_item_new_with_label ("");
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);
	
	for (l = gnumeric_font_family_list; l; l = l->next){
		item = gtk_menu_item_new_with_label (l->data);
		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			change_font_in_selection_cmd, wb);
		gtk_object_set_user_data (
			GTK_OBJECT (item), l->data);
	}
	gtk_option_menu_set_menu (GTK_OPTION_MENU (wb->priv->option_menu), menu);
	gtk_widget_show (wb->priv->option_menu);

	gtk_toolbar_insert_widget (
		GTK_TOOLBAR (toolbar), wb->priv->option_menu,
		_("Font selector"), NULL, 0);

	/*
	 * Create the font size control
	 */
	wb->priv->size_widget = gtk_entry_new ();
	gtk_widget_show (wb->priv->size_widget);

	len = gdk_string_measure (wb->priv->size_widget->style->font, "000000");
	gtk_widget_set_usize (GTK_WIDGET (wb->priv->size_widget), len, 0);
	gtk_signal_connect (
		GTK_OBJECT (wb->priv->size_widget), "activate",
		GTK_SIGNAL_FUNC (change_font_size_in_selection_cmd), wb);
		
	gtk_toolbar_insert_widget (
		GTK_TOOLBAR (toolbar),
		wb->priv->size_widget, _("Size"), NULL, 1);

	/*
	 * Create the combo boxes
	 */
	wb->priv->fore_combo = color_combo_new (font_xpm);
	gtk_widget_show (wb->priv->fore_combo);
	gtk_signal_connect (GTK_OBJECT (wb->priv->fore_combo), "changed",
			    GTK_SIGNAL_FUNC (fore_color_changed), wb);
	disable_focus (wb->priv->fore_combo, NULL);

	wb->priv->back_combo = color_combo_new (bucket_xpm);
	color_combo_select_color (COLOR_COMBO (wb->priv->back_combo), 1);
	gtk_widget_show (wb->priv->back_combo);
	gtk_signal_connect (GTK_OBJECT (wb->priv->back_combo), "changed",
			    GTK_SIGNAL_FUNC (back_color_changed), wb);
	disable_focus (wb->priv->back_combo, NULL);
	
	gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));
	
	gtk_toolbar_append_widget (
		GTK_TOOLBAR (toolbar),
		wb->priv->fore_combo, _("Foreground"), NULL);

	gtk_toolbar_append_widget (
		GTK_TOOLBAR (toolbar),
		wb->priv->back_combo, _("Background"), NULL);

	return toolbar;
}

/*
 * Updates the edit control state: bold, italic, font name and font size
 */
void
workbook_feedback_set (Workbook *workbook, MStyle *style)
{
	GtkToggleButton *t;
	GnumericToolbar *toolbar = GNUMERIC_TOOLBAR (workbook->priv->format_toolbar);
	gboolean         font_set;
	char             size_str [40];

	g_return_if_fail (style != NULL);
	g_return_if_fail (workbook != NULL);
	g_return_if_fail (IS_WORKBOOK (workbook));

	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_BOLD));
	t = GTK_TOGGLE_BUTTON (
		gnumeric_toolbar_get_widget (
			toolbar,
			TOOLBAR_BOLD_BUTTON_INDEX));
	
	gtk_signal_handler_block_by_func (GTK_OBJECT (t),
					  (GtkSignalFunc)&bold_cmd,
					  workbook);
	gtk_toggle_button_set_active (t, mstyle_get_font_bold (style));
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (t),
					    (GtkSignalFunc)&bold_cmd,
					    workbook);

	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC));
	t = GTK_TOGGLE_BUTTON (
		gnumeric_toolbar_get_widget (
			toolbar,
			TOOLBAR_ITALIC_BUTTON_INDEX));
	
	gtk_signal_handler_block_by_func (GTK_OBJECT (t),
					  (GtkSignalFunc)&italic_cmd,
					  workbook);
	gtk_toggle_button_set_active (t, mstyle_get_font_italic (style));
	gtk_signal_handler_unblock_by_func (GTK_OBJECT (t),
					    (GtkSignalFunc)&italic_cmd,
					    workbook);

	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_SIZE));
	sprintf (size_str, "%g", mstyle_get_font_size (style));
	gtk_entry_set_text (GTK_ENTRY (workbook->priv->size_widget),
			    size_str);

	/*
	 * hack: we try to find the key "gnumeric-index" in the
	 * GnomeFont object, this key represents the index of the
	 * font name on the Optionmenu we have.
	 *
	 * If this is not set, then we compute it
	 */
	font_set = FALSE;
	g_return_if_fail (mstyle_is_element_set (style, MSTYLE_FONT_NAME));
	{
		const char *font_name = mstyle_get_font_name (style);
		void *np;
		
		workbook->priv->current_font_name = font_name;

		/*
		 * We need the cache again sometime for performance on
		 * systems with lots of fonts.
		 */
/*		np = gtk_object_get_data ((GtkObject *)font, "gnumeric-idx");
		if (np == NULL)*/ {
			GList *l;
			int idx = 0;
 
			for (l = gnumeric_font_family_list; l; l = l->next, idx++) {
				char *f = l->data;
				
				if (strcmp (f, font_name) == 0) {
					np = GINT_TO_POINTER (idx);
/*					gtk_object_set_data ((GtkObject *) font,
					"gnumeric-idx", np);*/
					break;
				}
			}
		}
		/*
		 * +1 means, skip over the "undefined font" element
		 */
		gtk_option_menu_set_history (
			GTK_OPTION_MENU (workbook->priv->option_menu),
			GPOINTER_TO_INT (np)+1);
		font_set = TRUE;
	}
	if (!font_set)
		gtk_option_menu_set_history (
			GTK_OPTION_MENU (workbook->priv->option_menu), 0);
}
