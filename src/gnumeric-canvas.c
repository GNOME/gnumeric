/* vim: set sw=8: */
/*
 * Gnumeric's extended canvas used to display the sheet.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "gnumeric-canvas.h"

#include "sheet-control-gui-priv.h"
#include "gui-util.h"
#include "mstyle.h"
#include "style-color.h"
#include "selection.h"
#include "parse-util.h"
#include "ranges.h"
#include "sheet.h"
#include "application.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "workbook-cmd-format.h"
#include "commands.h"
#include "formats.h"
#include "cmd-edit.h"
#include "clipboard.h"
#include <gal/widgets/e-cursors.h>
#include <gsf/gsf-impl-utils.h>
#include <gdk/gdkkeysyms.h>

static GnomeCanvasClass *gcanvas_parent_class;

static gboolean
gnm_canvas_guru_key (WorkbookControlGUI const *wbcg, GdkEventKey *event)
{
	GtkWidget *guru = wbcg_edit_has_guru (wbcg);
	GtkWidget *entry = GTK_WIDGET (gnm_expr_entry_get_entry (wbcg_get_entry_logical (wbcg)));

	if (guru == NULL)
		return FALSE;

	gtk_widget_event ((entry != NULL) ? entry : guru, (GdkEvent *) event);
	return TRUE;
}

/*
 * key press event handler for the gnumeric sheet for the sheet mode
 */
static gboolean
gnm_canvas_key_mode_sheet (GnumericCanvas *gcanvas, GdkEventKey *event)
{
	SheetControl *sc = (SheetControl *) gcanvas->simple.scg;
	Sheet *sheet = sc->sheet;
	SheetView *sv = sc->view;
	WorkbookControlGUI *wbcg = gcanvas->simple.scg->wbcg;
	gboolean const jump_to_bounds = event->state & GDK_CONTROL_MASK;
	int state = gnumeric_filter_modifiers (event->state);
	void (*movefn) (SheetControlGUI *, int n,
			gboolean jump, gboolean horiz);

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
	} else {
		if ((event->state & GDK_CONTROL_MASK)) {
			char const *fmt = NULL;
			char const *desc = NULL;

			switch (event->keyval) {
			case GDK_asciitilde :
				fmt = cell_formats [FMT_NUMBER][0];
				desc = _("Format as Number");
				break;
			case GDK_dollar :
				fmt = cell_formats [FMT_CURRENCY][0];
				desc = _("Format as Currency");
				break;
			case GDK_percent :
				fmt = cell_formats [FMT_PERCENT][0];
				desc = _("Format as Percentage");
				break;
			case GDK_asciicircum :
				fmt = cell_formats [FMT_SCIENCE][0];
				desc = _("Format as Scientific");
				break;
			case GDK_numbersign :
				fmt = cell_formats [FMT_DATE][0];
				desc = _("Format as Date");
				break;
			case GDK_at :
				fmt = cell_formats [FMT_TIME][0];
				desc = _("Format as Time");
				break;
			case GDK_exclam :
				fmt = cell_formats [FMT_ACCOUNT][0];
				desc = _("Format as alternative Number"); /* FIXME: Better descriptor */
				break;

			case GDK_ampersand :
				workbook_cmd_mutate_borders (WORKBOOK_CONTROL (wbcg), sheet, TRUE);
				return TRUE;
			case GDK_underscore :
				workbook_cmd_mutate_borders (WORKBOOK_CONTROL (wbcg), sheet, TRUE);
				return TRUE;
			}

			if (fmt != NULL) {
				MStyle *mstyle = mstyle_new ();

				mstyle_set_format_text (mstyle, fmt);
				cmd_selection_format (WORKBOOK_CONTROL (wbcg), mstyle, NULL, desc);
				return TRUE;
			}
		}
		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_cursor_extend
			: scg_cursor_move;
	}

	switch (event->keyval) {
	case GDK_KP_Left:
	case GDK_Left:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_left_col (gcanvas->simple.scg, gcanvas->first.col - 1);
		else
			(*movefn) (gcanvas->simple.scg, -1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_left_col (gcanvas->simple.scg, gcanvas->first.col + 1);
		else
			(*movefn) (gcanvas->simple.scg, 1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_top_row (gcanvas->simple.scg, gcanvas->first.row - 1);
		else
			(*movefn) (gcanvas->simple.scg, -1, jump_to_bounds, FALSE);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_top_row (gcanvas->simple.scg, gcanvas->first.row + 1);
		else
			(*movefn) (gcanvas->simple.scg, 1, jump_to_bounds, FALSE);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_prev_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn)( gcanvas->simple.scg,
				   -(gcanvas->last_visible.row-gcanvas->first.row),
				   FALSE, FALSE);
		else
			(*movefn)(gcanvas->simple.scg,
				  -(gcanvas->last_visible.col-gcanvas->first.col),
				  FALSE, TRUE);
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn)(gcanvas->simple.scg,
				  gcanvas->last_visible.row-gcanvas->first.row,
				  FALSE, FALSE);
		else
			(*movefn)(gcanvas->simple.scg,
				  gcanvas->last_visible.col-gcanvas->first.col,
				  FALSE, TRUE);
		break;

	case GDK_KP_Home:
	case GDK_Home:
		if (event->state & GDK_MOD5_MASK) { /* Scroll Lock */
			scg_set_left_col (gcanvas->simple.scg, sv->edit_pos.col);
			scg_set_top_row (gcanvas->simple.scg, sv->edit_pos.row);
		} else {
			/* do the ctrl-home jump to A1 in 2 steps */
			(*movefn)(gcanvas->simple.scg, -SHEET_MAX_COLS, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK))
				(*movefn)(gcanvas->simple.scg, -SHEET_MAX_ROWS, FALSE, FALSE);
		}
		break;

	case GDK_KP_End:
	case GDK_End:
		if (event->state & GDK_MOD5_MASK) { /* Scroll Lock */
			int new_col = sv->edit_pos.col - (gcanvas->last_full.col - gcanvas->first.col);
			int new_row = sv->edit_pos.row - (gcanvas->last_full.row - gcanvas->first.row);
			scg_set_left_col (gcanvas->simple.scg, new_col);
			scg_set_top_row (gcanvas->simple.scg, new_row);
		} else {
			Range r = sheet_get_extent (sheet, FALSE);

			/* do the ctrl-end jump to the extent in 2 steps */
			(*movefn)(gcanvas->simple.scg, r.end.col - sv->edit_pos.col, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK))
				(*movefn)(gcanvas->simple.scg, r.end.row - sv->edit_pos.row, FALSE, FALSE);
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

		if (wbcg_edit_finish (wbcg, TRUE)) {
			/* Figure out the direction */
			gboolean const direction = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
			gboolean const horizontal = (event->keyval == GDK_KP_Enter ||
						     event->keyval == GDK_Return) ? FALSE : TRUE;

			sv_selection_walk_step (sv, direction, horizontal);
		}
		break;

	case GDK_Escape:
		wbcg_edit_finish (wbcg, FALSE);
		application_clipboard_unant ();
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

	if (wbcg_is_editing (wbcg))
		sheet_update_only_grid (sheet);
	else
		sheet_update (sheet);

	return TRUE;
}

