/* vim: set sw=8: */
/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * (C) 1999-2001 Miguel de Icaza & Jody Goldberg
 *
 * This module provides:
 *   * Integration of an in-sheet text editor (GtkEntry) with the Workbook
 *     GtkEntry as a canvas item.
 *
 *   * Feedback on expressions in the spreadsheet (referenced cells or
 *     ranges are highlighted on the spreadsheet).
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "item-edit.h"

#include "item-cursor.h"
#define GNUMERIC_ITEM "EDIT"
#include "item-debug.h"
#include "gnumeric-canvas.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
#include "sheet-style.h"
#include "value.h"
#include "ranges.h"
#include "style.h"
#include "parse-util.h"
#include "sheet-merge.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gui-util.h"

#include <ctype.h>
#include <string.h>

static GnomeCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,	/* The SheetControlGUI * argument */
};

static void
scan_at (const char *text, int *scan)
{
	int i;
	for (i = *scan ; i > 0 ; i--) {
		unsigned char const c = text [i-1];

		if (!(c == ':' || c == '$' || isalnum (c)))
			break;
	}
	*scan = i;
}

/*
 * This routine could definitely be better.
 *
 * Currently it will not handle ranges like R[-1]C1, nor named ranges,
 * nor will it detect whether something is part of an expression or a
 * string .
 */
static gboolean
point_is_inside_range (ItemEdit *item_edit, const char *text, Range *range)
{
	Value *v;
	Sheet *sheet = ((SheetControl *) item_edit->scg)->sheet;
	int cursor_pos, scan;

	if ((text = gnumeric_char_start_expr_p (text)) == NULL)
		return FALSE;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (item_edit->entry));
	if (cursor_pos == 0)
		return FALSE;
	cursor_pos--;

	scan = cursor_pos;
	scan_at (text, &scan);

	/* If the range is on another sheet ignore it */
	if (scan > 0 && text [scan-1] == '!')
		return FALSE;

	if ((v = range_parse (sheet, &text [scan], FALSE)) != NULL)
		return setup_range_from_value (range, v, TRUE);

	if (scan == cursor_pos && scan > 0)
		scan--;
	scan_at (text, &scan);

	/* If the range is on another sheet ignore it */
	if (scan > 0 && text [scan-1] == '!')
		return FALSE;

	if ((v = range_parse (sheet, &text [scan], FALSE)) != NULL)
		return setup_range_from_value (range, v, TRUE);

	return FALSE;
}

static void
entry_create_feedback_range (ItemEdit *item_edit, Range *r)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	if (!item_edit->feedback_cursor)
		item_edit->feedback_cursor = gnome_canvas_item_new (
			GNOME_CANVAS_GROUP (item->canvas->root),
			item_cursor_get_type (),
			"SheetControlGUI",  item_edit->scg,
			"Style",  ITEM_CURSOR_BLOCK,
			"Color",  "red",
			NULL);

	item_cursor_bound_set (ITEM_CURSOR (item_edit->feedback_cursor), r);
}

