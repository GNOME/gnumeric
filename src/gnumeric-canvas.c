/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric's extended canvas used to display the sheet.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 *
 * Port to Maemo:
 * 	Eduardo Lima  (eduardo.lima@indt.org.br)
 * 	Renato Araujo (renato.filho@indt.org.br)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
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
#include "cmd-edit.h"
#include "clipboard.h"
#include "sheet-filter-combo.h"
#include "widgets/gnm-cell-combo-foo-view.h"

#include <gsf/gsf-impl-utils.h>
#include <gdk/gdkkeysyms.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-locale.h>
#include <string.h>

#define SCROLL_LOCK_MASK GDK_MOD5_MASK

typedef FooCanvasClass GnmCanvasClass;
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
gnm_canvas_object_key_press (GnmCanvas *gcanvas, GdkEventKey *ev)
{
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetControl    *sc = SHEET_CONTROL (scg);
	gboolean const shift	= 0 != (ev->state & GDK_SHIFT_MASK);
	gboolean const control	= 0 != (ev->state & GDK_CONTROL_MASK);
	gboolean const alt	= 0 != (ev->state & GDK_MOD1_MASK);
	gboolean const symmetric = control && alt;
	double   const delta = 1.0 / FOO_CANVAS (gcanvas)->pixels_per_unit;

	switch (ev->keyval) {
	case GDK_Escape:
		scg_mode_edit (scg);
		gnm_app_clipboard_unant ();
		return TRUE;

	case GDK_BackSpace: /* Ick! */
	case GDK_KP_Delete:
	case GDK_Delete:
		if (scg->selected_objects != NULL) {
			cmd_objects_delete (sc->wbc,
				go_hash_keys (scg->selected_objects), NULL);
			return TRUE;
		}
		sc_mode_edit (sc);
		break;

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
		if (scg->selected_objects != NULL) {
			Sheet *sheet = sc_sheet (sc);
			GSList *prev = NULL, *ptr = sheet->sheet_objects;
			for (; ptr != NULL ; prev = ptr, ptr = ptr->next)
				if (NULL != g_hash_table_lookup (scg->selected_objects, ptr->data)) {
					SheetObject *target;
					if ((ev->state & GDK_SHIFT_MASK)) {
						if (ptr->next == NULL)
							target = sheet->sheet_objects->data;
						else
							target = ptr->next->data;
					} else {
						if (NULL == prev) {
							GSList *last = g_slist_last (ptr);
							target = last->data;
						} else
							target = prev->data;
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
 		scg_objects_nudge (scg, gcanvas, (alt ? 4 : (control ? 3 : 8)), -delta , 0, symmetric, shift);
		return TRUE;
	case GDK_KP_Right: case GDK_Right:
 		scg_objects_nudge (scg, gcanvas, (alt ? 4 : (control ? 3 : 8)), delta, 0, symmetric, shift);
		return TRUE;
	case GDK_KP_Up: case GDK_Up:
 		scg_objects_nudge (scg, gcanvas, (alt ? 6 : (control ? 1 : 8)), 0, -delta, symmetric, shift);
		return TRUE;
	case GDK_KP_Down: case GDK_Down:
		scg_objects_nudge (scg, gcanvas, (alt ? 6 : (control ? 1 : 8)), 0, delta, symmetric, shift);
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static gboolean
gnm_canvas_key_mode_sheet (GnmCanvas *gcanvas, GdkEventKey *event,
			   gboolean allow_rangesel)
{
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	SheetView *sv = sc->view;
	WorkbookControlGUI *wbcg = scg->wbcg;
	gboolean delayed_movement = FALSE;
	gboolean jump_to_bounds = event->state & GDK_CONTROL_MASK;
	gboolean is_enter = FALSE;
	int state = gnumeric_filter_modifiers (event->state);
	void (*movefn) (SheetControlGUI *, int n,
			gboolean jump, gboolean horiz);

	gboolean transition_keys = gnm_app_use_transition_keys();
	gboolean const end_mode = wbcg->last_key_was_end;

	/* Update end-mode for magic end key stuff. */
	if (event->keyval != GDK_End && event->keyval != GDK_KP_End)
		wbcg_set_end_mode (wbcg, FALSE);

	if (allow_rangesel)
		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_rangesel_extend
			: scg_rangesel_move;
	else
		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_cursor_extend
			: scg_cursor_move;

	switch (event->keyval) {
	case GDK_KP_Left:
	case GDK_Left:
		if (event->state & GDK_MOD1_MASK)
			return TRUE; /* Alt is used for accelerators */

		if (event->state & SCROLL_LOCK_MASK)
			scg_set_left_col (scg, gcanvas->first.col - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				 -(gcanvas->last_visible.col - gcanvas->first.col),
				 FALSE, TRUE);
		} else
			(*movefn) (scg, sheet->text_is_rtl ? 1 : -1,
				   jump_to_bounds || end_mode, TRUE);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		if (event->state & GDK_MOD1_MASK)
			return TRUE; /* Alt is used for accelerators */

		if (event->state & SCROLL_LOCK_MASK)
			scg_set_left_col (scg, gcanvas->first.col + 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				 gcanvas->last_visible.col - gcanvas->first.col,
				 FALSE, TRUE);
		} else
			(*movefn) (scg, sheet->text_is_rtl ? -1 : 1,
				   jump_to_bounds || end_mode, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_top_row (scg, gcanvas->first.row - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				 -(gcanvas->last_visible.row - gcanvas->first.row),
				 FALSE, FALSE);
		} else
			(*movefn) (scg, -1, jump_to_bounds || end_mode, FALSE);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		if ((event->state == GDK_MOD1_MASK)) {
			SheetObject *so;
			if (NULL == (so = sv_wbv (sv)->validation_combo)) {
				GnmRange r;
				GSList *objs = sheet_objects_get (sheet,
					range_init_cellpos (&r, &sv->edit_pos),
					GNM_FILTER_COMBO_TYPE);
				if (objs != NULL) {
					so = objs->data,
					g_slist_free (objs);
				}
			}

			if (NULL != so) {
				SheetObjectView	*sov = sheet_object_get_view (so,
					(SheetObjectViewContainer *)gcanvas->pane);
				gnm_cell_combo_foo_view_popdown (sov);
				break;
			}
		}

		if (event->state & SCROLL_LOCK_MASK)
			scg_set_top_row (scg, gcanvas->first.row + 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				 gcanvas->last_visible.row - gcanvas->first.row,
				 FALSE, FALSE);
		} else
			(*movefn) (scg, 1, jump_to_bounds || end_mode, FALSE);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_prev_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				-(gcanvas->last_visible.row - gcanvas->first.row),
				FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				-(gcanvas->last_visible.col - gcanvas->first.col),
				FALSE, TRUE);
		}
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				gcanvas->last_visible.row - gcanvas->first.row,
				FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				gcanvas->last_visible.col - gcanvas->first.col,
				FALSE, TRUE);
		}
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if (event->state & SCROLL_LOCK_MASK) {
			scg_set_left_col (scg, sv->edit_pos.col);
			scg_set_top_row (scg, sv->edit_pos.row);
		} else if (end_mode) {
			/* Same as ctrl-end.  */
			GnmRange r = sheet_get_extent (sheet, FALSE);
			(*movefn) (scg, r.end.col - sv->edit_pos.col, FALSE, TRUE);
			(*movefn)(scg, r.end.row - sv->edit_pos.row, FALSE, FALSE);
		} else {
			/* do the ctrl-home jump to A1 in 2 steps */
			(*movefn)(scg, -SHEET_MAX_COLS, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK) || transition_keys)
				(*movefn)(scg, -SHEET_MAX_ROWS, FALSE, FALSE);
		}
		break;

	case GDK_KP_End:
	case GDK_End:
		if (event->state & SCROLL_LOCK_MASK) {
			int new_col = sv->edit_pos.col - (gcanvas->last_full.col - gcanvas->first.col);
			int new_row = sv->edit_pos.row - (gcanvas->last_full.row - gcanvas->first.row);
			scg_set_left_col (scg, new_col);
			scg_set_top_row (scg, new_row);
		} else if ((event->state & GDK_CONTROL_MASK)) {
			GnmRange r = sheet_get_extent (sheet, FALSE);

			/* do the ctrl-end jump to the extent in 2 steps */
			(*movefn)(scg, r.end.col - sv->edit_pos.col, FALSE, TRUE);
			(*movefn)(scg, r.end.row - sv->edit_pos.row, FALSE, FALSE);
		} else  /* toggle end mode */
			wbcg_set_end_mode (wbcg, !end_mode);
		break;

	case GDK_KP_Insert :
	case GDK_Insert :
		if (gnm_canvas_guru_key (wbcg, event))
			break;
		if (state == GDK_CONTROL_MASK)
			sv_selection_copy (sv, WORKBOOK_CONTROL (wbcg));
		else if (state == GDK_SHIFT_MASK)
			cmd_paste_to_selection (WORKBOOK_CONTROL (wbcg), sv, PASTE_DEFAULT);
		break;

	case GDK_KP_Delete:
	case GDK_Delete:
		if (wbcg_is_editing (wbcg)) {
			/* stop auto-completion. then do a quick and cheesy update */
			wbcg_auto_complete_destroy (wbcg);
			SCG_FOREACH_PANE (scg, pane, {
				if (pane->editor)
					foo_canvas_item_request_update (FOO_CANVAS_ITEM (pane->editor));
			});
			return TRUE;
		}
		if (gnm_canvas_guru_key (wbcg, event))
			break;
		if (state == GDK_SHIFT_MASK) {
			scg_mode_edit (scg);
			sv_selection_cut (sv, WORKBOOK_CONTROL (wbcg));
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
		is_enter = TRUE;
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
			if ((event->state & GDK_MOD1_MASK) &&
			    (event->state & GDK_CONTROL_MASK) &&
			    !is_enter) {
				if (event->state & GDK_SHIFT_MASK)
					workbook_cmd_dec_indent (sc->wbc);
				else
					workbook_cmd_inc_indent	(sc->wbc);
			} else {
				/* Figure out the direction */
				gboolean forward = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
				gboolean horizontal = !is_enter;
				sv_selection_walk_step (sv, forward, horizontal);
			}
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

		if (wbcg_is_editing (wbcg)) {
			GtkWidget *entry = (GtkWidget *) wbcg_get_entry (wbcg);
			GtkWindow *top   = wbcg_toplevel (wbcg);
			if (entry != gtk_window_get_focus (top)) {
				gtk_window_set_focus (top, entry);
				return TRUE;
			}
		}
		if (!wbcg_edit_start (wbcg, FALSE, FALSE))
			return FALSE; /* attempt to edit failed */
		/* fall through */

	case GDK_BackSpace:
		/* Re-center the view on the active cell */
		if (!wbcg_is_editing (wbcg) && (event->state & GDK_CONTROL_MASK) != 0) {
			scg_make_cell_visible (scg, sv->edit_pos.col,
				sv->edit_pos.row, FALSE, TRUE);
			break;
		}
		/* fall through */

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
		scg_rangesel_stop (scg, FALSE);

		/* Forward the keystroke to the input line */
		return gtk_widget_event (GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg))),
					 (GdkEvent *) event);
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
gnm_canvas_colrow_key_press (SheetControlGUI *scg, GdkEventKey *event,
			     gboolean allow_rangesel)
{
	SheetControl *sc = (SheetControl *) scg;
	SheetView *sv = sc->view;
	GnmRange target;

	if (allow_rangesel) {
		if (scg->rangesel.active)
			target = scg->rangesel.displayed;
		else
			target.start = target.end = sv->edit_pos_real;
	} else {
		GnmRange const *r = selection_first_range (sv, NULL, NULL);
		if (NULL == r)
			return FALSE;
		target = *r;
	}

	if (event->state & GDK_SHIFT_MASK) {
		if (event->state & GDK_CONTROL_MASK)	/* full sheet */
			/* TODO : How to handle ctrl-A too ? */
			range_init_full_sheet (&target);
		else {				 	/* full row */
			target.start.col = 0;
			target.end.col = SHEET_MAX_COLS - 1;
		}
	} else if (event->state & GDK_CONTROL_MASK) {	/* full col */
		target.start.row = 0;
		target.end.row = SHEET_MAX_ROWS - 1;
	} else
		return FALSE;

	/* Accept during rangesel */
	if (allow_rangesel)
		scg_rangesel_bound (scg,
			target.start.col, target.start.row,
			target.end.col, target.end.row);
	/* actually want the ctrl/shift space keys handled by the input module
	 * filters during an edit */
	else if (!wbcg_is_editing (scg->wbcg))
		sv_selection_set (sv, &sv->edit_pos,
			target.start.col, target.start.row,
			target.end.col, target.end.row);
	else
		return FALSE;

	return TRUE;
}

