/*
 * cell-comment.c: Cell management of the Gnumeric spreadsheet.
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include "config.h"
#include "gnumeric.h"
#include "cell.h"
#include "cell-comment.h"
#include "cursors.h"
#include "sheet-view.h"
#include "gnumeric-sheet.h"

static void
cell_comment_cancel_timer (Cell *cell)
{
	if (cell->comment->timer_tag != -1) {
		gtk_timeout_remove (cell->comment->timer_tag);
		cell->comment->timer_tag = -1;
	}
}

static void
cell_display_comment (Cell *cell)
{
	GtkWidget *window, *label;
	int x, y;

	g_return_if_fail (cell != NULL);

	cell_comment_cancel_timer (cell);

	window = gtk_window_new (GTK_WINDOW_POPUP);
	label = gtk_label_new (cell->comment->comment->str);
	gtk_container_add (GTK_CONTAINER (window), label);

	gdk_window_get_pointer (NULL, &x, &y, NULL);
	gtk_widget_set_uposition (window, x+10, y+10);

	gtk_widget_show_all (window);

	cell->comment->window = window;
}

static gint
cell_popup_comment (gpointer data)
{
	Cell *cell = data;

	cell->comment->timer_tag = -1;

	cell_display_comment (cell);
	return FALSE;
}

static void
cell_comment_unrealize (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell->comment != NULL);

	sheet_cell_comment_unlink (cell);
	for (l = cell->comment->realized_list; l; l = l->next){
		GnomeCanvasItem *o = l->data;

		gtk_object_unref (GTK_OBJECT (o));
	}
	g_list_free (cell->comment->realized_list);
	cell->comment->realized_list = NULL;
}

static int
cell_comment_clicked (GnomeCanvasItem *item, GdkEvent *event, Cell *cell)
{
	GnomeCanvas *canvas = item->canvas;

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		if (event->button.button != 1)
			return FALSE;
		if (cell->comment->window)
			return FALSE;
		cell_display_comment (cell);
		break;

	case GDK_BUTTON_PRESS:
		if (event->button.button != 1)
			return FALSE;
		break;

	case GDK_ENTER_NOTIFY:
		cell->comment->timer_tag = gtk_timeout_add (1000, &cell_popup_comment, cell);
		cursor_set_widget (canvas, GNUMERIC_CURSOR_ARROW);
		break;

	case GDK_LEAVE_NOTIFY:
		cell_comment_cancel_timer (cell);
		if (cell->comment->window){
			gtk_object_destroy (GTK_OBJECT (cell->comment->window));
			cell->comment->window = NULL;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cell_comment_realize (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell->comment != NULL);

	sheet_cell_comment_link (cell);
	for (l = cell->sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = SHEET_VIEW (l->data);
		GnomeCanvasItem *o;

		o = sheet_view_comment_create_marker (
			sheet_view,
			cell->col_info->pos, cell->row_info->pos);
		gtk_object_ref (GTK_OBJECT (o));

		cell->comment->realized_list = g_list_prepend (
			cell->comment->realized_list, o);

		gtk_signal_connect (GTK_OBJECT (o), "event",
				    GTK_SIGNAL_FUNC (cell_comment_clicked), cell);
	}
}

void
cell_set_comment (Cell *cell, const char *str)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (str != NULL);

	cell_comment_destroy (cell);

	cell->comment = g_new (CellComment, 1);
	cell->comment->realized_list = NULL;
	cell->comment->timer_tag = -1;
	cell->comment->window = NULL;

	cell->comment->comment = string_get (str);

	if (cell->sheet)
		cell_comment_realize (cell);
}

char *
cell_get_comment (Cell *cell)
{
	char *str;

	g_return_val_if_fail (cell != NULL, NULL);

	if (cell->comment)
		str = g_strdup (cell->comment->comment->str);
	else
		str = NULL;

	return str;
}

void
cell_comment_destroy (Cell *cell)
{
	CellComment *comment;
	GList *l;

	g_return_if_fail (cell != NULL);

	comment = cell->comment;
	if (!comment)
		return;
	cell->comment = NULL;

	/* Free resources */
	string_unref (comment->comment);

	if (comment->timer_tag != -1)
		gtk_timeout_remove (comment->timer_tag);

	if (comment->window)
		gtk_object_destroy (GTK_OBJECT (comment->window));

	for (l = comment->realized_list; l; l = l->next)
		gtk_object_destroy (l->data);
	g_list_free (comment->realized_list);

	g_free (comment);
}

void
cell_comment_reposition (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->comment != NULL);

	/* FIXME : This should use the sheet_view list */
	for (l = cell->comment->realized_list; l; l = l->next){
		GnomeCanvasItem *o = l->data;
		SheetView *sheet_view = GNUMERIC_SHEET (o->canvas)->sheet_view;

		sheet_view_comment_relocate (sheet_view, cell->col_info->pos, cell->row_info->pos, o);
	}
}

void
cell_realize (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->comment)
		cell_comment_realize (cell);
}

void
cell_unrealize (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->comment)
		cell_comment_unrealize (cell);
}

