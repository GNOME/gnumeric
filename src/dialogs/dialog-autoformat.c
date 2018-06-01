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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * WORKING NOTE : Once the edit dialog is ready, search for FIXME and
 * remove the disabling of new/edit/remove buttons!
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

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

#include <goffice/goffice.h>
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
	GocItem		   *grid[NUM_PREVIEWS];              /* Previewgrid's */
	GocItem		   *selrect;                         /* Selection rectangle */
	GSList             *templates;                       /* List of GnmFT's */
	GnmFT  *selected_template;               /* The currently selected template */
	GList              *category_groups;                 /* List of groups of categories */

	GnmFTCategoryGroup *current_category_group; /* Currently selected category group */

	int               preview_top;       /* Top index of the previewlist */
	int               preview_index;     /* Selected canvas in previewlist */
	gboolean          previews_locked;   /* If true, the preview_free and preview_load will not function */
	gboolean          more_down;         /* If true, more was clicked and the button caption is now 'Less' */

	/*
	 * Gui elements
	 */
	GtkDialog      *dialog;

	GtkComboBox    *category;

	GocCanvas	 *canvas[NUM_PREVIEWS];
	GtkFrame         *frame[NUM_PREVIEWS];
	GtkScrollbar    *scroll;
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
	GnmPreviewGrid base;
	GnmFT *ft;
} AutoFormatGrid;
typedef GnmPreviewGridClass AutoFormatGridClass;

static GnmStyle *
afg_get_cell_style (GnmPreviewGrid *pg, int col, int row)
{
	/* If this happens to be NULL the default style
	 * will automatically be used. */
	AutoFormatGrid *ag = (AutoFormatGrid *) pg;
	return gnm_ft_get_style (ag->ft, row, col);
}

static GnmValue *
afg_get_cell_value (G_GNUC_UNUSED GnmPreviewGrid *pg, int col, int row)
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
auto_format_grid_class_init (GnmPreviewGridClass *klass)
{
	klass->get_cell_style = afg_get_cell_style;
	klass->get_cell_value = afg_get_cell_value;
}

static GSF_CLASS (AutoFormatGrid, auto_format_grid,
		  auto_format_grid_class_init, NULL,
		  gnm_preview_grid_get_type())

