/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-autoformat.c : implementation of the autoformat dialog
 *
 * Author : Almer S. Tigelaar <almer@gnome.org>
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
#include <workbook-edit.h>
#include <commands.h>
#include <selection.h>
#include <ranges.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <math.h>
#include <stdlib.h>

/* Table to show for
 * previews, please don't make this larger than 5x5
 */
#define PREVIEW_ROWS 5
#define PREVIEW_COLS 5
#define NUM_PREVIEWS 6
#define DEFAULT_COL_WIDTH  42
#define DEFAULT_ROW_HEIGHT 16

/* Keep these strings very short.
   They are used as a sample data for a sheet, so you can put anything here
   ("One", "Two", "Three" for example) */
static char const *
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
	GladeXML	   *gui;
	PreviewGrid        *grid[NUM_PREVIEWS];              /* Previewgrid's */
	GnomeCanvasItem    *rect[NUM_PREVIEWS];              /* Centering rectangles */
	GnomeCanvasItem    *selrect;                         /* Selection rectangle */
	GSList             *templates;                       /* List of FormatTemplate's */
	FormatTemplate     *selected_template;               /* The currently selected template */
	GList              *category_groups;                 /* List of groups of categories */

	FormatTemplateCategoryGroup *current_category_group; /* Currently selected category group */

	int               preview_top;       /* Top index of the previewlist */
	int               preview_index;     /* Selected canvas in previewlist */
	gboolean          previews_locked;   /* If true, the preview_free and preview_load will not function */
	gboolean          more_down;         /* If true, more was clicked and the button caption is now 'Less' */

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
	
	struct {
		GtkCheckMenuItem *left;
		GtkCheckMenuItem *right;
		GtkCheckMenuItem *top;
		GtkCheckMenuItem *bottom;
	} edges;
	
	GtkButton      *ok, *cancel;

	GtkTooltips    *tooltips;
} AutoFormatState;

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
	char const *text;
	char *endptr = NULL;
	double tmp;

	if (row >= PREVIEW_ROWS || col >= PREVIEW_COLS)
		return NULL;

	text = _(demotable[row][col]);
	tmp = g_strtod (text, &endptr);

	if (*endptr == '\0')
	    return value_new_float (tmp);
	return value_new_string (text);
}

/********************************************************************************
 * UTILITY FUNCTIONS
 ********************************************************************************/

static void
templates_free (AutoFormatState *state)
{
	GSList *ptr;

	g_return_if_fail (state != NULL);

	for (ptr = state->templates; ptr != NULL ; ptr = ptr->next)
		format_template_free (ptr->data);
	g_slist_free (state->templates);
	state->templates = NULL;
}

/**
 * templates_load:
 * @state: AutoFormatState
 *
 * This function will load the templates in the currently selected
 * category group (it looks at state->category_groups to determine the selection)
 *
 * Return value: TRUE if all went well, FALSE otherwise.
 **/
static gboolean
templates_load (AutoFormatState *state)
{
	GSList *l;
	gint n_templates;

	g_assert (state != NULL);

	if (state->category_groups == NULL)
		return FALSE;

	state->templates = category_group_get_templates_list (
		state->current_category_group, COMMAND_CONTEXT (state->wbcg));
	for (l = state->templates; l != NULL; l = l->next) {
		FormatTemplate *ft = l->data;
		range_init (&ft->dimension,
			0, 0, PREVIEW_COLS - 1, PREVIEW_ROWS - 1);
		ft->invalidate_hash = TRUE;
	}
	n_templates = g_slist_length (state->templates);

	/*
	 * We need to temporary lock the preview loading/freeing or
	 * else our scrollbar will trigger an event (value_changed) and create the
	 * previews. (which we don't want to happen at this moment)
	 */
	state->previews_locked = TRUE;
	{
		GtkAdjustment *adjustment = gtk_range_get_adjustment (GTK_RANGE (state->scroll));

		adjustment->value = 0;
		adjustment->lower = 0;
		adjustment->upper = n_templates / 2;
		adjustment->step_increment = 1;
		adjustment->page_increment = 3;
		adjustment->page_size = 3;

		gtk_adjustment_changed (adjustment);
	}
	state->previews_locked = FALSE;

	/*
	 * Hide the scrollbar when it's not needed
	 */
	if (n_templates > NUM_PREVIEWS)
		gtk_widget_show (GTK_WIDGET (state->scroll));
	else
		gtk_widget_hide (GTK_WIDGET (state->scroll));

	return TRUE;
}