static gint
gnm_canvas_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnmCanvas	*gcanvas = GNM_CANVAS (widget);
	SheetControlGUI *scg = gcanvas->simple.scg;
	gboolean	 allow_rangesel;

	switch (event->keyval) {
	case GDK_Shift_L:   case GDK_Shift_R:
	case GDK_Alt_L:     case GDK_Alt_R:
	case GDK_Control_L: case GDK_Control_R:
		return (*GTK_WIDGET_CLASS (parent_klass)->key_press_event) (widget, event);
	}

	/* Object manipulation */
	if ((scg->selected_objects != NULL || scg->new_object != NULL)) {
		if (wbcg_edit_get_guru (scg->wbcg) == NULL  &&
		    gnm_canvas_object_key_press (gcanvas, event))
			return TRUE;
	}

	/* handle grabs after object keys to allow Esc to cancel, and arrows to
	 * fine tune position even while dragging */
	if (scg->grab_stack > 0)
		return TRUE;

	allow_rangesel = wbcg_rangesel_possible (scg->wbcg);

	/* handle ctrl/shift space before input-method filter steals it */
	if (event->keyval == GDK_space &&
	    gnm_canvas_colrow_key_press (scg, event, allow_rangesel))
		return TRUE;

	gcanvas->insert_decimal =
		event->keyval == GDK_KP_Decimal ||
		event->keyval == GDK_KP_Separator;

	if (gtk_im_context_filter_keypress (gcanvas->im_context,event))
		return TRUE;
	gcanvas->reseting_im = TRUE;
	gtk_im_context_reset (gcanvas->im_context);
	gcanvas->reseting_im = FALSE;

	if (gnm_canvas_key_mode_sheet (gcanvas, event, allow_rangesel))
		return TRUE;

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
		return TRUE;
	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (gcanvas->simple.scg->selected_objects == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (sc->wbc), TRUE, NULL);

	return (*GTK_WIDGET_CLASS (parent_klass)->key_release_event) (widget, event);
}