static gboolean
gnm_canvas_key_mode_object (GnumericCanvas *gcanvas, GdkEventKey *event)
{
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetControl    *sc = SHEET_CONTROL (scg);
	int size = (event->state & GDK_CONTROL_MASK) ? 1 : 10;

	switch (event->keyval) {
	case GDK_Escape:
		scg_mode_edit (sc);
		application_clipboard_unant ();
		return TRUE;;

	case GDK_BackSpace: /* Ick! */
	case GDK_KP_Delete:
	case GDK_Delete:
		if (scg->current_object != NULL) {
			cmd_object_delete (sc->wbc, scg->current_object);
			return TRUE;
		}
		sc_mode_edit (sc);
		break;

	case GDK_KP_Left: case GDK_Left:
		scg_object_nudge (scg, -size, 0);
		return TRUE;
	case GDK_KP_Right: case GDK_Right:
		scg_object_nudge (scg,  size, 0);
		return TRUE;
	case GDK_KP_Up: case GDK_Up:
		scg_object_nudge (scg, 0, -size);
		return TRUE;
	case GDK_KP_Down: case GDK_Down:
		scg_object_nudge (scg, 0,  size);
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static gint
gnm_canvas_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (widget);
	SheetControlGUI *scg = gcanvas->simple.scg;
	gboolean res;

	if (scg->grab_stack > 0)
		return TRUE;

	if (wbcg_edit_has_guru (scg->wbcg) == NULL  &&
	    (scg->current_object != NULL || scg->new_object != NULL))
		res = gnm_canvas_key_mode_object (gcanvas, event);
	else
		res = gnm_canvas_key_mode_sheet (gcanvas, event);

	switch (event->keyval) {
	case GDK_Shift_L:   case GDK_Shift_R:
	case GDK_Alt_L:     case GDK_Alt_R:
	case GDK_Control_L: case GDK_Control_R:
		break;

	default : if (res)
			return TRUE;
	}

	return (*GTK_WIDGET_CLASS (gcanvas_parent_class)->key_press_event) (widget, event);
}

static gint
gnm_canvas_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (widget);
	SheetControl *sc = (SheetControl *) gcanvas->simple.scg;

	if (gcanvas->simple.scg->grab_stack > 0)
		return TRUE;

	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (gcanvas->simple.scg->current_object == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (
			sc->wbc), TRUE, NULL);

	return (*GTK_WIDGET_CLASS (gcanvas_parent_class)->key_release_event) (widget, event);
}

