/* vim: set sw=8: */
/*
 * Gnumeric's extended canvas used to display the sheet.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "gnumeric-canvas.h"

#include "sheet-control-gui-priv.h"
#include "gui-util.h"
#include "mstyle.h"
#include "selection.h"
#include "parse-util.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-view.h"
#include "application.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-control-gui.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "workbook-cmd-format.h"
#include "commands.h"
#include "format.h"
#include "cmd-edit.h"
#include "clipboard.h"
#include <gsf/gsf-impl-utils.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#define SCROLL_LOCK_MASK GDK_MOD5_MASK

static FooCanvasClass *parent_klass;

static gboolean
gnm_canvas_guru_key (WorkbookControlGUI const *wbcg, GdkEventKey *event)
{
	GtkWidget *entry, *guru = wbcg_edit_get_guru (wbcg);

	if (guru == NULL)
		return FALSE;

	entry = GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));
	gtk_widget_event ((entry != NULL) ? entry : guru, (GdkEvent *) event);
	return TRUE;
}

static gboolean
gnm_canvas_key_mode_sheet (GnmCanvas *gcanvas, GdkEventKey *event)
{
	SheetControl *sc = (SheetControl *) gcanvas->simple.scg;
	Sheet *sheet = sc->sheet;
	SheetView *sv = sc->view;
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	gboolean delayed_movement = FALSE;
	gboolean jump_to_bounds = event->state & GDK_CONTROL_MASK;
	int state = gnumeric_filter_modifiers (event->state);
	void (*movefn) (SheetControlGUI *, int n,
			gboolean jump, gboolean horiz);

	gboolean transition_keys = gnm_app_use_transition_keys();
	gboolean end_mode = wbcg->last_key_was_end;

	/* Magic : Some of these are accelerators,
	 * we need to catch them before entering because they appear to be printable
	 */
	if (!wbcg_is_editing (wbcg) && event->keyval == GDK_space &&
	    (event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
		return FALSE;

	if (wbcg_rangesel_possible (wbcg)) {
		/* Ignore a few keys to avoid killing range selection cursor */
		switch (event->keyval) {
		case GDK_Shift_L:   case GDK_Shift_R:
		case GDK_Alt_L:     case GDK_Alt_R:
		case GDK_Control_L: case GDK_Control_R:
			return TRUE;
		}

		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_rangesel_extend
			: scg_rangesel_move;
	} else
		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_cursor_extend
			: scg_cursor_move;

	switch (event->keyval) {
	case GDK_KP_Left:
	case GDK_Left:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_left_col (gcanvas->simple.scg, gcanvas->first.col - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				 -(gcanvas->last_visible.col-gcanvas->first.col),
				 FALSE, TRUE);
		} else
			(*movefn) (gcanvas->simple.scg, -1, jump_to_bounds || end_mode, TRUE);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_left_col (gcanvas->simple.scg, gcanvas->first.col + 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				 gcanvas->last_visible.col-gcanvas->first.col,
				 FALSE, TRUE);
		} else
			(*movefn) (gcanvas->simple.scg, 1, jump_to_bounds || end_mode, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_top_row (gcanvas->simple.scg, gcanvas->first.row - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				 -(gcanvas->last_visible.row-gcanvas->first.row),
				 FALSE, FALSE);
		} else
			(*movefn) (gcanvas->simple.scg, -1, jump_to_bounds || end_mode, FALSE);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_top_row (gcanvas->simple.scg, gcanvas->first.row + 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				 gcanvas->last_visible.row-gcanvas->first.row,
				 FALSE, FALSE);
		} else
			(*movefn) (gcanvas->simple.scg, 1, jump_to_bounds || end_mode, FALSE);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_prev_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				-(gcanvas->last_visible.row-gcanvas->first.row),
				FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				-(gcanvas->last_visible.col-gcanvas->first.col),
				FALSE, TRUE);
		}
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0) {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				gcanvas->last_visible.row-gcanvas->first.row,
				FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (gcanvas->simple.scg, movefn,
				gcanvas->last_visible.col-gcanvas->first.col,
				FALSE, TRUE);
		}
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if (event->state & SCROLL_LOCK_MASK) {
			scg_set_left_col (gcanvas->simple.scg, sv->edit_pos.col);
			scg_set_top_row (gcanvas->simple.scg, sv->edit_pos.row);
		} else if (end_mode) {
			/* Same as ctrl-end.  */
			GnmRange r = sheet_get_extent (sheet, FALSE);
			(*movefn)(gcanvas->simple.scg, r.end.col - sv->edit_pos.col, FALSE, TRUE);
			(*movefn)(gcanvas->simple.scg, r.end.row - sv->edit_pos.row, FALSE, FALSE);
		} else {
			/* do the ctrl-home jump to A1 in 2 steps */
			(*movefn)(gcanvas->simple.scg, -SHEET_MAX_COLS, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK) || transition_keys)
				(*movefn)(gcanvas->simple.scg, -SHEET_MAX_ROWS, FALSE, FALSE);
		}
		break;

	case GDK_KP_End:
	case GDK_End:
		if (event->state & SCROLL_LOCK_MASK) {
			int new_col = sv->edit_pos.col - (gcanvas->last_full.col - gcanvas->first.col);
			int new_row = sv->edit_pos.row - (gcanvas->last_full.row - gcanvas->first.row);
			scg_set_left_col (gcanvas->simple.scg, new_col);
			scg_set_top_row (gcanvas->simple.scg, new_row);
		} else if ((event->state & GDK_CONTROL_MASK)) {	
			GnmRange r = sheet_get_extent (sheet, FALSE);

			/* do the ctrl-end jump to the extent in 2 steps */
			(*movefn)(gcanvas->simple.scg, r.end.col - sv->edit_pos.col, FALSE, TRUE);
			(*movefn)(gcanvas->simple.scg, r.end.row - sv->edit_pos.row, FALSE, FALSE);
		} else {  /* toggle end mode */
			wbcg_toggle_end_mode(wbcg);
		}
			
		break;

	case GDK_KP_Insert :
	case GDK_Insert :
		if (gnm_canvas_guru_key (wbcg, event))
			break;
		if (state == GDK_CONTROL_MASK)
			sv_selection_copy (sc_view (sc), WORKBOOK_CONTROL (wbcg));
		else if (state == GDK_SHIFT_MASK)
			cmd_paste_to_selection (WORKBOOK_CONTROL (wbcg), sv, PASTE_DEFAULT);
		break;

	case GDK_KP_Delete:
	case GDK_Delete:
		if (gnm_canvas_guru_key (wbcg, event))
			break;
		if (state == GDK_SHIFT_MASK) {
			scg_mode_edit (sc);
			sv_selection_cut (sc_view (sc), WORKBOOK_CONTROL (wbcg));
		} else
			cmd_selection_clear (WORKBOOK_CONTROL (wbcg), CLEAR_VALUES);
		break;

	/*
	 * NOTE : Keep these in sync with the condition
	 *        for tabs.
	 */
	case GDK_KP_Enter:
	case GDK_Return:
		if (wbcg_is_editing (wbcg) &&
		    (state == GDK_CONTROL_MASK ||
		     state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK) ||
		     event->state == GDK_MOD1_MASK))
			/* Forward the keystroke to the input line */
			return gtk_widget_event (
				GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg))),
				(GdkEvent *) event);
		/* fall down */

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
		if (gnm_canvas_guru_key (wbcg, event))
			break;

		/* Be careful to restore the editing sheet if we are editing */
		if (wbcg_is_editing (wbcg))
			sheet = wbcg->wb_control.editing_sheet;

		if (wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL)) {
			/* Figure out the direction */
			gboolean const direction = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
			gboolean const horizontal = (event->keyval == GDK_KP_Enter ||
						     event->keyval == GDK_Return) ? FALSE : TRUE;

			sv_selection_walk_step (sv, direction, horizontal);
		}
		break;

	case GDK_Escape:
		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
		gnm_app_clipboard_unant ();
		break;

	case GDK_F4:
		if (wbcg_is_editing (wbcg))
			return gtk_widget_event (GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg))),
						 (GdkEvent *) event);
		return TRUE;

	case GDK_F2:
		if (gnm_canvas_guru_key (wbcg, event))
			break;

		if (!wbcg_edit_start (wbcg, FALSE, FALSE))
			return FALSE; /* attempt to edit failed */
		/* fall down */

	case GDK_BackSpace:
		/* Re-center the view on the active cell */
		if (!wbcg_is_editing (wbcg) && (event->state & GDK_CONTROL_MASK) != 0) {
			scg_make_cell_visible (gcanvas->simple.scg, sv->edit_pos.col,
					       sv->edit_pos.row, FALSE, TRUE);
			break;
		}

	default:
		if (!wbcg_is_editing (wbcg)) {
			if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0)
				return FALSE;

			/* If the character is not printable do not start editing */
			if (event->length == 0)
				return FALSE;

			if (!wbcg_edit_start (wbcg, TRUE, TRUE))
				return FALSE; /* attempt to edit failed */
		}
		scg_rangesel_stop (gcanvas->simple.scg, FALSE);

		/* Forward the keystroke to the input line */
		return gtk_widget_event (GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg))),
					 (GdkEvent *) event);
	}
	
	/* Update end-mode for magic end key stuff. */
	if (event->keyval != GDK_End && event->keyval != GDK_KP_End) {
		wbcg_set_end_mode(wbcg, FALSE);
	}

	if (!delayed_movement) {
		if (wbcg_is_editing (wbcg))
			sheet_update_only_grid (sheet);
		else
			sheet_update (sheet);
	}

	return TRUE;
}

