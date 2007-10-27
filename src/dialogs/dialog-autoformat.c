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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <mstyle.h>
#include <style-border.h>
#include <value.h>
#include <preview-grid-impl.h>
#include <format-template.h>
#include <file-autoft.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <selection.h>
#include <ranges.h>

#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <gsf/gsf-impl-utils.h>
#include <string.h>

/* Table to show for
 * previews, please don't make this larger than 5x5
 */
#define PREVIEW_COLS 5
#define PREVIEW_ROWS 5
#define NUM_PREVIEWS 6
#define DEFAULT_COL_WIDTH  52
#define DEFAULT_ROW_HEIGHT 17
#define BORDER	     7
#define INNER_BORDER 5
#define TOTAL_WIDTH  (DEFAULT_COL_WIDTH * PREVIEW_COLS)
#define TOTAL_HEIGHT (DEFAULT_ROW_HEIGHT * PREVIEW_ROWS)

/* Keep these strings very short.
   They are used as a sample data for a sheet, so you can put anything here
   ("One", "Two", "Three" for example) */
static char const *const
demotable[PREVIEW_ROWS][PREVIEW_COLS] = {
	{ N_(" ")    , N_("Jan"), N_("Feb"), N_("Mar"), N_("Total") },
	{ N_("North"),   N_("6"),  N_("13"),  N_("20"),    N_("39") },
	{ N_("South"),  N_("12"),   N_("4"),  N_("17"),    N_("33") },
	{ N_("West") ,   N_("8"),   N_("2"),   N_("0"),    N_("10") },
	{ N_("Total"),  N_("26"),  N_("19"),  N_("37"),    N_("81") }
};

typedef struct {
	Workbook           *wb;                              /* Workbook we are working on */
	WBCGtk *wbcg;
	GladeXML	   *gui;
	FooCanvasItem	   *grid[NUM_PREVIEWS];              /* Previewgrid's */
	FooCanvasItem	   *selrect;                         /* Selection rectangle */
	GSList             *templates;                       /* List of GnmFormatTemplate's */
	GnmFormatTemplate     *selected_template;               /* The currently selected template */
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

	GtkComboBox    *category;

	FooCanvas	 *canvas[NUM_PREVIEWS];
	GtkFrame         *frame[NUM_PREVIEWS];
	GtkVScrollbar    *scroll;
	GtkCheckMenuItem *gridlines;

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
} AutoFormatState;

/********************************************************************************/

typedef struct {
	PreviewGrid base;
	GnmFormatTemplate *ft;
} AutoFormatGrid;
typedef PreviewGridClass AutoFormatGridClass;

static GnmStyle *
afg_get_cell_style (PreviewGrid *pg, int col, int row)
{
	/* If this happens to be NULL the default style
	 * will automatically be used. */
	AutoFormatGrid *ag = (AutoFormatGrid *) pg;
	return format_template_get_style (ag->ft, row, col);
}

static GnmValue *
afg_get_cell_value (G_GNUC_UNUSED PreviewGrid *pg, int col, int row)
{
	char const *text;
	char *endptr = NULL;
	double tmp;

	if (row >= PREVIEW_ROWS || col >= PREVIEW_COLS)
		return NULL;

	text = _(demotable[row][col]);
	tmp = go_strtod (text, &endptr);

	if (*endptr == '\0')
		return value_new_float (tmp);
	return value_new_string (text);
}

static void
auto_format_grid_class_init (PreviewGridClass *klass)
{
	klass->get_cell_style = afg_get_cell_style;
	klass->get_cell_value = afg_get_cell_value;
}

static GSF_CLASS (AutoFormatGrid, auto_format_grid,
		  auto_format_grid_class_init, NULL,
		  preview_grid_get_type())