/* Focus in handler for the canvas */
static gint
gnm_canvas_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
#warning IM disabled
#if 0
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (widget);
	if (gcanvas->ic)
		gdk_im_begin (gcanvas->ic, gcanvas->simple.canvas.layout.bin_window);
#endif
	return (*GTK_WIDGET_CLASS (gcanvas_parent_class)->focus_in_event) (widget, event);
}

/* Focus out handler for the canvas */
static gint
gnm_canvas_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
#warning IM disabled
#if 0
	gdk_im_end ();
#endif
	return (*GTK_WIDGET_CLASS (gcanvas_parent_class)->focus_out_event) (widget, event);
}

static void
gnm_canvas_realize (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (gcanvas_parent_class)->realize)
		(*GTK_WIDGET_CLASS (gcanvas_parent_class)->realize)(widget);

#warning IM disabled
#if 0
	gint width, height;
	GdkWindow *window;
	GnumericCanvas *gcanvas;

	window = widget->window;
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);

	e_cursor_set (window, E_CURSOR_FAT_CROSS);

	gcanvas = GNUMERIC_CANVAS (widget);
	if (gdk_im_ready () && (gcanvas->ic_attr = gdk_ic_attr_new ()) != NULL) {
		GdkEventMask mask;
		GdkICAttr *attr = gcanvas->ic_attr;
		GdkICAttributesType attrmask = GDK_IC_ALL_REQ;
		GdkIMStyle style;
		GdkIMStyle supported_style = GDK_IM_PREEDIT_NONE |
			GDK_IM_PREEDIT_NOTHING |
			GDK_IM_PREEDIT_POSITION |
			GDK_IM_STATUS_NONE |
			GDK_IM_STATUS_NOTHING;

		if(widget->style && widget->style->font->type != GDK_FONT_FONTSET)
			supported_style &= ~GDK_IM_PREEDIT_POSITION;

		attr->style = style = gdk_im_decide_style (supported_style);
		attr->client_window = gcanvas->simple.canvas.layout.bin_window;

		if ((style & GDK_IM_PREEDIT_MASK) == GDK_IM_PREEDIT_POSITION) {
			if (widget->style && widget->style->font->type != GDK_FONT_FONTSET) {
				g_warning ("over-the-spot style requires fontset");
			} else {
				gdk_window_get_size (attr->client_window, &width, &height);
				height = widget->style->font->ascent +
					widget->style->font->descent;

				attrmask |= GDK_IC_PREEDIT_POSITION_REQ;
				attr->spot_location.x = 0;
				attr->spot_location.y = height;
				attr->preedit_area.x = 0;
				attr->preedit_area.y = 0;
				attr->preedit_area.width = width;
				attr->preedit_area.height = height;
				attr->preedit_fontset = widget->style->font;
			}
		}

		gcanvas->ic = gdk_ic_new (attr, attrmask);
		if (gcanvas->ic != NULL) {
			mask = gdk_window_get_events (attr->client_window);
			mask |= gdk_ic_get_events (gcanvas->ic);
			gdk_window_set_events (attr->client_window, mask);

			if (GTK_WIDGET_HAS_FOCUS (widget))
				gdk_im_begin (gcanvas->ic, attr->client_window);
		} else
			g_warning ("Can't create input context.");
	}
#endif
}

