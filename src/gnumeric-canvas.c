/* vim: set sw=8: */
/*
 * The Gnumeric Sheet widget.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include "gnumeric-sheet.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "item-edit.h"
#include "item-grid.h"
#include "sheet-control-gui.h"
#include "gnumeric-util.h"
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
#include "commands.h"

#ifdef ENABLE_BONOBO
#  include "sheet-object-container.h"
#endif
#include <gal/widgets/e-cursors.h>

static GnomeCanvasClass *gsheet_parent_class;

static void
gnumeric_sheet_destroy (GtkObject *object)
{
	GnumericSheet *gsheet;

	/* Add shutdown code here */
	gsheet = GNUMERIC_SHEET (object);

	if (GTK_OBJECT_CLASS (gsheet_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (gsheet_parent_class)->destroy)(object);
}

/*
 * key press event handler for the gnumeric sheet for the sheet mode
 */
static gint
gnumeric_sheet_key_mode_sheet (GnumericSheet *gsheet, GdkEventKey *event)
{
	Sheet *sheet = gsheet->scg->sheet;
	WorkbookControlGUI *wbcg = gsheet->scg->wbcg;
	gboolean const jump_to_bounds = event->state & GDK_CONTROL_MASK;
	void (*movefn) (SheetControlGUI *, int n,
			gboolean jump, gboolean horiz);

	/* Magic : Some of these are accelerators,
	 * we need to catch them before entering because they appear to be printable
	 */
	if (!wbcg->editing && event->keyval == GDK_space &&
	    (event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
		return FALSE;

	if (scg_rangesel_possible (gsheet->scg)) {
		/* Ignore a few keys to avoid killing range selection cursor */
		switch (event->keyval) {
		case GDK_Shift_L:   case GDK_Shift_R:
		case GDK_Alt_L:     case GDK_Alt_R:
		case GDK_Control_L: case GDK_Control_R:
			return 1;
		}

		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_rangesel_extend
			: scg_rangesel_move;
	} else {
		movefn = (event->state & GDK_SHIFT_MASK)
			? scg_cursor_extend
			: scg_cursor_move;
	}

	switch (event->keyval) {
	case GDK_KP_Left:
	case GDK_Left:
		(*movefn) (gsheet->scg, -1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		(*movefn) (gsheet->scg, 1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		(*movefn) (gsheet->scg, -1, jump_to_bounds, FALSE);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		(*movefn) (gsheet->scg, 1, jump_to_bounds, FALSE);
		break;

	case GDK_KP_Page_Up:
	case GDK_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_prev_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn)( gsheet->scg,
				   -(gsheet->row.last_visible-gsheet->row.first),
				   FALSE, FALSE);
		else
			(*movefn)(gsheet->scg,
				  -(gsheet->col.last_visible-gsheet->col.first),
				  FALSE, TRUE);
		break;

	case GDK_KP_Page_Down:
	case GDK_Page_Down:
		if ((event->state & GDK_CONTROL_MASK) != 0)
			gtk_notebook_next_page (wbcg->notebook);
		else if ((event->state & GDK_MOD1_MASK) == 0)
			(*movefn)(gsheet->scg,
				  gsheet->row.last_visible-gsheet->row.first,
				  FALSE, FALSE);
		else
			(*movefn)(gsheet->scg,
				  gsheet->col.last_visible-gsheet->col.first,
				  FALSE, TRUE);
		break;

	case GDK_KP_Home:
	case GDK_Home:
		/* do the ctrl-home jump to A1 in 2 steps */
		(*movefn)(gsheet->scg, -SHEET_MAX_COLS, FALSE, TRUE);
		if ((event->state & GDK_CONTROL_MASK))
			(*movefn)(gsheet->scg, -SHEET_MAX_ROWS, FALSE, FALSE);
		break;

	case GDK_KP_Delete:
	case GDK_Delete:
		cmd_clear_selection (WORKBOOK_CONTROL (wbcg), sheet, CLEAR_VALUES);
		break;

	/*
	 * NOTE : Keep these in sync with the condition
	 *        for tabs.
	 */
	case GDK_KP_Enter:
	case GDK_Return:
		if (wbcg->editing &&
		    (event->state == GDK_CONTROL_MASK ||
		     event->state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK) ||
		     event->state == GDK_MOD1_MASK))
			/* Forward the keystroke to the input line */
			return gtk_widget_event (
				GTK_WIDGET (wbcg_get_entry_logical (wbcg)),
				(GdkEvent *) event);
		/* fall down */

	case GDK_Tab:
	case GDK_ISO_Left_Tab:
	case GDK_KP_Tab:
	{
		/* Be careful to restore the editing sheet if we are editing */
		if (wbcg->editing)
			sheet = wbcg->editing_sheet;
			
		if (wbcg_edit_finish (wbcg, TRUE)) {
			/* Figure out the direction */
			gboolean const direction = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
			gboolean const horizontal = (event->keyval == GDK_KP_Enter ||
						     event->keyval == GDK_Return) ? FALSE : TRUE;

			sheet_selection_walk_step (sheet, direction, horizontal);
		}
		break;
	}

	case GDK_Escape:
		wbcg_edit_finish (wbcg, FALSE);
		application_clipboard_unant ();
		break;

	case GDK_F4:
		if (wbcg->editing && gsheet->sel_cursor)
			wbcg_edit_toggle_absolute (wbcg);
		break;

	case GDK_F2:
		wbcg_edit_start (wbcg, FALSE, FALSE);
		/* fall down */

	default:
		if (!wbcg->editing) {
			if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0)
				return FALSE;

			/* If the character is not printable do not start editing */
			if (event->length == 0)
				return FALSE;

			wbcg_edit_start (wbcg, TRUE, TRUE);
		}
		scg_rangesel_stop (gsheet->scg, FALSE);

		/* Forward the keystroke to the input line */
		return gtk_widget_event (GTK_WIDGET (wbcg_get_entry_logical (wbcg)),
					 (GdkEvent *) event);
	}

	if (wbcg->editing)
		sheet_update_only_grid (sheet);
	else
		sheet_update (sheet);

	return TRUE;
}

static gint
gnumeric_sheet_key_mode_object (GnumericSheet *gsheet, GdkEventKey *event)
{
	SheetControlGUI *scg = gsheet->scg;

	switch (event->keyval) {
	case GDK_Escape:
		scg_mode_edit (scg);
		application_clipboard_unant ();
		break;

	case GDK_BackSpace: /* Ick! */
	case GDK_KP_Delete:
	case GDK_Delete:
		gtk_object_destroy (GTK_OBJECT (scg->current_object));
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static gint
gnumeric_sheet_key_press (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	SheetControlGUI *scg = gsheet->scg;

	if (scg->current_object != NULL || scg->new_object != NULL)
		return gnumeric_sheet_key_mode_object (gsheet, event);
	return gnumeric_sheet_key_mode_sheet (gsheet, event);
}

static gint
gnumeric_sheet_key_release (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	SheetControlGUI *scg = gsheet->scg;

	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (scg->current_object == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (
			WORKBOOK_CONTROL (gsheet->scg->wbcg)), TRUE, NULL);

	return (*GTK_WIDGET_CLASS (gsheet_parent_class)->key_release_event)(widget, event);
}

static gint
gnumeric_sheet_button_release (GtkWidget *widget, GdkEventButton *button)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);

	if (button->button != 4 && button->button != 5)
		return (*GTK_WIDGET_CLASS (gsheet_parent_class)->button_release_event)(widget, button);

	/* Roll Up or Left */
	/* Roll Down or Right */
	if ((button->state & GDK_MOD1_MASK)) {
		int col = gsheet->col.last_full - gsheet->col.first;
		if (button->button == 4)
			col = MAX (gsheet->col.first - col, 0);
		else if (gsheet->col.last_full < SHEET_MAX_COLS-1)
			col = gsheet->col.last_full;
		else
			return FALSE;
		gnumeric_sheet_set_left_col (gsheet, col);
	} else {
		int row = gsheet->row.last_full - gsheet->row.first;
		if (button->button == 4)
			row = MAX (gsheet->row.first - row, 0);
		else if (gsheet->row.last_full < SHEET_MAX_ROWS-1)
			row = gsheet->row.last_full;
		else
			return FALSE;
		gnumeric_sheet_set_top_row (gsheet, row);
	}
	return TRUE;
}

/* Focus in handler for the canvas */
static gint
gnumeric_sheet_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	if (gsheet->ic)
		gdk_im_begin (gsheet->ic, gsheet->canvas.layout.bin_window);
	return (*GTK_WIDGET_CLASS (gsheet_parent_class)->focus_in_event)(widget, event);
}