static void
entry_destroy_feedback_range (ItemEdit *item_edit)
{
	if (item_edit->feedback_cursor){
		gtk_object_destroy (GTK_OBJECT (item_edit->feedback_cursor));
		item_edit->feedback_cursor = NULL;
	}
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
scan_for_range (ItemEdit *item_edit, const char *text)
{
	Range range;

	if (point_is_inside_range (item_edit, text, &range)){
		if (!item_edit->feedback_disabled)
			entry_create_feedback_range (item_edit, &range);
	} else
		entry_destroy_feedback_range (item_edit);
}

static void
item_edit_draw_cursor (ItemEdit *item_edit, GdkDrawable *drawable, GtkStyle *style,
		       int x, int y, GdkFont *font)
{
	if (!item_edit->cursor_visible)
		return;

	gdk_draw_line (drawable, style->black_gc,
		       x, y-font->ascent,
		       x, y+font->descent);
}

static void
item_edit_draw_text (ItemEdit *item_edit, GdkDrawable *drawable, GtkStyle *style,
		     int x1, int y, int w,
		     char const *text, int text_length, int cursor_pos)
{
	GdkFont *font = item_edit->font;

	GdkGC *gc = style->black_gc;

	/* skip leading newlines */
	if (*text == '\n') {
		text++;
		text_length--;
		cursor_pos--;
	}

	/* If this segment contains the cursor draw it */
	if (0 <= cursor_pos && cursor_pos <= text_length) {
		if (cursor_pos > 0) {
			gdk_draw_text (drawable, font, gc, x1, y, text, cursor_pos);
			x1 += gdk_text_width (font, text, cursor_pos);
			text += cursor_pos;
			text_length -= cursor_pos;
			cursor_pos = 0;
		}

		item_edit_draw_cursor (item_edit, drawable, style, x1, y, font);
	}

	if (text_length > 0) {
		if (text_length > cursor_pos &&
		    wbcg_auto_completing (item_edit->scg->wbcg)) {
			if (w < 0)
				w = gdk_text_width (font, text, text_length);
			gdk_draw_rectangle (drawable, style->black_gc, TRUE,
					    x1, y - font->ascent, w,
					    font->ascent + font->descent);
			gc = style->white_gc;
		}

		gdk_draw_text (drawable, font, gc, x1, y, text, text_length);
	}
}

static void
item_edit_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int x, int y, int width, int height)
{
	GtkWidget *canvas   = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	SheetControl *sc = (SheetControl *) item_edit->scg;
	ColRowInfo const *ci = sheet_col_get_info (sc->sheet,
						   item_edit->pos.col);
	int const left_pos = ((int)item->x1) + ci->margin_a - x;

	/* NOTE : This does not handle vertical alignment yet so there may be some
	 * vertical jumping when edit.
	 */
	int top_pos = ((int)item->y1) - y + 1; /* grid line */
	int text_offset = 0;
	int cursor_pos = gtk_editable_get_position (GTK_EDITABLE (item_edit->entry));
	GSList *ptr;
	char const *text;

	/* no drawing until the font is set */
	if (item_edit->font == NULL)
		return;
	top_pos += item_edit->font->ascent;

       	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (
		drawable, canvas->style->white_gc, TRUE,
		((int)item->x1)-x, ((int)item->y1)-y,
		(int)(item->x2-item->x1),
		(int)(item->y2-item->y1));

	/* Make a number of tests for auto-completion */
	text = wbcg_edit_get_display_text (item_edit->scg->wbcg);

	for (ptr = item_edit->text_offsets; ptr != NULL; ptr = ptr->next){
		int const text_end = GPOINTER_TO_INT (ptr->data);

		item_edit_draw_text (item_edit, drawable, canvas->style,
				     left_pos, top_pos, width,
				     text + text_offset,
				     text_end - text_offset,
				     cursor_pos - text_offset);
		text_offset = text_end;
		top_pos += item_edit->font_height;
	}

	/* draw the remainder */
	item_edit_draw_text (item_edit, drawable, canvas->style,
			     left_pos, top_pos, -1,
			     text + text_offset,
			     strlen (text + text_offset),
			     cursor_pos - text_offset);
}

static double
item_edit_point (GnomeCanvasItem *item, double c_x, double c_y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = NULL;
	if ((cx < item->x1) || (cy < item->y1) || (cx >= item->x2) || (cy >= item->y2))
		return 10000.0;

	*actual_item = item;
	return 0.0;
}

static int
item_edit_event (GnomeCanvasItem *item, GdkEvent *event)
{
	/* FIXME : Should we handle mouse events here ? */
	return 0;
}

