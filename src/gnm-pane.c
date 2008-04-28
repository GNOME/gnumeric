/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric's extended canvas used to display a pane
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 *
 * Port to Maemo:
 *	Eduardo Lima  (eduardo.lima@indt.org.br)
 *	Renato Araujo (renato.filho@indt.org.br)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "gnm-pane-impl.h"
#include "gnm-pane.h"

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
#include "wbc-gtk-impl.h"
#include "workbook.h"
#include "workbook-cmd-format.h"
#include "commands.h"
#include "cmd-edit.h"
#include "clipboard.h"
#include "sheet-filter-combo.h"
#include "widgets/gnm-cell-combo-foo-view.h"
#include "item-acetate.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "item-edit.h"
#include "item-grid.h"

#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-locale.h>
#include <goffice/utils/go-geometry.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-line.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <gsf/gsf-impl-utils.h>

#include <gtk/gtkdnd.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtklabel.h>
#include <gdk/gdkkeysyms.h>

#include <string.h>

#define SCROLL_LOCK_MASK GDK_MOD5_MASK

typedef FooCanvasClass GnmPaneClass;
static FooCanvasClass *parent_klass;

static void cb_pane_popup_menu (GnmPane *pane);
static void gnm_pane_clear_obj_size_tip (GnmPane *pane);

/**
 * For now, application/x-gnumeric is disabled. It handles neither
 * images nor graphs correctly.
 */
static GtkTargetEntry const drag_types_in[] = {
	{(char *) "GNUMERIC_SAME_PROC", GTK_TARGET_SAME_APP, 0},
	/* {(char *) "application/x-gnumeric", 0, 0}, */
};

static GtkTargetEntry const drag_types_out[] = {
	{(char *) "GNUMERIC_SAME_PROC", GTK_TARGET_SAME_APP, 0},
	{(char *) "application/x-gnumeric", 0, 0},
};

static gboolean
gnm_pane_guru_key (WBCGtk const *wbcg, GdkEventKey *event)
{
	GtkWidget *entry, *guru = wbc_gtk_get_guru (wbcg);

	if (guru == NULL)
		return FALSE;

	entry = wbcg_get_entry_underlying (wbcg);
	gtk_widget_event ((entry != NULL) ? entry : guru, (GdkEvent *) event);
	return TRUE;
}

static gboolean
gnm_pane_object_key_press (GnmPane *pane, GdkEventKey *ev)
{
	SheetControlGUI *scg = pane->simple.scg;
	SheetControl    *sc = SHEET_CONTROL (scg);
	gboolean const shift	= 0 != (ev->state & GDK_SHIFT_MASK);
	gboolean const control	= 0 != (ev->state & GDK_CONTROL_MASK);
	gboolean const alt	= 0 != (ev->state & GDK_MOD1_MASK);
	gboolean const symmetric = control && alt;
	double   const delta = 1.0 / FOO_CANVAS (pane)->pixels_per_unit;

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
		scg_objects_nudge (scg, pane, (alt ? 4 : (control ? 3 : 8)), -delta , 0, symmetric, shift);
		return TRUE;
	case GDK_KP_Right: case GDK_Right:
		scg_objects_nudge (scg, pane, (alt ? 4 : (control ? 3 : 8)), delta, 0, symmetric, shift);
		return TRUE;
	case GDK_KP_Up: case GDK_Up:
		scg_objects_nudge (scg, pane, (alt ? 6 : (control ? 1 : 8)), 0, -delta, symmetric, shift);
		return TRUE;
	case GDK_KP_Down: case GDK_Down:
		scg_objects_nudge (scg, pane, (alt ? 6 : (control ? 1 : 8)), 0, delta, symmetric, shift);
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static gboolean
gnm_pane_key_mode_sheet (GnmPane *pane, GdkEventKey *event,
			 gboolean allow_rangesel)
{
	SheetControlGUI *scg = pane->simple.scg;
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	SheetView *sv = sc->view;
	WBCGtk *wbcg = scg->wbcg;
	gboolean delayed_movement = FALSE;
	gboolean jump_to_bounds = event->state & GDK_CONTROL_MASK;
	gboolean is_enter = FALSE;
	int state = gnumeric_filter_modifiers (event->state);
	void (*movefn) (SheetControlGUI *, int n, gboolean jump, gboolean horiz);

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
			scg_set_left_col (scg, pane->first.col - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				-(pane->last_visible.col - pane->first.col),
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
			scg_set_left_col (scg, pane->first.col + 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				pane->last_visible.col - pane->first.col,
				FALSE, TRUE);
		} else
			(*movefn) (scg, sheet->text_is_rtl ? -1 : 1,
				   jump_to_bounds || end_mode, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		if (event->state & SCROLL_LOCK_MASK)
			scg_set_top_row (scg, pane->first.row - 1);
		else if (transition_keys && jump_to_bounds) {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
				-(pane->last_visible.row - pane->first.row),
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
				if (objs != NULL)
					so = objs->data, g_slist_free (objs);
			}

			if (NULL != so) {
				SheetObjectView	*sov = sheet_object_get_view (so,
					(SheetObjectViewContainer *)pane);
				gnm_cell_combo_foo_view_popdown (sov, event->time);
				break;
			}
	}

	if (event->state & SCROLL_LOCK_MASK)
		scg_set_top_row (scg, pane->first.row + 1);
	else if (transition_keys && jump_to_bounds) {
		delayed_movement = TRUE;
		scg_queue_movement (scg, movefn,
				    pane->last_visible.row - pane->first.row,
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
					    -(pane->last_visible.row - pane->first.row),
					    FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
					    -(pane->last_visible.col - pane->first.col),
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
					    pane->last_visible.row - pane->first.row,
					    FALSE, FALSE);
		} else {
			delayed_movement = TRUE;
			scg_queue_movement (scg, movefn,
					    pane->last_visible.col - pane->first.col,
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
			(*movefn)(scg, -gnm_sheet_get_max_cols (sheet), FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK) || transition_keys)
				(*movefn)(scg, -gnm_sheet_get_max_rows (sheet), FALSE, FALSE);
		}
		break;

	case GDK_KP_End:
	case GDK_End:
		if (event->state & SCROLL_LOCK_MASK) {
			int new_col = sv->edit_pos.col - (pane->last_full.col - pane->first.col);
			int new_row = sv->edit_pos.row - (pane->last_full.row - pane->first.row);
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
		if (gnm_pane_guru_key (wbcg, event))
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
		if (gnm_pane_guru_key (wbcg, event))
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
				wbcg_get_entry_underlying (wbcg), (GdkEvent *) event);
		is_enter = TRUE;
		/* fall down */

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
		if (gnm_pane_guru_key (wbcg, event))
			break;

		/* Be careful to restore the editing sheet if we are editing */
		if (wbcg_is_editing (wbcg))
			sheet = wbcg->editing_sheet;

		if (wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL)) {
			if ((event->state & GDK_MOD1_MASK) &&
			    (event->state & GDK_CONTROL_MASK) &&
			    !is_enter) {
				if (event->state & GDK_SHIFT_MASK)
					workbook_cmd_dec_indent (sc->wbc);
				else
					workbook_cmd_inc_indent	(sc->wbc);
			} else if (gnm_app_enter_moves_dir () != GO_DIRECTION_NONE) {
				gboolean forward = TRUE;
				gboolean horizontal = TRUE;
				if (is_enter) {
					horizontal = go_direction_is_horizontal (
						gnm_app_enter_moves_dir ());
					forward = go_direction_is_forward (
						gnm_app_enter_moves_dir ());
				}

				if (event->state & GDK_SHIFT_MASK)
					forward = !forward;

				sv_selection_walk_step (sv, forward, horizontal);

				/* invalidate, in case Enter direction changes */
				if (is_enter)
					sv->first_tab_col = -1;
			}
		}
		break;

	case GDK_Escape:
		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
		gnm_app_clipboard_unant ();
		break;

	case GDK_F4:
		if (wbcg_is_editing (wbcg))
			return gtk_widget_event (
				wbcg_get_entry_underlying (wbcg), (GdkEvent *) event);
		return TRUE;

	case GDK_F2:
	if (gnm_pane_guru_key (wbcg, event))
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
	return gtk_widget_event (
		wbcg_get_entry_underlying (wbcg), (GdkEvent *) event);
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
gnm_pane_colrow_key_press (SheetControlGUI *scg, GdkEventKey *event,
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
		else {					/* full row */
			target.start.col = 0;
			target.end.col = gnm_sheet_get_max_cols (sv->sheet) - 1;
		}
	} else if (event->state & GDK_CONTROL_MASK) {	/* full col */
		target.start.row = 0;
		target.end.row = gnm_sheet_get_max_rows (sv->sheet) - 1;
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
gnm_pane_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnmPane	*pane = GNM_PANE (widget);
	SheetControlGUI *scg = pane->simple.scg;
	gboolean	 allow_rangesel;

	switch (event->keyval) {
	case GDK_Shift_L:   case GDK_Shift_R:
	case GDK_Alt_L:     case GDK_Alt_R:
	case GDK_Control_L: case GDK_Control_R:
	return (*GTK_WIDGET_CLASS (parent_klass)->key_press_event) (widget, event);
	}

	/* Object manipulation */
	if ((scg->selected_objects != NULL || scg->new_object != NULL)) {
		if (wbc_gtk_get_guru (scg->wbcg) == NULL  &&
		    gnm_pane_object_key_press (pane, event))
			return TRUE;
	}

	/* handle grabs after object keys to allow Esc to cancel, and arrows to
	 * fine tune position even while dragging */
	if (scg->grab_stack > 0)
		return TRUE;

	allow_rangesel = wbcg_rangesel_possible (scg->wbcg);

	/* handle ctrl/shift space before input-method filter steals it */
	if (event->keyval == GDK_space &&
	    gnm_pane_colrow_key_press (scg, event, allow_rangesel))
		return TRUE;

	pane->insert_decimal =
		event->keyval == GDK_KP_Decimal ||
		event->keyval == GDK_KP_Separator;

	if (gtk_im_context_filter_keypress (pane->im_context,event))
		return TRUE;
	pane->reseting_im = TRUE;
	gtk_im_context_reset (pane->im_context);
	pane->reseting_im = FALSE;

	if (gnm_pane_key_mode_sheet (pane, event, allow_rangesel))
		return TRUE;

	return (*GTK_WIDGET_CLASS (parent_klass)->key_press_event) (widget, event);
}

static gint
gnm_pane_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnmPane *pane = GNM_PANE (widget);
	SheetControl *sc = (SheetControl *) pane->simple.scg;

	if (pane->simple.scg->grab_stack > 0)
		return TRUE;

	if (gtk_im_context_filter_keypress (pane->im_context,event))
		return TRUE;
	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (pane->simple.scg->selected_objects == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (sc->wbc), TRUE, NULL);

	return (*GTK_WIDGET_CLASS (parent_klass)->key_release_event) (widget, event);
}

static gint
gnm_pane_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
#ifndef GNM_USE_HILDON
	gtk_im_context_focus_in (GNM_PANE (widget)->im_context);
#endif
	return (*GTK_WIDGET_CLASS (parent_klass)->focus_in_event) (widget, event);
}

static gint
gnm_pane_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	gtk_im_context_focus_out (GNM_PANE (widget)->im_context);
	return (*GTK_WIDGET_CLASS (parent_klass)->focus_out_event) (widget, event);
}

static void
gnm_pane_realize (GtkWidget *w)
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

	gtk_im_context_set_client_window (GNM_PANE (w)->im_context,
					  gtk_widget_get_toplevel (w)->window);
}