static FooCanvasItem *
auto_format_grid_new (AutoFormatState *state, int i, GnmFormatTemplate *ft)
{
	FooCanvasItem *item = foo_canvas_item_new (
		foo_canvas_root (state->canvas[i]),
		auto_format_grid_get_type (),
		"render-gridlines",	state->gridlines->active,
		"default-col-width",	DEFAULT_COL_WIDTH,
		"default-row-height",	DEFAULT_ROW_HEIGHT,
		"x",			0.,
		"y",			0.,
		NULL);
	((AutoFormatGrid *) item)->ft = ft;
	return item;
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

	g_return_val_if_fail (state != NULL, FALSE);

	if (state->category_groups == NULL)
		return FALSE;

	state->templates = category_group_get_templates_list (
		state->current_category_group, GO_CMD_CONTEXT (state->wbcg));
	for (l = state->templates; l != NULL; l = l->next) {
		GnmFormatTemplate *ft = l->data;
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
			gtk_object_destroy (GTK_OBJECT (state->grid[i]));
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
			GnmFormatTemplate *ft = start->data;

			state->grid[i] = auto_format_grid_new (state, i, ft);

			/* Are we selected? Then draw a selection rectangle */
			if (topindex + i == state->preview_index) {
				g_return_if_fail (state->selrect == NULL);

				state->selrect = foo_canvas_item_new (foo_canvas_root (state->canvas[i]),
					FOO_TYPE_CANVAS_RECT,
					"x1", (double)(-INNER_BORDER),
					"y1", (double)(-INNER_BORDER),
					"x2", (double)(TOTAL_WIDTH + INNER_BORDER),
					"y2", (double)(TOTAL_HEIGHT + INNER_BORDER),
					"width-pixels", (int) 3,
					"outline-color", "red",
					"fill-color", NULL,
					NULL);
				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_IN);
			} else
				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_ETCHED_IN);

			foo_canvas_set_scroll_region (state->canvas[i],
				-BORDER, -BORDER,
				TOTAL_WIDTH + BORDER,
				TOTAL_HEIGHT + BORDER);
			foo_canvas_scroll_to (state->canvas[i],
				-BORDER, -BORDER);

			go_widget_set_tooltip_text
				(GTK_WIDGET (state->canvas[i]),
				 _(ft->name));

			gtk_widget_show (GTK_WIDGET (state->canvas[i]));
			start = g_slist_next (start);
		}
	}

	state->preview_top = topindex;
}

/********************************************************************************
 * SIGNAL HANDLERS
 ********************************************************************************/