static void
recalc_spans (GnomeCanvasItem *item)
{
	ItemEdit *item_edit = ITEM_EDIT (item);
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (item->canvas);
	int const visible_bottom = gcanvas->first_offset.row +
		GTK_WIDGET (gcanvas)->allocation.height;
	int item_bottom = item->y1 + item_edit->font_height;
	Sheet    *sheet  = sc_sheet (SHEET_CONTROL (item_edit->scg));
	GdkFont  *font      = item_edit->font;
	GSList	*text_offsets = NULL;
	Range const *merged;
	ColRowInfo const *cri;
	char const *start = wbcg_edit_get_display_text (item_edit->scg->wbcg);
	char const *text  = start;
	int col_span, row_span, tmp;
	int cur_line = 1;
	int max_col = item_edit->pos.col;
	int left_in_col, cur_col, ignore_rows = 0;

reset :
	cur_col = item_edit->pos.col;
	cri = sheet_col_get_info (sheet, cur_col);

	g_return_if_fail (cri != NULL);

	/* Start after the grid line and the left margin */
	left_in_col = cri->size_pixels - cri->margin_a - 1;

	/* the entire string */
	while (*text) {
		int pos_size;

		if (*text == '\n') {
			text_offsets = g_slist_prepend (text_offsets,
				GINT_TO_POINTER (text - start));
			text++;

			cur_line++;
			item_bottom += item_edit->font_height;
			if (item_bottom > visible_bottom)
				ignore_rows++;
			goto reset;
		}

		pos_size = gdk_text_width (font, text++, 1);

		while (left_in_col < pos_size) {
			do {
				++cur_col;
				if (cur_col > gcanvas->last_full.col || cur_col >= SHEET_MAX_COLS) {
					/* Be wary of large fonts and small columns */
					int offset = text - start - 1;
					if (offset < 1)
						offset = 1;

					cur_col = item_edit->pos.col;
					text_offsets = g_slist_prepend (text_offsets,
						GINT_TO_POINTER (offset));
					cur_line++;
					item_bottom += item_edit->font_height;
					if (item_bottom > visible_bottom)
						ignore_rows++;
				} else if (max_col < cur_col)
					max_col = cur_col;

				cri = sheet_col_get_info (sheet, cur_col);
				g_return_if_fail (cri != NULL);

				/* Be careful not to allow for the potential
				 * of an infinite loop if we somehow start on an
				 * invisible column
				 */
			} while (!cri->visible && cur_col != item_edit->pos.col);

			if (cur_col == item_edit->pos.col)
				left_in_col = cri->size_pixels - cri->margin_a - 1;
			else
				left_in_col += cri->size_pixels;
		}
		left_in_col -= pos_size;
	}
	item_edit->col_span = 1 + max_col - item_edit->pos.col;
	item_edit->lines = cur_line;

	if (item_edit->text_offsets != NULL)
		g_slist_free (item_edit->text_offsets);
	item_edit->text_offsets = g_slist_reverse (text_offsets);

	col_span = item_edit->col_span;
	merged = sheet_merge_is_corner (sheet, &item_edit->pos);
	if (merged != NULL) {
		int tmp = merged->end.col - merged->start.col + 1;
		if (col_span < tmp)
			col_span = tmp;
		row_span = merged->end.row - merged->start.row + 1;
	} else
		row_span = 1;

	/* The lower right is based on the span size excluding the grid lines
	 * Recall that the bound excludes the far point
	 */
	item->x2 = 1 + item->x1 - 2 +
		scg_colrow_distance_get (item_edit->scg, TRUE, item_edit->pos.col,
					 item_edit->pos.col + col_span);

	tmp = scg_colrow_distance_get (item_edit->scg, FALSE, item_edit->pos.row,
				       item_edit->pos.row + row_span) - 2;
	item->y2 = 1 + item->y1 +
		MAX (item_edit->lines * item_edit->font_height, tmp);
}

static void
item_edit_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemEdit *item_edit = ITEM_EDIT (item);

	if (GNOME_CANVAS_ITEM_CLASS (item_edit_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_edit_parent_class)->update)(item, affine, clip_path, flags);

	/* do not calculate spans until after row/col has been set */
	if (item_edit->font != NULL) {
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
		recalc_spans (item);
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
	}
}

static int
cb_item_edit_cursor_blink (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item_edit->cursor_visible = !item_edit->cursor_visible;

	gnome_canvas_item_request_update (item);
	return TRUE;
}

static void
item_edit_cursor_blink_stop (ItemEdit *item_edit)
{
	if (item_edit->blink_timer == -1)
		return;

	gtk_timeout_remove (item_edit->blink_timer);
	item_edit->blink_timer = -1;
}

static void
item_edit_cursor_blink_start (ItemEdit *item_edit)
{
	item_edit->blink_timer = gtk_timeout_add (
		500, (GtkFunction)(cb_item_edit_cursor_blink),
		item_edit);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->col_span = 1;
	item_edit->lines = 1;
	item_edit->scg = NULL;
	item_edit->pos.col = -1;
	item_edit->pos.row = -1;
	item_edit->font = NULL;
	item_edit->font_height = 1;
	item_edit->cursor_visible = TRUE;
	item_edit->feedback_disabled = FALSE;
	item_edit->feedback_cursor = NULL;
}

/*
 * Invoked when the GtkEntry has changed
 *
 * We use this to sync up the GtkEntry with our display on the screen.
 */
