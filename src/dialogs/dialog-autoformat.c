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
 * WORKING NOTE : Once the edit dialog is ready look at line 812 and remove
 * the disabling of new/edit/remove buttons!
 */
 
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>

#include <math.h>
#include <dirent.h>
#include <errno.h>

#include "gnumeric.h"
#include "gnumeric-util.h"
#include "mstyle.h"
#include "border.h"
#include "value.h"

#include "dialogs.h"

#include "preview-grid-controller.h"
#include "format-template.h"

#include "command-context.h"
#include "workbook.h"

#define GLADE_FILE "autoformat.glade"

/*
 * Table to show for
 * previews, please don't make this larger than 5x5
 */
#define PREVIEW_ROWS 5
#define PREVIEW_COLS 5

static char* demotable[PREVIEW_ROWS][PREVIEW_COLS] = {
	{ " "    , "Jan", "Feb", "Mrt", "Total" },
	{ "North", "6"  , "13" , "20" , "39"    },
	{ "South", "12" , "4"  , "17" , "33"    },
	{ "West" , "8"  , "2"  , "0"  , "10"     },
	{ "Total", "26" , "19" , "37" , "81"    }
};

typedef struct {
	Workbook       *wb;                    /* Workbook we are working on */
	PreviewGridController *controller[6];  /* Controller for each canvas */
	GSList         *templates;             /* List of FormatTemplate's */
	FormatTemplate *selected_template;     /* The currently selected template */

	int             category_index;    /* Selected category in category list */
	int             preview_top;       /* Top index of the previewlist */
	int             preview_index;     /* Selected canvas in previewlist */
	gboolean        previews_locked;   /* If true, the preview_free and preview_load will not function */
	gboolean        more_down;         /* If true, more was clicked and the button caption is now 'Less' */
	gboolean        canceled;          /* True if the user pressed cancel */

	/*
	 * Gui elements
	 */
	GnomeDialog    *dialog;

	GtkCList       *categories;

	GtkLabel       *catlabel;
	GnomeCanvas    *canvas[6];
	GtkFrame       *frame[6];
	GtkLabel       *label[6];
	GtkVScrollbar  *scroll;
	GtkCheckButton *gridlines;
	
	GtkButton      *new, *edit, *remove, *more;

	GtkFrame       *frame_info;
	GtkEntry       *info_name, *info_author, *info_cat;
	GtkText        *info_descr;

	GtkFrame       *frame_apply;
	GtkCheckButton *number, *border, *font, *patterns, *alignment, *dimensions;

	GtkButton      *ok, *cancel;

	GtkTooltips    *tooltips;
} AutoFormatInfo;

/********************************************************************************
 * CALLBACKS FOR PREVIEW GRID CONTROLLER
 ********************************************************************************/
 
/**
 * cb_get_cell_content:
 * @row: row offset
 * @col: column offset
 * @data: FormatTemplate (unused)
 * 
 * Return the value for cell @row, @col.
 * This if fairly easy and we don't even use @data here.
 * We simply return the entries from the demotable.
 * 
 * Return value: the value for cell @row, @col.
 **/
static Value *
cb_get_cell_content (int row, int col, gpointer data)
{
	Value *value = NULL;
	char *text;
	
	g_return_val_if_fail (row < PREVIEW_ROWS && col < PREVIEW_COLS, NULL);

	text = demotable[row][col];
	
       /*
        * Determine if the text to display only consists of
	* numbers, if so
        * set the value as number for proper alignment
        */
       {
               char *endptr = NULL;
               double tmp;

               tmp = g_strtod (text, &endptr);

               /*
                * String only consists of numbers if *endptr equals \0
                */
               if (*endptr == '\0')
                       value = value_new_float (tmp);
               else
                       value = value_new_string (text);
       }

       return value;
}

/**
 * cb_get_row_height:
 * @row: row index
 * @data: FormatTemplate (unused)
 * 
 * Return row height for row @row, we return
 * -1, this will cause the preview-grid-controller
 * to take the default value.
 *
 * Return value: always -1.
 **/