static void
gnm_pane_unrealize (GtkWidget *widget)
{
	GnmPane *pane;

	pane = GNM_PANE (widget);
	g_return_if_fail (pane != NULL);

	if (pane->im_context)
		gtk_im_context_set_client_window (pane->im_context, NULL);

	(*GTK_WIDGET_CLASS (parent_klass)->unrealize)(widget);
}

static void
gnm_pane_size_allocate (GtkWidget *w, GtkAllocation *allocation)
{
	GnmPane *pane = GNM_PANE (w);
	(*GTK_WIDGET_CLASS (parent_klass)->size_allocate) (w, allocation);
	gnm_pane_compute_visible_region (pane, TRUE);
}

static GtkEditable *
gnm_pane_get_editable (GnmPane const *pane)
{
	GnmExprEntry *gee = wbcg_get_entry_logical (pane->simple.scg->wbcg);
	GtkEntry *entry = gnm_expr_entry_get_entry (gee);
	return GTK_EDITABLE (entry);
}

static void
cb_gnm_pane_commit (GtkIMContext *context, char const *str, GnmPane *pane)
{
	gint tmp_pos, length;
	WBCGtk *wbcg = pane->simple.scg->wbcg;
	GtkEditable *editable = gnm_pane_get_editable (pane);

	if (!wbcg_is_editing (wbcg) && !wbcg_edit_start (wbcg, TRUE, TRUE))
		return;

	if (pane->insert_decimal) {
		GString const *s = go_locale_get_decimal ();
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
cb_gnm_pane_preedit_changed (GtkIMContext *context, GnmPane *pane)
{
	gchar *preedit_string;
	int tmp_pos;
	int cursor_pos;
	WBCGtk *wbcg = pane->simple.scg->wbcg;
	GtkEditable *editable = gnm_pane_get_editable (pane);

	tmp_pos = gtk_editable_get_position (editable);
	if (pane->preedit_attrs)
		pango_attr_list_unref (pane->preedit_attrs);
	gtk_im_context_get_preedit_string (pane->im_context, &preedit_string, &pane->preedit_attrs, &cursor_pos);

	/* in gtk-2.8 something changed.  gtk_im_context_reset started
	 * triggering a pre-edit-changed.  We'd end up start and finishing an
	 * empty edit every time the cursor moved */
	if (!pane->reseting_im &&
	    !wbcg_is_editing (wbcg) && !wbcg_edit_start (wbcg, TRUE, TRUE)) {
		gtk_im_context_reset (pane->im_context);
		pane->preedit_length = 0;
		if (pane->preedit_attrs)
			pango_attr_list_unref (pane->preedit_attrs);
		pane->preedit_attrs = NULL;
		g_free (preedit_string);
		return;
	}

	if (pane->preedit_length)
		gtk_editable_delete_text (editable,tmp_pos,tmp_pos+pane->preedit_length);
	pane->preedit_length = strlen (preedit_string);

	if (pane->preedit_length)
		gtk_editable_insert_text (editable, preedit_string, pane->preedit_length, &tmp_pos);
	g_free (preedit_string);
}

static gboolean
cb_gnm_pane_retrieve_surrounding (GtkIMContext *context, GnmPane *pane)
{
	GtkEditable *editable = gnm_pane_get_editable (pane);
	gchar *surrounding = gtk_editable_get_chars (editable, 0, -1);
	gint   cur_pos     = gtk_editable_get_position (editable);

	gtk_im_context_set_surrounding (context,
					surrounding, strlen (surrounding),
					g_utf8_offset_to_pointer (surrounding, cur_pos) - surrounding);

	g_free (surrounding);
	return TRUE;
}

static gboolean
cb_gnm_pane_delete_surrounding (GtkIMContext *context,
				gint         offset,
				gint         n_chars,
				GnmPane    *pane)
{
	GtkEditable *editable = gnm_pane_get_editable (pane);
	gint cur_pos = gtk_editable_get_position (editable);
	gtk_editable_delete_text (editable,
				  cur_pos + offset,
				  cur_pos + offset + n_chars);

	return TRUE;
}

/* create views for the sheet objects now that we exist */
static void
cb_pane_init_objs (GnmPane *pane)
{
	Sheet *sheet = scg_sheet (pane->simple.scg);
	GSList *ptr, *list;

	if (sheet != NULL) {
		/* List is stored in reverse stacking order.  Top of stack is
		 * first.  On creation new foocanvas item get added to
		 * the front, so we need to create the views in reverse order */
		list = g_slist_reverse (g_slist_copy (sheet->sheet_objects));
		for (ptr = list; ptr != NULL ; ptr = ptr->next)
			sheet_object_new_view (ptr->data,
				(SheetObjectViewContainer *)pane);
		g_slist_free (list);
	}
}

static void
cb_ctrl_pts_free (GtkObject **ctrl_pts)
{
	int i = 10;
	while (i-- > 0)
		if (ctrl_pts [i] != NULL)
			gtk_object_destroy (ctrl_pts [i]);
	g_free (ctrl_pts);
}

static void
gnm_pane_dispose (GObject *obj)
{
	GnmPane *pane = GNM_PANE (obj);

	if (pane->col.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->col.canvas));
		pane->col.canvas = NULL;
	}

	if (pane->row.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->row.canvas));
		pane->row.canvas = NULL;
	}

	if (pane->im_context) {
		GtkIMContext *imc = pane->im_context;

		pane->im_context = NULL;
		g_signal_handlers_disconnect_by_func
			(imc, cb_gnm_pane_commit, pane);
		g_signal_handlers_disconnect_by_func
			(imc, cb_gnm_pane_preedit_changed, pane);
		g_signal_handlers_disconnect_by_func
			(imc, cb_gnm_pane_retrieve_surrounding, pane);
		g_signal_handlers_disconnect_by_func
			(imc, cb_gnm_pane_delete_surrounding, pane);
		gtk_im_context_set_client_window (imc, NULL);
		g_object_unref (imc);
	}

	g_slist_free (pane->cursor.animated);
	pane->cursor.animated = NULL;

	if (pane->mouse_cursor) {
		gdk_cursor_unref (pane->mouse_cursor);
		pane->mouse_cursor = NULL;
	}
	gnm_pane_clear_obj_size_tip (pane);

	if (pane->drag.ctrl_pts) {
		g_hash_table_destroy (pane->drag.ctrl_pts);
		pane->drag.ctrl_pts = NULL;
	}

	/* Be anal just in case we somehow manage to remove a pane
	 * unexpectedly.  */
	pane->grid = NULL;
	pane->editor = NULL;
	pane->cursor.std = pane->cursor.rangesel = pane->cursor.special = pane->cursor.expr_range = NULL;
	pane->size_guide.guide = NULL;
	pane->size_guide.start = NULL;
	pane->size_guide.points = NULL;

	G_OBJECT_CLASS (parent_klass)->dispose (obj);
}

static void
gnm_pane_init (GnmPane *pane)
{
	FooCanvas	*canvas = FOO_CANVAS (pane);
	FooCanvasGroup	*root_group = FOO_CANVAS_GROUP (canvas->root);

	pane->grid_items   = FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));
	pane->object_views = FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));
	pane->action_items = FOO_CANVAS_GROUP (
		foo_canvas_item_new (root_group, FOO_TYPE_CANVAS_GROUP, NULL));

	pane->first.col = pane->last_full.col = pane->last_visible.col = 0;
	pane->first.row = pane->last_full.row = pane->last_visible.row = 0;
	pane->first_offset.col = 0;
	pane->first_offset.row = 0;

	pane->editor = NULL;
	pane->mouse_cursor = NULL;
	pane->cursor.rangesel = NULL;
	pane->cursor.special = NULL;
	pane->cursor.expr_range = NULL;
	pane->cursor.animated = NULL;
	pane->size_tip = NULL;

	pane->slide_handler = NULL;
	pane->slide_data = NULL;
	pane->sliding = -1;
	pane->sliding_x  = pane->sliding_dx = -1;
	pane->sliding_y  = pane->sliding_dy = -1;
	pane->sliding_adjacent_h = pane->sliding_adjacent_v = FALSE;

	pane->drag.button = 0;
	pane->drag.ctrl_pts = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) cb_ctrl_pts_free);

	pane->im_context = gtk_im_multicontext_new ();
	pane->preedit_length = 0;
	pane->preedit_attrs = NULL;
	pane->reseting_im = FALSE;

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);

	g_signal_connect (G_OBJECT (pane->im_context), "commit",
			  G_CALLBACK (cb_gnm_pane_commit), pane);
	g_signal_connect (G_OBJECT (pane->im_context), "preedit_changed",
			  G_CALLBACK (cb_gnm_pane_preedit_changed), pane);
	g_signal_connect (G_OBJECT (pane->im_context), "retrieve_surrounding",
			  G_CALLBACK (cb_gnm_pane_retrieve_surrounding),
			  pane);
	g_signal_connect (G_OBJECT (pane->im_context), "delete_surrounding",
			  G_CALLBACK (cb_gnm_pane_delete_surrounding),
			  pane);
}

static void
gnm_pane_class_init (GnmPaneClass *klass)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class  = (GtkWidgetClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);

	gobject_class->dispose  = gnm_pane_dispose;

	widget_class->realize		   = gnm_pane_realize;
	widget_class->unrealize		   = gnm_pane_unrealize;
	widget_class->size_allocate	   = gnm_pane_size_allocate;
	widget_class->key_press_event	   = gnm_pane_key_press;
	widget_class->key_release_event	   = gnm_pane_key_release;
	widget_class->focus_in_event	   = gnm_pane_focus_in;
	widget_class->focus_out_event	   = gnm_pane_focus_out;
}

GSF_CLASS (GnmPane, gnm_pane,
	   gnm_pane_class_init, gnm_pane_init,
	   GNM_SIMPLE_CANVAS_TYPE);

static void
cb_gnm_pane_header_realized (GtkWidget *widget)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnm_pane_header_init (GnmPane *pane, SheetControlGUI *scg,
		      gboolean is_col_header)
{
	Sheet *sheet;
	FooCanvas *canvas = gnm_simple_canvas_new (scg);
	FooCanvasGroup *group = FOO_CANVAS_GROUP (canvas->root);
	FooCanvasItem *item = foo_canvas_item_new (group,
		item_bar_get_type (),
		"pane",	pane,
		"IsColHeader", is_col_header,
		NULL);

	foo_canvas_set_center_scroll_region (canvas, FALSE);
	/* give a non-constraining default in case something scrolls before we
	 * are realized */
	foo_canvas_set_scroll_region (canvas,
		0, 0, GNM_PANE_MAX_X, GNM_PANE_MAX_Y);
	if (is_col_header) {
		pane->col.canvas = canvas;
		pane->col.item = ITEM_BAR (item);
	} else {
		pane->row.canvas = canvas;
		pane->row.item = ITEM_BAR (item);
	}
	pane->size_guide.points = NULL;
	pane->size_guide.start  = NULL;
	pane->size_guide.guide  = NULL;

	if (NULL != scg &&
	    NULL != (sheet = scg_sheet (scg)) &&
	    fabs (1. - sheet->last_zoom_factor_used) > 1e-6)
		foo_canvas_set_pixels_per_unit (canvas, sheet->last_zoom_factor_used);

	g_signal_connect (G_OBJECT (canvas), "realize",
		G_CALLBACK (cb_gnm_pane_header_realized), NULL);
}

