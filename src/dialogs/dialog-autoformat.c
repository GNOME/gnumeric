/*
 * dialog-autoformat.c : implementation of the autoformat dialog
 *
 * Author : Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
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
 */

/*
 * WORKING NOTE : Once the edit dialog is ready, search for FIXME and
 * remove the disabling of new/edit/remove buttons!
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <mstyle.h>
#include <style-border.h>
#include <value.h>
#include <preview-grid.h>
#include <format-template.h>
#include <file-autoft.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <commands.h>
#include <selection.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <math.h>
#include <stdlib.h>

#define GLADE_FILE "autoformat.glade"

/*
 * Table to show for
 * previews, please don't make this larger than 5x5
 */
#define PREVIEW_ROWS 5
#define PREVIEW_COLS 5

/*
 * The number of previews
 */
#define NUM_PREVIEWS 6

/*
 * Col widths/row heights
 */
#define DEFAULT_COL_WIDTH  42
#define DEFAULT_ROW_HEIGHT 16
/* Keep these strings very short.
   They are used as a sample data for a sheet, so you can put anything here
   ("One", "Two", "Three" for example) */
static const char*
demotable[PREVIEW_ROWS][PREVIEW_COLS] = {
	{ N_(" ")    , N_("Jan"), N_("Feb"), N_("Mrt"), N_("Total") },
	{ N_("North"),   N_("6"),  N_("13"),  N_("20"),    N_("39") },
	{ N_("South"),  N_("12"),   N_("4"),  N_("17"),    N_("33") },
	{ N_("West") ,   N_("8"),   N_("2"),   N_("0"),    N_("10") },
	{ N_("Total"),  N_("26"),  N_("19"),  N_("37"),    N_("81") }
};

typedef struct {
	Workbook           *wb;                              /* Workbook we are working on */
	WorkbookControlGUI *wbcg;
	PreviewGrid        *grid[NUM_PREVIEWS];              /* Previewgrid's */
	GnomeCanvasRect    *rect[NUM_PREVIEWS];              /* Centering rectangles */
	GnomeCanvasRect    *selrect;                         /* Selection rectangle */
	GSList             *templates;                       /* List of FormatTemplate's */
	FormatTemplate     *selected_template;               /* The currently selected template */
	GList              *category_groups;                 /* List of groups of categories */

	FormatTemplateCategoryGroup *current_category_group; /* Currently selected category group */
	
	int               preview_top;       /* Top index of the previewlist */
	int               preview_index;     /* Selected canvas in previewlist */
	gboolean          previews_locked;   /* If true, the preview_free and preview_load will not function */
	gboolean          more_down;         /* If true, more was clicked and the button caption is now 'Less' */
	gboolean          canceled;          /* True if the user pressed cancel */

	/*
	 * Gui elements
	 */
	GtkDialog      *dialog;

	GtkCombo       *category;

	GnomeCanvas      *canvas[NUM_PREVIEWS];
	GtkFrame         *frame[NUM_PREVIEWS];
	GtkVScrollbar    *scroll;
	GtkCheckMenuItem *gridlines;

	GtkMenuItem    *new, *edit, *remove_current;

	GtkEntry       *info_name, *info_author, *info_cat;
	GtkTextView    *info_descr;

	GtkCheckMenuItem *number, *border, *font, *patterns, *alignment;

	GtkButton      *ok, *cancel;

	GtkTooltips    *tooltips;
} AutoFormatInfo;

/********************************************************************************
 * CALLBACKS FOR PREVIEW GRID
 ********************************************************************************/

static MStyle *
cb_get_cell_style (PreviewGrid *pg, int row, int col, FormatTemplate *ft)
{
	/*
	 * If this happens to be NULL the default style
	 * will automatically be used.
	 */
	return format_template_get_style (ft, row, col);
}

static Value *
cb_get_cell_value (PreviewGrid *pg, int row, int col, gpointer data)
{
	Value *value;
	const char *text;

	if (row >= PREVIEW_ROWS || col >= PREVIEW_COLS)
		return NULL;

	text = _(demotable[row][col]);

       /*
        * Determine if the text to display is a number.  If so,
        * set the value as number for proper alignment
        */
       {
               char *endptr = NULL;
               double tmp;

               tmp = g_strtod (text, &endptr);

               /*
                * String is only a valid number if *endptr equals \0
                */
               if (*endptr == '\0')
                       value = value_new_float (tmp);
               else
                       value = value_new_string (text);
       }

       return value;
}