/**
 * previews_free:
 * @state: AutoFormatState
 *
 * This function will free all previews.
 **/
static void
previews_free (AutoFormatState *state)
{
	int i;

	if (state->previews_locked)
		return;

	if (state->selrect)
		gtk_object_destroy (GTK_OBJECT (state->selrect));
	state->selrect = NULL;

	for (i = 0; i < NUM_PREVIEWS; i++) {
		if (state->grid[i]) {
			gtk_layout_freeze (GTK_LAYOUT (state->canvas[i]));

			gtk_object_destroy (GTK_OBJECT (state->rect[i]));
			gtk_object_destroy (GTK_OBJECT (state->grid[i]));
			state->rect[i] = NULL;
			state->grid[i] = NULL;
		}
	}
}

/**
 * previews_load:
 * @state: AutoFormatState
 * @topindex: The index of the template to be displayed in the upper left corner
 *
 * This function will create grids and rects for each canvas and associate
 * them with the right format templates.
 * NOTE : if state->preview_locked is TRUE this function will do nothing,
 *        this is handy in situation where signals can cause previews_load to be
 *        called before previews_free.
 **/
static void
previews_load (AutoFormatState *state, int topindex)
{
	GSList *iterator, *start;
	int i, count = topindex;

	g_return_if_fail (state != NULL);

	if (state->previews_locked)
		return;

	iterator = state->templates;
	start = iterator;
	while (iterator && count > 0) {
		iterator = g_slist_next (iterator);
		start = iterator;
		count--;
	}

	for (i = 0; i < NUM_PREVIEWS; i++) {
		if (start == NULL) {
			gtk_widget_hide (GTK_WIDGET (state->canvas[i]));
			gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_NONE);
		} else {
			FormatTemplate *ft = start->data;

			/*
			 * This rect is used to properly center on the canvas. It covers the whole canvas area.
			 * Currently the canvas shifts the (0,0) 4,5 pixels downwards in vertical and horizontal
			 * directions. So we need (-4.5, -4.5) as the absolute top coordinate and (215.5, 85.5) for
			 * the absolute bottom of the canvas's region. Look at src/dialogs/autoformat.glade for
			 * the original canvas dimensions (look at the scrolledwindow that houses each canvas)
			 */
			state->rect[i] = gnome_canvas_item_new (gnome_canvas_root (state->canvas[i]),
				GNOME_TYPE_CANVAS_RECT,
				"x1", -4.5, "y1", -4.5,
				"x2", 215.5, "y2", 85.5,
				"width_pixels", (int) 0,
				"fill_color", NULL,
				NULL);

			/* Setup grid */
			gtk_layout_freeze (GTK_LAYOUT (state->canvas[i]));
			state->grid[i] = PREVIEW_GRID (
				gnome_canvas_item_new (gnome_canvas_root (state->canvas[i]),
						       preview_grid_get_type (),
						       "RenderGridlines", state->gridlines->active,
						       "DefaultRowHeight", DEFAULT_ROW_HEIGHT,
						       "DefaultColWidth", DEFAULT_COL_WIDTH,
						       NULL));

			g_signal_connect (G_OBJECT (state->grid[i]),
				"get_cell_style",
				G_CALLBACK (cb_get_cell_style), ft);
			g_signal_connect (G_OBJECT (state->grid[i]),
				"get_cell_value",
				G_CALLBACK (cb_get_cell_value), ft);

			/* Are we selected? Then draw a selection rectangle */
			if (topindex + i == state->preview_index) {
				g_return_if_fail (state->selrect == NULL);

				state->selrect = gnome_canvas_item_new (gnome_canvas_root (state->canvas[i]),
					GNOME_TYPE_CANVAS_RECT,
					"x1", -7.0, "y1", -2.5,
					"x2", 219.0, "y2", 84.5,
					"width_pixels", (int) 2,
					"outline_color", "red",
					"fill_color", NULL,
					NULL);
				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_IN);
			} else
				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_OUT);

			gnome_canvas_set_scroll_region (state->canvas[i], 0, 0,
				PREVIEW_COLS * DEFAULT_COL_WIDTH,
				PREVIEW_ROWS * DEFAULT_ROW_HEIGHT);

			gtk_tooltips_set_tip (state->tooltips,
				GTK_WIDGET (state->canvas[i]),
				ft->name, "");

			gtk_widget_show (GTK_WIDGET (state->canvas[i]));
			start = g_slist_next (start);
		}
	}

	state->preview_top = topindex;

	for (i = 0; i < NUM_PREVIEWS; i++)
		gtk_layout_thaw (GTK_LAYOUT (state->canvas[i]));
}