static void
cb_pane_drag_data_received (GtkWidget *widget, GdkDragContext *context,
			    gint x, gint y, GtkSelectionData *selection_data,
			    guint info, guint time, GnmPane *pane)
{
	double wx, wy;

#ifdef DEBUG_DND
	{
		gchar *target_name = gdk_atom_name (selection_data->target);
		g_print ("drag-data-received - %s\n", target_name);
		g_free (target_name);
	}
#endif

	gnm_pane_window_to_coord (pane, x, y, &wx, &wy);
	scg_drag_data_received (pane->simple.scg,
		gtk_drag_get_source_widget (context),
		wx, wy, selection_data);
}

static void
cb_pane_drag_data_get (GtkWidget *widget, GdkDragContext *context,
		       GtkSelectionData *selection_data,
		       guint info, guint time,
		       SheetControlGUI *scg)
{
#ifdef DEBUG_DND
	gchar *target_name = gdk_atom_name (selection_data->target);
	g_print ("drag-data-get - %s \n", target_name);
	g_free (target_name);
#endif
	scg_drag_data_get (scg, selection_data);
}

/* Move the rubber bands if we are the source */
static gboolean
cb_pane_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     int x, int y, guint32 time, GnmPane *pane)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);
	SheetControlGUI *scg = GNM_PANE (widget)->simple.scg;

	if ((IS_GNM_PANE (source_widget) &&
	     GNM_PANE (source_widget)->simple.scg == scg)) {
		/* same scg */
		GnmPane *pane = GNM_PANE (widget);
		GdkModifierType mask;
		double wx, wy;

		g_object_set_data (&context->parent_instance,
			"wbcg", scg_wbcg (scg));
		gnm_pane_window_to_coord (pane, x, y, &wx, &wy);

		gdk_window_get_pointer (gtk_widget_get_parent_window (source_widget),
			NULL, NULL, &mask);
		gnm_pane_objects_drag (GNM_PANE (source_widget), NULL,
			wx, wy, 8, FALSE, (mask & GDK_SHIFT_MASK) != 0);
		gdk_drag_status (context,
				 (mask & GDK_CONTROL_MASK) != 0 ? GDK_ACTION_COPY : GDK_ACTION_MOVE,
				 time);
	}
	return TRUE;
}

static void
cb_pane_drag_end (GtkWidget *widget, GdkDragContext *context,
		  GnmPane *source_pane)
{
	/* sync the ctrl-pts with the object in case the drag was canceled. */
	gnm_pane_objects_drag (source_pane, NULL,
		source_pane->drag.origin_x,
		source_pane->drag.origin_y,
		8, FALSE, FALSE);
	source_pane->drag.had_motion = FALSE;
}

/**
 * Move the rubber bands back to original position when curser leaves
 * the scg, but not when it moves to another pane. We use object data,
 * and rely on gtk sending drag_move to the new widget before sending
 * drag_leave to the old one.
 */
static void
cb_pane_drag_leave (GtkWidget *widget, GdkDragContext *context,
		    guint32 time, GnmPane *pane)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);
	GnmPane *source_pane;
	WBCGtk *wbcg;

	if (!source_widget || !IS_GNM_PANE (source_widget)) return;

	source_pane = GNM_PANE (source_widget);

	wbcg = scg_wbcg (source_pane->simple.scg);
	if (wbcg == g_object_get_data (&context->parent_instance, "wbcg"))
		return;

	gnm_pane_objects_drag (source_pane, NULL,
		source_pane->drag.origin_x,
		source_pane->drag.origin_y,
		8, FALSE, FALSE);
	source_pane->drag.had_motion = FALSE;
}

static void
gnm_pane_drag_dest_init (GnmPane *pane, SheetControlGUI *scg)
{
	GtkWidget *widget = GTK_WIDGET (pane);

	gtk_drag_dest_set (widget, GTK_DEST_DEFAULT_ALL,
			   drag_types_in, G_N_ELEMENTS (drag_types_in),
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_add_uri_targets (widget);
	gtk_drag_dest_add_image_targets (widget);
	gtk_drag_dest_add_text_targets (widget);

	g_object_connect (G_OBJECT (widget),
		"signal::drag-data-received",	G_CALLBACK (cb_pane_drag_data_received), pane,
		"signal::drag-data-get",	G_CALLBACK (cb_pane_drag_data_get),	scg,
		"signal::drag-motion",		G_CALLBACK (cb_pane_drag_motion),	pane,
		"signal::drag-leave",		G_CALLBACK (cb_pane_drag_leave),	pane,
		"signal::drag-end",		G_CALLBACK (cb_pane_drag_end),		pane,
		NULL);
}

GnmPane *
gnm_pane_new (SheetControlGUI *scg,
	      gboolean col_headers, gboolean row_headers, int index)
{
	FooCanvasItem	*item;
	GnmPane		*pane;
	Sheet *sheet;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	pane = g_object_new (GNM_PANE_TYPE, NULL);
	pane->index      = index;
	pane->simple.scg = scg;

	if (NULL != (sheet = scg_sheet (scg)) &&
	    fabs (1. - sheet->last_zoom_factor_used) > 1e-6)
		foo_canvas_set_pixels_per_unit (FOO_CANVAS (pane),
			sheet->last_zoom_factor_used);

	gnm_pane_drag_dest_init (pane, scg);

	item = foo_canvas_item_new (pane->grid_items,
		item_grid_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->grid = ITEM_GRID (item);

	item = foo_canvas_item_new (pane->grid_items,
		item_cursor_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->cursor.std = ITEM_CURSOR (item);
	if (col_headers)
		gnm_pane_header_init (pane, scg, TRUE);
	else
		pane->col.canvas = NULL;
	if (row_headers)
		gnm_pane_header_init (pane, scg, FALSE);
	else
		pane->row.canvas = NULL;

	/* FIXME: figure out some real size for the canvas scrolling region */
	foo_canvas_set_center_scroll_region (FOO_CANVAS (pane), FALSE);
	foo_canvas_set_scroll_region (FOO_CANVAS (pane), 0, 0,
		GNM_PANE_MAX_X, GNM_PANE_MAX_Y);

	g_signal_connect_swapped (pane, "popup-menu",
		G_CALLBACK (cb_pane_popup_menu), pane);
	g_signal_connect_swapped (G_OBJECT (pane), "realize",
		G_CALLBACK (cb_pane_init_objs), pane);

	return pane;
}

/**
 * gnm_pane_find_col:
 * @pane :
 * @x : In canvas coords
 * @col_origin : optionally return the canvas coord of the col
 *
 * Returns the column containing canvas coord @x
 **/
int
gnm_pane_find_col (GnmPane const *pane, int x, int *col_origin)
{
	Sheet const *sheet = scg_sheet (pane->simple.scg);
	int col   = pane->first.col;
	int pixel = pane->first_offset.col;

	x = gnm_pane_x_w2c (pane, x);

	if (x < pixel) {
		while (col > 0) {
			ColRowInfo const *ci = sheet_col_get_info (sheet, --col);
			if (ci->visible) {
				pixel -= ci->size_pixels;
				if (x >= pixel) {
					if (col_origin)
						*col_origin = gnm_pane_x_w2c (pane,
									      pixel);
					return col;
				}
			}
		}
		if (col_origin)
			*col_origin = gnm_pane_x_w2c (pane, 0);
		return 0;
	}

	do {
		ColRowInfo const *ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const tmp = ci->size_pixels;
			if (x <= pixel + tmp) {
				if (col_origin)
					*col_origin = gnm_pane_x_w2c (pane, pixel);
				return col;
			}
			pixel += tmp;
		}
	} while (++col < gnm_sheet_get_max_cols (sheet) - 1);

	if (col_origin)
		*col_origin = gnm_pane_x_w2c (pane, pixel);
	return gnm_sheet_get_max_cols (sheet) - 1;
}

/**
 * gnm_pane_find_row:
 * @pane :
 * @y : In canvas coords
 * @row_origin : optionally return the canvas coord of the row
 *
 * Returns the column containing canvas coord @y
 **/
int
gnm_pane_find_row (GnmPane const *pane, int y, int *row_origin)
{
	Sheet const *sheet = scg_sheet (pane->simple.scg);
	int row   = pane->first.row;
	int pixel = pane->first_offset.row;

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
	} while (++row < gnm_sheet_get_max_rows (sheet)-1);
	if (row_origin)
		*row_origin = pixel;
	return gnm_sheet_get_max_rows (sheet)-1;
}

/*
 * gnm_pane_compute_visible_region : Keeps the top left col/row the same and
 *     recalculates the visible boundaries.
 *
 * @full_recompute :
 *       if TRUE recompute the pixel offsets of the top left row/col
 *       else assumes that the pixel offsets of the top left have not changed.
 */
void
gnm_pane_compute_visible_region (GnmPane *pane,
				 gboolean const full_recompute)
{
	SheetControlGUI const * const scg = pane->simple.scg;
	Sheet const *sheet = scg_sheet (scg);
	FooCanvas   *canvas = FOO_CANVAS (pane);
	int pixels, col, row, width, height;

#if 0
	g_warning ("compute_vis(W)[%d] = %d", pane->index,
		   GTK_WIDGET (pane)->allocation.width);
#endif

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		int col_offset = pane->first_offset.col = scg_colrow_distance_get (scg,
										   TRUE, 0, pane->first.col);
		if (sheet->text_is_rtl)
			col_offset = gnm_pane_x_w2c (pane,
						     pane->first_offset.col + GTK_WIDGET (pane)->allocation.width - 1);
		if (NULL != pane->col.canvas)
			foo_canvas_scroll_to (pane->col.canvas, col_offset, 0);

		pane->first_offset.row = scg_colrow_distance_get (scg,
								  FALSE, 0, pane->first.row);
		if (NULL != pane->row.canvas)
			foo_canvas_scroll_to (pane->row.canvas,
					      0, pane->first_offset.row);

		foo_canvas_scroll_to (FOO_CANVAS (pane),
				      col_offset, pane->first_offset.row);
	}

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = pane->first.col;
	width = GTK_WIDGET (canvas)->allocation.width;

	do {
		ColRowInfo const * const ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const bound = pixels + ci->size_pixels;

			if (bound == width) {
				pane->last_visible.col = col;
				pane->last_full.col = col;
				break;
			}
			if (bound > width) {
				pane->last_visible.col = col;
				if (col == pane->first.col)
					pane->last_full.col = pane->first.col;
				else
					pane->last_full.col = col - 1;
				break;
			}
			pixels = bound;
		}
		++col;
	} while (pixels < width && col < gnm_sheet_get_max_cols (sheet));

	if (col >= gnm_sheet_get_max_cols (sheet)) {
		pane->last_visible.col = gnm_sheet_get_max_cols (sheet)-1;
		pane->last_full.col = gnm_sheet_get_max_cols (sheet)-1;
	}

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = pane->first.row;
	height = GTK_WIDGET (canvas)->allocation.height;
	do {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const bound = pixels + ri->size_pixels;

			if (bound == height) {
				pane->last_visible.row = row;
				pane->last_full.row = row;
				break;
			}
			if (bound > height) {
				pane->last_visible.row = row;
				if (row == pane->first.row)
					pane->last_full.row = pane->first.row;
				else
					pane->last_full.row = row - 1;
				break;
			}
			pixels = bound;
		}
		++row;
	} while (pixels < height && row < gnm_sheet_get_max_rows (sheet));

	if (row >= gnm_sheet_get_max_rows (sheet)) {
		pane->last_visible.row = gnm_sheet_get_max_rows (sheet)-1;
		pane->last_full.row = gnm_sheet_get_max_rows (sheet)-1;
	}

	/* Update the scrollbar sizes for the primary pane */
	if (pane->index == 0)
		sc_scrollbar_config (SHEET_CONTROL (scg));

	/* Force the cursor to update its bounds relative to the new visible region */
	gnm_pane_reposition_cursors (pane);
}