static int
cb_get_row_height (int row, gpointer data)
{
	return -1;
}

/**
 * cb_get_col_width:
 * @col: column index
 * @data: FormatTemplate (unused)
 * 
 * Return col width for col @col.
 * This always return -1 so the
 * preview-grid-controller will take
 * the default column width.
 * 
 * Return value: always -1.
 **/
static int
cb_get_col_width (int col, gpointer data)
{
	return -1;
}

/**
 * cb_get_cell_style:
 * @row: row offset
 * @col: col offset
 * @data: FormatTemplate
 * 
 * Return the style for cell @row, @col.
 * This callback function uses formattemplate
 * to calculate the style.
 * 
 * Return value: style associated with @row, @col
 **/
static MStyle *
cb_get_cell_style (int row, int col, gpointer data)
{
	FormatTemplate *ft = (FormatTemplate *) data;

	/*
	 * It is "OK" if format_template_get_style
	 * returns NULL, the preview grid controller
	 * will then automatically pick our default
	 * style and use that :-)
	 */
	return format_template_get_style (ft, row, col);
}

/********************************************************************************
 * UTILITY FUNCTIONS
 ********************************************************************************/
 
/**
 * fill_category_list:
 * @clist: The GtkCList to put the categories in.
 * 
 * Loads the list of categories available
 * 
 * Return value: TRUE on success, FALSE otherwise.
 **/
static gboolean
fill_category_list (GtkCList *clist)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	g_return_val_if_fail (clist != NULL, FALSE);
	
	dir = opendir (GNUMERIC_AUTOFORMATDIR);
	if (dir == NULL && errno == ENOENT) {
		g_warning ("The autoformat template directory %s, does not exist!", GNUMERIC_AUTOFORMATDIR);
		return FALSE;
	}
	
	while ((ent = readdir (dir))) {
		char *t[1];

		if (strcmp (ent->d_name, ".") != 0 && strcmp (ent->d_name, "..") != 0) {
			t[0] = ent->d_name;
			gtk_clist_append (clist, t);
			count++;
		}
	}

	if (errno) {
	
		g_warning ("Error while reading listing of %s", GNUMERIC_AUTOFORMATDIR);	
		closedir (dir);
		return FALSE;
	} else if (count == 0) {
	
		g_warning ("The autoformat template directory %s, has no category subdirectories!", GNUMERIC_AUTOFORMATDIR);
		closedir (dir);
		return FALSE;
	}
	
	closedir (dir);
	return TRUE;
}

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
 * category (it looks at info->categories to determine the selection)
 * 
 * Return value: TRUE if all went well, FALSE otherwise.
 **/
static gboolean
templates_load (AutoFormatInfo *info)
{
	DIR *dir;
	struct dirent *ent;
        char *directory;
	char *labeltext;
	char *t[1];
	int count = 0;
	gboolean error = FALSE;

	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (info->templates == NULL, FALSE);

	if (gtk_clist_get_text (info->categories, info->category_index, 0, t) == 0)
		return FALSE;
	
	directory = g_strdup_printf ("%s%s/", GNUMERIC_AUTOFORMATDIR, t[0]);
	
	dir = opendir (directory);
	if (dir == NULL) {
		g_warning ("Error opening directory %s", directory);
		error = TRUE;
	}

	if (dir) {
		while ((ent = readdir (dir))) {
	
			if (strcmp (ent->d_name, ".") != 0 && strcmp (ent->d_name, "..") != 0) {
				FormatTemplate *ft;
				char *filename;

				filename = g_strdup_printf ("%s%s", directory, ent->d_name);
				ft = format_template_new_from_file (workbook_command_context_gui (info->wb),
								    filename);
								    
				format_template_set_size (ft, 0, 0, PREVIEW_COLS - 1, PREVIEW_ROWS - 1);
				
				if (ft == NULL) {
				
					g_warning ("Error while reading %s", filename);
					error = TRUE;
					break;
				}

				g_free (filename);
				
				info->templates      = g_slist_prepend (info->templates, ft);
				count++;
			}
		}
		info->templates      = g_slist_reverse (info->templates);
	}

	g_free (directory);
	closedir (dir);

	/*
	 * Put some nice text on the label
	 */
	if (count == 1)
		labeltext = g_strdup_printf (_("%d template in category '%s'"), count, t[0]);
	else
		labeltext = g_strdup_printf (_("%d templates in category '%s'"), count, t[0]);
		
	gtk_label_set (info->catlabel, labeltext);
	g_free (labeltext);
	
	/*
	 * We need to temporary lock the preview loading/freeing or
	 * else our scrollbar will trigger an event (value_changed) and create the
	 * previews. (which we don't want to happen at this moment)
	 */
	info->previews_locked = TRUE;
	{
		GtkAdjustment *adjustment = gtk_range_get_adjustment (GTK_RANGE (info->scroll));

		count++;
		adjustment->value = 0;
		adjustment->lower = 0;
		adjustment->upper = count / 2;
		adjustment->step_increment = 1;
		adjustment->page_increment = 3;
		adjustment->page_size = 3;

		gtk_adjustment_changed (adjustment);
	}
	info->previews_locked = FALSE;

	return (!error);
}