static gboolean
gnm_canvas_key_mode_object (GnmCanvas *gcanvas, GdkEventKey *ev)
{
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetControl    *sc = SHEET_CONTROL (scg);
	unsigned const delta = (0 != (ev->state & GDK_SHIFT_MASK)) ? 15 : 1;
	gboolean const control = 0 != (ev->state & GDK_CONTROL_MASK);
	gboolean const alt = 0 != (ev->state & GDK_MOD1_MASK);
	gboolean const symmetric = control && alt;

	switch (ev->keyval) {
	case GDK_Escape:
		scg_mode_edit (sc);
		gnm_app_clipboard_unant ();
		return TRUE;;

	case GDK_BackSpace: /* Ick! */
	case GDK_KP_Delete:
	case GDK_Delete:
		if (scg->selected_objects != NULL) {
			cmd_objects_delete (sc->wbc,
				gnm_hash_keys (scg->selected_objects), NULL);
			return TRUE;
		}
		sc_mode_edit (sc);
		break;

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
		if (scg->selected_objects != NULL) {
			Sheet *sheet = sc_sheet (sc);
			GList *ptr = sheet->sheet_objects;
			for (; ptr != NULL ; ptr = ptr->next)
				if (NULL != g_hash_table_lookup (scg->selected_objects, ptr->data)) {
					SheetObject *target;
					if ((ev->state & GDK_SHIFT_MASK)) {
						if (ptr->next == NULL)
							target = sheet->sheet_objects->data;
						else
							target = ptr->next->data;
					} else {
						if (ptr->prev == NULL) {
							GList *last = g_list_last (ptr);
							target = last->data;
						} else
							target = ptr->prev->data;
					}
					if (ptr->data != target) {
						scg_object_unselect (scg, NULL);
						scg_object_select (scg, target);
						return TRUE;
					}
				}
		}
		break;

	case GDK_KP_Left: case GDK_Left:
		scg_objects_nudge (scg, (alt ? 4 : (control ? 3 : 8)), -delta, 0, symmetric);
		return TRUE;
	case GDK_KP_Right: case GDK_Right:
		scg_objects_nudge (scg, (alt ? 4 : (control ? 3 : 8)), delta, 0, symmetric);
		return TRUE;
	case GDK_KP_Up: case GDK_Up:
		scg_objects_nudge (scg, (alt ? 6 : (control ? 1 : 8)), 0, -delta, symmetric);
		return TRUE;
	case GDK_KP_Down: case GDK_Down:
		scg_objects_nudge (scg, (alt ? 6 : (control ? 1 : 8)), 0, delta, symmetric);
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static gint
gnm_canvas_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnmCanvas *gcanvas = GNM_CANVAS (widget);
	SheetControlGUI *scg = gcanvas->simple.scg;
	gboolean res;

	if (scg->grab_stack > 0)
		return TRUE;

	if (wbcg_edit_get_guru (scg->wbcg) == NULL  &&
	    (scg->selected_objects != NULL || scg->new_object != NULL))
		res = gnm_canvas_key_mode_object (gcanvas, event);
	else {
		gcanvas->mask_state = event->state;
		if (gtk_im_context_filter_keypress (gcanvas->im_context,event)) {
			gcanvas->need_im_reset = TRUE;
			return TRUE;
		}
		switch (event->keyval) {
		case GDK_Shift_L:   case GDK_Shift_R:
		case GDK_Alt_L:     case GDK_Alt_R:
		case GDK_Control_L: case GDK_Control_R:
			break;
		default:
			gtk_im_context_reset (gcanvas->im_context);
			break;
		}
		res = gnm_canvas_key_mode_sheet (gcanvas, event);
	}

	switch (event->keyval) {
	case GDK_Shift_L:   case GDK_Shift_R:
	case GDK_Alt_L:     case GDK_Alt_R:
	case GDK_Control_L: case GDK_Control_R:
		break;

	default : if (res)
			return TRUE;
	}

	return (*GTK_WIDGET_CLASS (parent_klass)->key_press_event) (widget, event);
}

static gint
gnm_canvas_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnmCanvas *gcanvas = GNM_CANVAS (widget);
	SheetControl *sc = (SheetControl *) gcanvas->simple.scg;

	if (gcanvas->simple.scg->grab_stack > 0)
		return TRUE;

	if (gtk_im_context_filter_keypress (gcanvas->im_context,event))
	{
		gcanvas->need_im_reset = TRUE;
		return TRUE;
	}
	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (gcanvas->simple.scg->selected_objects == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (
			sc->wbc), TRUE, NULL);

	return (*GTK_WIDGET_CLASS (parent_klass)->key_release_event) (widget, event);
}