void
gnm_pane_redraw_range (GnmPane *pane, GnmRange const *r)
{
	SheetControlGUI *scg;
	int x1, y1, x2, y2;
	GnmRange tmp;
	SheetControl *sc;
	Sheet *sheet;

	g_return_if_fail (IS_GNM_PANE (pane));

	scg = pane->simple.scg;
	sc = (SheetControl *) scg;
	sheet = sc->sheet;

	if ((r->end.col < pane->first.col) ||
	    (r->end.row < pane->first.row) ||
	    (r->start.col > pane->last_visible.col) ||
	    (r->start.row > pane->last_visible.row))
		return;

	/* Only draw those regions that are visible */
	tmp.start.col = MAX (pane->first.col, r->start.col);
	tmp.start.row = MAX (pane->first.row, r->start.row);
	tmp.end.col =  MIN (pane->last_visible.col, r->end.col);
	tmp.end.row =  MIN (pane->last_visible.row, r->end.row);

	/* redraw a border of 2 pixels around the region to handle thick borders
	 * NOTE the 2nd coordinates are excluded so add 1 extra (+2border +1include)
	 */
	x1 = scg_colrow_distance_get (scg, TRUE, pane->first.col, tmp.start.col) +
		pane->first_offset.col;
	y1 = scg_colrow_distance_get (scg, FALSE, pane->first.row, tmp.start.row) +
		pane->first_offset.row;
	x2 = (tmp.end.col < (gnm_sheet_get_max_cols (sheet)-1))
		? 4 + 1 + x1 + scg_colrow_distance_get (scg, TRUE,
							tmp.start.col, tmp.end.col+1)
		: INT_MAX;
	y2 = (tmp.end.row < (gnm_sheet_get_max_rows (sheet)-1))
		? 4 + 1 + y1 + scg_colrow_distance_get (scg, FALSE,
							tmp.start.row, tmp.end.row+1)
		: INT_MAX;

#if 0
	g_printerr ("%s%s:", col_name (min_col), row_name (first_row));
	g_printerr ("%s%s\n", col_name (max_col), row_name (last_row));
#endif

	if (sheet->text_is_rtl)  {
		int tmp = gnm_pane_x_w2c (pane, x1);
		x1 = gnm_pane_x_w2c (pane, x2);
		x2 = tmp;
	}
	foo_canvas_request_redraw (&pane->simple.canvas, x1-2, y1-2, x2, y2);
}

/*****************************************************************************/

void
gnm_pane_slide_stop (GnmPane *pane)
{
	if (pane->sliding == -1)
		return;

	g_source_remove (pane->sliding);
	pane->slide_handler = NULL;
	pane->slide_data = NULL;
	pane->sliding = -1;
}

static int
col_scroll_step (int dx, Sheet *sheet)
{
	/* FIXME: get from gdk.  */
	int dpi_x_this_screen = 90;
	int start_x = dpi_x_this_screen / 3;
	double double_dx = dpi_x_this_screen / 3.0;
	double step = pow (2.0, (dx - start_x) / double_dx);

	return (int) (CLAMP (step, 1.0, gnm_sheet_get_max_cols (sheet) / 15.0));
}

static int
row_scroll_step (int dy, Sheet *sheet)
{
	/* FIXME: get from gdk.  */
	int dpi_y_this_screen = 90;
	int start_y = dpi_y_this_screen / 4;
	double double_dy = dpi_y_this_screen / 8.0;
	double step = pow (2.0, (dy - start_y) / double_dy);

	return (int) (CLAMP (step, 1.0, gnm_sheet_get_max_rows (sheet) / 15.0));
}

static gint
cb_pane_sliding (GnmPane *pane)
{
	int const pane_index = pane->index;
	GnmPane *pane0 = scg_pane (pane->simple.scg, 0);
	GnmPane *pane1 = scg_pane (pane->simple.scg, 1);
	GnmPane *pane3 = scg_pane (pane->simple.scg, 3);
	gboolean slide_x = FALSE, slide_y = FALSE;
	int col = -1, row = -1;
	gboolean text_is_rtl = pane->simple.scg->sheet_control.sheet->text_is_rtl;
	GnmPaneSlideInfo info;
	Sheet *sheet = 	((SheetControl *) pane->simple.scg)->sheet;



#if 0
	g_warning ("slide: %d, %d", pane->sliding_dx, pane->sliding_dy);
#endif
	if (pane->sliding_dx > 0) {
		GnmPane *target_pane = pane;

		slide_x = TRUE;
		if (pane_index == 1 || pane_index == 2) {
			if (!pane->sliding_adjacent_h) {
				int width = GTK_WIDGET (pane)->allocation.width;
				int x = pane->first_offset.col + width + pane->sliding_dx;

				/* in case pane is narrow */
				col = gnm_pane_find_col (pane, text_is_rtl
							 ? -(x + pane->simple.canvas.scroll_x1 * pane->simple.canvas.pixels_per_unit) : x, NULL);
				if (col > pane0->last_full.col) {
					pane->sliding_adjacent_h = TRUE;
					pane->sliding_dx = 1; /* good enough */
				} else
					slide_x = FALSE;
			} else
				target_pane = pane0;
		} else
			pane->sliding_adjacent_h = FALSE;

		if (slide_x) {
			col = target_pane->last_full.col +
				col_scroll_step (pane->sliding_dx, sheet);
			if (col >= gnm_sheet_get_max_cols (sheet)-1) {
				col = gnm_sheet_get_max_cols (sheet)-1;
				slide_x = FALSE;
			}
		}
	} else if (pane->sliding_dx < 0) {
		slide_x = TRUE;
		col = pane0->first.col - col_scroll_step (-pane->sliding_dx, sheet);

		if (pane1 != NULL) {
			if (pane_index == 0 || pane_index == 3) {
				int width = GTK_WIDGET (pane1)->allocation.width;
				if (pane->sliding_dx > (-width) &&
				    col <= pane1->last_visible.col) {
					int x = pane1->first_offset.col + width + pane->sliding_dx;
					col = gnm_pane_find_col (pane, text_is_rtl
								 ? -(x + pane->simple.canvas.scroll_x1 * pane->simple.canvas.pixels_per_unit) : x, NULL);
					slide_x = FALSE;
				}
			}

			if (col <= pane1->first.col) {
				col = pane1->first.col;
				slide_x = FALSE;
			}
		} else if (col <= 0) {
			col = 0;
			slide_x = FALSE;
		}
	}

	if (pane->sliding_dy > 0) {
		GnmPane *target_pane = pane;

		slide_y = TRUE;
		if (pane_index == 3 || pane_index == 2) {
			if (!pane->sliding_adjacent_v) {
				int height = GTK_WIDGET (pane)->allocation.height;
				int y = pane->first_offset.row + height + pane->sliding_dy;

				/* in case pane is short */
				row = gnm_pane_find_row (pane, y, NULL);
				if (row > pane0->last_full.row) {
					pane->sliding_adjacent_v = TRUE;
					pane->sliding_dy = 1; /* good enough */
				} else
					slide_y = FALSE;
			} else
				target_pane = pane0;
		} else
			pane->sliding_adjacent_v = FALSE;

		if (slide_y) {
			row = target_pane->last_full.row +
				row_scroll_step (pane->sliding_dy, sheet);
			if (row >= gnm_sheet_get_max_rows (sheet)-1) {
				row = gnm_sheet_get_max_rows (sheet)-1;
				slide_y = FALSE;
			}
		}
	} else if (pane->sliding_dy < 0) {
		slide_y = TRUE;
		row = pane0->first.row - row_scroll_step (-pane->sliding_dy, sheet);

		if (pane3 != NULL) {
			if (pane_index == 0 || pane_index == 1) {
				int height = GTK_WIDGET (pane3)->allocation.height;
				if (pane->sliding_dy > (-height) &&
				    row <= pane3->last_visible.row) {
					int y = pane3->first_offset.row + height + pane->sliding_dy;
					row = gnm_pane_find_row (pane3, y, NULL);
					slide_y = FALSE;
				}
			}

			if (row <= pane3->first.row) {
				row = pane3->first.row;
				slide_y = FALSE;
			}
		} else if (row <= 0) {
			row = 0;
			slide_y = FALSE;
		}
	}

	if (col < 0 && row < 0) {
		gnm_pane_slide_stop (pane);
		return TRUE;
	}

	if (col < 0) {
		col = gnm_pane_find_col (pane, text_is_rtl
					 ? -(pane->sliding_x + pane->simple.canvas.scroll_x1 * pane->simple.canvas.pixels_per_unit)
					 : pane->sliding_x, NULL);
	} else if (row < 0)
		row = gnm_pane_find_row (pane, pane->sliding_y, NULL);

	info.col = col;
	info.row = row;
	info.user_data = pane->slide_data;
	if (pane->slide_handler == NULL ||
	    (*pane->slide_handler) (pane, &info))
		scg_make_cell_visible (pane->simple.scg, col, row, FALSE, TRUE);

	if (!slide_x && !slide_y)
		gnm_pane_slide_stop (pane);
	else if (pane->sliding == -1)
		pane->sliding = g_timeout_add (
					       300, (GSourceFunc) cb_pane_sliding, pane);

	return TRUE;
}

/**
 * gnm_pane_handle_motion :
 * @pane	 : The GnmPane managing the scroll
 * @canvas	 : The Canvas the event comes from
 * @event	 : The motion event (in world coords, with rtl inversion)
 * @slide_flags	 :
 * @slide_handler: The handler when sliding
 * @user_data	 : closure data
 *
 * Handle a motion event from a @canvas and scroll the @pane
 * depending on how far outside the bounds of @pane the @event is.
 * Usually @canvas == @pane however as long as the canvases share a basis
 * space they can be different.
 **/