static GocItem *
auto_format_grid_new (AutoFormatState *state, int i, GnmFT *ft)
{
	GocItem *item = goc_item_new (
		goc_canvas_get_root (state->canvas[i]),
		auto_format_grid_get_type (),
		"render-gridlines",	gtk_check_menu_item_get_active (state->gridlines),
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
		gnm_ft_free (ptr->data);
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
 * Returns: %TRUE if all went well, %FALSE otherwise.
 **/
static gboolean
templates_load (AutoFormatState *state)
{
	GSList *l;
	gint n_templates;

	g_return_val_if_fail (state != NULL, FALSE);

	if (state->category_groups == NULL)
		return FALSE;

	state->templates = gnm_ft_category_group_get_templates_list (
		state->current_category_group, GO_CMD_CONTEXT (state->wbcg));
	for (l = state->templates; l != NULL; l = l->next) {
		GnmFT *ft = l->data;
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
		gtk_adjustment_configure (adjustment,
					  0, 0, n_templates / 2,
					  1, 3, 3);
	}
	state->previews_locked = FALSE;

	/*
	 * Hide the scrollbar when it's not needed
	 */
	gtk_widget_set_visible (GTK_WIDGET (state->scroll),
				n_templates > NUM_PREVIEWS);

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

	if (state->selrect) {
		goc_item_destroy (state->selrect);
		state->selrect = NULL;
	}

	for (i = 0; i < NUM_PREVIEWS; i++) {
		GocItem *item = state->grid[i];
		if (item) {
			goc_item_destroy (state->grid[i]);
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
 * NOTE : if state->preview_locked is %TRUE this function will do nothing,
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
			GnmFT *ft = start->data;

			state->grid[i] = auto_format_grid_new (state, i, ft);

			/* Are we selected? Then draw a selection rectangle */
			if (topindex + i == state->preview_index) {
				GOStyle *style;
				g_return_if_fail (state->selrect == NULL);

				state->selrect = goc_item_new (goc_canvas_get_root (state->canvas[i]),
					GOC_TYPE_RECTANGLE,
					"x", (double)(-INNER_BORDER),
					"y", (double)(-INNER_BORDER),
					"width", (double)(TOTAL_WIDTH + 2 * INNER_BORDER),
					"height", (double)(TOTAL_HEIGHT + 2 * INNER_BORDER),
					NULL);
				style = go_styled_object_get_style (GO_STYLED_OBJECT (state->selrect));
				style->line.width = 3.;
				style->line.color = GO_COLOR_RED;
				style->fill.pattern.back = 0;

				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_IN);
			} else
				gtk_frame_set_shadow_type (state->frame[i], GTK_SHADOW_ETCHED_IN);

			goc_canvas_scroll_to (state->canvas[i],
				-BORDER, -BORDER);

			gtk_widget_set_tooltip_text
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
		cmd_selection_autoformat (GNM_WBC (state->wbcg),
			gnm_ft_clone (state->selected_template));

	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_autoformat_destroy (AutoFormatState *state)
{
	templates_free (state);
	gnm_ft_category_group_list_free (state->category_groups);
	g_free (state);
}

static void
cb_scroll_value_changed (GtkAdjustment *adjustment, AutoFormatState *state)
{
	previews_free (state);
	previews_load (state, rint (gtk_adjustment_get_value (adjustment)) * 2);
}

static gboolean
cb_canvas_button_press (GocCanvas *canvas,
			G_GNUC_UNUSED GdkEventButton *event,
			AutoFormatState *state)
{
	GnmFT *ft;
	GSList *ptr;
	int index = 0;

	while (index < NUM_PREVIEWS && canvas != state->canvas[index])
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
	gnm_textview_set_text (GTK_TEXT_VIEW (state->info_descr),
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
		GnmFT *ft = ptr->data;

		ft->number    = gtk_check_menu_item_get_active (state->number);
		ft->border    = gtk_check_menu_item_get_active (state->border);
		ft->font      = gtk_check_menu_item_get_active (state->font);
		ft->patterns  = gtk_check_menu_item_get_active (state->patterns);
		ft->alignment = gtk_check_menu_item_get_active (state->alignment);

		ft->edges.left   = gtk_check_menu_item_get_active (state->edges.left);
		ft->edges.right  = gtk_check_menu_item_get_active (state->edges.right);
		ft->edges.top    = gtk_check_menu_item_get_active (state->edges.top);
		ft->edges.bottom = gtk_check_menu_item_get_active (state->edges.bottom);

		ft->invalidate_hash = TRUE;
	}

	for (i = 0; i < NUM_PREVIEWS; i++)
		goc_canvas_invalidate (state->canvas [i],
			-2, -2, INT_MAX/2, INT_MAX/2);
}

static void
cb_category_changed (AutoFormatState *state)
{
	GList *selection = g_list_nth (state->category_groups,
		gtk_combo_box_get_active (state->category));
	char const *tip = NULL;

	state->current_category_group = (selection != NULL) ? selection->data : NULL;
	previews_free (state);
	templates_free (state);
	if (templates_load (state) == FALSE)
		g_warning ("Error while loading templates!");

	if (NULL != state->current_category_group) {
		tip = state->current_category_group->description;
		if (NULL == tip)
			tip = state->current_category_group->name;
	}
	gtk_widget_set_tooltip_text (GTK_WIDGET (state->category),
		(NULL != tip) ? _(tip) : "");
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

static gboolean
cb_canvas_focus (GtkWidget *canvas,
		 G_GNUC_UNUSED GtkDirectionType direction,
		 AutoFormatState *state)
{
	if (!gtk_widget_has_focus (canvas)) {
		gtk_widget_grab_focus (canvas);
		cb_canvas_button_press (GOC_CANVAS (canvas), NULL, state);
		return TRUE;
	}
	return FALSE;
}

/**
 * dialog_autoformat:
 * @wbcg: the control that invoked this dialog
 *
 * This function will show the AutoFormatTemplate dialog and apply
 * the template the user chooses to the current selection in the active
 * sheet of the workbook if the user desires.
 **/
void
dialog_autoformat (WBCGtk *wbcg)
{
	GtkBuilder *gui;
	AutoFormatState *state;
	int i;

	gui = gnm_gtk_builder_load ("res:ui/autoformat.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new0 (AutoFormatState, 1);
	state->wb              = wb_control_get_workbook (GNM_WBC (wbcg));
	state->wbcg            = wbcg;
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

	state->dialog     = GTK_DIALOG (go_gtk_builder_get_widget (gui, "dialog"));
	state->category   = GTK_COMBO_BOX (go_gtk_builder_get_widget (gui, "format_category"));
	state->scroll     = GTK_SCROLLBAR (go_gtk_builder_get_widget (gui, "format_scroll"));
	state->gridlines  = GTK_CHECK_MENU_ITEM  (go_gtk_builder_get_widget (gui, "format_gridlines"));

	state->info_name   = GTK_ENTRY (go_gtk_builder_get_widget (gui, "format_info_name"));
	state->info_author = GTK_ENTRY (go_gtk_builder_get_widget (gui, "format_info_author"));
	state->info_cat    = GTK_ENTRY (go_gtk_builder_get_widget (gui, "format_info_cat"));
	state->info_descr  = GTK_TEXT_VIEW (go_gtk_builder_get_widget (gui, "format_info_descr"));

	state->ok     = GTK_BUTTON (go_gtk_builder_get_widget (gui, "format_ok"));
	state->cancel = GTK_BUTTON (go_gtk_builder_get_widget (gui, "format_cancel"));

#define CHECK_ITEM(v_, w_,h_) do {				\
	GtkWidget *w = go_gtk_builder_get_widget (gui, (w_));	\
	state->v_ = GTK_CHECK_MENU_ITEM (w);			\
	g_signal_connect (w, "activate", G_CALLBACK (h_), state);	\
} while (0)

	CHECK_ITEM (number, "number_menuitem", cb_check_item_toggled);
	CHECK_ITEM (border, "border_menuitem", cb_check_item_toggled);
	CHECK_ITEM (font, "font_menuitem", cb_check_item_toggled);
	CHECK_ITEM (patterns, "pattern_menuitem", cb_check_item_toggled);
	CHECK_ITEM (alignment, "alignment_menuitem", cb_check_item_toggled);
	CHECK_ITEM (edges.left, "left_menuitem", cb_check_item_toggled);
	CHECK_ITEM (edges.right, "right_menuitem", cb_check_item_toggled);
	CHECK_ITEM (edges.top, "top_menuitem", cb_check_item_toggled);
	CHECK_ITEM (edges.bottom, "bottom_menuitem", cb_check_item_toggled);
	CHECK_ITEM (gridlines, "gridlines_menuitem", cb_gridlines_item_toggled);

#undef CHECK_ITEM

	for (i = 0; i < NUM_PREVIEWS; i++) {
		char *name;

		name = g_strdup_printf ("format_frame%d", i+1);
		state->frame[i] = GTK_FRAME (go_gtk_builder_get_widget (gui, name));
		g_free (name);

		state->canvas[i] = GOC_CANVAS (g_object_new (GOC_TYPE_CANVAS, NULL));
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

	g_signal_connect (G_OBJECT (gtk_range_get_adjustment (GTK_RANGE (state->scroll))),
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
		g_list_sort (gnm_ft_category_group_list_get (),  gnm_ft_category_group_cmp);

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
		g_object_unref (store);
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->category), renderer, TRUE);
		gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (state->category), renderer,
						"text", 0,
						NULL);

		for (i = 0 ; ptr != NULL ; ptr = ptr->next, i++) {
			GnmFTCategoryGroup *group = ptr->data;
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

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
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
	g_object_unref (gui);
}