/********************************************************************************
 * SIGNAL HANDLERS
 ********************************************************************************/

static void
cb_ok_clicked (GtkButton *button, AutoFormatState *state)
{
	if (state->selected_template) {
		WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
		Sheet *sheet = wb_control_cur_sheet (wbc);

		cmd_autoformat (wbc, sheet,
			format_template_clone (state->selected_template));
	}
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_cancel_clicked (GtkButton *button, AutoFormatState *state)
{
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_autoformat_destroy (GtkWidget *ignored, AutoFormatState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	previews_free (state);
	templates_free (state);
	gtk_object_unref (GTK_OBJECT (state->tooltips));
	category_group_list_free (state->category_groups);
	g_object_unref (G_OBJECT (state->gui));
	state->gui = NULL;
	g_free (state);
}

static void
cb_scroll_value_changed (GtkAdjustment *adjustment, AutoFormatState *state)
{
	previews_free (state);
	previews_load (state, rint (adjustment->value) * 2);
}

static gboolean
cb_canvas_button_press (GnomeCanvas *canvas, GdkEventButton *event, AutoFormatState *state)
{
	FormatTemplate *ft;
	GSList *ptr;
	int index = 0;

	while (canvas != state->canvas[index] && index < NUM_PREVIEWS)
		index++;

	g_return_val_if_fail (index < NUM_PREVIEWS, FALSE);

	state->preview_index = state->preview_top + index;

	previews_free (state);
	previews_load (state, state->preview_top);

	for (ptr = state->templates, index = 0; ptr != NULL ;  ptr = ptr->next, index++)
		if (index == state->preview_index)
			break;

	g_return_val_if_fail (ptr != NULL && ptr->data != NULL, FALSE);

	ft = ptr->data;
	state->selected_template = ft;
	gtk_entry_set_text (state->info_name, ft->name);
	gtk_entry_set_text (state->info_author, ft->author);
	gnumeric_textview_set_text (GTK_TEXT_VIEW (state->info_descr),
		ft->description);

	gtk_entry_set_text (state->info_cat, ft->category->name);
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove_current),
		ft->category->is_writable);

	return TRUE;
}

static void
cb_check_item_toggled (GtkCheckMenuItem *item, AutoFormatState *state)
{
	GSList *ptr;
	int i;

	for (ptr = state->templates; ptr != NULL ; ptr = ptr->next) {
		FormatTemplate *ft = ptr->data;
		
		ft->number    = state->number->active;
		ft->border    = state->border->active;
		ft->font      = state->font->active;
		ft->patterns  = state->patterns->active;
		ft->alignment = state->alignment->active;
		
		ft->edges.left   = state->edges.left->active;
		ft->edges.right  = state->edges.right->active;
		ft->edges.top    = state->edges.top->active;
		ft->edges.bottom = state->edges.bottom->active;
		
		ft->invalidate_hash = TRUE;
	}

	for (i = 0; i < NUM_PREVIEWS; i++)
		gnome_canvas_request_redraw (state->canvas [i],
			INT_MIN, INT_MIN, INT_MAX/2, INT_MAX/2);
}