gboolean
gnm_pane_handle_motion (GnmPane *pane,
			FooCanvas *canvas, GdkEventMotion *event,
			GnmPaneSlideFlags slide_flags,
			GnmPaneSlideHandler slide_handler,
			gpointer user_data)
{
	GnmPane *pane0, *pane1, *pane3;
	int pindex, left, top, x, y, width, height;
	int dx = 0, dy = 0;
	gboolean text_is_rtl;

	g_return_val_if_fail (IS_GNM_PANE (pane), FALSE);
	g_return_val_if_fail (FOO_IS_CANVAS (canvas), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (slide_handler != NULL, FALSE);

	text_is_rtl = pane->simple.scg->sheet_control.sheet->text_is_rtl;

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
	    event->x < (-64000 / pane->simple.canvas.pixels_per_unit)) {
#if SHEET_MAX_COLS > 700 /* a guestimate */
#warning WARNING We need a better solution to the rtl event kludge with SHEET_MAX_COLS so large
#endif
		foo_canvas_w2c (canvas, event->x + 65536, event->y, &x, &y);
	} else
		foo_canvas_w2c (canvas, event->x, event->y, &x, &y);
	if (text_is_rtl)
		x = -(x + pane->simple.canvas.scroll_x1 * pane->simple.canvas.pixels_per_unit);

	pindex = pane->index;
	left = pane->first_offset.col;
	top = pane->first_offset.row;
	width = GTK_WIDGET (pane)->allocation.width;
	height = GTK_WIDGET (pane)->allocation.height;

	pane0 = scg_pane (pane->simple.scg, 0);
	pane1 = scg_pane (pane->simple.scg, 1);
	pane3 = scg_pane (pane->simple.scg, 3);

	if (slide_flags & GNM_PANE_SLIDE_X) {
		if (x < left)
			dx = x - left;
		else if (x >= left + width)
			dx = x - width - left;
	}

	if (slide_flags & GNM_PANE_SLIDE_Y) {
		if (y < top)
			dy = y - top;
		else if (y >= top + height)
			dy = y - height - top;
	}

	if (pane->sliding_adjacent_h) {
		if (pindex == 0 || pindex == 3) {
			if (dx < 0) {
				x = pane1->first_offset.col;
				dx += GTK_WIDGET (pane1)->allocation.width;
				if (dx > 0)
					x += dx;
				dx = 0;
			} else
				pane->sliding_adjacent_h = FALSE;
		} else {
			if (dx > 0) {
				x = pane0->first_offset.col + dx;
				dx -= GTK_WIDGET (pane0)->allocation.width;
				if (dx < 0)
					dx = 0;
			} else if (dx == 0) {
				/* initiate a reverse scroll of panes 0,3 */
				if ((pane1->last_visible.col+1) != pane0->first.col)
					dx = x - (left + width);
			} else
				dx = 0;
		}
	}

	if (pane->sliding_adjacent_v) {
		if (pindex == 0 || pindex == 1) {
			if (dy < 0) {
				y = pane3->first_offset.row;
				dy += GTK_WIDGET (pane3)->allocation.height;
				if (dy > 0)
					y += dy;
				dy = 0;
			} else
				pane->sliding_adjacent_v = FALSE;
		} else {
			if (dy > 0) {
				y = pane0->first_offset.row + dy;
				dy -= GTK_WIDGET (pane0)->allocation.height;
				if (dy < 0)
					dy = 0;
			} else if (dy == 0) {
				/* initiate a reverse scroll of panes 0,1 */
				if ((pane3->last_visible.row+1) != pane0->first.row)
					dy = y - (top + height);
			} else
				dy = 0;
		}
	}

	/* Movement is inside the visible region */
	if (dx == 0 && dy == 0) {
		if (!(slide_flags & GNM_PANE_SLIDE_EXTERIOR_ONLY)) {
			GnmPaneSlideInfo info;
			info.row = gnm_pane_find_row (pane, y, NULL);
			info.col = gnm_pane_find_col (pane, text_is_rtl
						      ? -(x + pane->simple.canvas.scroll_x1 * pane->simple.canvas.pixels_per_unit)
						      : x, NULL);
			info.user_data = user_data;
			(*slide_handler) (pane, &info);
		}
		gnm_pane_slide_stop (pane);
		return TRUE;
	}

	pane->sliding_x  = x;
	pane->sliding_dx = dx;
	pane->sliding_y  = y;
	pane->sliding_dy = dy;
	pane->slide_handler = slide_handler;
	pane->slide_data = user_data;

	if (pane->sliding == -1)
		cb_pane_sliding (pane);
	return FALSE;
}

/* TODO : All the slide_* members of GnmPane really aught to be in
 * SheetControlGUI, most of these routines also belong there.  However, since
 * the primary point of access is via GnmPane and SCG is very large
 * already I'm leaving them here for now.  Move them when we return to
 * investigate how to do reverse scrolling for pseudo-adjacent panes.
 */
void
gnm_pane_slide_init (GnmPane *pane)
{
	GnmPane *pane0, *pane1, *pane3;

	g_return_if_fail (IS_GNM_PANE (pane));

	pane0 = scg_pane (pane->simple.scg, 0);
	pane1 = scg_pane (pane->simple.scg, 1);
	pane3 = scg_pane (pane->simple.scg, 3);

	pane->sliding_adjacent_h = (pane1 != NULL)
		? (pane1->last_full.col == (pane0->first.col - 1))
		: FALSE;
	pane->sliding_adjacent_v = (pane3 != NULL)
		? (pane3->last_full.row == (pane0->first.row - 1))
		: FALSE;
}

/*
 * gnm_pane_window_to_coord :
 * @pane : #GnmPane
 * @x :
 * @y :
 * @wx : result
 * @wy : result
 *
 * Map window coords into sheet object coords
 **/
void
gnm_pane_window_to_coord (GnmPane *pane,
			  gint    x,	gint    y,
			  double *wx, double *wy)
{
	double const scale = 1. / FOO_CANVAS (pane)->pixels_per_unit;
	y += pane->first_offset.row;

	if (pane->simple.scg->sheet_control.sheet->text_is_rtl)
		x = x - GTK_WIDGET (pane)->allocation.width - 1 - pane->first_offset.col;
	else
		x += pane->first_offset.col;
	*wx = x * scale;
	*wy = y * scale;
}

static gboolean
cb_obj_autoscroll (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	SheetControlGUI *scg = pane->simple.scg;
	GdkModifierType mask;

	/* Cheesy hack calculate distance we move the screen, this loses the
	 * mouse position */
	double dx = pane->first_offset.col;
	double dy = pane->first_offset.row;
	scg_make_cell_visible (scg, info->col, info->row, FALSE, TRUE);
	dx = pane->first_offset.col - dx;
	dy = pane->first_offset.row - dy;

#if 0
	g_warning ("dx = %g, dy = %g", dx, dy);
#endif

	pane->drag.had_motion = TRUE;
	gdk_window_get_pointer (gtk_widget_get_parent_window (GTK_WIDGET (pane)),
				NULL, NULL, &mask);
	scg_objects_drag (pane->simple.scg, pane,
			  NULL, &dx, &dy, 8, FALSE, (mask & GDK_SHIFT_MASK) != 0, TRUE);

	pane->drag.last_x += dx;
	pane->drag.last_y += dy;
	return FALSE;
}

void
gnm_pane_object_autoscroll (GnmPane *pane, GdkDragContext *context,
			    gint x, gint y, guint time)
{
	int const pane_index = pane->index;
	SheetControlGUI *scg = pane->simple.scg;
	GnmPane *pane0 = scg_pane (scg, 0);
	GnmPane *pane1 = scg_pane (scg, 1);
	GnmPane *pane3 = scg_pane (scg, 3);
	GtkWidget *w = GTK_WIDGET (pane);
	gint dx, dy;

	if (y < w->allocation.y) {
		if (pane_index < 2 && pane3 != NULL)
			w = GTK_WIDGET (pane3);
		dy = y - w->allocation.y;
		g_return_if_fail (dy <= 0);
	} else if (y >= (w->allocation.y + w->allocation.height)) {
		if (pane_index >= 2)
			w = GTK_WIDGET (pane0);
		dy = y - (w->allocation.y + w->allocation.height);
		g_return_if_fail (dy >= 0);
	} else
		dy = 0;
	if (x < w->allocation.x) {
		if ((pane_index == 0 || pane_index == 3) && pane1 != NULL)
			w = GTK_WIDGET (pane1);
		dx = x - w->allocation.x;
		g_return_if_fail (dx <= 0);
	} else if (x >= (w->allocation.x + w->allocation.width)) {
		if (pane_index >= 2)
			w = GTK_WIDGET (pane0);
		dx = x - (w->allocation.x + w->allocation.width);
		g_return_if_fail (dx >= 0);
	} else
		dx = 0;

	g_object_set_data (&context->parent_instance,
			   "wbcg", scg_wbcg (scg));
	pane->sliding_dx    = dx;
	pane->sliding_dy    = dy;
	pane->slide_handler = &cb_obj_autoscroll;
	pane->slide_data    = NULL;
	pane->sliding_x     = x;
	pane->sliding_y     = y;
	if (pane->sliding == -1)
		cb_pane_sliding (pane);
}

FooCanvasGroup *
gnm_pane_object_group (GnmPane *pane)
{
	return pane->object_views;
}

static void
gnm_pane_clear_obj_size_tip (GnmPane *pane)
{
	if (pane->size_tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (pane->size_tip));
		pane->size_tip = NULL;
	}
}

static void
gnm_pane_display_obj_size_tip (GnmPane *pane, SheetObject const *so)
{
	SheetControlGUI *scg = pane->simple.scg;
	double const *coords = g_hash_table_lookup (scg->selected_objects, so);
	double pts[4];
	char *msg;
	SheetObjectAnchor anchor;

	g_return_if_fail (so != NULL);

	if (pane->size_tip == NULL) {
		GtkWidget *top;
		int x, y;
		pane->size_tip = gnumeric_create_tooltip ();
		top = gtk_widget_get_toplevel (pane->size_tip);
		/* do not use gnumeric_position_tooltip because it places the tip
		 * too close to the mouse and generates LEAVE events */
		gdk_window_get_pointer (NULL, &x, &y, NULL);
		gtk_window_move (GTK_WINDOW (top), x + 10, y + 10);
		gtk_widget_show_all (top);
	}

	g_return_if_fail (pane->size_tip != NULL);

	sheet_object_anchor_assign (&anchor, sheet_object_get_anchor (so));
	scg_object_coords_to_anchor (scg, coords, &anchor);
	sheet_object_anchor_to_pts (&anchor, scg_sheet (scg), pts);
	msg = g_strdup_printf (_("%.1f x %.1f pts\n%d x %d pixels"),
		MAX (fabs (pts[2] - pts[0]), 0),
		MAX (fabs (pts[3] - pts[1]), 0),
		MAX ((int)floor (fabs (coords [2] - coords [0]) + 0.5), 0),
		MAX ((int)floor (fabs (coords [3] - coords [1]) + 0.5), 0));
	gtk_label_set_text (GTK_LABEL (pane->size_tip), msg);
	g_free (msg);
}

void
gnm_pane_bound_set (GnmPane *pane,
		    int start_col, int start_row,
		    int end_col, int end_row)
{
	GnmRange r;

	g_return_if_fail (pane != NULL);

	range_init (&r, start_col, start_row, end_col, end_row);
	foo_canvas_item_set (FOO_CANVAS_ITEM (pane->grid),
			     "bound", &r,
			     NULL);
}

/****************************************************************************/

void
gnm_pane_size_guide_start (GnmPane *pane, gboolean vert, int colrow, int width)
{
	SheetControlGUI const *scg;
	FooCanvasPoints *points;
	double zoom;
	gboolean text_is_rtl;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->size_guide.guide  == NULL);
	g_return_if_fail (pane->size_guide.start  == NULL);
	g_return_if_fail (pane->size_guide.points == NULL);

	scg = pane->simple.scg;
	text_is_rtl = scg->sheet_control.sheet->text_is_rtl;
	zoom = FOO_CANVAS (pane)->pixels_per_unit;

	points = pane->size_guide.points = foo_canvas_points_new (2);
	if (vert) {
		double x = scg_colrow_distance_get (scg, TRUE,
					0, colrow) / zoom;
		if (text_is_rtl)
			x = -x;
		points->coords [0] = x;
		points->coords [1] = scg_colrow_distance_get (scg, FALSE,
					0, pane->first.row) / zoom;
		points->coords [2] = x;
		points->coords [3] = scg_colrow_distance_get (scg, FALSE,
					0, pane->last_visible.row+1) / zoom;
	} else {
		double const y = scg_colrow_distance_get (scg, FALSE,
					0, colrow) / zoom;
		points->coords [0] = scg_colrow_distance_get (scg, TRUE,
					0, pane->first.col) / zoom;
		points->coords [1] = y;
		points->coords [2] = scg_colrow_distance_get (scg, TRUE,
					0, pane->last_visible.col+1) / zoom;
		points->coords [3] = y;

		if (text_is_rtl) {
			points->coords [0] *= -1.;
			points->coords [2] *= -1.;
		}
	}

	/* Guideline positioning is done in gnm_pane_size_guide_motion */
	pane->size_guide.guide = foo_canvas_item_new (pane->action_items,
		FOO_TYPE_CANVAS_LINE,
		"fill-color", "black",
		"width-pixels", width,
		NULL);

	/* cheat for now and differentiate between col/row resize and frozen panes
	 * using the width.  Frozen pane guides do not require a start line */
	if (width == 1)
		pane->size_guide.start = foo_canvas_item_new (pane->action_items,
			FOO_TYPE_CANVAS_LINE,
			"points", points,
			"fill-color", "black",
			"width-pixels", 1,
			NULL);
	else {
		static char const dat [] = { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (
			GTK_WIDGET (pane)->window, dat, 8, 8);
		foo_canvas_item_set (pane->size_guide.guide, "fill-stipple", stipple, NULL);
		g_object_unref (stipple);
	}
}