/********************************************************************************
 * UTILITY FUNCTIONS
 ********************************************************************************/

/**
 * templates_free:
 * @info: AutoFormatInfo
 *
 * This function will free all templates currently in memory
 * (previously loaded with templates_load)
 **/
static void
templates_free (AutoFormatInfo *info)
{
	GSList *iterator;

	g_return_if_fail (info != NULL);

	iterator = info->templates;

	while (iterator) {
		FormatTemplate *ft = iterator->data;

		format_template_free (ft);

		iterator = g_slist_next (iterator);
	}
	g_slist_free (info->templates);

	info->templates = NULL;
}

/**
 * templates_load:
 * @info: AutoFormatInfo
 *
 * This function will load the templates in the currently selected
 * category group (it looks at info->category_groups to determine the selection)
 *
 * Return value: TRUE if all went well, FALSE otherwise.
 **/
static gboolean
templates_load (AutoFormatInfo *info)
{
	GSList *l;
	gint n_templates;

	g_assert (info != NULL);

	if (info->category_groups == NULL)
		return FALSE;

	info->templates = category_group_get_templates_list (
		info->current_category_group, COMMAND_CONTEXT (info->wbcg));
	for (l = info->templates; l != NULL; l = l->next)
		format_template_set_size ((FormatTemplate *) l->data, 0, 0,
					  PREVIEW_COLS - 1, PREVIEW_ROWS - 1);
	n_templates = g_slist_length (info->templates);

	/*
	 * We need to temporary lock the preview loading/freeing or
	 * else our scrollbar will trigger an event (value_changed) and create the
	 * previews. (which we don't want to happen at this moment)
	 */
	info->previews_locked = TRUE;
	{
		GtkAdjustment *adjustment = gtk_range_get_adjustment (GTK_RANGE (info->scroll));

		adjustment->value = 0;
		adjustment->lower = 0;
		adjustment->upper = n_templates / 2;
		adjustment->step_increment = 1;
		adjustment->page_increment = 3;
		adjustment->page_size = 3;

		gtk_adjustment_changed (adjustment);
	}
	info->previews_locked = FALSE;

	/*
	 * Hide the scrollbar when it's not needed
	 */
	if (n_templates > NUM_PREVIEWS)
		gtk_widget_show (GTK_WIDGET (info->scroll));
	else
		gtk_widget_hide (GTK_WIDGET (info->scroll));

	return TRUE;
}

/**
 * previews_free:
 * @info: AutoFormatInfo
 *
 * This function will free all previews.
 **/
static void
previews_free (AutoFormatInfo *info)
{
	int i;

	if (info->previews_locked)
		return;

	if (info->selrect)
		gtk_object_destroy (GTK_OBJECT (info->selrect));
	info->selrect = NULL;
	
	for (i = 0; i < NUM_PREVIEWS; i++) {
		if (info->grid[i]) {
			gtk_layout_freeze (GTK_LAYOUT (info->canvas[i]));
			
			gtk_object_destroy (GTK_OBJECT (info->rect[i]));
			gtk_object_destroy (GTK_OBJECT (info->grid[i]));
			info->rect[i] = NULL;
			info->grid[i] = NULL;
		}
	}
}

/**
 * previews_load:
 * @info: AutoFormatInfo
 * @topindex: The index of the template to be displayed in the upper left corner
 *
 * This function will create grids and rects for each canvas and associate
 * them with the right format templates.
 * NOTE : if info->preview_locked is TRUE this function will do nothing,
 *        this is handy in situation where signals can cause previews_load to be
 *        called before previews_free.
 **/