static void
cb_category_popwin_hide (GtkWidget *widget, AutoFormatState *state)
{
	state->current_category_group = category_group_list_find_category_by_name(
	                               state->category_groups,
	                               gtk_entry_get_text (GTK_ENTRY (state->category->entry)));

	previews_free (state);
	templates_free (state);

	if (templates_load (state) == FALSE)
		g_warning ("Error while loading templates!");

	gtk_tooltips_set_tip (state->tooltips, GTK_WIDGET (state->category->entry),
		(state->current_category_group->description != NULL)
			? state->current_category_group->description
			: state->current_category_group->name,
		"");

	previews_load (state, 0);

	cb_check_item_toggled (NULL, state);

	/* This is for the initial selection */
	if (state->grid[0] != NULL) {
		cb_canvas_button_press (state->canvas[0], NULL, state);

		gtk_widget_set_sensitive (GTK_WIDGET (state->edit), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (state->ok), TRUE);
	} else {
		state->selected_template = NULL;

		gtk_widget_set_sensitive (GTK_WIDGET (state->edit), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (state->ok), FALSE);

		/* Be sure to clear the template information */
		gtk_entry_set_text (state->info_name, "");
		gtk_entry_set_text (state->info_author, "");
		gtk_entry_set_text (state->info_cat, "");
		gtk_editable_delete_text (GTK_EDITABLE (state->info_descr), 0, -1);
	}

	/* FIXME: REMOVE THIS WHEN YOU WANT NEW/EDIT/REMOVE TO WORK!!!!!!! */
	gtk_widget_set_sensitive (GTK_WIDGET (state->edit), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->new), FALSE);
}

static void
cb_gridlines_item_toggled (GtkCheckMenuItem *item, AutoFormatState *state)
{
	previews_free (state);
	previews_load (state, state->preview_top);
}

/********************************************************************************
 * MAIN
 ********************************************************************************/