/* Focus in handler for the canvas */
static gint
gnm_canvas_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
	GNM_CANVAS (widget)->need_im_reset = TRUE;
	gtk_im_context_focus_in (GNM_CANVAS (widget)->im_context);
	return (*GTK_WIDGET_CLASS (parent_klass)->focus_in_event) (widget, event);
}

/* Focus out handler for the canvas */
static gint
gnm_canvas_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	GNM_CANVAS (widget)->need_im_reset = TRUE;
	gtk_im_context_focus_out (GNM_CANVAS (widget)->im_context);
	return (*GTK_WIDGET_CLASS (parent_klass)->focus_out_event) (widget, event);
}

static void
gnm_canvas_realize (GtkWidget *w)
{
	GtkStyle  *style;

	if (GTK_WIDGET_CLASS (parent_klass)->realize)
		(*GTK_WIDGET_CLASS (parent_klass)->realize) (w);

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less. */
	style = gtk_style_copy (w->style);
	style->bg[GTK_STATE_NORMAL] = style->white;
	gtk_widget_set_style (w, style);
	g_object_unref (style);

	gtk_im_context_set_client_window (GNM_CANVAS (w)->im_context,
		gtk_widget_get_toplevel (w)->window);
}

static void
gnm_canvas_unrealize (GtkWidget *widget)
{
	GnmCanvas *gcanvas;

	gcanvas = GNM_CANVAS (widget);
	g_return_if_fail (gcanvas != NULL);

	gtk_im_context_set_client_window (GNM_CANVAS (widget)->im_context,
		gtk_widget_get_toplevel (widget)->window);

	(*GTK_WIDGET_CLASS (parent_klass)->unrealize)(widget);
}

static void
gnm_canvas_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (parent_klass)->size_allocate)(widget, allocation);

	gnm_canvas_compute_visible_region (GNM_CANVAS (widget), FALSE);
}