static gint
gnm_canvas_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
#ifndef USE_HILDON
	gtk_im_context_focus_in (GNM_CANVAS (widget)->im_context);
#endif
	return (*GTK_WIDGET_CLASS (parent_klass)->focus_in_event) (widget, event);
}

static gint
gnm_canvas_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
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
gnm_canvas_size_allocate (GtkWidget *w, GtkAllocation *allocation)
{
	GnmCanvas *gcanvas = GNM_CANVAS (w);
	(*GTK_WIDGET_CLASS (parent_klass)->size_allocate) (w, allocation);
	gnm_canvas_compute_visible_region (gcanvas, TRUE);
}

static void
gnm_canvas_finalize (GObject *object)
{
	g_object_unref (G_OBJECT (GNM_CANVAS (object)->im_context));
	G_OBJECT_CLASS (parent_klass)->finalize (object);
}

static void
gnm_canvas_class_init (GnmCanvasClass *klass)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class  = (GtkWidgetClass *) klass;

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

static GtkEditable *
gnm_canvas_get_editable (GnmCanvas const *gcanvas)
{
	GnmExprEntry *ee = wbcg_get_entry_logical (gcanvas->simple.scg->wbcg);
	GtkEntry *entry = gnm_expr_entry_get_entry (ee);
	return GTK_EDITABLE (entry);
}