static void
gnm_canvas_unrealize (GtkWidget *widget)
{
	GnumericCanvas *gcanvas;

	gcanvas = GNUMERIC_CANVAS (widget);
	g_return_if_fail (gcanvas != NULL);

#warning IM disabled
#if 0
	if (gcanvas->ic) {
		gdk_ic_destroy (gcanvas->ic);
		gcanvas->ic = NULL;
	}
	if (gcanvas->ic_attr) {
		gdk_ic_attr_destroy (gcanvas->ic_attr);
		gcanvas->ic_attr = NULL;
	}
#endif

	(*GTK_WIDGET_CLASS (gcanvas_parent_class)->unrealize)(widget);
}

static void
gnm_canvas_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (gcanvas_parent_class)->size_allocate)(widget, allocation);

	gnm_canvas_compute_visible_region (GNUMERIC_CANVAS (widget), FALSE);
}

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericCanvasClass;

static void
gnm_canvas_class_init (GnumericCanvasClass *Class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) Class;
	widget_class = (GtkWidgetClass *) Class;
	canvas_class = (GnomeCanvasClass *) Class;

	gcanvas_parent_class = g_type_class_peek (GNM_SIMPLE_CANVAS_TYPE);

	widget_class->realize		   = gnm_canvas_realize;
	widget_class->unrealize		   = gnm_canvas_unrealize;
 	widget_class->size_allocate	   = gnm_canvas_size_allocate;
	widget_class->key_press_event	   = gnm_canvas_key_press;
	widget_class->key_release_event	   = gnm_canvas_key_release;
	widget_class->focus_in_event	   = gnm_canvas_focus_in;
	widget_class->focus_out_event	   = gnm_canvas_focus_out;
}

static void
gnm_canvas_init (GnumericCanvas *gcanvas)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gcanvas);

#warning IM disabled
#if 0
	gcanvas->ic = NULL;
	gcanvas->ic_attr = NULL;
#endif
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

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

GSF_CLASS (GnumericCanvas, gnumeric_canvas,
	   gnm_canvas_class_init, gnm_canvas_init,
	   GNM_SIMPLE_CANVAS_TYPE);

GnumericCanvas *
gnumeric_canvas_new (SheetControlGUI *scg, GnumericPane *pane)
{
	GnumericCanvas	 *gcanvas;
	GnomeCanvasGroup *root_group;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	gcanvas = g_object_new (GNUMERIC_CANVAS_TYPE, NULL);

	gcanvas->simple.scg = scg;
	gcanvas->pane = pane;

	/* FIXME: figure out some real size for the canvas scrolling region */
	gnome_canvas_set_center_scroll_region (GNOME_CANVAS (gcanvas), FALSE);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (gcanvas), 0, 0,
		GNUMERIC_CANVAS_FACTOR_X, GNUMERIC_CANVAS_FACTOR_Y);


	root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (gcanvas)->root);
	gcanvas->anted_group = root_group;
	gcanvas->object_group = root_group;
	gcanvas->sheet_object_group =  GNOME_CANVAS_GROUP (gnome_canvas_item_new (
								   root_group,
								   GNOME_TYPE_CANVAS_GROUP,
								   NULL));

	return gcanvas;
}

/**
 * gnm_canvas_find_col: return the column containing pixel x
 */
