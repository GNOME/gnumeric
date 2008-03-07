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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialog-stf-preview.h"
#include "dialog-stf.h"

#include <gnm-format.h>
#include <number-match.h>
#include <value.h>
#include <style.h>
#include <gtk/gtk.h>
#include <widgets/gnumeric-lazy-list.h>
#include <string.h>

/******************************************************************************************************************
 * ADVANCED DRAWING FUNCTIONS
 ******************************************************************************************************************/

static void
render_get_value (gint row, gint column, gpointer _rd, GValue *value)
{
	RenderData_t *rd = (RenderData_t *)_rd;
	GnumericLazyList *ll = GNUMERIC_LAZY_LIST (gtk_tree_view_get_model (rd->tree_view));
	GPtrArray *lines = rd->lines;
	GPtrArray *line = (row < (int)lines->len)
		? g_ptr_array_index (lines, row)
		: NULL;
	const char *text = (line && column < (int)line->len)
		? g_ptr_array_index (line, column)
		: NULL;

	g_value_init (value, ll->column_headers[column]);

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
		g_value_set_string (value, text);
		g_free (copy);
	}
}


/******************************************************************************************************************
 * STRUCTURE MANIPULATION FUNCTIONS
 ******************************************************************************************************************/

/**
 * stf_preview_new
 * @data_container: a container in which to put a treeview.
 *
 * returns : a new renderdata struct
 **/
RenderData_t*
stf_preview_new (GtkWidget *data_container,
		 GODateConventions const *date_conv)
{
	RenderData_t* renderdata;
	GnumericLazyList *ll;

	g_return_val_if_fail (data_container != NULL, NULL);

	renderdata = g_new (RenderData_t, 1);

	renderdata->data_container = data_container;
	renderdata->startrow       = 1;
	renderdata->colformats     = g_ptr_array_new ();
	renderdata->ignore_formats = FALSE;
	renderdata->lines_chunk    = NULL;
	renderdata->lines          = NULL;

	renderdata->date_conv	   = date_conv;

	ll = gnumeric_lazy_list_new (render_get_value, renderdata, 0, 1, G_TYPE_STRING);
	renderdata->tree_view =
		GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (ll)));
	g_object_ref (renderdata->tree_view);
	g_object_unref (ll);

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
 * stf_preview_free
 * @renderdata : a renderdata struct
 *
 * This will free the @renderdata
 *
 * returns : nothing
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

void
stf_preview_set_lines (RenderData_t *renderdata,
		       GStringChunk *lines_chunk,
		       GPtrArray *lines)
{
	unsigned int i;
	int colcount = 1;
	GnumericLazyList *ll;
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
	 * because performance other wise suffers a lot.
	 */
	hidden = GTK_WIDGET_VISIBLE (GTK_WIDGET (renderdata->tree_view)) &&
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
		GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes
			(text, cell,
			 "text", renderdata->colcount,
			 NULL);
		g_object_set (cell,
			      "single_paragraph_mode", TRUE,
			      NULL);
		gtk_tree_view_append_column (renderdata->tree_view, column);
		g_free (text);
		renderdata->colcount++;
	}

	ll = gnumeric_lazy_list_new (render_get_value, renderdata,
				     MIN (lines->len, LINE_DISPLAY_LIMIT),
				     0);
	gnumeric_lazy_list_add_column (ll, colcount, G_TYPE_STRING);
	gtk_tree_view_set_model (renderdata->tree_view, GTK_TREE_MODEL (ll));
	g_object_unref (ll);

	if (hidden)
		gtk_widget_show (GTK_WIDGET (renderdata->tree_view));
}

/**
 * stf_preview_colformats_clear
 * @renderdata : a struct containing rendering information
 *
 * This will clear the @renderdata->colformats array which contains the format of
 * each column.
 *
 * returns : nothing
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
 * stf_preview_colformats_add
 * @renderdata : a struct containing rendering information
 * @format : the format of the column
 *
 * This will add an entry to the @renderdata->colformats array.
 * The widths of the columns will be set to at least have the width of
 * the @format.
 *
 * returns : nothing
 **/
void
stf_preview_colformats_add (RenderData_t *renderdata, GOFormat *format)
{

	g_return_if_fail (renderdata != NULL);
	g_return_if_fail (format != NULL);

	g_ptr_array_add (renderdata->colformats, go_format_ref (format));
}


GtkTreeViewColumn *
stf_preview_get_column (RenderData_t *renderdata, int col)
{
	return gtk_tree_view_get_column (renderdata->tree_view, col);
}

GtkCellRenderer *
stf_preview_get_cell_renderer (RenderData_t *renderdata, int col)
{
	GtkCellRenderer *res = NULL;
	GtkTreeViewColumn *column = stf_preview_get_column (renderdata, col);
	if (column) {
		GList *renderers =
			gtk_tree_view_column_get_cell_renderers (column);
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

	/* Figure out what column we pressed in.  */
	for (col = 0; 1; col++) {
		GtkWidget *w;
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, col);
		if (!column)
			break;
		w = GTK_BIN (column->button)->child;
		if (x < w->allocation.x + w->allocation.width) {
			*dx = x - w->allocation.x;
			break;
		}
	}

	*pcol = col;
	*dx = 0;
}