/**
 * previews_free:
 * @info: AutoFormatInfo
 * 
 * This function will free all preview-grid-controllers.
 **/
static void
previews_free (AutoFormatInfo *info)
{
	int i;

	if (info->previews_locked)
		return;
		
	for (i = 0; i < 6; i++) {
		if (info->controller[i]) {
			gtk_layout_freeze (GTK_LAYOUT (info->canvas[i]));
			preview_grid_controller_free (info->controller[i]);
			info->controller[i] = NULL;
		}
	}
}

/**
 * previews_load:
 * @info: AutoFormatInfo
 * @topindex: The index of the template to be displayed in the upper left corner
 * 
 * This function will create preview-grid-controller for each canvas and associate
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

	for (i = 0; i < 6; i++) {
		
		if (start == NULL) {

			/*
			 * Hide the canvas and make sure label is empty and frame is invisible
			 */
			gtk_widget_hide (GTK_WIDGET (info->canvas[i]));

			gtk_label_set (info->label[i], "");
			gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_NONE);
			
		} else {
			FormatTemplate *ft = start->data;
			char *name;
			
			/*
			 * Create the controller and set the label text
			 */	
			if (info->controller[i])
				g_warning ("Serious troubles -> A previous preview controller was not freed!");

			gtk_layout_freeze (GTK_LAYOUT (info->canvas[i]));
			info->controller[i] = preview_grid_controller_new (info->canvas[i],
									   PREVIEW_ROWS, PREVIEW_COLS, 16, 42,
									   cb_get_row_height, cb_get_col_width,
									   cb_get_cell_content, cb_get_cell_style,
									   ft,
									   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->gridlines)));
			name = format_template_get_name (ft);
			gtk_label_set (info->label[i], name);
			g_free (name);

			{
				char *author = format_template_get_author (ft);
				char *description = format_template_get_description (ft);
				char *tooltip;

				tooltip = g_strdup_printf ("Created by %s \n%s", author, description);

				gtk_tooltips_set_tip (info->tooltips, GTK_WIDGET (info->canvas[i]), tooltip, "");
				
				g_free (tooltip);
				g_free (description);
				g_free (author);
			}
			/*
			 * Make sure the canvas and frame are visible
			 */
			gtk_widget_show (GTK_WIDGET (info->canvas[i]));

			if (topindex + i == info->preview_index)
				gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_IN);
			else
				gtk_frame_set_shadow_type (info->frame[i], GTK_SHADOW_OUT);
			
			start = g_slist_next (start);
		}
	}

	info->preview_top = topindex;

	for (i = 0; i < 6; i++) {
	
		gtk_layout_thaw (GTK_LAYOUT (info->canvas[i]));
	}
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
cb_dialog_close (GnomeDialog *dialog, AutoFormatInfo *info)
{
	info->canceled = TRUE;
	gtk_main_quit ();
	
	return TRUE;
}

