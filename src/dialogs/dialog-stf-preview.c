/*
 * dialog-stf-preview.c : by utilizing the stf-parse engine this unit can
 *                        render sheet previews and offers
 *                        functions for making this preview more interactive.
 *
 * Copyright 2001 Almer S. Tigelaar <almer@gnome.org>
 * Copyright 2003 Morten Welinder <terra@gnome.org>
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
#include <dialogs/dialog-stf-preview.h>
#include <dialogs/dialog-stf.h>

#include <gnm-format.h>
#include <number-match.h>
#include <value.h>
#include <style.h>
#include <string.h>

/******************************************************************************************************************
 * ADVANCED DRAWING FUNCTIONS
 ******************************************************************************************************************/

enum { ITEM_LINENO };

static void
line_renderer_func (GtkTreeViewColumn *tvc,
		    GtkCellRenderer   *cr,
		    GtkTreeModel      *model,
		    GtkTreeIter       *iter,
		    gpointer           user_data)
{
	RenderData_t *renderdata = user_data;
	unsigned row, col;
	GPtrArray *line;
	const char *text;

	gtk_tree_model_get (model, iter, ITEM_LINENO, &row, -1);
	col = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (tvc), "col-no"));

	line = (renderdata->lines && row < renderdata->lines->len)
		? g_ptr_array_index (renderdata->lines, row)
		: NULL;
	text = (line && col < line->len)
		? g_ptr_array_index (line, col)
		: NULL;

	if (text) {
		char *copy = NULL;
		char *tab = strchr (text, '\t');
		if (tab) {
			copy = g_strdup (text);
			tab = copy + (tab - text);
			do {
				*tab = ' ';
				tab = strchr (tab + 1, '\t');
			} while (tab);
			text = copy;
		}

		/*
		 * Throwing really long strings at Gtk+ is known to cause
		 * trouble, so cut long strings and hope no-one notices.
		 */
		if (g_utf8_strlen (text, -1) > STF_LINE_LENGTH_LIMIT) {
			char *cut = g_strdup (text);
			strcpy (g_utf8_offset_to_pointer (cut, STF_LINE_LENGTH_LIMIT - 3),
				"...");
			g_free (copy);
			text = copy = cut;
		}

		g_object_set (cr, "text", text, NULL);
		g_free (copy);
	} else
		g_object_set (cr, "text", "", NULL);
}

static GtkTreeModel *
make_model (GPtrArray *lines)
{
	GtkListStore *list_store = gtk_list_store_new (1, G_TYPE_UINT);
	unsigned ui;
	unsigned count = lines ? MIN (lines->len, STF_LINE_DISPLAY_LIMIT) : 0;

	for (ui = 0; ui < count; ui++) {
		GtkTreeIter iter;
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, ITEM_LINENO, ui, -1);
	}

	return GTK_TREE_MODEL (list_store);
}



/******************************************************************************************************************
 * STRUCTURE MANIPULATION FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_new: (skip)
 * @data_container: a container in which to put a treeview.
 *
 * returns : a new renderdata struct
 **/
RenderData_t *
stf_preview_new (GtkWidget *data_container,
		 GODateConventions const *date_conv)
{
	RenderData_t* renderdata;
	GtkTreeModel *model;

	g_return_val_if_fail (data_container != NULL, NULL);

	renderdata = g_new (RenderData_t, 1);

	renderdata->data_container = data_container;
	renderdata->startrow       = 1;
	renderdata->colformats     = g_ptr_array_new ();
	renderdata->ignore_formats = FALSE;
	renderdata->lines_chunk    = NULL;
	renderdata->lines          = NULL;

	renderdata->date_conv	   = date_conv;

	model = make_model (NULL);
	renderdata->tree_view =
		GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
	gtk_tree_view_set_grid_lines (renderdata->tree_view,
				      GTK_TREE_VIEW_GRID_LINES_VERTICAL);
	g_object_ref (renderdata->tree_view);
	g_object_unref (model);

	renderdata->colcount = 0;

	{
		GtkWidget *w = GTK_WIDGET (renderdata->tree_view);
		int width, height, vertical_separator;
		PangoLayout *layout =
			gtk_widget_create_pango_layout (w, "Mg19");

		gtk_widget_style_get (w,
				      "vertical_separator", &vertical_separator,
				      NULL);

		pango_layout_get_pixel_size (layout, &width, &height);
		/*
		 * Make room for about 80 characters and about 7 lines of data.
		 * (The +2 allows room for the headers and the h-scrollbar.
		 */
		gtk_widget_set_size_request (renderdata->data_container,
					     width * 20,  /* About 80 chars.  */
					     (height + vertical_separator) * (7 + 2));
		g_object_unref (layout);
	}

	gtk_container_add (GTK_CONTAINER (renderdata->data_container),
			   GTK_WIDGET (renderdata->tree_view));
	gtk_widget_show_all (GTK_WIDGET (renderdata->tree_view));

	return renderdata;
}

/**
 * stf_preview_free: (skip)
 * @data: a renderdata struct
 *
 * This will free the @renderdata
 **/
void
stf_preview_free (RenderData_t *renderdata)
{
	g_return_if_fail (renderdata != NULL);

	stf_preview_colformats_clear (renderdata);
	g_ptr_array_free (renderdata->colformats, TRUE);

	stf_preview_set_lines (renderdata, NULL, NULL);

	g_object_unref (renderdata->tree_view);

	g_free (renderdata);
}

