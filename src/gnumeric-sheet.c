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
#include "sheet-control-gui-priv.h"
#include "gnumeric-util.h"
#include "mstyle.h"
#include "style-border.h"
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
#include <gal/util/e-util.h>

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
 * Adds borders to all the selected regions on the sheet.
 * FIXME: This is a little more simplistic then it should be, it always
 * removes and/or overwrites any borders. What we should do is
 * 1) When adding -> don't add a border if the border is thicker than 'THIN'
 * 2) When removing -> don't remove unless the border is 'THIN'
 */
static void
borders_mutate (WorkbookControlGUI *wbcg, Sheet *sheet, gboolean add)
{
	StyleBorder *borders[STYLE_BORDER_EDGE_MAX];
	int i;

	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; ++i)
		if (i <= STYLE_BORDER_RIGHT)
			borders[i] = style_border_fetch (
				add ? STYLE_BORDER_THIN : STYLE_BORDER_NONE,
				style_color_black (), style_border_get_orientation (i));
		else
			borders[i] = NULL;
								  
	cmd_format (WORKBOOK_CONTROL (wbcg), sheet, NULL, borders,
		    add ? _("Add Borders") : _("Remove borders"));
}

/*
 * key press event handler for the gnumeric sheet for the sheet mode
 */
static gint
gnumeric_sheet_key_mode_sheet (GnumericSheet *gsheet, GdkEventKey *event)
{
	SheetControl *sc = (SheetControl *) gsheet->scg;
	Sheet *sheet = sc->sheet;
	WorkbookControlGUI *wbcg = gsheet->scg->wbcg;
	gboolean const jump_to_bounds = event->state & GDK_CONTROL_MASK;
	int state = gnumeric_filter_modifiers (event->state);
	void (*movefn) (SheetControlGUI *, int n,
			gboolean jump, gboolean horiz);

	/* Magic : Some of these are accelerators,
	 * we need to catch them before entering because they appear to be printable
	 */
	if (!wbcg->editing && event->keyval == GDK_space &&
	    (event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK)))
		return FALSE;

	if (wbcg_rangesel_possible (wbcg)) {
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

	if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
		char *fmt = NULL;
		char *desc = NULL;
		
		switch (event->keyval) {
		case GDK_asciitilde :
			fmt = "0";
			desc = _("Format as Number");
			break;
		case GDK_dollar :
			fmt = "$0.00_);($0.00)";
			desc = _("Format as Currency");
			break;
		case GDK_percent :
			fmt = "0%";
			desc = _("Format as Percentage");
			break;
		case GDK_asciicircum :
			fmt = "0.00E+00";
			desc = _("Format as Scientific");
			break;
		case GDK_numbersign :
			fmt = "d-mmm-yy";
			desc = _("Format as Date");
			break;
		case GDK_at :
			fmt = "h:mm AM/PM";
			desc = _("Format as Time");
			break;
		case GDK_exclam :
			fmt = "#,##0.00";
			desc = _("Format as alternative Number"); /* FIXME: Better descriptor */
			break;
		case GDK_ampersand :
			borders_mutate (wbcg, sheet, TRUE);
			return TRUE;
		case GDK_underscore :
			borders_mutate (wbcg, sheet, FALSE);
			return TRUE;
		}

		if (fmt) {
			MStyle *mstyle = mstyle_new ();

			mstyle_set_format_text (mstyle, fmt);
			cmd_format (WORKBOOK_CONTROL (wbcg), sheet, mstyle, NULL, desc);
			return TRUE;
		}
	}
	
	switch (event->keyval) {
	case GDK_KP_Left:
	case GDK_Left:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_left_col (gsheet->scg,
					  gsheet->col.first > 0
					  ? gsheet->col.first - 1
					  : 0);
		else
			(*movefn) (gsheet->scg, -1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Right:
	case GDK_Right:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_left_col (gsheet->scg,
					  gsheet->col.first < SHEET_MAX_COLS - 1
					  ? gsheet->col.first + 1
					  : SHEET_MAX_COLS - 1);
		else
			(*movefn) (gsheet->scg, 1, jump_to_bounds, TRUE);
		break;

	case GDK_KP_Up:
	case GDK_Up:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_top_row (gsheet->scg,
					 gsheet->row.first > 0
					 ? gsheet->row.first - 1
					 : 0);
		else
			(*movefn) (gsheet->scg, -1, jump_to_bounds, FALSE);
		break;

	case GDK_KP_Down:
	case GDK_Down:
		if (event->state & GDK_MOD5_MASK) /* Scroll Lock */
			scg_set_top_row (gsheet->scg,
					 gsheet->row.first < SHEET_MAX_ROWS - 1
					 ? gsheet->row.first + 1
					 : SHEET_MAX_ROWS - 1);
		else
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
		if (event->state & GDK_MOD5_MASK) { /* Scroll Lock */
			scg_set_left_col (gsheet->scg, sheet->edit_pos.col);
			scg_set_top_row (gsheet->scg, sheet->edit_pos.row);
		} else {
			/* do the ctrl-home jump to A1 in 2 steps */
			(*movefn)(gsheet->scg, -SHEET_MAX_COLS, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK))
				(*movefn)(gsheet->scg, -SHEET_MAX_ROWS, FALSE, FALSE);
		}
		break;

	case GDK_KP_End:
	case GDK_End:
		if (event->state & GDK_MOD5_MASK) { /* Scroll Lock */
			int new_col = sheet->edit_pos.col - (gsheet->col.last_full - gsheet->col.first);
			int new_row = sheet->edit_pos.row - (gsheet->row.last_full - gsheet->row.first);
			
			if (new_col > 0)
				scg_set_left_col (gsheet->scg, new_col);
			if (new_row > 0)
				scg_set_top_row (gsheet->scg, new_row);
		} else {
			Range r = sheet_get_extent (sheet, FALSE);
		
			/* do the ctrl-end jump to the extent in 2 steps */
			(*movefn)(gsheet->scg, r.end.col - sheet->edit_pos.col, FALSE, TRUE);
			if ((event->state & GDK_CONTROL_MASK))
				(*movefn)(gsheet->scg, r.end.row - sheet->edit_pos.row, FALSE, FALSE);
		}
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
		    (state == GDK_CONTROL_MASK ||
		     state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK) ||
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
		if (gsheet->sel_cursor)
			wbcg_edit_toggle_absolute (wbcg);
		break;

	case GDK_F2:
		wbcg_edit_start (wbcg, FALSE, FALSE);
		/* fall down */

	case GDK_BackSpace:
		/* Re-center the view on the active cell */
		if (!wbcg->editing && (event->state & GDK_CONTROL_MASK) != 0) {
			scg_make_cell_visible (gsheet->scg, sheet->edit_pos.col,
					       sheet->edit_pos.row, TRUE);
			break;
		}

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
		scg_mode_edit ((SheetControl *) scg);
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
	SheetControl *sc = (SheetControl *) gsheet->scg;

	/*
	 * The status_region normally displays the current edit_pos
	 * When we extend the selection it changes to displaying the size of
	 * the selected region while we are selecting.  When the shift key
	 * is released, or the mouse button is release we need to reset
	 * to displaying the edit pos.
	 */
	if (gsheet->scg->current_object == NULL &&
	    (event->keyval == GDK_Shift_L || event->keyval == GDK_Shift_R))
		wb_view_selection_desc (wb_control_view (
			sc->wbc), TRUE, NULL);

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
		scg_set_left_col (gsheet->scg, col);
	} else {
		int row = gsheet->row.last_full - gsheet->row.first;
		if (button->button == 4)
			row = MAX (gsheet->row.first - row, 0);
		else if (gsheet->row.last_full < SHEET_MAX_ROWS-1)
			row = gsheet->row.last_full;
		else
			return FALSE;
		scg_set_top_row (gsheet->scg, row);
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
	SheetControl *sc = (SheetControl *) gsheet->scg;
	WorkbookControl *wbc = sc->wbc;

	names = gnome_uri_list_extract_filenames ((char *)selection_data->data);

	for (tmp_list = names; tmp_list != NULL; tmp_list = tmp_list->next) {
		gchar *file_name = tmp_list->data;

		if (!wb_view_open (wb_control_view (wbc), wbc, file_name, FALSE)) {
#ifdef ENABLE_BONOBO
			/* If it wasn't a workbook, see if we have a control for it */
			SheetObject *so = sheet_object_container_new_file (
				sc->sheet, file_name);
			if (so != NULL)
				scg_mode_create_object (gsheet->scg, so);
#else
			gchar *msg;

			msg = g_strdup_printf (_("File \"%s\" has unknown format."),
			                       file_name);
			gnumeric_error_read (COMMAND_CONTEXT (sc->wbc), msg);
			g_free (msg);
#endif
		}
	}
}