static void
gnm_canvas_commit_cb (GtkIMContext *context, const gchar *str, GnmCanvas *gcanvas)
{
	gint tmp_pos, length;
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = gnm_canvas_get_editable (gcanvas);

	if (!wbcg_is_editing (wbcg) && !wbcg_edit_start (wbcg, TRUE, TRUE))
		return;

	if (gcanvas->insert_decimal) {
		GString const *s = go_format_get_decimal ();
		str = s->str;
		length = s->len;
	} else
		length = strlen (str);

	if (gtk_editable_get_selection_bounds (editable, NULL, NULL))
		gtk_editable_delete_selection (editable);
	else {
		tmp_pos = gtk_editable_get_position (editable);
		if (GTK_ENTRY (editable)->overwrite_mode)
			gtk_editable_delete_text (editable,tmp_pos,tmp_pos+1);
	}

	tmp_pos = gtk_editable_get_position (editable);
	gtk_editable_insert_text (editable, str, length, &tmp_pos);
	gtk_editable_set_position (editable, tmp_pos);
}

static void
gnm_canvas_preedit_changed_cb (GtkIMContext *context, GnmCanvas *gcanvas)
{
	gchar *preedit_string;
	int tmp_pos;
	int cursor_pos;
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	GtkEditable *editable = gnm_canvas_get_editable (gcanvas);

	tmp_pos = gtk_editable_get_position (editable);
	if (gcanvas->preedit_attrs)
		pango_attr_list_unref (gcanvas->preedit_attrs);
	gtk_im_context_get_preedit_string (gcanvas->im_context, &preedit_string, &gcanvas->preedit_attrs, &cursor_pos);

	/* in gtk-2.8 something changed.  gtk_im_context_reset started
	 * triggering a pre-edit-changed.  We'd end up start and finishing an
	 * empty edit every time the cursor moved */
	if (!gcanvas->reseting_im &&
	    !wbcg_is_editing (wbcg) && !wbcg_edit_start (wbcg, TRUE, TRUE)) {
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
	GtkEditable *editable = gnm_canvas_get_editable (gcanvas);
	gchar *surrounding = gtk_editable_get_chars (editable, 0, -1);
	gint   cur_pos     = gtk_editable_get_position (editable);

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
	GtkEditable *editable = gnm_canvas_get_editable (gcanvas);
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
	gcanvas->reseting_im = FALSE;

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
 * gnm_canvas_find_col:
 * @gcanvas :
 * @x : In canvas coords
 * @col_origin : optionally return the canvas coord of the col
 *
 * Returns the column containing canvas coord @x
 **/
int
gnm_canvas_find_col (GnmCanvas const *gcanvas, int x, int *col_origin)
{
	Sheet const *sheet = scg_sheet (gcanvas->simple.scg);
	int col   = gcanvas->first.col;
	int pixel = gcanvas->first_offset.col;

	x = gnm_canvas_x_w2c (gcanvas, x);

	if (x < pixel) {
		while (col > 0) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, --col);
			if (ci->visible) {
				pixel -= ci->size_pixels;
				if (x >= pixel) {
					if (col_origin)
						*col_origin = gnm_canvas_x_w2c (gcanvas,
										pixel);
					return col;
				}
			}
		}
		if (col_origin)
			*col_origin = gnm_canvas_x_w2c (gcanvas, 0);
		return 0;
	}

	do {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const tmp = ci->size_pixels;
			if (x <= pixel + tmp) {
				if (col_origin)
					*col_origin = gnm_canvas_x_w2c (gcanvas, pixel);
				return col;
			}
			pixel += tmp;
		}
	} while (++col < SHEET_MAX_COLS - 1);

	if (col_origin)
		*col_origin = gnm_canvas_x_w2c (gcanvas, pixel);
	return SHEET_MAX_COLS - 1;
}