/**
 * stf_preview_set_lines: (skip)
 */
void
stf_preview_set_lines (RenderData_t *renderdata,
		       GStringChunk *lines_chunk,
		       GPtrArray *lines)
{
	unsigned int i;
	int colcount = 1;
	GtkTreeModel *model;
	gboolean hidden;

	g_return_if_fail (renderdata != NULL);

	/* Empty the table.  */
	gtk_tree_view_set_model (renderdata->tree_view, NULL);

	if (renderdata->lines != lines) {
		if (renderdata->lines)
			stf_parse_general_free (renderdata->lines);
		renderdata->lines = lines;
	}

	if (renderdata->lines_chunk != lines_chunk) {
		if (renderdata->lines_chunk)
			g_string_chunk_free (renderdata->lines_chunk);
		renderdata->lines_chunk = lines_chunk;
	}

	if (lines == NULL)
		return;

	for (i = 0; i < lines->len; i++) {
		GPtrArray *line = g_ptr_array_index (lines, i);
		colcount = MAX (colcount, (int)line->len);
	}

	/*
	 * If we are making large changes we need to hide the treeview
	 * because performance otherwise suffers a lot.
	 */
	hidden = gtk_widget_get_visible (GTK_WIDGET (renderdata->tree_view)) &&
		(colcount < renderdata->colcount - 1 ||
		 colcount > renderdata->colcount + 10);
	if (hidden)
		gtk_widget_hide (GTK_WIDGET (renderdata->tree_view));

	while (renderdata->colcount > colcount)
               gtk_tree_view_remove_column
                       (renderdata->tree_view,
                        gtk_tree_view_get_column (renderdata->tree_view,
                                                  --(renderdata->colcount)));

	while (renderdata->colcount < colcount) {
		char *text = g_strdup_printf (_(COLUMN_CAPTION),
					      renderdata->colcount + 1);
		GtkCellRenderer *cr = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *tvc = gtk_tree_view_column_new ();

		g_object_set (cr, "single_paragraph_mode", TRUE, NULL);

		gtk_tree_view_column_set_title (tvc, text);
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr,
			 line_renderer_func,
			 renderdata, NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);

		g_object_set_data (G_OBJECT (tvc), "col-no",
				   GUINT_TO_POINTER (renderdata->colcount));
		gtk_tree_view_append_column (renderdata->tree_view, tvc);
		g_free (text);
		renderdata->colcount++;
	}

	model = make_model (lines);
	gtk_tree_view_set_model (renderdata->tree_view, model);
	g_object_unref (model);

	if (hidden)
		gtk_widget_show (GTK_WIDGET (renderdata->tree_view));
}

/**
 * stf_preview_colformats_clear: (skip)
 * @renderdata: a struct containing rendering information
 *
 * This will clear the @renderdata->colformats array which contains the format of
 * each column.
 **/
void
stf_preview_colformats_clear (RenderData_t *renderdata)
{
	guint i;
	g_return_if_fail (renderdata != NULL);

	for (i = 0; i < renderdata->colformats->len; i++)
		go_format_unref (g_ptr_array_index (renderdata->colformats, i));
	g_ptr_array_free (renderdata->colformats, TRUE);
	renderdata->colformats = g_ptr_array_new ();
}

/**
 * stf_preview_colformats_add: (skip)
 * @renderdata: a struct containing rendering information
 * @format: the format of the column
 *
 * This will add an entry to the @renderdata->colformats array.
 * The widths of the columns will be set to at least have the width of
 * the @format.
 **/
void
stf_preview_colformats_add (RenderData_t *renderdata, GOFormat *format)
{

	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (format != NULL);

	g_ptr_array_add (renderdata->colformats, go_format_ref (format));
}

/**
 * stf_preview_get_column: (skip)
 */
GtkTreeViewColumn *
stf_preview_get_column (RenderData_t *renderdata, int col)
{
	return gtk_tree_view_get_column (renderdata->tree_view, col);
}

/**
 * stf_preview_get_cell_renderer: (skip)
 */
GtkCellRenderer *
stf_preview_get_cell_renderer (RenderData_t *renderdata, int col)
{
	GtkCellRenderer *res = NULL;
	GtkTreeViewColumn *column = stf_preview_get_column (renderdata, col);
	if (column) {
		GList *renderers = gtk_cell_layout_get_cells
			(GTK_CELL_LAYOUT(column));
		if (renderers) {
			res = renderers->data;
			g_list_free (renderers);
		}
	}
	return res;
}

void
stf_preview_find_column (RenderData_t *renderdata, int x, int *pcol, int *dx)
{
	int col;

	*dx = 0;

	/* Figure out what column we pressed in.  */
	for (col = 0; TRUE; col++) {
		int cx, cw;
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, col);
		GtkCellRenderer *cell =
			stf_preview_get_cell_renderer (renderdata, col);
		int padx;

		if (!column || !cell)
			break;

		gtk_cell_renderer_get_padding (cell, &padx, NULL);
		cx = gtk_tree_view_column_get_x_offset (column);
		cw = gtk_tree_view_column_get_width (column);

		if (x < (cx + padx) + cw) {
			*dx = x - (cx + padx);
			break;
		}
	}

	*pcol = col;
}