/* Focus out handler for the canvas */
static gint
gnumeric_sheet_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	gdk_im_end ();
	return (*GTK_WIDGET_CLASS (gsheet_parent_class)->focus_out_event)(widget, event);
}

static void
gnumeric_sheet_drag_data_get (GtkWidget *widget,
			      GdkDragContext *context,
			      GtkSelectionData *selection_data,
			      guint info,
			      guint time)
{
#if 0
	BonoboMoniker *moniker;
	Sheet *sheet = GNUMERIC_SHEET (widget)->scg->sheet;
	Workbook *wb = sheet->workbook;
	char *s;
	WorkbookControl *wbc = WORKBOOK_CONTROL (gsheet->scg->wbcg);

	if (wb->filename == NULL)
		workbook_save (wbc, wb);
	if (wb->filename == NULL)
		return;

	moniker = bonobo_moniker_new ();
	bonobo_moniker_set_server (
		moniker,
		"IDL:GNOME:Gnumeric:Workbook:1.0",
		wb->filename);

	bonobo_moniker_append_item_name (
		moniker, "Sheet1");
	s = bonobo_moniker_get_as_string (moniker);
	gtk_object_destroy (GTK_OBJECT (moniker));

	gtk_selection_data_set (selection_data, selection_data->target, 8, s, strlen (s)+1);
#endif
}