/**
 * cb_more_clicked:
 * @button:
 * @info: 
 * 
 * This callback is invocated once the more button is pressed, note
 * that it will change the button caption and make the Template Information
 * and Apply sections on the dialog visible (or invisible again).
 **/
static void
cb_more_clicked (GtkButton *button, AutoFormatInfo *info)
{
	info->more_down = (!info->more_down);
	
	if (info->more_down) {

		gtk_widget_show_all (GTK_WIDGET (info->frame_info));
		gtk_widget_show_all (GTK_WIDGET (info->frame_apply));
		gtk_label_set_text (GTK_LABEL (GTK_BIN (button)->child), _("<< Less"));
	} else {
	
		gtk_widget_hide (GTK_WIDGET (info->frame_info));
		gtk_widget_hide (GTK_WIDGET (info->frame_apply));
		gtk_label_set_text (GTK_LABEL (GTK_BIN (button)->child), _("More >>"));
	}
}

/**
 * cb_new_clicked:
 * @button: 
 * @info:
 * 
 * Invoked when the new button is clicked, this will pop-up the
 * edit dialog and allow the user to create a new format template
 * on the fly :-)
 **/
static void
cb_new_clicked (GtkButton *button, AutoFormatInfo *info)
{
	g_warning ("Not implemented yet");
}

/**
 * cb_edit_clicked:
 * @button: 
 * @info: 
 * 
 * Called when the edit button is pressed, this will pop-up
 * the edit dialog which allows the user to edit the currently
 * selected template.
 **/
static void
cb_edit_clicked (GtkButton *button, AutoFormatInfo *info)
{
	g_warning ("Not implemented yet");

	/*
	dialog_autoformat_edit (info->wb, info->selected_template);
	format_template_save (info->selected_template);
	*/
}

/**
 * cb_remove_clicked:
 * @button: 
 * @info: 
 * 
 * This pops up a dialog asking the user if he/she wants to
 * remove the currently selected template.
 **/
static void
cb_remove_clicked (GtkButton *button, AutoFormatInfo *info)
{
	g_warning ("Not implemented yet");

	/*
	 * The implementation below is nice to start with, we need
	 * some way to distinguish between templates that are read-only
	 * (in the /usr/share/gnumeric/... dir) an templates that
	 * are in ~/.gnumeric/....
	 * Only private templates (~/.gnumeric) should be removable.
	 */
	 
/*	GtkWidget *dialog;
	GtkWidget *no_button;
	int ret;
	char *question;

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

			gtk_signal_emit_by_name (GTK_OBJECT (info->categories),
						 "select-row", info->category_index, 0,
						 NULL, info);
		}
	}
*/
}

/**
 * cb_scroll_value_changed:
 * @adjustment: 
 * @info: 
 * 
 * Invoked when the scrollbar is slid, simply changes the topindex for the previews
 * and reloads the preview grid controllers.
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
	while (canvas != info->canvas[index] && index < 6) {
		
		index++;
	}

	if (index == 6) {
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
		char *name, *author, *cat, *descr;
		
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
		cat    = format_template_get_category (ft);
		descr  = format_template_get_description (ft);
		
		gtk_entry_set_text (info->info_name, name);
		gtk_entry_set_text (info->info_author, author);
		gtk_entry_set_text (info->info_cat, cat);
		gtk_editable_delete_text (GTK_EDITABLE (info->info_descr), 0, -1);
		
		dummy = 0;
		gtk_editable_insert_text (GTK_EDITABLE (info->info_descr), descr,
					  strlen (descr), &dummy);

		g_free (name);
		g_free (author);
		g_free (cat);
		g_free (descr);
		
		info->selected_template = ft;
	}
	
	return TRUE;
}

/**
 * cb_categories_select_row:
 * @clist: 
 * @row: 
 * @column: 
 * @event: 
 * @info: 
 * 
 * Invoked when a row is selected in the category list, this will quickly load
 * all templates in the newly selected category, load the previews and select
 * the first one by default.
 * If there are no templates in the newly selected category the "OK", "Edit" and "Remove"
 * buttons will be disable and the Template Information cleared.
 **/
