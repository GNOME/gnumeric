/*
 * dialog-workbook-attr.c:  Implements a dialog to set workbook attributes.
 *
 * Author:
 *  JP Rosevear <jpr@arcavia.com>
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

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <workbook-view.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <workbook-priv.h>

#define WORKBOOK_ATTRIBUTE_KEY "workbook-attribute-dialog"

enum {
	ITEM_ICON,
	ITEM_NAME,
	PAGE_NUMBER,
	NUM_COLUMNS
};

typedef struct {
	GtkBuilder	*gui;
	GtkWidget	*dialog;
	GtkWidget	*notebook;
	GtkWidget	*ok_button;
	GtkWidget	*apply_button;
	gboolean         destroying;

	Workbook	 *wb;
	WorkbookView     *wbv;
	WBCGtk	 *wbcg;

	GtkTreeStore            *store;
	GtkTreeView             *tview;
} AttrState;

/*****************************************************************************/
/* Some utility routines shared by all pages */

/* Default to the 'View' page but remember which page we were on between
 * invocations */
static int attr_dialog_page = 0;

/*****************************************************************************/

static void
cb_widget_changed (GtkWidget *widget, AttrState *state)
{
	char const *key;

	key = g_object_get_data (G_OBJECT (widget), "GNUMERIC:VIEWPROPERTY");
	g_object_set (G_OBJECT (state->wbv),
		      key, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)),
		      NULL);
}

static void
cb_attr_dialog_dialog_close (G_GNUC_UNUSED GtkWidget *button,
			     AttrState *state)
{
	state->destroying = TRUE;
	gtk_widget_destroy (state->dialog);
}

static void
cb_attr_dialog_dialog_destroy (AttrState *state)
{
	if (state->gui != NULL) {
		g_object_unref (state->gui);
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

/*****************************************************************************/

static void
attr_dialog_init_toggle (AttrState *state, char const *name, char const *key)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, name);
	gboolean val = FALSE;

	g_object_get (G_OBJECT (state->wbv), key, &val, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), val);

	g_signal_connect (G_OBJECT (w),
		"toggled",
		G_CALLBACK (cb_widget_changed), state);
	g_object_set_data (G_OBJECT (w), "GNUMERIC:VIEWPROPERTY", (gpointer) key);
	return;
}

static void
attr_dialog_init_widget_page (AttrState *state)
{
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::show_horizontal_scrollbar", "show_horizontal_scrollbar");
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::show_vertical_scrollbar", "show_vertical_scrollbar");
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::show_notebook_tabs", "show_notebook_tabs");
}

static void
attr_dialog_init_autocompletion_page (AttrState *state)
{
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::do_auto_completion", "do_auto_completion");
}

static void
attr_dialog_init_cell_marker_page (AttrState *state)
{
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::show_function_cell_markers",
		 "show_function_cell_markers");
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::show_extension_markers",
		 "show_extension_markers");
}

static void
attr_dialog_init_protection_page (AttrState *state)
{
	attr_dialog_init_toggle
		(state,
		 "WorkbookView::workbook_protected", "protected");
}
/*****************************************************************************/


static void
attr_dialog_add_item (AttrState *state, char const *page_name,
		      char const *icon_name,
		      int page, char const* parent_path)
{
	GtkTreeIter iter, parent;
	GdkPixbuf *icon = icon_name
		? go_gtk_widget_render_icon_pixbuf (GTK_WIDGET (wbcg_toplevel (state->wbcg)), icon_name, GTK_ICON_SIZE_MENU)
		: NULL;

	if (parent_path && gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (state->store),
								&parent, parent_path))
		gtk_tree_store_append (state->store, &iter, &parent);
	else
		gtk_tree_store_append (state->store, &iter, NULL);

	gtk_tree_store_set (state->store, &iter,
			    ITEM_ICON, icon,
			    ITEM_NAME, _(page_name),
			    PAGE_NUMBER, page,
			    -1);
	if (icon != NULL)
		g_object_unref (icon);
}

typedef struct {
	char const *page_name;
	char const *icon_name;
	char const *parent_path;
	int  const page;
	void (*page_initializer) (AttrState *state);
} page_info_t;