/*
 * gnumeric_sheet_filenames_dropped :
 */
static void
gnumeric_sheet_filenames_dropped (GtkWidget        *widget,
				  GdkDragContext   *context,
				  gint              x,
				  gint              y,
				  GtkSelectionData *selection_data,
				  guint             info,
				  guint             time,
				  GnumericSheet    *gsheet)
{
	GList *names, *tmp_list;
	WorkbookControl *wbc = WORKBOOK_CONTROL (gsheet->scg->wbcg);

	names = gnome_uri_list_extract_filenames ((char *)selection_data->data);

	for (tmp_list = names; tmp_list != NULL; tmp_list = tmp_list->next) {
		gchar *file_name = tmp_list->data;
		WorkbookView *new_wb = workbook_try_read (wbc, file_name);

		if (new_wb != NULL) {
			(void) file_finish_load (wbc, new_wb);
		} else {
#ifdef ENABLE_BONOBO
			/* If it wasn't a workbook, see if we have a control for it */
			SheetObject *so = sheet_object_container_new_file (
			                  gsheet->scg->sheet, file_name);
			if (so != NULL)
				scg_mode_create_object (gsheet->scg, so);
#else
			gchar *msg;

			msg = g_strdup_printf (_("File \"%s\" has unknown format."),
			                       file_name);
			gnumeric_error_read (COMMAND_CONTEXT (wbc), msg);
			g_free (msg);
#endif
		}
	}
}

static void
gnumeric_sheet_realize (GtkWidget *widget)
{
	GdkWindow *window;
	GnumericSheet *gsheet;

	if (GTK_WIDGET_CLASS (gsheet_parent_class)->realize)
		(*GTK_WIDGET_CLASS (gsheet_parent_class)->realize)(widget);

	window = widget->window;
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);

	e_cursor_set (window, E_CURSOR_FAT_CROSS);

	gsheet = GNUMERIC_SHEET (widget);
	if (gdk_im_ready () && (gsheet->ic_attr = gdk_ic_attr_new ()) != NULL) {
		GdkEventMask mask;
		GdkICAttr *attr = gsheet->ic_attr;
		GdkICAttributesType attrmask = GDK_IC_ALL_REQ;
		GdkIMStyle style;
		GdkIMStyle supported_style = GDK_IM_PREEDIT_NONE |
			GDK_IM_PREEDIT_NOTHING |
			GDK_IM_STATUS_NONE |
			GDK_IM_STATUS_NOTHING;

		attr->style = style = gdk_im_decide_style (supported_style);
		attr->client_window = gsheet->canvas.layout.bin_window;

		gsheet->ic = gdk_ic_new (attr, attrmask);
		if (gsheet->ic != NULL) {
			mask = gdk_window_get_events (attr->client_window);
			mask |= gdk_ic_get_events (gsheet->ic);
			gdk_window_set_events (attr->client_window, mask);

			if (GTK_WIDGET_HAS_FOCUS (widget))
				gdk_im_begin (gsheet->ic, attr->client_window);
		} else
			g_warning ("Can't create input context.");
	}
}