typedef struct {
	FooCanvasClass parent_class;
} GnmCanvasClass;
#define GNM_CANVAS_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),	  GNM_CANVAS_TYPE))

static void
gnm_canvas_finalize (GObject *object)
{
	g_object_unref (G_OBJECT (GNM_CANVAS (object)->im_context));
	G_OBJECT_CLASS (parent_klass)->finalize (object);
}

static void
gnm_canvas_class_init (GnmCanvasClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	FooCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) klass;
	widget_class = (GtkWidgetClass *) klass;
	canvas_class = (FooCanvasClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);

	gobject_class->finalize = gnm_canvas_finalize;

	widget_class->realize		   = gnm_canvas_realize;
	widget_class->unrealize		   = gnm_canvas_unrealize;
 	widget_class->size_allocate	   = gnm_canvas_size_allocate;
	widget_class->key_press_event	   = gnm_canvas_key_press;
	widget_class->key_release_event	   = gnm_canvas_key_release;
	widget_class->focus_in_event	   = gnm_canvas_focus_in;
	widget_class->focus_out_event	   = gnm_canvas_focus_out;
}

/* IM Context Callbacks
 */

static void
gnm_canvas_commit_cb (GtkIMContext *context, const gchar *str, GnmCanvas *gcanvas)
{
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = GTK_EDITABLE (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));
	gint tmp_pos;

	if (!wbcg_is_editing (wbcg) &&
	    !wbcg_edit_start (wbcg, TRUE, TRUE))
		return;

	if (gtk_editable_get_selection_bounds (editable, NULL, NULL))
		gtk_editable_delete_selection (editable);
	else {
		tmp_pos = gtk_editable_get_position (editable);
		if (GTK_ENTRY (editable)->overwrite_mode)
			gtk_editable_delete_text (editable,tmp_pos,tmp_pos+1);
	}

	tmp_pos = gtk_editable_get_position (editable);
	gtk_editable_insert_text (editable, str, strlen (str), &tmp_pos);
	gtk_editable_set_position (editable, tmp_pos);
}

static void
gnm_canvas_preedit_changed_cb (GtkIMContext *context, GnmCanvas *gcanvas)
{
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = GTK_EDITABLE (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));
	gchar *preedit_string;
	int tmp_pos;
	int cursor_pos;

	tmp_pos = gtk_editable_get_position (editable);
	if (gcanvas->preedit_attrs)
		pango_attr_list_unref (gcanvas->preedit_attrs);
	gtk_im_context_get_preedit_string (gcanvas->im_context, &preedit_string, &gcanvas->preedit_attrs, &cursor_pos);

	if (!wbcg_is_editing (wbcg) && !wbcg_edit_start (wbcg, TRUE, TRUE)) {
		gtk_im_context_reset (gcanvas->im_context);
		gcanvas->preedit_length = 0;
		if (gcanvas->preedit_attrs)
				pango_attr_list_unref (gcanvas->preedit_attrs);
		gcanvas->preedit_attrs = NULL;
		g_free (preedit_string);
		return;
	}

	if (gcanvas->preedit_length)
		gtk_editable_delete_text (editable,tmp_pos,tmp_pos+gcanvas->preedit_length);
	gcanvas->preedit_length = strlen (preedit_string);

	if (gcanvas->preedit_length)
		gtk_editable_insert_text (editable, preedit_string, gcanvas->preedit_length, &tmp_pos);
	g_free (preedit_string);
}

static gboolean
gnm_canvas_retrieve_surrounding_cb (GtkIMContext *context, GnmCanvas *gcanvas)
{
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = GTK_EDITABLE (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));
	gchar *surrounding = gtk_editable_get_chars (editable, 0, -1);
	gint  cur_pos = gtk_editable_get_position (editable);

	gtk_im_context_set_surrounding (context,
	                                surrounding, strlen (surrounding),
	                                g_utf8_offset_to_pointer (surrounding, cur_pos) - surrounding);

	g_free (surrounding);
	return TRUE;
}

static gboolean
gnm_canvas_delete_surrounding_cb (GtkIMContext *context,
                                  gint         offset,
                                  gint         n_chars,
                                  GnmCanvas    *gcanvas)
{
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = GTK_EDITABLE (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));
	gint cur_pos = gtk_editable_get_position (editable);

	gtk_editable_delete_text (editable,
	                          cur_pos + offset,
	                          cur_pos + offset + n_chars);

	return TRUE;
}