static void
cb_categories_select_row (GtkCList *clist, int row, int column, GdkEventButton *event, AutoFormatInfo *info)
{
	info->category_index = row;

	previews_free (info);
	templates_free (info);
	
	if (templates_load (info) == FALSE)
		g_warning ("Error while loading templates!");
		
	previews_load (info, 0);

	/*
	 * This is for the initial selection
	 */
	if (info->controller[0] != NULL) {
		cb_canvas_button_release (info->canvas[0], NULL, info);
		
		gtk_widget_set_sensitive (GTK_WIDGET (info->edit), TRUE); 
		gtk_widget_set_sensitive (GTK_WIDGET (info->remove), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (info->ok), TRUE);
	} else {
		info->selected_template = NULL;
		
		gtk_widget_set_sensitive (GTK_WIDGET (info->edit), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (info->remove), FALSE);
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
	 * REMOVE THIS WHEN YOU WANT NEW/EDIT/REMOVE TO WORK!!!!!!!
	 */
	gtk_widget_set_sensitive (GTK_WIDGET (info->edit), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (info->remove), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (info->new), FALSE);
}

/**
 * cb_apply_button_toggled:
 * @togglebutton: 
 * @info: 
 * 
 * This callback is invoked when one of the (6) apply buttons is toggled.
 * It will change the filter for each FormatTemplate. This way certain elements
 * can be filtered out (like the background, border, etc..)
 **/
static void
cb_apply_button_toggled (GtkToggleButton *togglebutton, AutoFormatInfo *info)
{
	GSList *iterator = info->templates;
	int i;

	while (iterator) {
		format_template_set_filter ((FormatTemplate *) iterator->data,
					    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->number)),
					    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->border)),
					    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->font)),
					    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->patterns)),
					    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->alignment)));

		iterator = g_slist_next (iterator);
	}

	for (i = 0; i < 6; i++)
		if (info->controller[i])
			preview_grid_controller_force_redraw (info->controller[i]);
}

static void
cb_gridlines_button_toggled (GtkToggleButton *togglebutton, AutoFormatInfo *info)
{
	previews_free (info);
	previews_load (info, info->preview_top);
}

/********************************************************************************
 * MAIN
 ********************************************************************************/
 
/**
 * dialog_autoformat:
 * @wb: The Workbook
 * 
 * This function will show the AutoFormatTemplate dialog and apply
 * the template the user chooses to the current selection in the active
 * sheet of the workbook if the user desires.
 **/