static void
previews_load (AutoFormatInfo *info, int topindex)
{
	GSList *iterator, *start;
	int i, count = topindex;

	g_return_if_fail (info != NULL);

	if (info->previews_locked)
		return;

	iterator = info->templates;
	start = iterator;
	while (iterator && count > 0) {
		iterator = g_slist_next (iterator);
		start = iterator;

		count--;
	}

	for (i = 0; i < NUM_PREVIEWS; i++) {
		if (start == NULL) {
			/*
			 * Hide the canvas and make sure label is empty and frame is invisible
			 */
			gtk_widget_hide (GTK_WIDGET (info->canvas[i]));
			gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_NONE);
		} else {
			FormatTemplate *ft = start->data;

			/*
			 * This rect is used to properly center on the canvas. It covers the whole canvas area.
			 * Currently the canvas shifts the (0,0) 4,5 pixels downwards in vertical and horizontal
			 * directions. So we need (-4.5, -4.5) as the absolute top coordinate and (215.5, 85.5) for
			 * the absolute bottom of the canvas's region. Look at src/dialogs/autoformat.glade for
			 * the original canvas dimensions (look at the scrolledwindow that houses each canvas)
			 */
			info->rect[i] = GNOME_CANVAS_RECT (
				gnome_canvas_item_new (gnome_canvas_root (info->canvas[i]),
						       gnome_canvas_rect_get_type (),
						       "x1", -4.5, "y1", -4.5,
						       "x2", 215.5, "y2", 85.5,
						       "width_pixels", (int) 0,
						       "fill_color", NULL,
						       NULL));

			/* Setup grid */
			gtk_layout_freeze (GTK_LAYOUT (info->canvas[i]));
			info->grid[i] = PREVIEW_GRID (
				gnome_canvas_item_new (gnome_canvas_root (info->canvas[i]),
						       preview_grid_get_type (),
						       "RenderGridlines", info->gridlines->active,
						       "DefaultRowHeight", DEFAULT_ROW_HEIGHT,
						       "DefaultColWidth", DEFAULT_COL_WIDTH,
						       NULL));

			gtk_signal_connect (GTK_OBJECT (info->grid[i]), "get_cell_style",
					    GTK_SIGNAL_FUNC (cb_get_cell_style), ft);
			gtk_signal_connect (GTK_OBJECT (info->grid[i]), "get_cell_value",
					    GTK_SIGNAL_FUNC (cb_get_cell_value), ft);

			/* Are we selected? Then draw a selection rectangle */
			if (topindex + i == info->preview_index) {
				g_return_if_fail (info->selrect == NULL);
				
				info->selrect = GNOME_CANVAS_RECT (
					gnome_canvas_item_new (gnome_canvas_root (info->canvas[i]),
							       gnome_canvas_rect_get_type (),
							       "x1", -7.0, "y1", -2.5,
							       "x2", 219.0, "y2", 84.5,
							       "width_pixels", (int) 2,
							       "outline_color", "red",
							       "fill_color", NULL,
							       NULL));
				gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_IN);
			} else
				gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_OUT);
			
			gnome_canvas_set_scroll_region (info->canvas[i], 0, 0,
							PREVIEW_COLS * DEFAULT_COL_WIDTH,
							PREVIEW_ROWS * DEFAULT_ROW_HEIGHT);

			{
				char *name = format_template_get_name (ft);

				gtk_tooltips_set_tip (info->tooltips,
						      GTK_WIDGET (info->canvas[i]),
						      name, "");
				g_free (name);
			}
			
			/*
			 * Make sure the canvas is visible
			 */
			gtk_widget_show (GTK_WIDGET (info->canvas[i]));
			start = g_slist_next (start);
		}
	}

	info->preview_top = topindex;

	for (i = 0; i < NUM_PREVIEWS; i++)
		gtk_layout_thaw (GTK_LAYOUT (info->canvas[i]));
}

/********************************************************************************
 * SIGNAL HANDLERS
 ********************************************************************************/

/**
 * cb_ok_clicked:
 * @button:
 * @data:
 *
 * Called-back when user presses the OK button,
 * throws us out of the gtk main loop.
 **/
static void
cb_ok_clicked (GtkButton *button, AutoFormatInfo *info)
{
	info->canceled = FALSE;
	gtk_main_quit ();
}