static void
gnm_canvas_init (GnmCanvas *gcanvas)
{
	FooCanvas *canvas = FOO_CANVAS (gcanvas);

	gcanvas->first.col = gcanvas->last_full.col = gcanvas->last_visible.col = 0;
	gcanvas->first.row = gcanvas->last_full.row = gcanvas->last_visible.row = 0;
	gcanvas->first_offset.col = 0;
	gcanvas->first_offset.row = 0;

	gcanvas->slide_handler = NULL;
	gcanvas->slide_data = NULL;
	gcanvas->sliding = -1;
	gcanvas->sliding_x  = gcanvas->sliding_dx = -1;
	gcanvas->sliding_y  = gcanvas->sliding_dy = -1;
	gcanvas->sliding_adjacent_h = gcanvas->sliding_adjacent_v = FALSE;

	gcanvas->im_context = gtk_im_multicontext_new ();
	gcanvas->preedit_length = 0;
	gcanvas->preedit_attrs = NULL;

	g_signal_connect (G_OBJECT (gcanvas->im_context), "commit",
		G_CALLBACK (gnm_canvas_commit_cb), gcanvas);
	g_signal_connect (G_OBJECT (gcanvas->im_context), "preedit_changed",
		G_CALLBACK (gnm_canvas_preedit_changed_cb), gcanvas);
	g_signal_connect (G_OBJECT (gcanvas->im_context), "retrieve_surrounding",
		G_CALLBACK (gnm_canvas_retrieve_surrounding_cb), gcanvas);
	g_signal_connect (G_OBJECT (gcanvas->im_context), "delete_surrounding",
		G_CALLBACK (gnm_canvas_delete_surrounding_cb), gcanvas);

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

GSF_CLASS (GnmCanvas, gnm_canvas,
	   gnm_canvas_class_init, gnm_canvas_init,
	   GNM_SIMPLE_CANVAS_TYPE);

GnmCanvas *
gnm_canvas_new (SheetControlGUI *scg, GnmPane *pane)
{
	GnmCanvas	 *gcanvas;
	FooCanvasGroup *root_group;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	gcanvas = g_object_new (GNM_CANVAS_TYPE, NULL);

	gcanvas->simple.scg = scg;
	gcanvas->pane = pane;

	/* FIXME: figure out some real size for the canvas scrolling region */
	foo_canvas_set_center_scroll_region (FOO_CANVAS (gcanvas), FALSE);
	foo_canvas_set_scroll_region (FOO_CANVAS (gcanvas), 0, 0,
		GNUMERIC_CANVAS_FACTOR_X, GNUMERIC_CANVAS_FACTOR_Y);

	root_group = FOO_CANVAS_GROUP (FOO_CANVAS (gcanvas)->root);
	gcanvas->grid_items	= FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));
	gcanvas->object_views	= FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));
	gcanvas->action_items	= FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));
	return gcanvas;
}

/**
 * gnm_canvas_find_col: return the column containing pixel x
 */
int
gnm_canvas_find_col (GnmCanvas *gcanvas, int x, int *col_origin)
{
	Sheet *sheet = ((SheetControl *) gcanvas->simple.scg)->sheet;
	int col   = gcanvas->first.col;
	int pixel = gcanvas->first_offset.col;

	if (x < pixel) {
		while (col > 0) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, --col);
			if (ci->visible) {
				pixel -= ci->size_pixels;
				if (x >= pixel) {
					if (col_origin)
						*col_origin = pixel;
					return col;
				}
			}
		}
		if (col_origin)
			*col_origin = 0;
		return 0;
	}

	do {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const tmp = ci->size_pixels;
			if (x <= pixel + tmp) {
				if (col_origin)
					*col_origin = pixel;
				return col;
			}
			pixel += tmp;
		}
	} while (++col < SHEET_MAX_COLS-1);
	if (col_origin)
		*col_origin = pixel;
	return SHEET_MAX_COLS-1;
}

/*
 * gnm_canvas_find_row: return the row where y belongs to
 */
int
gnm_canvas_find_row (GnmCanvas *gcanvas, int y, int *row_origin)
{
	Sheet *sheet = ((SheetControl *) gcanvas->simple.scg)->sheet;
	int row   = gcanvas->first.row;
	int pixel = gcanvas->first_offset.row;

	if (y < pixel) {
		while (row > 0) {
			ColRowInfo const *ri = sheet_row_get_info (sheet, --row);
			if (ri->visible) {
				pixel -= ri->size_pixels;
				if (y >= pixel) {
					if (row_origin)
						*row_origin = pixel;
					return row;
				}
			}
		}
		if (row_origin)
			*row_origin = 0;
		return 0;
	}

	do {
		ColRowInfo const *ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const tmp = ri->size_pixels;
			if (pixel <= y && y <= pixel + tmp) {
				if (row_origin)
					*row_origin = pixel;
				return row;
			}
			pixel += tmp;
		}
	} while (++row < SHEET_MAX_ROWS-1);
	if (row_origin)
		*row_origin = pixel;
	return SHEET_MAX_ROWS-1;
}

/*
 * gnm_canvas_compute_visible_region : Keeps the top left col/row the same and
 *     recalculates the visible boundaries.
 *
 * @full_recompute :
 *       if TRUE recompute the pixel offsets of the top left row/col
 *       else assumes that the pixel offsets of the top left have not changed.
 */