static void
gnumeric_sheet_realize (GtkWidget *widget)
{
	gint width, height;
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
			GDK_IM_PREEDIT_POSITION |
			GDK_IM_STATUS_NONE |
			GDK_IM_STATUS_NOTHING;

		if(widget->style && widget->style->font->type != GDK_FONT_FONTSET)
		  supported_style &= ~GDK_IM_PREEDIT_POSITION;

		attr->style = style = gdk_im_decide_style (supported_style);
		attr->client_window = gsheet->canvas.layout.bin_window;

		switch (style & GDK_IM_PREEDIT_MASK)
		  {
		  case GDK_IM_PREEDIT_POSITION:
		    if (widget->style && widget->style->font->type != GDK_FONT_FONTSET)
		      {
		        g_warning ("over-the-spot style requires fontset");			        break;
		      }

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

		    break;
		  }

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
	gsheet->col.first = gsheet->col.last_full = gsheet->col.last_visible = 0;
	gsheet->row.first = gsheet->row.last_full = gsheet->row.last_visible = 0;
	gsheet->col_offset.first = gsheet->col_offset.last_full = gsheet->col_offset.last_visible = 0;
	gsheet->row_offset.first = gsheet->row_offset.last_full = gsheet->row_offset.last_visible = 0;

	gsheet->slide_handler = NULL;
	gsheet->slide_data = NULL;
	gsheet->sliding = -1;
	gsheet->sliding_x  = gsheet->sliding_dx = -1;
	gsheet->sliding_y  = gsheet->sliding_dy = -1;
	gsheet->sliding_adjacent_h = gsheet->sliding_adjacent_v = FALSE;

	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

E_MAKE_TYPE (gnumeric_sheet, "GnumericSheet", GnumericSheet,
	     gnumeric_sheet_class_init, gnumeric_sheet_init,
	     GNOME_TYPE_CANVAS);

GnumericSheet *
gnumeric_sheet_new (SheetControlGUI *scg, GnumericPane *pane)
{
	static GtkTargetEntry const drag_types[] = {
		{ "text/uri-list", 0, 0 },
	};
	static gint const n_drag_types = sizeof (drag_types) / sizeof (drag_types [0]);

	GnomeCanvasItem	 *item;
	GnumericSheet	 *gsheet;
	GnomeCanvasGroup *gsheet_group, *root_group;
	Range		  r;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	gsheet_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (gsheet)->root);

	gsheet->scg = scg;
	gsheet->pane = pane;

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
	gtk_signal_connect (GTK_OBJECT (gsheet),
		"drag_data_get",
		GTK_SIGNAL_FUNC (gnumeric_sheet_drag_data_get), NULL);
	gtk_drag_dest_set (GTK_WIDGET (gsheet),
		GTK_DEST_DEFAULT_ALL,
		drag_types, n_drag_types,
		GDK_ACTION_COPY);
	gtk_signal_connect (GTK_OBJECT (gsheet),
		"drag_data_received",
		GTK_SIGNAL_FUNC (gnumeric_sheet_filenames_dropped),
		gsheet);

	/* cursor group */
	root_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (gsheet)->root);
	gsheet->anted_group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (
			root_group, gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	/* sheet object support */
	gsheet->object_group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (
			root_group, gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	return gsheet;
}

/*
 * gnumeric_sheet_find_col: return the column containing pixel x
 */
int
gnumeric_sheet_find_col (GnumericSheet *gsheet, int x, int *col_origin)
{
	Sheet *sheet = ((SheetControl *) gsheet->scg)->sheet;
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
	Sheet *sheet = ((SheetControl *) gsheet->scg)->sheet;
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
	SheetControl *sc = (SheetControl *) gsheet->scg;
	Range r;

	g_return_if_fail (gsheet->sel_cursor == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (sc->sheet != wb_control_cur_sheet (sc->wbc))
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

	gnumeric_sheet_slide_stop (gsheet);
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
	Sheet const * const sheet = ((SheetControl *) scg)->sheet;
	GnomeCanvas   *canvas = GNOME_CANVAS (gsheet);
	int pixels, col, row, width, height;

	/* When col/row sizes change we need to do a full recompute */
	if (full_recompute) {
		gsheet->col_offset.first = scg_colrow_distance_get (scg,
			TRUE, 0, gsheet->col.first);
		if (NULL != gsheet->pane->col.canvas)
			gnome_canvas_scroll_to (gsheet->pane->col.canvas,
				gsheet->col_offset.first, 0);

		gsheet->row_offset.first = scg_colrow_distance_get (scg,
			FALSE, 0, gsheet->row.first);
		if (NULL != gsheet->pane->row.canvas)
			gnome_canvas_scroll_to (gsheet->pane->row.canvas,
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

	/* Update the scrollbar sizes for the primary pane */
	if (gsheet->pane->index == 0)
		scg_scrollbar_config (SHEET_CONTROL (scg));

	/* Force the cursor to update its bounds relative to the new visible region */
	item_cursor_reposition (gsheet->item_cursor);
}

void
gnumeric_sheet_redraw_region (GnumericSheet *gsheet,
			      int start_col, int start_row,
			      int end_col, int end_row)
{
	SheetControlGUI *scg;
	GnomeCanvas *canvas;
	int x1, y1, x2, y2;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	scg = gsheet->scg;
	canvas = GNOME_CANVAS (gsheet);

	if ((end_col < gsheet->col.first) ||
	    (end_row < gsheet->row.first) ||
	    (start_col > gsheet->col.last_visible) ||
	    (start_row > gsheet->row.last_visible))
		return;

	/* Only draw those regions that are visible */
	start_col = MAX (gsheet->col.first, start_col);
	start_row = MAX (gsheet->row.first, start_row);
	end_col =  MIN (gsheet->col.last_visible, end_col);
	end_row =  MIN (gsheet->row.last_visible, end_row);

	/* redraw a border of 2 pixels around the region to handle thick borders
	 * NOTE the 2nd coordinates are excluded so add 1 extra (+2border +1include)
	 */
	x1 = scg_colrow_distance_get (scg, TRUE, gsheet->col.first, start_col) +
		gsheet->col_offset.first;
	y1 = scg_colrow_distance_get (scg, FALSE, gsheet->row.first, start_row) +
		gsheet->row_offset.first;
	x2 = (end_col < (SHEET_MAX_COLS-1))
		? 4 + 1 + x1 + scg_colrow_distance_get (scg, TRUE,
							start_col, end_col+1)
		: INT_MAX;
	y2 = (end_row < (SHEET_MAX_ROWS-1))
		? 4 + 1 + y1 + scg_colrow_distance_get (scg, FALSE,
							start_row, end_row+1)
		: INT_MAX;

#if 0
	fprintf (stderr, "%s%s:", col_name (min_col), row_name (first_row));
	fprintf (stderr, "%s%s\n", col_name (max_col), row_name (last_row));
#endif

	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet), x1-2, y1-2, x2, y2);
}

/*****************************************************************************/

void
gnumeric_sheet_slide_stop (GnumericSheet *gsheet)
{
	if (gsheet->sliding == -1)
		return;

	gtk_timeout_remove (gsheet->sliding);
	gsheet->slide_handler = NULL;
	gsheet->slide_data = NULL;
	gsheet->sliding = -1;
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
gsheet_sliding_callback (gpointer data)
{
	GnumericSheet *gsheet = data;
	int const pane_index = gsheet->pane->index;
	Sheet const *sheet = sc_sheet (SHEET_CONTROL (gsheet->scg));
	GnumericSheet *gsheet0 = scg_pane (gsheet->scg, 0);
	GnumericSheet *gsheet2 = sheet_is_frozen (sheet)
		? scg_pane (gsheet->scg, 2) : NULL;
	gboolean slide_x = FALSE, slide_y = FALSE;
	int col = -1, row = -1;

	if (gsheet->sliding_dx > 0) {
		GnumericSheet *target_gsheet = gsheet;

		slide_x = TRUE;
		if (pane_index == 1 || pane_index == 2) {
			if (!gsheet->sliding_adjacent_h) {
				int width = GTK_WIDGET (gsheet)->allocation.width;
				int x = gsheet->col_offset.first + width + gsheet->sliding_dx;

				/* in case pane is narrow */
				col = gnumeric_sheet_find_col (gsheet, x, NULL);
				if (col > gsheet0->col.last_full) {
					gsheet->sliding_adjacent_h = TRUE;
					gsheet->sliding_dx = 1; /* good enough */
				} else
					slide_x = FALSE;
			} else
				target_gsheet = gsheet0;
		}

		if (slide_x) {
			col = target_gsheet->col.last_full + 
				col_scroll_step (gsheet->sliding_dx);
			if (col >= SHEET_MAX_COLS) {
				col = SHEET_MAX_COLS-1;
				slide_x = FALSE;
			}
		}
	} else if (gsheet->sliding_dx < 0) {
		slide_x = FALSE;
		if (pane_index != 1 && pane_index != 2 &&
		    !gsheet->sliding_adjacent_h) {
			int x = gsheet->col_offset.first + gsheet->sliding_dx;
			col = gnumeric_sheet_find_col (gsheet, x, NULL);
			/* Be careful if sheet is narrow, don't
			 * autoscroll past edge
			 */
			if (col <= gsheet2->col.last_visible) {
				if (col < gsheet2->col.first)
					col = gsheet2->col.first;
				gsheet->sliding_adjacent_h = TRUE;
			} else
				slide_x = TRUE;
		}

		if (slide_x) {
			col = gsheet->col.first -
				col_scroll_step (-gsheet->sliding_dx);
			if (col < 0) {
				col = 0;
				slide_x = FALSE;
			}
		}
	} 

	if (gsheet->sliding_dy > 0) {
		GnumericSheet *target_gsheet = gsheet;

		slide_y = TRUE;
		if (pane_index == 3 || pane_index == 2) {
			if (!gsheet->sliding_adjacent_v) {
				int height = GTK_WIDGET (gsheet)->allocation.height;
				int y = gsheet->row_offset.first + height + gsheet->sliding_dy;

				/* in case pane is short */
				row = gnumeric_sheet_find_row (gsheet, y, NULL);
				if (row > gsheet0->row.last_full) {
					gsheet->sliding_adjacent_v = TRUE;
					gsheet->sliding_dy = 1; /* good enough */
				} else
					slide_y = FALSE;
			} else
				target_gsheet = gsheet0;
		}

		if (slide_y) {
			row = target_gsheet->row.last_full + 
				row_scroll_step (gsheet->sliding_dy);
			if (row >= SHEET_MAX_ROWS) {
				row = SHEET_MAX_ROWS-1;
				slide_y = FALSE;
			}
		}
	} else if (gsheet->sliding_dy < 0) {
		slide_y = FALSE;
		if (pane_index != 3 && pane_index != 2 &&
		    !gsheet->sliding_adjacent_v) {
			int y = gsheet->row_offset.first + gsheet->sliding_dy;
			row = gnumeric_sheet_find_row (gsheet, y, NULL);
			/* Be careful if sheet is narrow, don't
			 * autoscroll past edge
			 */
			if (row <= gsheet2->row.last_visible) {
				if (row < gsheet2->row.first)
					row = gsheet2->row.first;
				gsheet->sliding_adjacent_v = TRUE;
			} else
				slide_y = TRUE;
		}

		if (slide_y) {
			row = gsheet->row.first -
				row_scroll_step (-gsheet->sliding_dy);
			if (row < 0) {
				row = 0;
				slide_y = FALSE;
			}
		}
	}

	if (col < 0 && row < 0) {
		gnumeric_sheet_slide_stop (gsheet);
		return TRUE;
	}

	if (col < 0)
		col = gnumeric_sheet_find_col (gsheet, gsheet->sliding_x, NULL);
	else if (row < 0)
		row = gnumeric_sheet_find_row (gsheet, gsheet->sliding_y, NULL);

	if (gsheet->slide_handler == NULL ||
	    (*gsheet->slide_handler) (gsheet, col, row, gsheet->slide_data))
		scg_make_cell_visible (gsheet->scg, col, row, FALSE);

	if (slide_x || slide_y) {
		if (gsheet->sliding == -1)
			gsheet->sliding = gtk_timeout_add (
				300, gsheet_sliding_callback, gsheet);
	} else
		gnumeric_sheet_slide_stop (gsheet);

	return TRUE;
}

/**
 * gnumeric_sheet_handle_motion :
 * @gsheet	 : The GnumericSheet managing the scroll
 * @canvas	 : The Canvas the event comes from
 * @event	 : The motion event
 * @allow_h      : Can we slide horizontally
 * @allow_v	 : Can we slide vertically
 * @colrow_bound : Use last full col/row as bound
 * @slide_handler: The handler when sliding
 * @user_data	 : closure data
 *
 * Handle a motion event from a @canvas and scroll the @gsheet
 * depending on how far outside the bounds of @gsheet the @event is.
 * Usually @canvas == @gsheet however as long as the canvases share a basis
 * space they can be different.
 *
 * @colrow_bound is not implemented yet.
 */
void
gnumeric_sheet_handle_motion (GnumericSheet *gsheet,
			      GnomeCanvas *canvas, GdkEventMotion *event,
			      gboolean allow_h, gboolean allow_v,
			      gboolean colrow_bound,
			      GnumericSheetSlideHandler slide_handler,
			      gpointer user_data)
{
	Sheet const *sheet;
	GnumericSheet *gsheet0;
	int left, top, x, y, width, height;
	int dx = 0, dy = 0;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (GNOME_IS_CANVAS (canvas));
	g_return_if_fail (event != NULL);
	g_return_if_fail (slide_handler != NULL);

	gnome_canvas_w2c (canvas, event->x, event->y, &x, &y);

	left = gsheet->col_offset.first;
	top = gsheet->row_offset.first;
	width = GTK_WIDGET (gsheet)->allocation.width;
	height = GTK_WIDGET (gsheet)->allocation.height;

	sheet = sc_sheet (SHEET_CONTROL (gsheet->scg));
	gsheet0 = scg_pane (gsheet->scg, 0);

	if (allow_h) {
		if (x < left)
			dx = x - left;
		else if (x >= left + width)
			dx = x - width - left;
	}

	if (allow_v) {
		if (y < top)
			dy = y - top;
		else if (y >= top + height)
			dy = y - height - top;
	}

	if (sheet_is_frozen (sheet)) {
		int const pane = gsheet->pane->index;
		GnumericSheet *gsheet2 = scg_pane (gsheet->scg, 2);

		if (gsheet->sliding_adjacent_h) {
			if (pane == 0 || pane == 3) {
				if (dx < 0) {
					dx += GTK_WIDGET (gsheet2)->allocation.width;
					x = gsheet2->col_offset.first + dx;
					if (dx > 0)
						dx = 0;
				}
			} else {
				if (dx > 0) {
					x = gsheet0->col_offset.first + dx;
					dx -= GTK_WIDGET (gsheet0)->allocation.width;
					if (dx < 0)
						dx = 0;
				}
			}
		}

		if (gsheet->sliding_adjacent_v) {
			if (pane == 0 || pane == 1) {
				if (dy < 0) {
					dy += GTK_WIDGET (gsheet2)->allocation.height;
					y = gsheet2->row_offset.first +  dy;
					if (dy > 0)
						dy = 0;
				}
			} else {
				if (dy > 0) {
					y = gsheet0->row_offset.first + dy;
					dy -= GTK_WIDGET (gsheet0)->allocation.height;
					if (dy < 0)
						dy = 0;
				}
			}
		}
	}
	/* Movement is inside the visible region */
	if (dx == 0 && dy == 0) {
		int const col = gnumeric_sheet_find_col (gsheet, x, NULL);
		int const row = gnumeric_sheet_find_row (gsheet, y, NULL);

		gnumeric_sheet_slide_stop (gsheet);
		(*slide_handler) (gsheet, col, row, user_data);
		return;
	}

	gsheet->sliding_x  = x;
	gsheet->sliding_dx = dx;
	gsheet->sliding_y  = y;
	gsheet->sliding_dy = dy;
	gsheet->slide_handler = slide_handler;
	gsheet->slide_data = user_data;

	if (gsheet->sliding == -1)
		(void) gsheet_sliding_callback (gsheet);
}

void
gnumeric_sheet_slide_init (GnumericSheet *gsheet)
{
	Sheet const *sheet;
	GnumericSheet *gsheet0;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));

	sheet = sc_sheet (SHEET_CONTROL (gsheet->scg));
	gsheet0 = scg_pane (gsheet->scg, 0);

	if (sheet_is_frozen (sheet)) {
		GnumericSheet const *gsheet2 = scg_pane (gsheet->scg, 2);

		gsheet->sliding_adjacent_h =
			gsheet2->col.last_full == (gsheet0->col.first - 1);
		gsheet->sliding_adjacent_v =
			gsheet2->row.last_full == (gsheet0->row.first - 1);
	} else
		gsheet->sliding_adjacent_h = gsheet->sliding_adjacent_v = FALSE;

}