static void
entry_changed (GtkEntry *entry, void *data)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (data);
	ItemEdit *item_edit = ITEM_EDIT (item);
	char const *text = gtk_entry_get_text (item_edit->entry);

	if (gnumeric_char_start_expr_p (text))
		scan_for_range (item_edit, text);

	gnome_canvas_item_request_update (item);
}

static void
item_edit_destroy (GtkObject *o)
{
	ItemEdit *item_edit = ITEM_EDIT (o);
	GtkEntry *entry = item_edit->entry;

	if (item_edit->text_offsets != NULL)
		g_slist_free (item_edit->text_offsets);

	item_edit_cursor_blink_stop (item_edit);
	entry_destroy_feedback_range (item_edit);

	if (item_edit->signal_changed != 0) {
		g_signal_handler_disconnect (GTK_OBJECT (entry), item_edit->signal_changed);
		g_signal_handler_disconnect (GTK_OBJECT (entry), item_edit->signal_key_press);
		g_signal_handler_disconnect (GTK_OBJECT (entry), item_edit->signal_button_press);
		item_edit->signal_changed = 0;
	}

	if (GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)(o);
}

static int
entry_event (GtkEntry *entry, GdkEvent *event, GnomeCanvasItem *item)
{
	entry_changed (entry, item);
	return TRUE;
}

static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item      = GNOME_CANVAS_ITEM (o);
	ItemEdit        *item_edit = ITEM_EDIT (o);
	GnumericCanvas  *gcanvas    = GNUMERIC_CANVAS (item->canvas);
	Sheet		*sheet;
	GtkEntry        *entry;

	/* We can only set the sheet-control-gui once */
	g_return_if_fail (arg_id == ARG_SHEET_CONTROL_GUI);
	g_return_if_fail (item_edit->scg == NULL);

	item_edit->scg = GTK_VALUE_POINTER (*arg);
	sheet = ((SheetControl *) item_edit->scg)->sheet;
	item_edit->entry = wbcg_get_entry (item_edit->scg->wbcg);
	item_edit->pos = sheet->edit_pos;

	entry = item_edit->entry;
	item_edit->signal_changed = g_signal_connect (G_OBJECT (entry),
		"changed",
		G_CALLBACK (entry_changed), item_edit);
	item_edit->signal_key_press = g_signal_connect_after (G_OBJECT (entry),
		"key-press-event",
		G_CALLBACK (entry_event), item_edit);
	item_edit->signal_button_press = g_signal_connect_after (G_OBJECT (entry),
		"button-press-event",
		G_CALLBACK (entry_event), item_edit);

	scan_for_range (item_edit, "");

	/*
	 * Init the auto-completion
	 */

	/* set the font and the upper left corner if this is the first pass */
	if (item_edit->font == NULL) {
		MStyle *mstyle = sheet_style_get (sheet,
			item_edit->pos.col, item_edit->pos.row);
		StyleFont *sf = scg_get_style_font (sheet, mstyle);

		item_edit->font = style_font_gdk_font (sf);
		item_edit->font_height = style_font_get_height (sf);
		style_font_unref (sf);

		/* move inwards 1 pixel for the grid line */
		item->x1 = 1 + gcanvas->first_offset.col +
			scg_colrow_distance_get (item_edit->scg, TRUE,
					  gcanvas->first.col, item_edit->pos.col);
		item->y1 = 1 + gcanvas->first_offset.row +
			scg_colrow_distance_get (item_edit->scg, FALSE,
					  gcanvas->first.row, item_edit->pos.row);

		item->x2 = item->x1 + 1;
		item->y2 = item->y2 + 1;
	}

	item_edit_cursor_blink_start (item_edit);

	gnome_canvas_item_request_update (item);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_edit_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (GnomeCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);

	object_class->set_arg = item_edit_set_arg;
	object_class->destroy = item_edit_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_edit_update;
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->event       = item_edit_event;
}

E_MAKE_TYPE (item_edit, "ItemEdit", ItemEdit,
	     item_edit_class_init, item_edit_init,
	     GNOME_TYPE_CANVAS_ITEM);

void
item_edit_disable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	entry_destroy_feedback_range (item_edit);
	item_edit->feedback_disabled = TRUE;
}

void
item_edit_enable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	item_edit->feedback_disabled = FALSE;
}