void
gnm_pane_size_guide_stop (GnmPane *pane)
{
	g_return_if_fail (pane != NULL);

	if (pane->size_guide.points != NULL) {
		foo_canvas_points_free (pane->size_guide.points);
		pane->size_guide.points = NULL;
	}
	if (pane->size_guide.start != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->size_guide.start));
		pane->size_guide.start = NULL;
	}
	if (pane->size_guide.guide != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->size_guide.guide));
		pane->size_guide.guide = NULL;
	}
}

/**
 * gnm_pane_size_guide_motion
 * @pane : #GnmPane
 * @vert : TRUE for a vertical guide, FALSE for horizontal
 * @guide_pos : in unscaled sheet pixel coords
 *
 * Moves the guide line to @guide_pos.
 * NOTE : gnm_pane_size_guide_start must be called before any calls to
 *	gnm_pane_size_guide_motion
 **/
void
gnm_pane_size_guide_motion (GnmPane *pane, gboolean vert, int guide_pos)
{
	FooCanvasItem *resize_guide = FOO_CANVAS_ITEM (pane->size_guide.guide);
	FooCanvasPoints *points     = pane->size_guide.points;
	double const	 scale	    = 1. / resize_guide->canvas->pixels_per_unit;

	if (vert) {
		gboolean text_is_rtl = pane->simple.scg->sheet_control.sheet->text_is_rtl;
		points->coords [0] = points->coords [2] = scale *
			(text_is_rtl ? -guide_pos : guide_pos);
	} else
		points->coords [1] = points->coords [3] = scale * guide_pos;

	foo_canvas_item_set (resize_guide, "points",  points, NULL);
}

/****************************************************************************/

static void
cb_update_ctrl_pts (SheetObject *so, FooCanvasItem **ctrl_pts, GnmPane *pane)
{
	gnm_pane_object_update_bbox (pane, so);
}

/* Called when the zoom changes */
void
gnm_pane_reposition_cursors (GnmPane *pane)
{
	GSList *l;

	item_cursor_reposition (pane->cursor.std);
	if (NULL != pane->cursor.rangesel)
		item_cursor_reposition (pane->cursor.rangesel);
	if (NULL != pane->cursor.special)
		item_cursor_reposition (pane->cursor.special);
	if (NULL != pane->cursor.expr_range)
		item_cursor_reposition (ITEM_CURSOR (pane->cursor.expr_range));
	for (l = pane->cursor.animated; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));

	/* ctrl pts do not scale with the zoom, compensate */
	if (pane->drag.ctrl_pts != NULL)
		g_hash_table_foreach (pane->drag.ctrl_pts,
			(GHFunc) cb_update_ctrl_pts, pane);
}

gboolean
gnm_pane_cursor_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.std, r);
}

/****************************************************************************/

gboolean
gnm_pane_rangesel_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.rangesel, r);
}
void
gnm_pane_rangesel_start (GnmPane *pane, GnmRange const *r)
{
	FooCanvasItem *item;
	SheetControlGUI *scg = pane->simple.scg;
	GnmExprEntry *gee = wbcg_get_entry_logical (pane->simple.scg->wbcg);

	g_return_if_fail (pane->cursor.rangesel == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited */
	if (scg_sheet (scg) != wb_control_cur_sheet (scg_wbc (scg)))
		item_cursor_set_visibility (pane->cursor.std, FALSE);
	if (NULL != gee)
		gnm_expr_entry_disable_highlight (gee);
	item = foo_canvas_item_new (pane->grid_items,
		item_cursor_get_type (),
		"SheetControlGUI", scg,
		"style",	ITEM_CURSOR_ANTED,
		NULL);
	pane->cursor.rangesel = ITEM_CURSOR (item);
	item_cursor_bound_set (pane->cursor.rangesel, r);
}

void
gnm_pane_rangesel_stop (GnmPane *pane)
{
	GnmExprEntry *gee = wbcg_get_entry_logical (pane->simple.scg->wbcg);
	if (NULL != gee)
		gnm_expr_entry_enable_highlight (gee);

	g_return_if_fail (pane->cursor.rangesel != NULL);
	gtk_object_destroy (GTK_OBJECT (pane->cursor.rangesel));
	pane->cursor.rangesel = NULL;

	/* Make the primary cursor visible again */
	item_cursor_set_visibility (pane->cursor.std, TRUE);

	gnm_pane_slide_stop (pane);
}

/****************************************************************************/

gboolean
gnm_pane_special_cursor_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.special, r);
}

void
gnm_pane_special_cursor_start (GnmPane *pane, int style, int button)
{
	FooCanvasItem *item;
	FooCanvas *canvas = FOO_CANVAS (pane);

	g_return_if_fail (pane->cursor.special == NULL);
	item = foo_canvas_item_new (
		FOO_CANVAS_GROUP (canvas->root),
		item_cursor_get_type (),
		"SheetControlGUI", pane->simple.scg,
		"style",	   style,
		"button",	   button,
		NULL);
	pane->cursor.special = ITEM_CURSOR (item);
}

void
gnm_pane_special_cursor_stop (GnmPane *pane)
{
	g_return_if_fail (pane->cursor.special != NULL);

	gtk_object_destroy (GTK_OBJECT (pane->cursor.special));
	pane->cursor.special = NULL;
}

void
gnm_pane_mouse_cursor_set (GnmPane *pane, GdkCursor *c)
{
	gdk_cursor_ref (c);
	if (pane->mouse_cursor)
		gdk_cursor_unref (pane->mouse_cursor);
	pane->mouse_cursor = c;
}

/****************************************************************************/

void
gnm_pane_expr_cursor_bound_set (GnmPane *pane, GnmRange const *r)
{
	if (NULL == pane->cursor.expr_range)
		pane->cursor.expr_range = (ItemCursor *)foo_canvas_item_new (
			FOO_CANVAS_GROUP (FOO_CANVAS (pane)->root),
			item_cursor_get_type (),
			"SheetControlGUI",	pane->simple.scg,
			"style",		ITEM_CURSOR_BLOCK,
			"color",		"red",
			NULL);

	item_cursor_bound_set (pane->cursor.expr_range, r);
}

void
gnm_pane_expr_cursor_stop (GnmPane *pane)
{
	if (NULL != pane->cursor.expr_range) {
		gtk_object_destroy (GTK_OBJECT (pane->cursor.expr_range));
		pane->cursor.expr_range = NULL;
	}
}

/****************************************************************************/

void
gnm_pane_edit_start (GnmPane *pane)
{
	FooCanvas *canvas = FOO_CANVAS (pane);

	g_return_if_fail (pane->editor == NULL);

	/* edit item handles visibility checks */
	pane->editor = (ItemEdit *)foo_canvas_item_new (
		FOO_CANVAS_GROUP (canvas->root),
		item_edit_get_type (),
		"SheetControlGUI",	pane->simple.scg,
		NULL);
}

void
gnm_pane_edit_stop (GnmPane *pane)
{
	if (pane->editor != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->editor));
		pane->editor = NULL;
	}
}

void
gnm_pane_objects_drag (GnmPane *pane, SheetObject *so,
		       gdouble new_x, gdouble new_y, int drag_type,
		       gboolean symmetric,gboolean snap_to_grid)
{
	double dx, dy;
	dx = new_x - pane->drag.last_x;
	dy = new_y - pane->drag.last_y;
	pane->drag.had_motion = TRUE;

	scg_objects_drag (pane->simple.scg, pane,
		so, &dx, &dy, drag_type, symmetric, snap_to_grid, TRUE);

	pane->drag.last_x += dx;
	pane->drag.last_y += dy;
}

#define CTRL_PT_SIZE		4
#define CTRL_PT_OUTLINE		0
/* space for 2 halves and a full */
#define CTRL_PT_TOTAL_SIZE	(CTRL_PT_SIZE*4 + CTRL_PT_OUTLINE*2)

/* new_x and new_y are in world coords */
static void
gnm_pane_object_move (GnmPane *pane, GObject *ctrl_pt,
		      gdouble new_x, gdouble new_y,
		      gboolean symmetric,
		      gboolean snap_to_grid)
{
	int const idx = GPOINTER_TO_INT (g_object_get_data (ctrl_pt, "index"));
	SheetObject *so  = g_object_get_data (G_OBJECT (ctrl_pt), "so");

	gnm_pane_objects_drag (pane, so, new_x, new_y, idx,
			       symmetric, snap_to_grid);
	if (idx != 8)
		gnm_pane_display_obj_size_tip (pane, so);
}

static gboolean
cb_slide_handler (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	int x, y;
	SheetControlGUI const *scg = pane->simple.scg;
	double const scale = 1. / FOO_CANVAS (pane)->pixels_per_unit;

	x = scg_colrow_distance_get (scg, TRUE, pane->first.col, info->col);
	x += pane->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, pane->first.row, info->row);
	y += pane->first_offset.row;

	if (scg->sheet_control.sheet->text_is_rtl)
		x *= -1;

	gnm_pane_object_move (pane, info->user_data,
		x * scale, y * scale, FALSE, FALSE);

	return TRUE;
}

static void
cb_so_menu_activate (GObject *menu, FooCanvasItem *view)
{
	SheetObjectAction const *a = g_object_get_data (menu, "action");
	if (a->func)
		(a->func) (sheet_object_view_get_so (SHEET_OBJECT_VIEW (view)),
			   SHEET_CONTROL (GNM_SIMPLE_CANVAS (view->canvas)->scg));


}

static GtkWidget *
build_so_menu (GnmPane *pane, SheetObjectView *view,
	       GPtrArray const *actions, unsigned *i)
{
	SheetObjectAction const *a;
	GtkWidget *item, *menu = gtk_menu_new ();

	while (*i < actions->len) {
		a = g_ptr_array_index (actions, *i);
		(*i)++;
		if (a->submenu < 0)
			break;
		if (a->icon != NULL) {
			if (a->label != NULL) {
				item = gtk_image_menu_item_new_with_mnemonic (_(a->label));
				gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
					gtk_image_new_from_stock (a->icon, GTK_ICON_SIZE_MENU));
			} else
				item = gtk_image_menu_item_new_from_stock (a->icon, NULL);
		} else if (a->label != NULL)
			item = gtk_menu_item_new_with_mnemonic (_(a->label));
		else
			item = gtk_separator_menu_item_new ();
		if (a->submenu > 0)
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
				build_so_menu (pane, view, actions, i));
		else if (a->label != NULL || a->icon != NULL) { /* not a separator or menu */
			g_object_set_data (G_OBJECT (item), "action", (gpointer)a);
			g_signal_connect_object (G_OBJECT (item), "activate",
				G_CALLBACK (cb_so_menu_activate), view, 0);
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
	}
	return menu;
}