static void
gnumeric_sheet_unrealize (GtkWidget *widget)
{
	GnumericSheet *gsheet;

	gsheet = GNUMERIC_SHEET (widget);
	g_return_if_fail (gsheet != NULL);

	if (gsheet->ic) {
		gdk_ic_destroy (gsheet->ic);
		gsheet->ic = NULL;
	}
	if (gsheet->ic_attr) {
		gdk_ic_attr_destroy (gsheet->ic_attr);
		gsheet->ic_attr = NULL;
	}

	(*GTK_WIDGET_CLASS (gsheet_parent_class)->unrealize)(widget);
}

static void
gnumeric_sheet_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (gsheet_parent_class)->size_allocate)(widget, allocation);

	gsheet_compute_visible_region (GNUMERIC_SHEET (widget), FALSE);
}

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericSheetClass;

static void
gnumeric_sheet_class_init (GnumericSheetClass *Class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) Class;
	widget_class = (GtkWidgetClass *) Class;
	canvas_class = (GnomeCanvasClass *) Class;

	gsheet_parent_class = gtk_type_class (gnome_canvas_get_type ());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;

	widget_class->realize		   = gnumeric_sheet_realize;
	widget_class->unrealize		   = gnumeric_sheet_unrealize;
 	widget_class->size_allocate	   = gnumeric_sheet_size_allocate;
	widget_class->key_press_event	   = gnumeric_sheet_key_press;
	widget_class->key_release_event	   = gnumeric_sheet_key_release;
	widget_class->button_release_event = gnumeric_sheet_button_release;
	widget_class->focus_in_event	   = gnumeric_sheet_focus_in;
	widget_class->focus_out_event	   = gnumeric_sheet_focus_out;
}

static void
gnumeric_sheet_init (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);

	gsheet->ic = NULL;
	gsheet->ic_attr = NULL;

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

GtkType
gnumeric_sheet_get_type (void)
{
	static GtkType gnumeric_sheet_type = 0;

	if (!gnumeric_sheet_type) {
		GtkTypeInfo gnumeric_sheet_info = {
			"GnumericSheet",
			sizeof (GnumericSheet),
			sizeof (GnumericSheetClass),
			(GtkClassInitFunc) gnumeric_sheet_class_init,
			(GtkObjectInitFunc) gnumeric_sheet_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		gnumeric_sheet_type = gtk_type_unique (gnome_canvas_get_type (), &gnumeric_sheet_info);
	}

	return gnumeric_sheet_type;
}

GtkWidget *
gnumeric_sheet_new (SheetControlGUI *scg)
{
	static GtkTargetEntry const drag_types[] = {
		{ "text/uri-list", 0, 0 },
	};
	static gint const n_drag_types = sizeof (drag_types) / sizeof (drag_types [0]);

	GnomeCanvasItem	 *item;
	GnumericSheet	 *gsheet;
	GnomeCanvasGroup *gsheet_group;
	GtkWidget	 *widget;
	Range		  r;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	gsheet_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (gsheet)->root);
	widget = GTK_WIDGET (gsheet);

	gsheet->scg = scg;
	gsheet->row.first = gsheet->row.last_full = gsheet->row.last_visible = 0;
	gsheet->col.first = gsheet->col.last_full = gsheet->col.last_visible = 0;
	gsheet->row_offset.first = gsheet->row_offset.last_full = gsheet->row_offset.last_visible = 0;
	gsheet->col_offset.first = gsheet->col_offset.last_full = gsheet->col_offset.last_visible = 0;

	/* FIXME: figure out some real size for the canvas scrolling region */
	gnome_canvas_set_scroll_region (GNOME_CANVAS (gsheet), 0, 0,
					GNUMERIC_SHEET_FACTOR_X, GNUMERIC_SHEET_FACTOR_Y);

	/* The grid */
	item = gnome_canvas_item_new (gsheet_group,
		item_grid_get_type (),
		"ItemGrid::SheetControlGUI", scg,
		NULL);
	gsheet->item_grid = ITEM_GRID (item);

	/* The cursor */
	item = gnome_canvas_item_new (gsheet_group,
		item_cursor_get_type (),
		"ItemCursor::SheetControlGUI", scg,
		NULL);
	gsheet->item_cursor = ITEM_CURSOR (item);
	gnumeric_sheet_cursor_bound (gsheet, range_init (&r, 0, 0, 0, 0)); /* A1 */

	/* Setup a test of Drag and Drop */
	gtk_signal_connect (GTK_OBJECT (widget),
		"drag_data_get",
		GTK_SIGNAL_FUNC (gnumeric_sheet_drag_data_get), NULL);
	gtk_drag_dest_set (widget,
		GTK_DEST_DEFAULT_ALL,
		drag_types, n_drag_types,
		GDK_ACTION_COPY);
	gtk_signal_connect (GTK_OBJECT (widget),
		"drag_data_received",
		GTK_SIGNAL_FUNC (gnumeric_sheet_filenames_dropped),
		widget);

	return widget;
}