/**
 * cb_cancel_clicked:
 * @button:
 * @data:
 *
 * Called-back when user presses the CANCEL button
 * throws us out of the gtk main loop and sets the canceled
 * indicator.
 **/
static void
cb_cancel_clicked (GtkButton *button, AutoFormatInfo *info)
{
	info->canceled = TRUE;
	gtk_main_quit ();
}

/**
 * cb_dialog_close:
 * @dialog:
 * @info:
 *
 * This callback does exacly the same as cb_cancel_clicked. It
 * is attached to dialogs close handler, (when the user presses
 * the close button in the window frame or in the window menu)
 *
 * Return value: always TRUE indicating that we have closed the dialog.
 **/
static int
cb_dialog_close (GtkDialog *dialog, AutoFormatInfo *info)
{
	info->canceled = TRUE;
	gtk_main_quit ();

	return TRUE;
}

/**
 * cb_new_activated:
 * @button:
 * @info:
 *
 * Invoked when the new menu item is clicked, this will pop-up the
 * edit dialog and allow the user to create a new format template
 * on the fly :-)
 **/
static void
cb_new_activated (GtkMenuItem *item, AutoFormatInfo *info)
{
	g_warning ("Not implemented yet");
}

/**
 * cb_edit_activated:
 * @button:
 * @info:
 *
 * Called when the edit menu item is pressed, this will pop-up
 * the edit dialog which allows the user to edit the currently
 * selected template.
 **/
static void
cb_edit_activated (GtkMenuItem *item, AutoFormatInfo *info)
{
	g_warning ("Not implemented yet");

	/*
	dialog_autoformat_edit (info->wb, info->selected_template);
	format_template_save (info->selected_template);
	*/
}

/**
 * cb_remove_current_activated:
 * @button:
 * @info:
 *
 * This pops up a dialog asking the user if he/she wants to
 * remove the currently selected template.
 **/
static void
cb_remove_current_activated (GtkMenuItem *item, AutoFormatInfo *info)
{
	GtkWidget *dialog;
	GtkWidget *no_button;
	gint ret;
	gchar *question;

	question = g_strdup_printf (_("Are you sure you want to remove the template '%s' ?"), info->selected_template->name->str);

	dialog = gnome_question_dialog_parented (question, NULL,NULL, GTK_WINDOW (info->dialog));

	no_button = g_list_last (GNOME_DIALOG (dialog)->buttons)->data;
	gtk_widget_grab_focus (no_button);
	ret = gnome_dialog_run (GNOME_DIALOG (dialog));

	g_free (question);

	if (ret == 0) {
		if (unlink (info->selected_template->filename->str) != 0) {
			GtkWidget *edialog;

			edialog = gnome_error_dialog_parented (_("Could not remove template"),
			                                       GTK_WINDOW (info->dialog));
			gnome_dialog_run (GNOME_DIALOG (edialog));
		} else {
			gtk_signal_emit_by_name (GTK_OBJECT (info->category->popwin), "hide", info);
		}
	}
}

/**
 * cb_scroll_value_changed:
 * @adjustment:
 * @info:
 *
 * Invoked when the scrollbar is slid, simply changes the topindex for the previews
 * and reloads the previews.
 **/
static void
cb_scroll_value_changed (GtkAdjustment *adjustment, AutoFormatInfo *info)
{
	int val;

	previews_free (info);

	val = rint (adjustment->value) * 2;

	previews_load (info, val);
}

/**
 * cb_canvas_button_release:
 * @canvas:
 * @event:
 * @info:
 *
 * Handles a click on one of the six canvases.
 * It will change the GtkFrame surrounding the selected canvas
 * and update the Template Information too.
 *
 * Return value: TRUE on success or FALSE otherwise.
 **/