static page_info_t const page_info[] = {
	{N_("Widgets"),         "gnumeric-object-scrollbar",     NULL, 0, &attr_dialog_init_widget_page          },
	{N_("Protection"),      GTK_STOCK_DIALOG_AUTHENTICATION, NULL, 1 ,&attr_dialog_init_protection_page      },
	{N_("Auto Completion"), NULL,                            NULL, 2 ,&attr_dialog_init_autocompletion_page  },
	{N_("Cell Markers"), NULL,                               NULL, 3 ,&attr_dialog_init_cell_marker_page     },
	{NULL, NULL, NULL, -1, NULL},
};

typedef struct {
	int  const page;
	GtkTreePath *path;
} page_search_t;

static gboolean
attr_dialog_select_page_search (GtkTreeModel *model,
				GtkTreePath *path,
				GtkTreeIter *iter,
				page_search_t *pst)
{
	int page;
	gtk_tree_model_get (model, iter, PAGE_NUMBER, &page, -1);
	if (page == pst->page) {
		pst->path = gtk_tree_path_copy (path);
		return TRUE;
	} else
		return FALSE;
}

static void
attr_dialog_select_page (AttrState *state, int page)
{
	page_search_t pst = {page, NULL};

	if (page >= 0)
		gtk_tree_model_foreach (GTK_TREE_MODEL (state->store),
					(GtkTreeModelForeachFunc) attr_dialog_select_page_search,
					&pst);

	if (pst.path == NULL)
		pst.path = gtk_tree_path_new_from_string ("0");

	if (pst.path != NULL) {
		gtk_tree_view_set_cursor (state->tview, pst.path, NULL, FALSE);
		gtk_tree_view_expand_row (state->tview, pst.path, TRUE);
		gtk_tree_path_free (pst.path);
	}
}

static void
cb_attr_dialog_selection_changed (GtkTreeSelection *selection,
				  AttrState *state)
{
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->store), &iter,
				    PAGE_NUMBER, &attr_dialog_page,
				    -1);
		gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook), attr_dialog_page);
	} else
		attr_dialog_select_page (state, attr_dialog_page);
}

/*****************************************************************************/

static void
attr_dialog_impl (AttrState *state)
{
	GtkWidget *dialog = go_gtk_builder_get_widget (state->gui, "WorkbookAttr");
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	int i;

	g_return_if_fail (dialog != NULL);

	/* Initialize */
	state->dialog			= dialog;
	state->notebook                 = go_gtk_builder_get_widget (state->gui, "notebook");
	state->destroying               = FALSE;

	state->tview = GTK_TREE_VIEW(go_gtk_builder_get_widget (state->gui, "itemlist"));
	state->store = gtk_tree_store_new (NUM_COLUMNS,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   G_TYPE_INT);
	gtk_tree_view_set_model (state->tview, GTK_TREE_MODEL(state->store));
	g_object_unref (state->store);
	selection = gtk_tree_view_get_selection (state->tview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_pixbuf_new (),
							   "pixbuf", ITEM_ICON,
							   NULL);
	gtk_tree_view_append_column (state->tview, column);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME,
							   NULL);
	gtk_tree_view_append_column (state->tview, column);
	gtk_tree_view_set_expander_column (state->tview, column);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_attr_dialog_selection_changed), state);

	for (i = 0; page_info[i].page > -1; i++) {
		const page_info_t *this_page =  &page_info[i];
		this_page->page_initializer (state);
		attr_dialog_add_item (state, this_page->page_name, this_page->icon_name,
					       this_page->page, this_page->parent_path);
	}

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (state->store), ITEM_NAME, GTK_SORT_ASCENDING);

	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (state->gui, "close_button")),
			  "clicked",
			  G_CALLBACK (cb_attr_dialog_dialog_close), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_WORKBOOK_ATTRIBUTE);

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (dialog),
		"state", state, (GDestroyNotify) cb_attr_dialog_dialog_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       WORKBOOK_ATTRIBUTE_KEY);
	gtk_widget_show (state->dialog);
}

void
dialog_workbook_attr (WBCGtk *wbcg)
{
	GtkBuilder   *gui;
	AttrState    *state;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, WORKBOOK_ATTRIBUTE_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/workbook-attr.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (AttrState, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->wbv  = wb_control_view (GNM_WBC (wbcg));
	state->wb   = wb_control_get_workbook (GNM_WBC (wbcg));

	attr_dialog_impl (state);

	/* Select the same page the last invocation used */
	attr_dialog_select_page (state, attr_dialog_page);
}