void
gnm_canvas_compute_visible_region (GnmCanvas *gcanvas,
				   gboolean const full_recompute)
{
	SheetControlGUI const * const scg = gcanvas->simple.scg;
	Sheet const * const sheet = ((SheetControl *) scg)->sheet;
	FooCanvas   *canvas = FOO_CANVAS (gcanvas);
	int pixels, col, row, width, height;

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		gcanvas->first_offset.col = scg_colrow_distance_get (scg,
			TRUE, 0, gcanvas->first.col);
		if (NULL != gcanvas->pane->col.canvas)
			foo_canvas_scroll_to (gcanvas->pane->col.canvas,
				gcanvas->first_offset.col, 0);

		gcanvas->first_offset.row = scg_colrow_distance_get (scg,
			FALSE, 0, gcanvas->first.row);
		if (NULL != gcanvas->pane->row.canvas)
			foo_canvas_scroll_to (gcanvas->pane->row.canvas,
				0, gcanvas->first_offset.row);

		foo_canvas_scroll_to (FOO_CANVAS (gcanvas),
					gcanvas->first_offset.col,
					gcanvas->first_offset.row);
	}

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = gcanvas->first.col;
	width = GTK_WIDGET (canvas)->allocation.width;

	do {
		ColRowInfo const * const ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const bound = pixels + ci->size_pixels;

			if (bound == width) {
				gcanvas->last_visible.col = col;
				gcanvas->last_full.col = col;
				break;
			}
			if (bound > width) {
				gcanvas->last_visible.col = col;
				if (col == gcanvas->first.col)
					gcanvas->last_full.col = gcanvas->first.col;
				else
					gcanvas->last_full.col = col - 1;
				break;
			}
			pixels = bound;
		}
		++col;
	} while (pixels < width && col < SHEET_MAX_COLS);

	if (col >= SHEET_MAX_COLS) {
		gcanvas->last_visible.col = SHEET_MAX_COLS-1;
		gcanvas->last_full.col = SHEET_MAX_COLS-1;
	}

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = gcanvas->first.row;
	height = GTK_WIDGET (canvas)->allocation.height;
	do {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const bound = pixels + ri->size_pixels;

			if (bound == height) {
				gcanvas->last_visible.row = row;
				gcanvas->last_full.row = row;
				break;
			}
			if (bound > height) {
				gcanvas->last_visible.row = row;
				if (row == gcanvas->first.row)
					gcanvas->last_full.row = gcanvas->first.row;
				else
					gcanvas->last_full.row = row - 1;
				break;
			}
			pixels = bound;
		}
		++row;
	} while (pixels < height && row < SHEET_MAX_ROWS);

	if (row >= SHEET_MAX_ROWS) {
		gcanvas->last_visible.row = SHEET_MAX_ROWS-1;
		gcanvas->last_full.row = SHEET_MAX_ROWS-1;
	}

	/* Update the scrollbar sizes for the primary pane */
	if (gcanvas->pane->index == 0)
		scg_scrollbar_config (SHEET_CONTROL (scg));

	/* Force the cursor to update its bounds relative to the new visible region */
	gnm_pane_reposition_cursors (gcanvas->pane);
}

void
gnm_canvas_redraw_range (GnmCanvas *gcanvas, GnmRange const *r)
{
	SheetControlGUI *scg;
	FooCanvas *canvas;
	int x1, y1, x2, y2;
	GnmRange tmp;

	g_return_if_fail (IS_GNM_CANVAS (gcanvas));

	scg = gcanvas->simple.scg;
	canvas = FOO_CANVAS (gcanvas);

	if ((r->end.col < gcanvas->first.col) ||
	    (r->end.row < gcanvas->first.row) ||
	    (r->start.col > gcanvas->last_visible.col) ||
	    (r->start.row > gcanvas->last_visible.row))
		return;

	/* Only draw those regions that are visible */
	tmp.start.col = MAX (gcanvas->first.col, r->start.col);
	tmp.start.row = MAX (gcanvas->first.row, r->start.row);
	tmp.end.col =  MIN (gcanvas->last_visible.col, r->end.col);
	tmp.end.row =  MIN (gcanvas->last_visible.row, r->end.row);

	/* redraw a border of 2 pixels around the region to handle thick borders
	 * NOTE the 2nd coordinates are excluded so add 1 extra (+2border +1include)
	 */
	x1 = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, tmp.start.col) +
		gcanvas->first_offset.col;
	y1 = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, tmp.start.row) +
		gcanvas->first_offset.row;
	x2 = (tmp.end.col < (SHEET_MAX_COLS-1))
		? 4 + 1 + x1 + scg_colrow_distance_get (scg, TRUE,
							tmp.start.col, tmp.end.col+1)
		: INT_MAX;
	y2 = (tmp.end.row < (SHEET_MAX_ROWS-1))
		? 4 + 1 + y1 + scg_colrow_distance_get (scg, FALSE,
							tmp.start.row, tmp.end.row+1)
		: INT_MAX;

#if 0
	fprintf (stderr, "%s%s:", col_name (min_col), row_name (first_row));
	fprintf (stderr, "%s%s\n", col_name (max_col), row_name (last_row));
#endif

	foo_canvas_request_redraw (FOO_CANVAS (gcanvas), x1-2, y1-2, x2, y2);
}

/*****************************************************************************/

void
gnm_canvas_slide_stop (GnmCanvas *gcanvas)
{
	if (gcanvas->sliding == -1)
		return;

	g_source_remove (gcanvas->sliding);
	gcanvas->slide_handler = NULL;
	gcanvas->slide_data = NULL;
	gcanvas->sliding = -1;
}

static int
col_scroll_step (int dx)
{
	/* FIXME: get from gdk.  */
	int dpi_x_this_screen = 90;
	int start_x = dpi_x_this_screen / 3;
	double double_dx = dpi_x_this_screen / 3.0;
	double step = pow (2.0, (dx - start_x) / double_dx);

	return (int) (CLAMP (step, 1.0, SHEET_MAX_COLS / 15.0));
}

static int
row_scroll_step (int dy)
{
	/* FIXME: get from gdk.  */
	int dpi_y_this_screen = 90;
	int start_y = dpi_y_this_screen / 4;
	double double_dy = dpi_y_this_screen / 8.0;
	double step = pow (2.0, (dy - start_y) / double_dy);

	return (int) (CLAMP (step, 1.0, SHEET_MAX_ROWS / 15.0));
}