static void
cb_ptr_array_free (GPtrArray *actions)
{
	g_ptr_array_free (actions, TRUE);
}

/* event and so can be NULL */
static void
display_object_menu (GnmPane *pane, SheetObject *so, GdkEvent *event)
{
	SheetControlGUI *scg = pane->simple.scg;
	GPtrArray *actions = g_ptr_array_new ();
	GtkWidget *menu;
	unsigned i = 0;

	if (NULL != so &&
	    NULL == g_hash_table_lookup (scg->selected_objects, so))
		scg_object_select (scg, so);

	sheet_object_populate_menu (so, actions);

	if (actions->len == 0) {
		g_ptr_array_free (actions, TRUE);
		return;
	}

	menu = build_so_menu (pane,
		sheet_object_get_view (so, (SheetObjectViewContainer *) pane),
		actions, &i);
	g_object_set_data_full (G_OBJECT (menu), "actions", actions,
		(GDestroyNotify)cb_ptr_array_free);
	gtk_widget_show_all (menu);
	gnumeric_popup_menu (GTK_MENU (menu), &event->button);
}

static void
cb_collect_selected_objs (SheetObject *so, double *coords, GSList **accum)
{
	*accum = g_slist_prepend (*accum, so);
}

static void
cb_pane_popup_menu (GnmPane *pane)
{
	SheetControlGUI *scg = pane->simple.scg;

	/* ignore new_object, it is not visible, and should not create a
	 * context menu */
	if (NULL != scg->selected_objects) {
		GSList *accum = NULL;
		g_hash_table_foreach (scg->selected_objects,
			(GHFunc) cb_collect_selected_objs, &accum);
		if (NULL != accum && NULL == accum->next)
			display_object_menu (pane, accum->data, NULL);
		g_slist_free (accum);
	} else {
		/* the popup-menu signal is a binding. the grid almost always
		 * has focus we need to cheat to find out if the user
		 * realllllly wants a col/row header menu */
		gboolean is_col = FALSE;
		gboolean is_row = FALSE;
		GdkWindow *gdk_win = gdk_display_get_window_at_pointer (
			gtk_widget_get_display (GTK_WIDGET (pane)),
			NULL, NULL);

		if (gdk_win != NULL) {
			gpointer gtk_win_void = NULL;
			GtkWindow *gtk_win = NULL;
			gdk_window_get_user_data (gdk_win, &gtk_win_void);
			gtk_win = gtk_win_void;
			if (gtk_win != NULL) {
				if (gtk_win == (GtkWindow *)pane->col.canvas)
					is_col = TRUE;
				else if (gtk_win == (GtkWindow *)pane->row.canvas)
					is_row = TRUE;
			}
		}

		scg_context_menu (scg, NULL, is_col, is_row);
	}
}

static void
control_point_set_cursor (SheetControlGUI const *scg, FooCanvasItem *ctrl_pt)
{
	double const *coords = g_hash_table_lookup (scg->selected_objects,
		g_object_get_data (G_OBJECT (ctrl_pt), "so"));
	gboolean invert_h = coords [0] > coords [2];
	gboolean invert_v = coords [1] > coords [3];
	GdkCursorType cursor;

	switch (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (ctrl_pt), "index"))) {
	case 1: invert_v = !invert_v;
	case 6: cursor = invert_v ? GDK_TOP_SIDE : GDK_BOTTOM_SIDE;
		break;

	case 3: invert_h = !invert_h;
	case 4: cursor = invert_h ? GDK_LEFT_SIDE  : GDK_RIGHT_SIDE;
		break;

	case 2: invert_h = !invert_h;
	case 0: cursor = invert_v
			? (invert_h ? GDK_BOTTOM_RIGHT_CORNER : GDK_BOTTOM_LEFT_CORNER)
			: (invert_h ? GDK_TOP_RIGHT_CORNER : GDK_TOP_LEFT_CORNER);
		break;

	case 7: invert_h = !invert_h;
	case 5: cursor = invert_v
			? (invert_h ? GDK_TOP_RIGHT_CORNER : GDK_TOP_LEFT_CORNER)
			: (invert_h ? GDK_BOTTOM_RIGHT_CORNER : GDK_BOTTOM_LEFT_CORNER);
		break;

	case 8:
	default :
		cursor = GDK_FLEUR;
	}
	gnm_widget_set_cursor_type (GTK_WIDGET (ctrl_pt->canvas), cursor);
}

static void
target_list_add_list (GtkTargetList *targets, GtkTargetList *added_targets)
{
	GList *ptr;
	GtkTargetPair *tp;

	g_return_if_fail (targets != NULL);

	if (added_targets == NULL)
		return;

	for (ptr = added_targets->list; ptr !=  NULL; ptr = ptr->next) {
		tp = (GtkTargetPair *)ptr->data;
		gtk_target_list_add (targets, tp->target, tp->flags, tp->info);
	}
}

/**
 * Drag one or more sheet objects using GTK drag and drop, to the same
 * sheet, another workbook, another gnumeric or a different application.
 */
static void
gnm_pane_drag_begin (GnmPane *pane, SheetObject *so, GdkEvent *event)
{
	GdkDragContext *context;
	GtkTargetList *targets, *im_targets;
	FooCanvas *canvas    = FOO_CANVAS (pane);
	SheetControlGUI *scg = pane->simple.scg;
	GSList *objects;
	SheetObject *imageable = NULL, *exportable = NULL;
	GSList *ptr;
	SheetObject *candidate;

	targets = gtk_target_list_new (drag_types_out,
				  G_N_ELEMENTS (drag_types_out));
	objects = go_hash_keys (scg->selected_objects);
	for (ptr = objects; ptr != NULL; ptr = ptr->next) {
		candidate = SHEET_OBJECT (ptr->data);

		if (exportable == NULL &&
		    IS_SHEET_OBJECT_EXPORTABLE (candidate))
			exportable = candidate;
		if (imageable == NULL &&
		    IS_SHEET_OBJECT_IMAGEABLE (candidate))
			imageable = candidate;
	}

	if (exportable) {
		im_targets = sheet_object_exportable_get_target_list (exportable);
		if (im_targets != NULL) {
			target_list_add_list (targets, im_targets);
			gtk_target_list_unref (im_targets);
		}
	}
	if (imageable) {
		im_targets = sheet_object_get_target_list (imageable);
		if (im_targets != NULL) {
			target_list_add_list (targets, im_targets);
			gtk_target_list_unref (im_targets);
		}
	}
#ifdef DEBUG_DND
	{
		GList *l;
		g_print ("%d offered formats:\n", g_list_length (targets->list));
		for (l = targets->list; l; l = l->next) {
			GtkTargetPair *pair = (GtkTargetPair *)l->data;
			char *target_name = gdk_atom_name (pair->target);
			g_print ("%s\n", target_name);
			g_free (target_name);
		}
	}
#endif

	context = gtk_drag_begin (GTK_WIDGET (canvas), targets,
				  GDK_ACTION_COPY | GDK_ACTION_MOVE,
				  pane->drag.button, event);
	gtk_target_list_unref (targets);
	g_slist_free (objects);
}

void
gnm_pane_object_start_resize (GnmPane *pane, GdkEventButton *event,
			      SheetObject *so, int drag_type, gboolean is_creation)
{
	FooCanvasItem **ctrl_pts;

	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (0 <= drag_type);
	g_return_if_fail (drag_type < 9);

	ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);

	g_return_if_fail (NULL != ctrl_pts);

	gnm_simple_canvas_grab (ctrl_pts [drag_type],
		GDK_POINTER_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK,
		NULL, event->time);
	pane->drag.created_objects = is_creation;
	pane->drag.button = event->button;
	pane->drag.last_x = pane->drag.origin_x = event->x;
	pane->drag.last_y = pane->drag.origin_y = event->y;
	pane->drag.had_motion = FALSE;
	gnm_pane_slide_init (pane);
	gnm_widget_set_cursor_type (GTK_WIDGET (pane), GDK_HAND2);
}

static int
cb_control_point_event (FooCanvasItem *ctrl_pt, GdkEvent *event, GnmPane *pane)
{
	SheetControlGUI *scg = pane->simple.scg;
	SheetObject *so;
	int idx;

	if (wbc_gtk_get_guru (scg_wbcg (scg)) != NULL)
		return FALSE;

	so  = g_object_get_data (G_OBJECT (ctrl_pt), "so");
	idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (ctrl_pt), "index"));
	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		control_point_set_cursor (scg, ctrl_pt);

		if (idx != 8) {
			foo_canvas_item_set (ctrl_pt,
				"fill-color",    "green",
				NULL);
			gnm_pane_display_obj_size_tip (pane, so);
		}
		break;

	case GDK_LEAVE_NOTIFY:
		scg_set_display_cursor (scg);
		if (idx != 8) {
			foo_canvas_item_set (ctrl_pt,
				"fill-color",    "white",
				NULL);
			gnm_pane_clear_obj_size_tip (pane);
		}
		break;

	case GDK_2BUTTON_PRESS:
		if (pane->drag.button == 1)
			sheet_object_get_editor (so, SHEET_CONTROL (scg));
		else
			break;

	case GDK_BUTTON_RELEASE:
		if (pane->drag.button != (int)event->button.button)
			break;
		pane->drag.button = 0;
		gnm_simple_canvas_ungrab (ctrl_pt, event->button.time);
		gnm_pane_slide_stop (pane);
		control_point_set_cursor (scg, ctrl_pt);
		if (idx == 8)
			; /* ignore fake event generated by the dnd code */
		else if (pane->drag.had_motion)
			scg_objects_drag_commit	(scg, idx,
				pane->drag.created_objects);
		else if (pane->drag.created_objects && idx == 7) {
			double w, h;
			sheet_object_default_size (so, &w, &h);
			scg_objects_drag (scg, NULL, NULL, &w, &h, 7, FALSE, FALSE, FALSE);
			scg_objects_drag_commit	(scg, 7, TRUE);
		}
		gnm_pane_clear_obj_size_tip (pane);
		break;

	case GDK_BUTTON_PRESS:
		if (0 != pane->drag.button)
			break;
		switch (event->button.button) {
		case 1:
		case 2: gnm_pane_object_start_resize (pane, &event->button, so,  idx, FALSE);
			break;
		case 3: display_object_menu (pane, so, event);
			break;
		default: /* Ignore mouse wheel events */
			return FALSE;
		}
		break;

	case GDK_MOTION_NOTIFY :
		if (0 == pane->drag.button)
			break;

		/* TODO : motion is still too granular along the internal axis
		 * when the other axis is external.
		 * eg  drag from middle of sheet down.  The x axis is still internal
		 * onlt the y is external, however, since we are autoscrolling
		 * we are limited to moving with col/row coords, not x,y.
		 * Possible solution would be to change the EXTERIOR_ONLY flag
		 * to something like USE_PIXELS_INSTEAD_OF_COLROW and change
		 * the semantics of the col,row args in the callback.  However,
		 * that is more work than I want to do right now.
		 */
		if (idx == 8)
			gnm_pane_drag_begin (pane, so, event);
		else if (gnm_pane_handle_motion (GNM_PANE (ctrl_pt->canvas),
						   ctrl_pt->canvas, &event->motion,
						   GNM_PANE_SLIDE_X | GNM_PANE_SLIDE_Y |
						   GNM_PANE_SLIDE_EXTERIOR_ONLY,
						   cb_slide_handler, ctrl_pt))
			gnm_pane_object_move (pane, G_OBJECT (ctrl_pt),
					      event->motion.x, event->motion.y,
					      (event->button.state & GDK_CONTROL_MASK) != 0,
					      (event->button.state & GDK_SHIFT_MASK) != 0);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/**
 * new_control_point
 * @pane: #GnmPane
 * @idx:    control point index to be created
 * @x:      x coordinate of control point
 * @y:      y coordinate of control point
 *
 * This is used to create a number of control points in a sheet
 * object, the meaning of them is used in other parts of the code
 * to belong to the following locations:
 *
 *     0 -------- 1 -------- 2
 *     |                     |
 *     3                     4
 *     |                     |
 *     5 -------- 6 -------- 7
 *
 *     8 == a clear overlay that extends slightly beyond the region
 *     9 == an optional stippled rectangle for moving/resizing expensive
 *         objects
 **/