static int
gnumeric_sheet_bar_set_top_row (GnumericSheet *gsheet, int new_first_row)
{
	GnomeCanvas *rowc;
	int row_offset;
	int x;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (gsheet->item_grid != NULL, 0);
	g_return_val_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS, 0);

	rowc = gsheet->scg->row_item->canvas;
	row_offset = gsheet->row_offset.first +=
		scg_colrow_distance_get (gsheet->scg, FALSE, gsheet->row.first, new_first_row);
	gsheet->row.first = new_first_row;

	/* Scroll the row headers */
	gnome_canvas_get_scroll_offsets (rowc, &x, NULL);
	gnome_canvas_scroll_to (rowc, x, row_offset);

	return row_offset;
}

void
gnumeric_sheet_set_top_row (GnumericSheet *gsheet, int new_first_row)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS);

	if (gsheet->row.first != new_first_row) {
		int x;
		GnomeCanvas * const canvas = GNOME_CANVAS(gsheet);
		int const y_offset =
			gnumeric_sheet_bar_set_top_row (gsheet, new_first_row);

		gsheet_compute_visible_region (gsheet, FALSE);

		/* Scroll the cell canvas */
		gnome_canvas_get_scroll_offsets (canvas, &x, NULL);
		gnome_canvas_scroll_to (canvas, x, y_offset);
	}
}

static int
gnumeric_sheet_bar_set_left_col (GnumericSheet *gsheet, int new_first_col)
{
	GnomeCanvas *colc;
	int col_offset;
	int y;

	g_return_val_if_fail (gsheet != NULL, 0);
	g_return_val_if_fail (gsheet->item_grid != NULL, 0);
	g_return_val_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS, 0);

	colc = gsheet->scg->col_item->canvas;
	col_offset = gsheet->col_offset.first +=
		scg_colrow_distance_get (gsheet->scg, TRUE, gsheet->col.first, new_first_col);
	gsheet->col.first = new_first_col;

	/* Scroll the column headers */
	gnome_canvas_get_scroll_offsets (colc, NULL, &y);
	gnome_canvas_scroll_to (colc, col_offset, y);

	return col_offset;
}

void
gnumeric_sheet_set_left_col (GnumericSheet *gsheet, int new_first_col)
{
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS);

	if (gsheet->col.first != new_first_col) {
		int y;
		GnomeCanvas * const canvas = GNOME_CANVAS(gsheet);
		int const x_offset =
			gnumeric_sheet_bar_set_left_col (gsheet, new_first_col);

		gsheet_compute_visible_region (gsheet, FALSE);

		/* Scroll the cell canvas */
		gnome_canvas_get_scroll_offsets (canvas, NULL, &y);
		gnome_canvas_scroll_to (canvas, x_offset, y);
	}
}

/*
 * gnumeric_sheet_find_col: return the column containing pixel x
 */