static gint
gcanvas_sliding_callback (gpointer data)
{
	GnmCanvas *gcanvas = data;
	int const pane_index = gcanvas->pane->index;
	GnmCanvas *gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	GnmCanvas *gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	GnmCanvas *gcanvas3 = scg_pane (gcanvas->simple.scg, 3);
	gboolean slide_x = FALSE, slide_y = FALSE;
	int col = -1, row = -1;

	if (gcanvas->sliding_dx > 0) {
		GnmCanvas *target_gcanvas = gcanvas;

		slide_x = TRUE;
		if (pane_index == 1 || pane_index == 2) {
			if (!gcanvas->sliding_adjacent_h) {
				int width = GTK_WIDGET (gcanvas)->allocation.width;
				int x = gcanvas->first_offset.col + width + gcanvas->sliding_dx;

				/* in case pane is narrow */
				col = gnm_canvas_find_col (gcanvas, x, NULL);
				if (col > gcanvas0->last_full.col) {
					gcanvas->sliding_adjacent_h = TRUE;
					gcanvas->sliding_dx = 1; /* good enough */
				} else
					slide_x = FALSE;
			} else
				target_gcanvas = gcanvas0;
		} else
			gcanvas->sliding_adjacent_h = FALSE;

		if (slide_x) {
			col = target_gcanvas->last_full.col +
				col_scroll_step (gcanvas->sliding_dx);
			if (col >= SHEET_MAX_COLS-1) {
				col = SHEET_MAX_COLS-1;
				slide_x = FALSE;
			}
		}
	} else if (gcanvas->sliding_dx < 0) {
		slide_x = TRUE;
		col = gcanvas0->first.col - col_scroll_step (-gcanvas->sliding_dx);

		if (gcanvas1 != NULL) {
			if (pane_index == 0 || pane_index == 3) {
				int width = GTK_WIDGET (gcanvas1)->allocation.width;
				if (gcanvas->sliding_dx > (-width) &&
				    col <= gcanvas1->last_visible.col) {
					int x = gcanvas1->first_offset.col + width + gcanvas->sliding_dx;
					col = gnm_canvas_find_col (gcanvas1, x, NULL);
					slide_x = FALSE;
				}
			}

			if (col <= gcanvas1->first.col) {
				col = gcanvas1->first.col;
				slide_x = FALSE;
			}
		} else if (col <= 0) {
			col = 0;
			slide_x = FALSE;
		}
	}

	if (gcanvas->sliding_dy > 0) {
		GnmCanvas *target_gcanvas = gcanvas;

		slide_y = TRUE;
		if (pane_index == 3 || pane_index == 2) {
			if (!gcanvas->sliding_adjacent_v) {
				int height = GTK_WIDGET (gcanvas)->allocation.height;
				int y = gcanvas->first_offset.row + height + gcanvas->sliding_dy;

				/* in case pane is short */
				row = gnm_canvas_find_row (gcanvas, y, NULL);
				if (row > gcanvas0->last_full.row) {
					gcanvas->sliding_adjacent_v = TRUE;
					gcanvas->sliding_dy = 1; /* good enough */
				} else
					slide_y = FALSE;
			} else
				target_gcanvas = gcanvas0;
		} else
			gcanvas->sliding_adjacent_v = FALSE;

		if (slide_y) {
			row = target_gcanvas->last_full.row +
				row_scroll_step (gcanvas->sliding_dy);
			if (row >= SHEET_MAX_ROWS-1) {
				row = SHEET_MAX_ROWS-1;
				slide_y = FALSE;
			}
		}
	} else if (gcanvas->sliding_dy < 0) {
		slide_y = TRUE;
		row = gcanvas0->first.row - row_scroll_step (-gcanvas->sliding_dy);

		if (gcanvas3 != NULL) {
			if (pane_index == 0 || pane_index == 1) {
				int height = GTK_WIDGET (gcanvas3)->allocation.height;
				if (gcanvas->sliding_dy > (-height) &&
				    row <= gcanvas3->last_visible.row) {
					int y = gcanvas3->first_offset.row + height + gcanvas->sliding_dy;
					row = gnm_canvas_find_row (gcanvas3, y, NULL);
					slide_y = FALSE;
				}
			}

			if (row <= gcanvas3->first.row) {
				row = gcanvas3->first.row;
				slide_y = FALSE;
			}
		} else if (row <= 0) {
			row = 0;
			slide_y = FALSE;
		}
	}

	if (col < 0 && row < 0) {
		gnm_canvas_slide_stop (gcanvas);
		return TRUE;
	}

	if (col < 0)
		col = gnm_canvas_find_col (gcanvas, gcanvas->sliding_x, NULL);
	else if (row < 0)
		row = gnm_canvas_find_row (gcanvas, gcanvas->sliding_y, NULL);

	if (gcanvas->slide_handler == NULL ||
	    (*gcanvas->slide_handler) (gcanvas, col, row, gcanvas->slide_data))
		scg_make_cell_visible (gcanvas->simple.scg, col, row, FALSE, TRUE);

	if (slide_x || slide_y) {
		if (gcanvas->sliding == -1)
			gcanvas->sliding = g_timeout_add (
				300, gcanvas_sliding_callback, gcanvas);
	} else
		gnm_canvas_slide_stop (gcanvas);

	return TRUE;
}