static void
cb_ok_clicked (G_GNUC_UNUSED GtkButton *button,
	       AutoFormatState *state)
{
	if (state->selected_template)
		cmd_selection_autoformat (WORKBOOK_CONTROL (state->wbcg),
			format_template_clone (state->selected_template));

	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_autoformat_destroy (AutoFormatState *state)
{
	templates_free (state);
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
cb_canvas_button_press (FooCanvas *canvas,
			G_GNUC_UNUSED GdkEventButton *event,
			AutoFormatState *state)
{
	GnmFormatTemplate *ft;
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
	gtk_entry_set_text (state->info_name,   _(ft->name));
	gtk_entry_set_text (state->info_author, ft->author);
	gnumeric_textview_set_text (GTK_TEXT_VIEW (state->info_descr),
		_(ft->description));

	gtk_entry_set_text (state->info_cat, _(ft->category->name));

	return TRUE;
}

static void
cb_check_item_toggled (G_GNUC_UNUSED GtkCheckMenuItem *item,
		       AutoFormatState *state)
{
	GSList *ptr;
	int i;

	for (ptr = state->templates; ptr != NULL ; ptr = ptr->next) {
		GnmFormatTemplate *ft = ptr->data;

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
		foo_canvas_request_redraw (state->canvas [i],
			-2, -2, INT_MAX/2, INT_MAX/2);
}

static void
cb_category_changed (AutoFormatState *state)
{
	GList *selection = g_list_nth (state->category_groups,
		gtk_combo_box_get_active (state->category));
	state->current_category_group = (selection != NULL) ? selection->data : NULL;
	previews_free (state);
	templates_free (state);
	if (templates_load (state) == FALSE)
		g_warning ("Error while loading templates!");

	go_widget_set_tooltip_text
		(GTK_WIDGET (state->category),
		 _((state->current_category_group->description != NULL)
		   ? state->current_category_group->description
		   : state->current_category_group->name));

	previews_load (state, 0);
	cb_check_item_toggled (NULL, state);
	cb_canvas_button_press (state->canvas[0], NULL, state);
}

static void
cb_gridlines_item_toggled (G_GNUC_UNUSED GtkCheckMenuItem *item,
			   AutoFormatState *state)
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

static int
category_group_cmp (gconstpointer a, gconstpointer b)
{
	FormatTemplateCategoryGroup const *group_a = a;
	FormatTemplateCategoryGroup const *group_b = b;
	return g_utf8_collate (_(group_a->name), _(group_b->name));
}

static gboolean
cb_canvas_focus (GtkWidget *canvas, GtkDirectionType direction,
		 AutoFormatState *state)
{
	if (!GTK_WIDGET_HAS_FOCUS (canvas)) {
		gtk_widget_grab_focus (canvas);
		cb_canvas_button_press (FOO_CANVAS (canvas), NULL, state);
		return TRUE;
	}
	return FALSE;
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
dialog_autoformat (WBCGtk *wbcg)
{
	GladeXML *gui;
	AutoFormatState *state;
	int i;

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"autoformat.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new0 (AutoFormatState, 1);
	state->wb              = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->wbcg            = wbcg;
	state->gui             = gui;
	state->templates       = NULL;
	state->category_groups = NULL;
	state->selrect         = NULL;
	for (i = 0; i < NUM_PREVIEWS; i++)
		state->grid[i] = NULL;

	state->current_category_group  = NULL;
	state->preview_top       = 0;
	state->preview_index     = -1;
	state->previews_locked   = FALSE;
	state->more_down         = FALSE;
	state->selected_template = NULL;

	state->dialog     = GTK_DIALOG (glade_xml_get_widget (gui, "dialog"));
	state->category   = GTK_COMBO_BOX (glade_xml_get_widget (gui, "format_category"));
	state->scroll     = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "format_scroll"));
	state->gridlines  = GTK_CHECK_MENU_ITEM  (glade_xml_get_widget (gui, "format_gridlines"));

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

		name = g_strdup_printf ("format_frame%d", i+1);
		state->frame[i] = GTK_FRAME (glade_xml_get_widget (gui, name));
		g_free (name);

		state->canvas[i] = FOO_CANVAS (foo_canvas_new ());
		gtk_widget_set_size_request (GTK_WIDGET (state->canvas[i]),
			TOTAL_WIDTH + (2 * BORDER),
			TOTAL_HEIGHT + (2 * BORDER));
		gtk_container_add (GTK_CONTAINER (state->frame[i]),
				   GTK_WIDGET (state->canvas[i]));

		g_signal_connect (G_OBJECT (state->canvas[i]),
			"button-press-event",
			G_CALLBACK (cb_canvas_button_press), state);
		g_signal_connect (G_OBJECT (state->canvas[i]),
			"focus",
			G_CALLBACK (cb_canvas_focus), state);
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
	g_signal_connect_swapped (G_OBJECT (state->cancel), "clicked",
		G_CALLBACK (gtk_widget_destroy), state->dialog);

	/* Fill category list */
	state->category_groups =
		g_list_sort (category_group_list_get (),  category_group_cmp);

	if (state->category_groups == NULL) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (state->dialog),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_WARNING,
				GTK_BUTTONS_CLOSE,
				_("An error occurred while reading the category list"));
		gtk_dialog_run (GTK_DIALOG (dialog));
	} else {
		unsigned i, select = 0;
		GList *ptr = state->category_groups;
		GtkListStore* store = gtk_list_store_new (1, G_TYPE_STRING);
		GtkTreeIter iter;
		GtkCellRenderer *renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
		gtk_combo_box_set_model (state->category, GTK_TREE_MODEL (store));
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->category), renderer, TRUE);
		gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (state->category), renderer,
											"text", 0,
											NULL);

		for (i = 0 ; ptr != NULL ; ptr = ptr->next, i++) {
			FormatTemplateCategoryGroup *group = ptr->data;
			if (!strcmp (group->name,   "General" ))
				select = i;
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
						0, _(group->name),
						-1);
		}

		g_signal_connect_swapped (G_OBJECT (state->category),
			"changed",
			G_CALLBACK (cb_category_changed), state);
		gtk_combo_box_set_active (GTK_COMBO_BOX (state->category), select);
		gtk_widget_show_all (GTK_WIDGET (state->category));
	}

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_AUTOFORMAT);

	gtk_dialog_set_default_response (state->dialog, GTK_RESPONSE_OK);

	/* a candidate for merging into attach guru */
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_autoformat_destroy);

	/* not show all or the scrollbars will appear */
	gtk_widget_show (GTK_WIDGET (state->dialog));
}