static gboolean
cb_canvas_button_release (GnomeCanvas *canvas, GdkEventButton *event, AutoFormatInfo *info)
{
	int index = 0;

	/*
	 * Find out which canvas we are
	 */
	while (canvas != info->canvas[index] && index < NUM_PREVIEWS) {

		index++;
	}

	if (index == NUM_PREVIEWS) {
		g_warning ("Canvas click detected, but no associated index could be determined");
		return FALSE;
	}

	info->preview_index = info->preview_top + index;

	/*
	 * Reload the previews
	 */
	previews_free (info);
	previews_load (info, info->preview_top);

	/*
	 * Set the template information
	 */
	{
		FormatTemplate *ft;
		GSList *iterator = info->templates;
		int dummy;
		char *name, *author, *descr;
		FormatTemplateCategory *category;

		index = 0;
		while (iterator != NULL) {

			if (index == info->preview_index)
				break;

			iterator = g_slist_next (iterator);
			index++;
		}

		if (iterator == NULL || iterator->data == NULL) {

			g_warning ("Error while trying to retrieve template information");
			return FALSE;
		}

		ft = iterator->data;

		name   = format_template_get_name (ft);
		author = format_template_get_author (ft);
		descr  = format_template_get_description (ft);
		category  = format_template_get_category (ft);

		gtk_entry_set_text (info->info_name, name);
		gtk_entry_set_text (info->info_author, author);
		gtk_entry_set_text (info->info_cat, category->name);
		gtk_editable_delete_text (GTK_EDITABLE (info->info_descr), 0, -1);

		dummy = 0;
		gtk_editable_insert_text (GTK_EDITABLE (info->info_descr), descr,
					  strlen (descr), &dummy);

		g_free (name);
		g_free (author);
		g_free (descr);

		gtk_widget_set_sensitive (GTK_WIDGET (info->remove_current), category->is_writable);

		info->selected_template = ft;
	}

	return TRUE;
}

/**
 * cb_apply_item_toggled:
 * @item:
 * @info:
 *
 * This callback is invoked when one of the (6) apply items is toggled.
 * It will change the filter for each FormatTemplate. This way certain elements
 * can be filtered out (like the background, border, etc..)
 **/
static void
cb_apply_item_toggled (GtkCheckMenuItem *item, AutoFormatInfo *info)
{
	GSList *iterator = info->templates;
	int i;

	while (iterator) {
		format_template_set_filter ((FormatTemplate *) iterator->data,
					    info->number->active,
					    info->border->active,
					    info->font->active,
					    info->patterns->active,
					    info->alignment->active);

		iterator = g_slist_next (iterator);
	}

	for (i = 0; i < NUM_PREVIEWS; i++)
		gnome_canvas_request_redraw (info->canvas[i], INT_MIN, INT_MIN,
					     INT_MAX/2, INT_MAX/2);
}

/**
 * cb_category_popwin_hide:
 * @widget:
 * @info:
 *
 * Invoked when a category is selected in the category list, this will quickly load
 * all templates in the newly selected category, load the previews and select
 * the first one by default.
 * If there are no templates in the newly selected category the "OK", "Edit" and "Remove"
 * menu items will be disable and the Template Information cleared.
 **/
static void
cb_category_popwin_hide (GtkWidget *widget, AutoFormatInfo *info)
{
	info->current_category_group = category_group_list_find_category_by_name(
	                               info->category_groups,
	                               gtk_entry_get_text (GTK_ENTRY (info->category->entry)));

	previews_free (info);
	templates_free (info);

	if (templates_load (info) == FALSE) {
		g_warning ("Error while loading templates!");
	}

	if (info->current_category_group->description != NULL) {
		gtk_tooltips_set_tip (info->tooltips, GTK_WIDGET (info->category->entry), info->current_category_group->description, "");
	} else {
		gtk_tooltips_set_tip (info->tooltips, GTK_WIDGET (info->category->entry), info->current_category_group->name, "");
	}

	previews_load (info, 0);

	/*
	 * Apply filter
	 */
	cb_apply_item_toggled (NULL, info);

	/*
	 * This is for the initial selection
	 */
	if (info->grid[0] != NULL) {
		cb_canvas_button_release (info->canvas[0], NULL, info);

		gtk_widget_set_sensitive (GTK_WIDGET (info->edit), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (info->ok), TRUE);
	} else {
		info->selected_template = NULL;

		gtk_widget_set_sensitive (GTK_WIDGET (info->edit), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (info->ok), FALSE);

		/*
		 * Be sure to clear the template information
		 */
		gtk_entry_set_text (info->info_name, "");
		gtk_entry_set_text (info->info_author, "");
		gtk_entry_set_text (info->info_cat, "");
		gtk_editable_delete_text (GTK_EDITABLE (info->info_descr), 0, -1);
	}

	/*
	 * FIXME: REMOVE THIS WHEN YOU WANT NEW/EDIT/REMOVE TO WORK!!!!!!!
	 */
	gtk_widget_set_sensitive (GTK_WIDGET (info->edit), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (info->new), FALSE);
}