static FooCanvasItem *
new_control_point (GnmPane *pane, SheetObject *so, int idx, double x, double y)
{
	FooCanvasItem *item = foo_canvas_item_new (
		pane->action_items,
		FOO_TYPE_CANVAS_ELLIPSE,
		"outline-color", "black",
		"fill-color",    "white",
		"width-pixels",  CTRL_PT_OUTLINE,
		NULL);

	g_signal_connect (G_OBJECT (item),
		"event",
		G_CALLBACK (cb_control_point_event), pane);

	g_object_set_data (G_OBJECT (item), "index",  GINT_TO_POINTER (idx));
	g_object_set_data (G_OBJECT (item), "so",  so);

	return item;
}

/**
 * set_item_x_y:
 * Changes the x and y position of the idx-th control point,
 * creating the control point if necessary.
 **/
static void
set_item_x_y (GnmPane *pane, SheetObject *so, FooCanvasItem **ctrl_pts,
	      int idx, double x, double y, gboolean visible)
{
	double const scale = 1. / FOO_CANVAS (pane)->pixels_per_unit;
	if (ctrl_pts [idx] == NULL)
		ctrl_pts [idx] = new_control_point (pane, so, idx, x, y);
	foo_canvas_item_set (ctrl_pts [idx],
	       "x1", x - CTRL_PT_SIZE * scale,
	       "y1", y - CTRL_PT_SIZE * scale,
	       "x2", x + CTRL_PT_SIZE * scale,
	       "y2", y + CTRL_PT_SIZE * scale,
	       NULL);
	if (visible)
		foo_canvas_item_show (ctrl_pts [idx]);
	else
		foo_canvas_item_hide (ctrl_pts [idx]);
}

#define normalize_high_low(d1,d2) if (d1<d2) { double tmp=d1; d1=d2; d2=tmp;}

static void
set_acetate_coords (GnmPane *pane, SheetObject *so, FooCanvasItem **ctrl_pts,
		    double l, double t, double r, double b)
{
	if (!sheet_object_rubber_band_directly (so)) {
		if (NULL == ctrl_pts [9]) {
			static char const dashed [] = { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 };
			GdkBitmap *stipple = gdk_bitmap_create_from_data (
				GTK_WIDGET (pane)->window, dashed, 8, 8);
			ctrl_pts [9] = foo_canvas_item_new (pane->action_items,
				FOO_TYPE_CANVAS_RECT,
				"fill-color",		NULL,
				"width-units",		1.,
				"outline-color",	"black",
				"outline-stipple",	stipple,
				NULL);
			g_object_unref (stipple);
			foo_canvas_item_lower_to_bottom (ctrl_pts [9]);
		}
		normalize_high_low (r, l);
		normalize_high_low (b, t);
		foo_canvas_item_set (ctrl_pts [9],
		       "x1", l, "y1", t, "x2", r, "y2", b,
		       NULL);
	} else {
		double coords[4];
		SheetObjectView *sov = sheet_object_get_view (so, (SheetObjectViewContainer *)pane);
		if (NULL == sov)
			sov = sheet_object_new_view (so, (SheetObjectViewContainer *)pane);

		coords [0] = l; coords [2] = r; coords [1] = t; coords [3] = b;
		if (NULL != sov)
			sheet_object_view_set_bounds (sov, coords, TRUE);
		normalize_high_low (r, l);
		normalize_high_low (b, t);
	}

	l -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	r += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;
	t -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	b += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;

	if (NULL == ctrl_pts [8]) {
#undef WITH_STIPPLE_BORDER /* not so pretty */
#ifdef WITH_STIPPLE_BORDER
		static char const diagonal [] = { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (
			GTK_WIDGET (pane)->window, diagonal, 8, 8);
#endif
		FooCanvasItem *item = foo_canvas_item_new (
			pane->action_items,
			item_acetate_get_type (),
			"fill-color",		NULL,
#ifdef WITH_STIPPLE_BORDER
			"width-units",		(double)(CTRL_PT_SIZE + CTRL_PT_OUTLINE),
			"outline-color",	"black",
			"outline-stipple",	stipple,
#endif
#if 0
			/* work around the screwup in canvas-item-shape that adds a large
			 * border to anything that uses miter (required for gnome-canvas
			 * not foocanvas */
			"join-style",		GDK_JOIN_ROUND,
#endif
			NULL);
#ifdef WITH_STIPPLE_BORDER
		g_object_unref (stipple);
#endif
		g_signal_connect (G_OBJECT (item), "event",
			G_CALLBACK (cb_control_point_event), pane);
		g_object_set_data (G_OBJECT (item), "index",
			GINT_TO_POINTER (8));
		g_object_set_data (G_OBJECT (item), "so", so);

		ctrl_pts [8] = item;
	}
	foo_canvas_item_set (ctrl_pts [8],
	       "x1", l,
	       "y1", t,
	       "x2", r,
	       "y2", b,
	       NULL);
}

void
gnm_pane_object_unselect (GnmPane *pane, SheetObject *so)
{
	gnm_pane_clear_obj_size_tip (pane);
	g_hash_table_remove (pane->drag.ctrl_pts, so);
}

/**
 * gnm_pane_object_update_bbox :
 * @pane : #GnmPane
 * @so : #SheetObject
 *
 * Updates the position and potentially creates control points
 * for manipulating the size/position of @so.
 **/
void
gnm_pane_object_update_bbox (GnmPane *pane, SheetObject *so)
{
	FooCanvasItem **ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);
	double const *pts = g_hash_table_lookup (
		pane->simple.scg->selected_objects, so);

	if (ctrl_pts == NULL) {
		ctrl_pts = g_new0 (FooCanvasItem *, 10);
		g_hash_table_insert (pane->drag.ctrl_pts, so, ctrl_pts);
	}

	g_return_if_fail (ctrl_pts != NULL);

	/* set the acetate 1st so that the other points will override it */
	set_acetate_coords (pane, so, ctrl_pts, pts[0], pts[1], pts[2], pts[3]);
	set_item_x_y (pane, so, ctrl_pts, 0, pts[0], pts[1], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 1, (pts[0] + pts[2]) / 2, pts[1],
		      fabs (pts[2]-pts[0]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 2, pts[2], pts[1], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 3, pts[0], (pts[1] + pts[3]) / 2,
		      fabs (pts[3]-pts[1]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 4, pts[2], (pts[1] + pts[3]) / 2,
		      fabs (pts[3]-pts[1]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 5, pts[0], pts[3], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 6, (pts[0] + pts[2]) / 2, pts[3],
		      fabs (pts[2]-pts[0]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 7, pts[2], pts[3], TRUE);
}

static int
cb_sheet_object_canvas_event (FooCanvasItem *view, GdkEvent *event,
			      SheetObject *so)
{
	GnmPane	*pane = GNM_PANE (view->canvas);

	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		gnm_widget_set_cursor_type (GTK_WIDGET (view->canvas),
			(so->flags & SHEET_OBJECT_CAN_PRESS) ? GDK_HAND2 : GDK_ARROW);
		return FALSE;

	case GDK_BUTTON_PRESS:
		if (event->button.button > 3)
			return FALSE;

		/* cb_sheet_object_widget_canvas_event calls even if selected */
		if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so)) {
			SheetObjectClass *soc =
				G_TYPE_INSTANCE_GET_CLASS (so, SHEET_OBJECT_TYPE, SheetObjectClass);

			if (soc->interactive && event->button.button != 3)
				return FALSE;

			if (!(event->button.state & GDK_SHIFT_MASK))
				scg_object_unselect (pane->simple.scg, NULL);
			scg_object_select (pane->simple.scg, so);
			if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so))
				return FALSE;	/* protected ? */
		}

		if (event->button.button < 3)
			gnm_pane_object_start_resize (pane, &event->button, so, 8, FALSE);
		else
			display_object_menu (pane, so, event);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cb_sheet_object_view_destroyed (FooCanvasItem *view, SheetObject *so)
{
	if (NULL != view->canvas) {
		GnmPane *pane = GNM_PANE (view->canvas);
		if (pane != NULL &&
		    g_hash_table_lookup (pane->drag.ctrl_pts, so) != NULL)
			scg_object_unselect (GNM_SIMPLE_CANVAS (view->canvas)->scg, so);
	}
}

static int
cb_sheet_object_widget_canvas_event (GtkWidget *widget, GdkEvent *event,
				     FooCanvasItem *view)
{
	if (event->type == GDK_ENTER_NOTIFY ||
	    (event->type == GDK_BUTTON_PRESS && event->button.button == 3))
		return cb_sheet_object_canvas_event (view, event,
			sheet_object_view_get_so (SHEET_OBJECT_VIEW (view)));
	return FALSE;
}

static void
cb_bounds_changed (SheetObject *so, FooCanvasItem *sov)
{
	double coords[4], *cur;
	SheetControlGUI *scg = GNM_SIMPLE_CANVAS (sov->canvas)->scg;

	scg_object_anchor_to_coords (scg, sheet_object_get_anchor (so), coords);
	if (NULL != scg->selected_objects &&
	    NULL != (cur = g_hash_table_lookup (scg->selected_objects, so))) {
		int i;
		for (i = 4; i-- > 0 ;) cur[i] = coords[i];
		gnm_pane_object_update_bbox (GNM_PANE (sov->canvas), so);
	}

	sheet_object_view_set_bounds (SHEET_OBJECT_VIEW (sov),
		coords, so->flags & SHEET_OBJECT_IS_VISIBLE);
}

/**
 * gnm_pane_object_register :
 * @so : A sheet object
 * @view   : A canvas item acting as a view for @so
 * @selectable : Add handlers for selecting and editing the object
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 **/
SheetObjectView *
gnm_pane_object_register (SheetObject *so, FooCanvasItem *view, gboolean selectable)
{
	if (selectable) {
		g_signal_connect (view, "event",
			G_CALLBACK (cb_sheet_object_canvas_event), so);
		g_signal_connect (view, "destroy",
			G_CALLBACK (cb_sheet_object_view_destroyed), so);
	}
	g_signal_connect_object (so, "bounds-changed",
		G_CALLBACK (cb_bounds_changed), view, 0);
	return SHEET_OBJECT_VIEW (view);
}

/**
 * gnm_pane_object_widget_register :
 *
 * @so : A sheet object
 * @widget : The widget for the sheet object view
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating widgets as views of sheet
 * objects.
 **/
void
gnm_pane_widget_register (SheetObject *so, GtkWidget *w, FooCanvasItem *view)
{
	g_signal_connect (G_OBJECT (w), "event",
		G_CALLBACK (cb_sheet_object_widget_canvas_event), view);

	if (GTK_IS_CONTAINER (w)) {
		GList *ptr, *children = gtk_container_get_children (GTK_CONTAINER (w));
		for (ptr = children ; ptr != NULL; ptr = ptr->next)
			gnm_pane_widget_register (so, ptr->data, view);
		g_list_free (children);
	}
}