/**
 * gnm_canvas_find_row:
 * @gcanvas :
 * @y : In canvas coords
 * @row_origin : optionally return the canvas coord of the row
 *
 * Returns the column containing canvas coord @y
 **/
int
gnm_canvas_find_row (GnmCanvas const *gcanvas, int y, int *row_origin)
{
	Sheet const *sheet = scg_sheet (gcanvas->simple.scg);
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
	Sheet const *sheet = scg_sheet (scg);
	FooCanvas   *canvas = FOO_CANVAS (gcanvas);
	int pixels, col, row, width, height;

#if 0
	g_warning ("compute_vis(W)[%d] = %d", gcanvas->pane->index,
		   GTK_WIDGET (gcanvas)->allocation.width);
#endif

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		int col_offset = gcanvas->first_offset.col = scg_colrow_distance_get (scg,
			TRUE, 0, gcanvas->first.col);
		if (sheet->text_is_rtl)
			col_offset = gnm_canvas_x_w2c (gcanvas,
				gcanvas->first_offset.col + GTK_WIDGET (gcanvas)->allocation.width - 1);
		if (NULL != gcanvas->pane->col.canvas)
			foo_canvas_scroll_to (gcanvas->pane->col.canvas, col_offset, 0);

		gcanvas->first_offset.row = scg_colrow_distance_get (scg,
			FALSE, 0, gcanvas->first.row);
		if (NULL != gcanvas->pane->row.canvas)
			foo_canvas_scroll_to (gcanvas->pane->row.canvas,
				0, gcanvas->first_offset.row);

		foo_canvas_scroll_to (FOO_CANVAS (gcanvas),
			col_offset, gcanvas->first_offset.row);
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
		sc_scrollbar_config (SHEET_CONTROL (scg));

	/* Force the cursor to update its bounds relative to the new visible region */
	gnm_pane_reposition_cursors (gcanvas->pane);
}