int
gnumeric_sheet_find_col (GnumericSheet *gsheet, int x, int *col_origin)
{
	Sheet *sheet = gsheet->scg->sheet;
	int col   = gsheet->col.first;
	int pixel = gsheet->col_offset.first;

	if (x < pixel) {
		while (col > 0) {
			ColRowInfo *ci = sheet_col_get_info (sheet, --col);
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
		ColRowInfo *ci = sheet_col_get_info (sheet, col);
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
 * gnumeric_sheet_find_row: return the row where y belongs to
 */
int
gnumeric_sheet_find_row (GnumericSheet *gsheet, int y, int *row_origin)
{
	Sheet *sheet = gsheet->scg->sheet;
	int row   = gsheet->row.first;
	int pixel = gsheet->row_offset.first;

	if (y < pixel) {
		while (row > 0) {
			ColRowInfo *ri = sheet_row_get_info (sheet, --row);
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
		ColRowInfo *ri = sheet_row_get_info (sheet, row);
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

void
gnumeric_sheet_create_editor (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasItem *item;

	g_return_if_fail (gsheet->item_editor == NULL);

	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (canvas->root),
				      item_edit_get_type (),
				      "ItemEdit::SheetControlGUI",     gsheet->scg,
				      NULL);

	gsheet->item_editor = ITEM_EDIT (item);
}

void
gnumeric_sheet_stop_editing (GnumericSheet *gsheet)
{
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	if (gsheet->item_editor != NULL) {
		gtk_object_destroy (GTK_OBJECT (gsheet->item_editor));
		gsheet->item_editor = NULL;
	}
}

void
gnumeric_sheet_cursor_bound (GnumericSheet *gsheet, Range const *r)
{
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	item_cursor_set_bounds (gsheet->item_cursor, r);
}

void
gnumeric_sheet_rangesel_bound (GnumericSheet *gsheet, Range const *r)
{
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	item_cursor_set_bounds (gsheet->sel_cursor, r);
}

void
gnumeric_sheet_rangesel_start (GnumericSheet *gsheet, int col, int row)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasItem *tmp;
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	Sheet *sheet = gsheet->scg->sheet;
	WorkbookControlGUI *wbcg = gsheet->scg->wbcg;
	Range r;

	g_return_if_fail (gsheet->sel_cursor == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (sheet != wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)))
		item_cursor_set_visibility (gsheet->item_cursor, FALSE);

	tmp = gnome_canvas_item_new (group,
		item_cursor_get_type (),
		"SheetControlGUI", gsheet->scg,
		"Style", ITEM_CURSOR_ANTED, NULL);
	gsheet->sel_cursor = ITEM_CURSOR (tmp);
	item_cursor_set_bounds (gsheet->sel_cursor,
		range_init (&r, col, row, col, row));

	/* If we are selecting a range on a different sheet this may be NULL */
	if (gsheet->item_editor)
		item_edit_disable_highlight (ITEM_EDIT (gsheet->item_editor));
}

void
gnumeric_sheet_rangesel_stop (GnumericSheet *gsheet)
{
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (gsheet->sel_cursor != NULL);

	gtk_object_destroy (GTK_OBJECT (gsheet->sel_cursor));
	gsheet->sel_cursor = NULL;

	/* If we are selecting a range on a different sheet this may be NULL */
	if (gsheet->item_editor)
		item_edit_enable_highlight (ITEM_EDIT (gsheet->item_editor));

	/* Make the primary cursor visible again */
	item_cursor_set_visibility (gsheet->item_cursor, TRUE);
}

/*
 * gsheet_compute_visible_region : Keeps the top left col/row the same and
 *     recalculates the visible boundaries.
 *
 * @full_recompute :
 *       if TRUE recompute the pixel offsets of the top left row/col
 *       else assumes that the pixel offsets of the top left have not changed.
 */
void
gsheet_compute_visible_region (GnumericSheet *gsheet,
			       gboolean const full_recompute)
{
	SheetControlGUI const * const scg = gsheet->scg;
	Sheet const * const sheet = scg->sheet;
	GnomeCanvas   *canvas = GNOME_CANVAS (gsheet);
	int pixels, col, row, width, height;

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		gsheet->col_offset.first =
			scg_colrow_distance_get (scg, TRUE, 0, gsheet->col.first);
		gnome_canvas_scroll_to (scg->col_item->canvas,
			gsheet->col_offset.first, 0);
		gsheet->row_offset.first =
			scg_colrow_distance_get (scg, FALSE, 0, gsheet->row.first);
		gnome_canvas_scroll_to (scg->row_item->canvas,
			0, gsheet->row_offset.first);

		gnome_canvas_scroll_to (GNOME_CANVAS (gsheet),
					gsheet->col_offset.first,
					gsheet->row_offset.first);
	}

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = gsheet->col.first;
	width = GTK_WIDGET (canvas)->allocation.width;

	do {
		ColRowInfo const * const ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			int const bound = pixels + ci->size_pixels;

			if (bound == width) {
				gsheet->col.last_visible = col;
				gsheet->col.last_full = col;
				break;
			}
			if (bound > width) {
				gsheet->col.last_visible = col;
				if (col == gsheet->col.first)
					gsheet->col.last_full = gsheet->col.first;
				else
					gsheet->col.last_full = col - 1;
				break;
			}
			pixels = bound;
		}
		++col;
	} while (pixels < width && col < SHEET_MAX_COLS);

	if (col >= SHEET_MAX_COLS) {
		gsheet->col.last_visible = SHEET_MAX_COLS-1;
		gsheet->col.last_full = SHEET_MAX_COLS-1;
	}

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = gsheet->row.first;
	height = GTK_WIDGET (canvas)->allocation.height;
	do {
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (ri->visible) {
			int const bound = pixels + ri->size_pixels;

			if (bound == height) {
				gsheet->row.last_visible = row;
				gsheet->row.last_full = row;
				break;
			}
			if (bound > height) {
				gsheet->row.last_visible = row;
				if (row == gsheet->row.first)
					gsheet->row.last_full = gsheet->row.first;
				else
					gsheet->row.last_full = row - 1;
				break;
			}
			pixels = bound;
		}
		++row;
	} while (pixels < height && row < SHEET_MAX_ROWS);

	if (row >= SHEET_MAX_ROWS) {
		gsheet->row.last_visible = SHEET_MAX_ROWS-1;
		gsheet->row.last_full = SHEET_MAX_ROWS-1;
	}

	/* Update the scrollbar sizes */
	scg_scrollbar_config (scg);

	/* Force the cursor to update its bounds relative to the new visible region */
	item_cursor_reposition (gsheet->item_cursor);
}

/**
 * gnumeric_sheet_make_cell_visible
 * @gsheet        sheet widget
 * @col           column
 * @row           row
 * @force_scroll  force a scroll
 *
 * Ensure that cell (col, row) is visible.
 * Sheet is scrolled if cell is outside viewport.
 *
 * Avoid calling this before the canvas is realized:
 * We do not know the visible area, and would unconditionally scroll the cell
 * to the top left of the viewport.
 */
void
gnumeric_sheet_make_cell_visible (GnumericSheet *gsheet, int col, int row,
				  gboolean const force_scroll)
{
	GnomeCanvas *canvas;
	Sheet *sheet;
	int   did_change = 0;
	int   new_first_col, new_first_row;
	int   col_offset, row_offset;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	sheet = gsheet->scg->sheet;
	canvas = GNOME_CANVAS (gsheet);

	/* Find the new gsheet->col.first */
	if (col < gsheet->col.first) {
		new_first_col = col;
	} else if (col > gsheet->col.last_full) {
		int width = GTK_WIDGET (canvas)->allocation.width;
		int first_col = (gsheet->col.last_visible == gsheet->col.first)
			? gsheet->col.first : col;

		for (; first_col > 0; --first_col) {
			ColRowInfo const * const ci = sheet_col_get_info (sheet, first_col);
			if (ci->visible) {
				width -= ci->size_pixels;
				if (width < 0)
					break;
			}
		}
		new_first_col = first_col+1;
	} else
		new_first_col = gsheet->col.first;

	/* Find the new gsheet->row.first */
	if (row < gsheet->row.first) {
		new_first_row = row;
	} else if (row > gsheet->row.last_full) {
		int height = GTK_WIDGET (canvas)->allocation.height;
		int first_row = (gsheet->row.last_visible == gsheet->row.first)
			? gsheet->row.first : row;

		for (; first_row > 0; --first_row) {
			ColRowInfo const * const ri = sheet_row_get_info (sheet, first_row);
			if (ri->visible) {
				height -= ri->size_pixels;
				if (height < 0)
					break;
			}
		}
		new_first_row = first_row+1;
	} else
		new_first_row = gsheet->row.first;

	/* Determine if scrolling is required */
	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (gsheet), &col_offset, &row_offset);

	if (gsheet->col.first != new_first_col || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gsheet->col_offset.first = 0;
			gsheet->col.first = 0;
		}
		col_offset = gnumeric_sheet_bar_set_left_col (gsheet, new_first_col);
		did_change = 1;
	}

	if (gsheet->row.first != new_first_row || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gsheet->row_offset.first = 0;
			gsheet->row.first = 0;
		}
		row_offset = gnumeric_sheet_bar_set_top_row (gsheet, new_first_row);
		did_change = 1;
	}

	if (!did_change && !force_scroll)
		return;

	gsheet_compute_visible_region (gsheet, FALSE);

	gnome_canvas_scroll_to (GNOME_CANVAS (gsheet), col_offset, row_offset);
}