void
dialog_autoformat (Workbook *wb)
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

	info->wb         = wb;
	info->templates  = NULL;
	for (i = 0; i < 6; i++) {
		info->controller[i] = NULL;
	}

	info->category_index    = 0;
	info->preview_top       = 0;
	info->preview_index     = -1;
	info->previews_locked   = FALSE;
	info->more_down         = FALSE;
	info->selected_template = NULL;
	info->tooltips          = gtk_tooltips_new (); 
	
	info->dialog     = GNOME_DIALOG (glade_xml_get_widget (gui, "dialog"));
	
	info->categories = GTK_CLIST (glade_xml_get_widget (gui, "format_categories"));
	gtk_clist_column_titles_passive (info->categories);

	info->catlabel   = GTK_LABEL (glade_xml_get_widget (gui, "format_catlabel"));
	for (i = 1; i <= 6; i++) {
		char *name;

		name = g_strdup_printf ("format_canvas%d", i);
		info->canvas[i - 1] = GNOME_CANVAS (glade_xml_get_widget (gui, name));
		g_free (name);

		name = g_strdup_printf ("format_frame%d", i);
		info->frame[i - 1] = GTK_FRAME (glade_xml_get_widget (gui, name));
		g_free (name);

		name = g_strdup_printf ("format_label%d", i);
		info->label[i - 1] = GTK_LABEL (glade_xml_get_widget (gui, name));
		g_free (name);
	}
	info->scroll     = GTK_VSCROLLBAR   (glade_xml_get_widget (gui, "format_scroll"));
	info->gridlines  = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_gridlines"));

	info->new        = GTK_BUTTON (glade_xml_get_widget (gui, "format_new"));
	info->edit       = GTK_BUTTON (glade_xml_get_widget (gui, "format_edit"));
	info->remove     = GTK_BUTTON (glade_xml_get_widget (gui, "format_remove"));	
	info->more       = GTK_BUTTON (glade_xml_get_widget (gui, "format_more"));

	info->frame_info  = GTK_FRAME (glade_xml_get_widget (gui, "format_frame_info"));
	info->info_name   = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_name"));
	info->info_author = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_author"));
	info->info_cat    = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_cat"));
	info->info_descr  = GTK_TEXT (glade_xml_get_widget (gui, "format_info_descr"));

	info->frame_apply = GTK_FRAME (glade_xml_get_widget (gui, "format_frame_apply"));
	info->number      = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_number"));
	info->border      = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_border"));
	info->font        = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_font"));
	info->patterns    = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_patterns"));
	info->alignment   = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_alignment"));
	info->dimensions  = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "format_dimensions"));
	
	info->ok     = GTK_BUTTON (glade_xml_get_widget (gui, "format_ok"));
	info->cancel = GTK_BUTTON (glade_xml_get_widget (gui, "format_cancel"));
	
	/*
	 * Connect signals
	 */ 
	gtk_signal_connect (GTK_OBJECT (info->dialog),
			    "close",
			    GTK_SIGNAL_FUNC (cb_dialog_close),
			    info);
			    
	gtk_signal_connect (GTK_OBJECT (info->categories),
			    "select_row",
			    GTK_SIGNAL_FUNC (cb_categories_select_row),
			    info);

	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (info->scroll)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (cb_scroll_value_changed),
			    info);
			    
	for (i = 0; i < 6; i++) {
		gtk_signal_connect (GTK_OBJECT (info->canvas[i]),
				    "button-release-event",
				    GTK_SIGNAL_FUNC (cb_canvas_button_release),
				    info);
	}
	gtk_signal_connect (GTK_OBJECT (info->gridlines),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_gridlines_button_toggled),
			    info);

	gtk_signal_connect (GTK_OBJECT (info->number),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->border),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->font),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->patterns),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->alignment),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->dimensions),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_apply_button_toggled),
			    info);

	gtk_signal_connect (GTK_OBJECT (info->new),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_new_clicked),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->edit),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_edit_clicked),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->remove),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_remove_clicked),
			    info);
	gtk_signal_connect (GTK_OBJECT (info->more),
			    "clicked",
			    GTK_SIGNAL_FUNC (cb_more_clicked),
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
	if (fill_category_list (info->categories) == FALSE) {
		GtkWidget *wdialog;
      
		wdialog = gnome_warning_dialog_parented (_("An error occured while reading the category list"), 
							 GTK_WINDOW (info->dialog));

		gnome_dialog_run (GNOME_DIALOG (wdialog));
	}

	/*
	 * Select the first category and load previews for it
	 */
	gtk_clist_select_row (info->categories, 0, 0);

	/*
	 * Make sure we just hide the dialog when the close button
	 * is pressed, we'll handle closing the dialog ourselves.
	 */
	gnome_dialog_close_hides (info->dialog, TRUE);


	gnome_dialog_set_default (info->dialog, 0);

	/*
	 * Show the dialog and enter gtk main loop
	 */
	info->canceled = FALSE;

	gnumeric_dialog_run (wb, info->dialog);	


	/*
	 * If the user didn't cancel, apply the autoformat
	 * template to the current selection
	 */
	if (!info->canceled && info->selected_template)
		format_template_apply_to_sheet_selection (info->selected_template, wb->current_sheet);

	previews_free (info);
	templates_free (info);

	g_free (info->tooltips);
	g_free (info);
	
	gtk_widget_destroy (GTK_WIDGET (info->dialog));
	gtk_object_unref (GTK_OBJECT (gui));
}