void
gnm_canvas_redraw_range (GnmCanvas *gcanvas, GnmRange const *r)
{
	SheetControlGUI *scg;
	int x1, y1, x2, y2;
	GnmRange tmp;
	SheetControl *sc;
	Sheet *sheet;

	g_return_if_fail (IS_GNM_CANVAS (gcanvas));

	scg = gcanvas->simple.scg;
	sc = (SheetControl *) scg;
	sheet = sc->sheet;

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

	if (sheet->text_is_rtl)  {
		int tmp = gnm_canvas_x_w2c (gcanvas, x1);
		x1 = gnm_canvas_x_w2c (gcanvas, x2);
		x2 = tmp;
	}
	foo_canvas_request_redraw (&gcanvas->simple.canvas, x1-2, y1-2, x2, y2);
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
cb_gcanvas_sliding (GnmCanvas *gcanvas)
{
	int const pane_index = gcanvas->pane->index;
	GnmCanvas *gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	GnmCanvas *gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	GnmCanvas *gcanvas3 = scg_pane (gcanvas->simple.scg, 3);
	gboolean slide_x = FALSE, slide_y = FALSE;
	int col = -1, row = -1;
	gboolean text_is_rtl = gcanvas->simple.scg->sheet_control.sheet->text_is_rtl;
	GnmCanvasSlideInfo info;

#if 0
	g_warning ("slide: %d, %d", gcanvas->sliding_dx, gcanvas->sliding_dy);
#endif
	if (gcanvas->sliding_dx > 0) {
		GnmCanvas *target_gcanvas = gcanvas;

		slide_x = TRUE;
		if (pane_index == 1 || pane_index == 2) {
			if (!gcanvas->sliding_adjacent_h) {
				int width = GTK_WIDGET (gcanvas)->allocation.width;
				int x = gcanvas->first_offset.col + width + gcanvas->sliding_dx;

				/* in case pane is narrow */
				col = gnm_canvas_find_col (gcanvas, text_is_rtl
					? -(x + gcanvas->simple.canvas.scroll_x1 * gcanvas->simple.canvas.pixels_per_unit) : x, NULL);
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
					col = gnm_canvas_find_col (gcanvas, text_is_rtl
						? -(x + gcanvas->simple.canvas.scroll_x1 * gcanvas->simple.canvas.pixels_per_unit) : x, NULL);
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

	if (col < 0) {
		col = gnm_canvas_find_col (gcanvas, text_is_rtl
			? -(gcanvas->sliding_x + gcanvas->simple.canvas.scroll_x1 * gcanvas->simple.canvas.pixels_per_unit)
			: gcanvas->sliding_x, NULL);
	} else if (row < 0)
		row = gnm_canvas_find_row (gcanvas, gcanvas->sliding_y, NULL);

	info.col = col;
	info.row = row;
	info.user_data = gcanvas->slide_data;
	if (gcanvas->slide_handler == NULL ||
	    (*gcanvas->slide_handler) (gcanvas, &info))
		scg_make_cell_visible (gcanvas->simple.scg, col, row, FALSE, TRUE);

	if (!slide_x && !slide_y)
		gnm_canvas_slide_stop (gcanvas);
	else if (gcanvas->sliding == -1)
		gcanvas->sliding = g_timeout_add (
			300, (GSourceFunc) cb_gcanvas_sliding, gcanvas);

	return TRUE;
}

/**
 * gnm_canvas_handle_motion :
 * @gcanvas	 : The GnmCanvas managing the scroll
 * @canvas	 : The Canvas the event comes from
 * @event	 : The motion event (in world coords, with rtl inversion)
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
	gboolean text_is_rtl;

	g_return_val_if_fail (IS_GNM_CANVAS (gcanvas), FALSE);
	g_return_val_if_fail (FOO_IS_CANVAS (canvas), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (slide_handler != NULL, FALSE);

	text_is_rtl = gcanvas->simple.scg->sheet_control.sheet->text_is_rtl;

	/* NOTE : work around a bug in gtk's use of X.
	 * When dragging past the right edge of the sheet in rtl mode
	 * we are operating at the edge of a 32k wide window and the event
	 * coords get larger than can be held in a signed short.  As a result
	 * we get world coords of -65535 or so.
	 *
	 * KLUDGE KLUDGE KLUDGE
	 * with our current limit of 256 columns it is unlikely that we'll hit
	 * -65535 (at appropriate zoom) as a valid coord (it would require all
	 * cols to be 256 pixels wide.  it is not impossible, but at least
	 * unlikely.  So we put in a kludge here to catch the screw up and
	 * remap it.   This is not pretty,  at large zooms this is not far
	 * fetched.*/
	if (text_is_rtl &&
	    event->x < (-64000 / gcanvas->simple.canvas.pixels_per_unit)) {
#if SHEET_MAX_COLS > 700 /* a guestimate */
#warning WARNING We need a better solution to the rtl event kludge with SHEET_MAX_COLS so large
#endif
		foo_canvas_w2c (canvas, event->x + 65536, event->y, &x, &y);
	} else
		foo_canvas_w2c (canvas, event->x, event->y, &x, &y);
	if (text_is_rtl)
		x = -(x + gcanvas->simple.canvas.scroll_x1 * gcanvas->simple.canvas.pixels_per_unit);

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
			GnmCanvasSlideInfo info;
			info.row = gnm_canvas_find_row (gcanvas, y, NULL);
			info.col = gnm_canvas_find_col (gcanvas, text_is_rtl
				? -(x + gcanvas->simple.canvas.scroll_x1 * gcanvas->simple.canvas.pixels_per_unit)
				: x, NULL);
			info.user_data = user_data;
			(*slide_handler) (gcanvas, &info);
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
		cb_gcanvas_sliding (gcanvas);
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

/*
 * gnm_canvas_window_to_coord :
 * @gcanvas : #GnmCanvas
 * @x :
 * @y :
 * @wx : result
 * @wy : result
 *
 * Map window coords into sheet object coords
 **/
void
gnm_canvas_window_to_coord (GnmCanvas *gcanvas,
			    gint    x,	gint    y,
			    double *wx, double *wy)
{
	double const scale = 1. / FOO_CANVAS (gcanvas)->pixels_per_unit;
	y += gcanvas->first_offset.row;

	if (gcanvas->simple.scg->sheet_control.sheet->text_is_rtl)
		x = x - GTK_WIDGET (gcanvas)->allocation.width - 1 - gcanvas->first_offset.col;
	else
		x += gcanvas->first_offset.col;
	*wx = x * scale;
	*wy = y * scale;
}

static gboolean
cb_obj_autoscroll (GnmCanvas *gcanvas, GnmCanvasSlideInfo const *info)
{
	SheetControlGUI *scg = gcanvas->simple.scg;
	GdkModifierType mask;

	/* Cheesy hack calculate distance we move the screen, this loses the
	 * mouse position */
	double dx = gcanvas->first_offset.col;
	double dy = gcanvas->first_offset.row;
	scg_make_cell_visible (scg, info->col, info->row, FALSE, TRUE);
	dx = gcanvas->first_offset.col - dx;
	dy = gcanvas->first_offset.row - dy;

#if 0
	g_warning ("dx = %g, dy = %g", dx, dy);
#endif

	gcanvas->pane->drag.had_motion = TRUE;
	gdk_window_get_pointer (gtk_widget_get_parent_window (GTK_WIDGET (gcanvas)),
		NULL, NULL, &mask);
 	scg_objects_drag (gcanvas->simple.scg, gcanvas,
 		NULL, &dx, &dy, 8, FALSE, (mask & GDK_SHIFT_MASK) != 0, TRUE);

 	gcanvas->pane->drag.last_x += dx;
 	gcanvas->pane->drag.last_y += dy;
	return FALSE;
}

/**
 * This does not really belong here.  We're breaking all sorts of encapsulations
 * but the line between GnmCanvas and GnmPane is blurry.
 **/
void
gnm_canvas_object_autoscroll (GnmCanvas *gcanvas, GdkDragContext *context,
			      gint x, gint y, guint time)
{
	int const pane_index = gcanvas->pane->index;
	SheetControlGUI *scg = gcanvas->simple.scg;
	GnmCanvas *gcanvas0 = scg_pane (scg, 0);
	GnmCanvas *gcanvas1 = scg_pane (scg, 1);
	GnmCanvas *gcanvas3 = scg_pane (scg, 3);
	GtkWidget *w = GTK_WIDGET (gcanvas);
	gint dx, dy;

	if (y < w->allocation.y) {
		if (pane_index < 2 && gcanvas3 != NULL)
			w = GTK_WIDGET (gcanvas3);
		dy = y - w->allocation.y;
		g_return_if_fail (dy <= 0);
	} else if (y >= (w->allocation.y + w->allocation.height)) {
		if (pane_index >= 2)
			w = GTK_WIDGET (gcanvas0);
		dy = y - (w->allocation.y + w->allocation.height);
		g_return_if_fail (dy >= 0);
	} else
		dy = 0;
	if (x < w->allocation.x) {
		if ((pane_index == 0 || pane_index == 3) && gcanvas1 != NULL)
			w = GTK_WIDGET (gcanvas1);
		dx = x - w->allocation.x;
		g_return_if_fail (dx <= 0);
	} else if (x >= (w->allocation.x + w->allocation.width)) {
		if (pane_index >= 2)
			w = GTK_WIDGET (gcanvas0);
		dx = x - (w->allocation.x + w->allocation.width);
		g_return_if_fail (dx >= 0);
	} else
		dx = 0;

	g_object_set_data (&context->parent_instance,
		"wbcg", scg_wbcg (scg));
	gcanvas->sliding_dx    = dx;
	gcanvas->sliding_dy    = dy;
	gcanvas->slide_handler = &cb_obj_autoscroll;
	gcanvas->slide_data    = NULL;
	gcanvas->sliding_x     = x;
	gcanvas->sliding_y     = y;
	if (gcanvas->sliding == -1)
		cb_gcanvas_sliding (gcanvas);
}