static GtkCheckMenuItem *
setup_check_item (GladeXML *gui, AutoFormatState *state, char const *name)
{
	GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gui, name));
	g_signal_connect (G_OBJECT (item),
		"toggled",
		G_CALLBACK (cb_check_item_toggled), state);
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
	AutoFormatState *state;
	int i;

	gui = gnumeric_glade_xml_new (NULL, "autoformat.glade");
	g_return_if_fail (gui != NULL);

	state = g_new0 (AutoFormatState, 1);

	state->wb              = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->wbcg            = wbcg;
	state->gui             = gui;
	state->templates       = NULL;
	state->category_groups = NULL;
	state->selrect         = NULL;
	for (i = 0; i < NUM_PREVIEWS; i++) {
		state->grid[i] = NULL;
		state->rect[i] = NULL;
	}

	state->current_category_group  = NULL;
	state->preview_top       = 0;
	state->preview_index     = -1;
	state->previews_locked   = FALSE;
	state->more_down         = FALSE;
	state->selected_template = NULL;
	state->tooltips          = gtk_tooltips_new ();
	gtk_object_ref  (GTK_OBJECT (state->tooltips));
	gtk_object_sink (GTK_OBJECT (state->tooltips));

	state->dialog     = GTK_DIALOG (glade_xml_get_widget (gui, "dialog"));
	state->category   = GTK_COMBO (glade_xml_get_widget (gui, "format_category"));
	state->scroll     = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "format_scroll"));
	state->gridlines  = GTK_CHECK_MENU_ITEM  (glade_xml_get_widget (gui, "format_gridlines"));
	state->new            = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_new"));
	state->edit           = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_edit"));
	state->remove_current = GTK_MENU_ITEM (glade_xml_get_widget (gui, "format_remove_current"));

	state->info_name   = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_name"));
	state->info_author = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_author"));
	state->info_cat    = GTK_ENTRY (glade_xml_get_widget (gui, "format_info_cat"));
	state->info_descr  = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "format_info_descr"));

	state->ok     = GTK_BUTTON (glade_xml_get_widget (gui, "format_ok"));
	state->cancel = GTK_BUTTON (glade_xml_get_widget (gui, "format_cancel"));

 	state->number      = setup_check_item (gui, state, "format_number");
	state->border      = setup_check_item (gui, state, "format_border");
	state->font        = setup_check_item (gui, state, "format_font");
	state->patterns    = setup_check_item (gui, state, "format_patterns");
	state->alignment   = setup_check_item (gui, state, "format_alignment");

	state->edges.left   = setup_check_item (gui, state, "format_edges_left");
	state->edges.right  = setup_check_item (gui, state, "format_edges_right");
	state->edges.top    = setup_check_item (gui, state, "format_edges_top");
	state->edges.bottom = setup_check_item (gui, state, "format_edges_bottom");

	for (i = 0; i < NUM_PREVIEWS; i++) {
		char *name;

		name = g_strdup_printf ("format_canvas%d", i+1);
		state->canvas[i] = GNOME_CANVAS (glade_xml_get_widget (gui, name));
		g_free (name);

		name = g_strdup_printf ("format_frame%d", i+1);
		state->frame[i] = GTK_FRAME (glade_xml_get_widget (gui, name));
		g_free (name);

		g_signal_connect (G_OBJECT (state->canvas[i]),
			"button-press-event",
			G_CALLBACK (cb_canvas_button_press), state);

	}

	g_signal_connect (G_OBJECT (GTK_RANGE (state->scroll)->adjustment),
		"value_changed",
		G_CALLBACK (cb_scroll_value_changed), state);
	g_signal_connect (G_OBJECT (state->gridlines),
		"toggled",
		G_CALLBACK (cb_gridlines_item_toggled), state);
	g_signal_connect (G_OBJECT (state->ok),
		"clicked",
		G_CALLBACK (cb_ok_clicked), state);
	g_signal_connect (G_OBJECT (state->cancel),
		"clicked",
		G_CALLBACK (cb_cancel_clicked), state);

	/*
	 * FIXME: UGLY! This actually connects a signal to the window
	 * which is popped-up by the category combo, this is actually
	 * not allowed (only entry and list are public) and may
	 * very well break when gtkcombo's implementation changes
	 */
	g_signal_connect (G_OBJECT (state->category->popwin),
		"hide",
		G_CALLBACK (cb_category_popwin_hide), state);

	/* Fill category list */
	state->category_groups = category_group_list_get ();
	if (state->category_groups == NULL) {
		GtkWidget *wdialog;

		wdialog = gnome_warning_dialog_parented (
		          _("An error occurred while reading the category list"),
		          GTK_WINDOW (state->dialog));
		gnome_dialog_run (GNOME_DIALOG (wdialog));
	} else {
		GList *names_list, *general_category_group_link;

		names_list = category_group_list_get_names_list (state->category_groups);
		gtk_combo_set_popdown_strings (state->category, names_list);
		/* This is a name of the "General" autoformat template category.
		   Please use the same translation as in General.category XML file */
		general_category_group_link = g_list_find_custom (names_list, _("General"), g_str_compare);
		if (general_category_group_link == NULL)
			general_category_group_link = g_list_find_custom (names_list, "General", g_str_compare);

		if (general_category_group_link != NULL) {
			gint general_category_group_index;

			general_category_group_index = g_list_position (names_list, general_category_group_link);
			gtk_list_select_item (GTK_LIST (state->category->list), general_category_group_index);
		} else
			g_warning ("General category not found");

		e_free_string_list (names_list);

		cb_category_popwin_hide (GTK_WIDGET (state->category), state);
	}

	gtk_dialog_set_default_response (state->dialog, GTK_RESPONSE_OK);

	/* a candidate for merging into attach guru */
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_autoformat_destroy), state);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}