static void
cb_gridlines_item_toggled (GtkCheckMenuItem *item, AutoFormatInfo *info)
{
	previews_free (info);
	previews_load (info, info->preview_top);
}

/********************************************************************************
 * MAIN
 ********************************************************************************/

/**
 * setup_apply_item:
 * @info: AutoFormatInfo
 * @name: GtkCheckMenuItem
 *
 * Retrieve the CheckMenuItem @name from the glade file and
 * connect it's toggled signal handler to cb_apply_item_toggled.
 *
 * Return value: pointer to the gtkcheckmenuitem
 **/
static GtkCheckMenuItem *
setup_apply_item (GladeXML *gui, AutoFormatInfo *info, const char *name)
{
	GtkCheckMenuItem *item;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gui, name));

	gtk_signal_connect (GTK_OBJECT (item),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_item_toggled),
			    info);

	return item;
}

/**
 * dialog_autoformat:
 * @wb: The Workbook
 *
 * This function will show the AutoFormatTemplate dialog and apply
 * the template the user chooses to the current selection in the active
 * sheet of the workbook if the user desires.
 **/
void
dialog_autoformat (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	AutoFormatInfo *info;
	int i;

	/*
	 * Load gui from xml file
	 */
	gui = gnumeric_glade_xml_new (NULL, GLADE_FILE);
	if (!gui) {
		char *message;

		message = g_strdup_printf (_("Missing %s file"), GLADE_FILE);

		g_warning (message);

		g_free (message);

		return;
	}

	/*
	 * Get widgets
	 */
	info = g_new0 (AutoFormatInfo, 1);

	info->wb              = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	info->wbcg            = wbcg;
	info->templates       = NULL;
	info->category_groups = NULL;
	info->selrect         = NULL;
	for (i = 0; i < NUM_PREVIEWS; i++) {
		info->grid[i] = NULL;
		info->rect[i] = NULL;
	}

	info->current_category_group  = NULL;
	info->preview_top       = 0;
	info->preview_index     = -1;
	info->previews_locked   = FALSE;
	info->more_down         = FALSE;
	info->selected_template = NULL;
	info->tooltips          = gtk_tooltips_new ();

	info->dialog     = GTK_DIALOG (glade_xml_get_widget (gui, "dialog"));

	info->category   = GTK_COMBO (glade_xml_get_widget (gui, "format_category"));

	for (i = 1; i <= NUM_PREVIEWS; i++) {
		char *name;

		name = g_strdup_printf ("format_canvas%d", i);
		info->canvas[i - 1] = GNOME_CANVAS (glade_xml_get_widget (gui, name));
		g_free (name);

		name = g_strdup_printf ("format_frame%d", i);
		info->frame[i - 1] = GTK_FRAME (glade_xml_get_widget (gui, name));
		g_free (name);
	}
	info->scroll     = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "format_scroll"));
	info->gridlines  = GTK_CHECK_MENU_ITEM  (glade_xml_get_widget (gui, "format_gridlines"));

	info->new            = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_new"));
	info->edit           = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_edit"));
	info->remove_current = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_remove_current"));

	info->info_name   = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_name"));
	info->info_author = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_author"));
	info->info_cat    = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_cat"));
	info->info_descr  = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "format_info_descr"));

	info->ok     = GTK_BUTTON (glade_xml_get_widget (gui, "format_ok"));
	info->cancel = GTK_BUTTON (glade_xml_get_widget (gui, "format_cancel"));

	/*
	 * Retrieve apply items and connect
	 * their signals
	 */

 	info->number      = setup_apply_item (gui, info, "format_number");
	info->border      = setup_apply_item (gui, info, "format_border");
	info->font        = setup_apply_item (gui, info, "format_font");
	info->patterns    = setup_apply_item (gui, info, "format_patterns");
	info->alignment   = setup_apply_item (gui, info, "format_alignment");

	/*
	 * Connect signals
	 */
	gtk_signal_connect (GTK_OBJECT (info->dialog),
			    "close",
			    GTK_SIGNAL_FUNC (cb_dialog_close),
			    info);

	/*
	 * FIXME: UGLY! This actually connects a signal to the window
	 * which is popped-up by the category combo, this is actually
	 * not allowed (only entry and list are public) and may
	 * very well break when gtkcombo's implementation changes
	 */
	gtk_signal_connect (GTK_OBJECT (info->category->popwin),
			    "hide",
			    GTK_SIGNAL_FUNC (cb_category_popwin_hide),
			    info);

	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (info->scroll)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (cb_scroll_value_changed),
			    info);

	for (i = 0; i < NUM_PREVIEWS; i++) {
		gtk_signal_connect (GTK_OBJECT (info->canvas[i]),
				    "button-release-event",
				    GTK_SIGNAL_FUNC (cb_canvas_button_release),
				    info);
	}

	gtk_signal_connect (GTK_OBJECT (info->gridlines),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_gridlines_item_toggled),
			    info);

	gtk_signal_connect (GTK_OBJECT (info->new),
			    "activate",
			    GTK_SIGNAL_FUNC (cb_new_activated),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->edit),
			    "activate",
			    GTK_SIGNAL_FUNC (cb_edit_activated),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->remove_current),
			    "activate",
			    GTK_SIGNAL_FUNC (cb_remove_current_activated),
			    info);

	gtk_signal_connect (GTK_OBJECT (info->ok),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_ok_clicked),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->cancel),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_cancel_clicked),
			    info);

	/*
	 * Fill category list
	 */
	info->category_groups = category_group_list_get ();
	if (info->category_groups == NULL) {
		GtkWidget *wdialog;

		wdialog = gnome_warning_dialog_parented (
		          _("An error occurred while reading the category list"),
		          GTK_WINDOW (info->dialog));
		gnome_dialog_run (GNOME_DIALOG (wdialog));
	} else {
		GList *names_list, *general_category_group_link;

		names_list = category_group_list_get_names_list (info->category_groups);
		gtk_combo_set_popdown_strings (info->category, names_list);
		/* This is a name of the "General" autoformat template category.
		   Please use the same translation as in General.category XML file */
		general_category_group_link = g_list_find_custom (names_list, _("General"), g_str_compare);
		if (general_category_group_link == NULL) {
			general_category_group_link = g_list_find_custom (names_list, "General", g_str_compare);
		}
		if (general_category_group_link != NULL) {
			gint general_category_group_index;
			
			general_category_group_index = g_list_position (names_list, general_category_group_link);
			gtk_list_select_item (GTK_LIST (info->category->list), general_category_group_index);
		} else {
			g_warning ("General category not found");
		}
		e_free_string_list (names_list);

		/*
		 * Call callback the screen updates
		 */
		cb_category_popwin_hide (GTK_WIDGET (info->category), info);
	}

	gtk_dialog_set_default_response (info->dialog, GTK_RESPONSE_OK);

	/*
	 * Show the dialog and enter gtk main loop
	 */
	info->canceled = FALSE;

	gnumeric_dialog_run (wbcg, info->dialog);

	/*
	 * If the user didn't cancel, record undo information and apply the autoformat
	 * template to the current selection.
	 * Observe that we pass a copy of the selected template to the undo function.
	 */
	if (!info->canceled && info->selected_template) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
		Sheet *sheet = wb_control_cur_sheet (wbc);

		cmd_autoformat (wbc, sheet,
			format_template_clone (info->selected_template));
	}

	/*
	 * Free templates, previews, etc..
	 */
	previews_free (info);
	templates_free (info);

	gtk_object_unref (GTK_OBJECT (info->tooltips));

	category_group_list_free (info->category_groups);

	gtk_widget_destroy (GTK_WIDGET (info->dialog));
	g_object_unref (G_OBJECT (gui));

	g_free (info);
}