/**
 * gnm_canvas_handle_motion :
 * @gcanvas	 : The GnmCanvas managing the scroll
 * @canvas	 : The Canvas the event comes from
 * @event	 : The motion event
 * @slide_flags	 :
 * @slide_handler: The handler when sliding
 * @user_data	 : closure data
 *
 * Handle a motion event from a @canvas and scroll the @gcanvas
 * depending on how far outside the bounds of @gcanvas the @event is.
 * Usually @canvas == @gcanvas however as long as the canvases share a basis
 * space they can be different.
 **/
gboolean
gnm_canvas_handle_motion (GnmCanvas *gcanvas,
			  FooCanvas *canvas, GdkEventMotion *event,
			  GnmCanvasSlideFlags slide_flags,
			  GnmCanvasSlideHandler slide_handler,
			  gpointer user_data)
{
	GnmCanvas *gcanvas0, *gcanvas1, *gcanvas3;
	int pane, left, top, x, y, width, height;
	int dx = 0, dy = 0;

	g_return_val_if_fail (IS_GNM_CANVAS (gcanvas), FALSE);
	g_return_val_if_fail (FOO_IS_CANVAS (canvas), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (slide_handler != NULL, FALSE);

	foo_canvas_w2c (canvas, event->x, event->y, &x, &y);

	pane = gcanvas->pane->index;
	left = gcanvas->first_offset.col;
	top = gcanvas->first_offset.row;
	width = GTK_WIDGET (gcanvas)->allocation.width;
	height = GTK_WIDGET (gcanvas)->allocation.height;

	gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	gcanvas3 = scg_pane (gcanvas->simple.scg, 3);

	if (slide_flags & GNM_CANVAS_SLIDE_X) {
		if (x < left)
			dx = x - left;
		else if (x >= left + width)
			dx = x - width - left;
	}

	if (slide_flags & GNM_CANVAS_SLIDE_Y) {
		if (y < top)
			dy = y - top;
		else if (y >= top + height)
			dy = y - height - top;
	}

	if (gcanvas->sliding_adjacent_h) {
		if (pane == 0 || pane == 3) {
			if (dx < 0) {
				x = gcanvas1->first_offset.col;
				dx += GTK_WIDGET (gcanvas1)->allocation.width;
				if (dx > 0)
					x += dx;
				dx = 0;
			} else
				gcanvas->sliding_adjacent_h = FALSE;
		} else {
			if (dx > 0) {
				x = gcanvas0->first_offset.col + dx;
				dx -= GTK_WIDGET (gcanvas0)->allocation.width;
				if (dx < 0)
					dx = 0;
			} else if (dx == 0) {
				/* initiate a reverse scroll of panes 0,3 */
				if ((gcanvas1->last_visible.col+1) != gcanvas0->first.col)
					dx = x - (left + width);
			} else
				dx = 0;
		}
	}

	if (gcanvas->sliding_adjacent_v) {
		if (pane == 0 || pane == 1) {
			if (dy < 0) {
				y = gcanvas3->first_offset.row;
				dy += GTK_WIDGET (gcanvas3)->allocation.height;
				if (dy > 0)
					y += dy;
				dy = 0;
			} else
				gcanvas->sliding_adjacent_v = FALSE;
		} else {
			if (dy > 0) {
				y = gcanvas0->first_offset.row + dy;
				dy -= GTK_WIDGET (gcanvas0)->allocation.height;
				if (dy < 0)
					dy = 0;
			} else if (dy == 0) {
				/* initiate a reverse scroll of panes 0,1 */
				if ((gcanvas3->last_visible.row+1) != gcanvas0->first.row)
					dy = y - (top + height);
			} else
				dy = 0;
		}
	}

	/* Movement is inside the visible region */
	if (dx == 0 && dy == 0) {
		if (!(slide_flags & GNM_CANVAS_SLIDE_EXTERIOR_ONLY)) {
			int const col = gnm_canvas_find_col (gcanvas, x, NULL);
			int const row = gnm_canvas_find_row (gcanvas, y, NULL);

			(*slide_handler) (gcanvas, col, row, user_data);
		}
		gnm_canvas_slide_stop (gcanvas);
		return TRUE;
	}

	gcanvas->sliding_x  = x;
	gcanvas->sliding_dx = dx;
	gcanvas->sliding_y  = y;
	gcanvas->sliding_dy = dy;
	gcanvas->slide_handler = slide_handler;
	gcanvas->slide_data = user_data;

	if (gcanvas->sliding == -1)
		(void) gcanvas_sliding_callback (gcanvas);
	return FALSE;
}

/* TODO : All the slide_* members of GnmCanvas really aught to be in
 * SheetControlGUI, most of these routines also belong there.  However, since
 * the primary point of access is via GnmCanvas and SCG is very large
 * already I'm leaving them here for now.  Move them when we return to
 * investigate how to do reverse scrolling for pseudo-adjacent panes.
 */
void
gnm_canvas_slide_init (GnmCanvas *gcanvas)
{
	GnmCanvas *gcanvas0, *gcanvas1, *gcanvas3;

	g_return_if_fail (IS_GNM_CANVAS (gcanvas));

	gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	gcanvas3 = scg_pane (gcanvas->simple.scg, 3);

	gcanvas->sliding_adjacent_h = (gcanvas1 != NULL)
		? (gcanvas1->last_full.col == (gcanvas0->first.col - 1))
		: FALSE;
	gcanvas->sliding_adjacent_v = (gcanvas3 != NULL)
		? (gcanvas3->last_full.row == (gcanvas0->first.row - 1))
		: FALSE;
}