int
gnm_canvas_find_col (GnumericCanvas *gcanvas, int x, int *col_origin)
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
gnm_canvas_find_row (GnumericCanvas *gcanvas, int y, int *row_origin)
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
gnm_canvas_compute_visible_region (GnumericCanvas *gcanvas,
				   gboolean const full_recompute)
{
	SheetControlGUI const * const scg = gcanvas->simple.scg;
	Sheet const * const sheet = ((SheetControl *) scg)->sheet;
	GnomeCanvas   *canvas = GNOME_CANVAS (gcanvas);
	int pixels, col, row, width, height;

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		gcanvas->first_offset.col = scg_colrow_distance_get (scg,
			TRUE, 0, gcanvas->first.col);
		if (NULL != gcanvas->pane->col.canvas)
			gnome_canvas_scroll_to (gcanvas->pane->col.canvas,
				gcanvas->first_offset.col, 0);

		gcanvas->first_offset.row = scg_colrow_distance_get (scg,
			FALSE, 0, gcanvas->first.row);
		if (NULL != gcanvas->pane->row.canvas)
			gnome_canvas_scroll_to (gcanvas->pane->row.canvas,
				0, gcanvas->first_offset.row);

		gnome_canvas_scroll_to (GNOME_CANVAS (gcanvas),
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
gnm_canvas_redraw_range (GnumericCanvas *gcanvas, Range const *r)
{
	SheetControlGUI *scg;
	GnomeCanvas *canvas;
	int x1, y1, x2, y2;
	Range tmp;

	g_return_if_fail (IS_GNUMERIC_CANVAS (gcanvas));

	scg = gcanvas->simple.scg;
	canvas = GNOME_CANVAS (gcanvas);

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

	gnome_canvas_request_redraw (GNOME_CANVAS (gcanvas), x1-2, y1-2, x2, y2);
}

/*****************************************************************************/

void
gnm_canvas_slide_stop (GnumericCanvas *gcanvas)
{
	if (gcanvas->sliding == -1)
		return;

	gtk_timeout_remove (gcanvas->sliding);
	gcanvas->slide_handler = NULL;
	gcanvas->slide_data = NULL;
	gcanvas->sliding = -1;
}

static int
col_scroll_step (int dx)
{
	if (dx <= 30)
		return 1;
	if (dx <= 60)
		return 2;
	if (dx <= 90)
		return 4;
	if (dx <= 120)
		return 8;
	return 16;
}

static int
row_scroll_step (int dy)
{
	if (dy <= 20)
		return 1;
	if (dy <= 30)
		return 2;
	if (dy <= 40)
		return 4;
	if (dy <= 45)
		return 8;
	if (dy <= 50)
		return 16;
	if (dy <= 60)
		return 32;
	if (dy <= 100)
		return 500;
	return 5000;
}

static gint
gcanvas_sliding_callback (gpointer data)
{
	GnumericCanvas *gcanvas = data;
	int const pane_index = gcanvas->pane->index;
	GnumericCanvas *gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	GnumericCanvas *gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	GnumericCanvas *gcanvas3 = scg_pane (gcanvas->simple.scg, 3);
	gboolean slide_x = FALSE, slide_y = FALSE;
	int col = -1, row = -1;

	if (gcanvas->sliding_dx > 0) {
		GnumericCanvas *target_gcanvas = gcanvas;

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
		GnumericCanvas *target_gcanvas = gcanvas;

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
			gcanvas->sliding = gtk_timeout_add (
				300, gcanvas_sliding_callback, gcanvas);
	} else
		gnm_canvas_slide_stop (gcanvas);

	return TRUE;
}

/**
 * gnm_canvas_handle_motion :
 * @gcanvas	 : The GnumericCanvas managing the scroll
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
 */
gboolean
gnm_canvas_handle_motion (GnumericCanvas *gcanvas,
			      GnomeCanvas *canvas, GdkEventMotion *event,
			      GnumericSlideFlags slide_flags,
			      GnumericCanvasSlideHandler slide_handler,
			      gpointer user_data)
{
	GnumericCanvas *gcanvas0, *gcanvas1, *gcanvas3;
	int pane, left, top, x, y, width, height;
	int dx = 0, dy = 0;

	g_return_val_if_fail (IS_GNUMERIC_CANVAS (gcanvas), FALSE);
	g_return_val_if_fail (GNOME_IS_CANVAS (canvas), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (slide_handler != NULL, FALSE);

	gnome_canvas_w2c (canvas, event->x, event->y, &x, &y);

	pane = gcanvas->pane->index;
	left = gcanvas->first_offset.col;
	top = gcanvas->first_offset.row;
	width = GTK_WIDGET (gcanvas)->allocation.width;
	height = GTK_WIDGET (gcanvas)->allocation.height;

	gcanvas0 = scg_pane (gcanvas->simple.scg, 0);
	gcanvas1 = scg_pane (gcanvas->simple.scg, 1);
	gcanvas3 = scg_pane (gcanvas->simple.scg, 3);

	if (slide_flags & GNM_SLIDE_X) {
		if (x < left)
			dx = x - left;
		else if (x >= left + width)
			dx = x - width - left;
	}

	if (slide_flags & GNM_SLIDE_Y) {
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
		if (!(slide_flags & GNM_SLIDE_EXTERIOR_ONLY)) {
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

/* TODO : All the slide_* members of GnumericCanvas really aught to be in
 * SheetControlGUI, most of these routines also belong there.  However, since
 * the primary point of access is via GnumericCanvas and SCG is very large
 * already I'm leaving them here for now.  Move them when we return to
 * investigate how to do reverse scrolling for pseudo-adjacent panes.
 */
void
gnm_canvas_slide_init (GnumericCanvas *gcanvas)
{
	GnumericCanvas *gcanvas0, *gcanvas1, *gcanvas3;

	g_return_if_fail (IS_GNUMERIC_CANVAS (gcanvas));

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
