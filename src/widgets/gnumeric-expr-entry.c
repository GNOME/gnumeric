/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges.
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "gnumeric-expr-entry.h"

#include <workbook-edit.h>
#include <workbook-control-gui-priv.h>
#include <sheet-control-gui.h>
#include <sheet-merge.h>
#include <parse-util.h>
#include <gui-util.h>
#include <ranges.h>
#include <value.h>
#include <expr.h>
#include <dependent.h>
#include <sheet.h>
#include <selection.h>
#include <commands.h>
#include <gnm-marshalers.h>

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcelleditable.h>
#include <ctype.h>
#include <stdio.h>
#include <libgnome/gnome-i18n.h>

typedef struct {
	Range range;
	Sheet *sheet;
	int text_start;
	int text_end;
	gboolean  abs_col;
	gboolean  abs_row;
	gboolean  is_valid;
} Rangesel;

struct _GnumericExprEntry {
	GtkHBox	parent;

	GtkEntry		*entry;
	WorkbookControlGUI	*wbcg;
	SheetControlGUI		*scg;
	GnumericExprEntryFlags	 flags;
	int			 freeze_count;
	Sheet			*target_sheet;
	Rangesel		 rangesel;

	GtkUpdateType		 update_policy;
	guint			 update_timeout_id;

	gboolean                 is_cell_renderer;  /* as cell_editable */
	gboolean                 editing_canceled;  /* as cell_editable */
};

typedef struct _GnumericExprEntryClass {
	GtkHBoxClass parent_class;

	void (* update)  (GnumericExprEntry *gee);
	void (* changed) (GnumericExprEntry *gee);
} GnumericExprEntryClass;

/* Signals */
enum {
	UPDATE,
	CHANGED,
	LAST_SIGNAL
};

/* Properties */
enum {
	PROP_0,
	PROP_UPDATE_POLICY
};

static GQuark signals [LAST_SIGNAL] = { 0 };


/* GObject, GtkObject methods
 */
static void     gee_set_property (GObject *object, guint prop_id,
				  GValue const *value, GParamSpec *pspec);
static void     gee_get_property (GObject *object, guint prop_id,
				  GValue *value, GParamSpec *pspec);
static void     gee_class_init (GtkObjectClass *klass);
static void     gee_init (GnumericExprEntry *entry);
static void     gnumeric_expr_entry_cell_editable_init (GtkCellEditableIface *iface);


/* GtkHBox methods
 */


/* GtkCellEditable
 */
static void     gnumeric_expr_entry_start_editing (GtkCellEditable *cell_editable,
						   GdkEvent *event);
static void     gnumeric_cell_editable_entry_activated (gpointer data,
							GnumericExprEntry *entry);


/* Call Backs
 */
static void     cb_scg_destroy (GnumericExprEntry *gee, SheetControlGUI *scg);
static void     cb_entry_changed (GtkEntry *ignored, GnumericExprEntry *gee);
static gint     cb_gee_key_press_event (GtkEntry *entry, GdkEventKey *key_event,
					GnumericExprEntry *gee);

/* Internal routines
 */
static void     gee_rangesel_reset (GnumericExprEntry *gee);
static void     gee_make_display_range (GnumericExprEntry *gee, Range *dst);
static char    *gee_rangesel_make_text (GnumericExprEntry *gee);
static void     gee_rangesel_update_text (GnumericExprEntry *gee);
static gboolean split_char_p (unsigned char c);
static void     gee_detach_scg (GnumericExprEntry *gee);
static gboolean gee_update_timeout (gpointer data);
static void     gee_remove_update_timer (GnumericExprEntry *range);
static void     gee_reset_update_timer (GnumericExprEntry *gee);
static void     gee_destroy (GtkObject *object);
static void     gee_notify_cursor_position (GObject *object, GParamSpec *pspec,
					    GnumericExprEntry *gee);

static GtkHBoxClass *gnumeric_expr_entry_parent_class = NULL;

/* E_MAKE_TYPE (gnumeric_expr_entry, "GnumericExprEntry", GnumericExprEntry, */
/* 	     gee_class_init, NULL, GTK_TYPE_HBOX)                            */

GType gnumeric_expr_entry_get_type(void)
{
        static GType type = 0;
        if (!type){
                static GtkTypeInfo const object_info = {
			(char *) "GnumericExprEntry",
			sizeof (GnumericExprEntry),
                        sizeof (GnumericExprEntryClass),
                        (GtkClassInitFunc) gee_class_init,
			(GtkObjectInitFunc) gee_init,
                        NULL,
                        NULL,   /* class_data */
                        (GtkClassInitFunc) NULL,
                };

		static const GInterfaceInfo cell_editable_info =
			{
				(GInterfaceInitFunc) gnumeric_expr_entry_cell_editable_init,
				NULL,
				NULL
			};

		type = gtk_type_unique (GTK_TYPE_HBOX, &object_info);
		g_type_add_interface_static (type,
					     GTK_TYPE_CELL_EDITABLE,
					     &cell_editable_info);
        }
        return type;
}

static void
gee_class_init (GtkObjectClass *klass)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);

	gnumeric_expr_entry_parent_class
		= gtk_type_class (gtk_hbox_get_type());

	gobject_class->set_property = gee_set_property;
	gobject_class->get_property = gee_get_property;
	klass->destroy		    = gee_destroy;

	signals [UPDATE] = g_signal_new ("update",
		GNUMERIC_TYPE_EXPR_ENTRY,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnumericExprEntryClass, update),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);
	signals [CHANGED] = g_signal_new ("changed",
		GNUMERIC_TYPE_EXPR_ENTRY,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnumericExprEntryClass, changed),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	g_object_class_install_property (gobject_class,
		PROP_UPDATE_POLICY,
		g_param_spec_enum ("update_policy",
			"Update policy",
			"How frequently changes to the entry should be applied",
			GTK_TYPE_UPDATE_TYPE,
			GTK_UPDATE_CONTINUOUS,
			G_PARAM_READWRITE));

}

static void
gee_init (GnumericExprEntry *entry)
{
	entry->editing_canceled = FALSE;
	entry->is_cell_renderer = FALSE;
	entry->rangesel.is_valid = FALSE;
	entry->scg = NULL;
}

static void
gnumeric_expr_entry_cell_editable_init (GtkCellEditableIface *iface)
{
  iface->start_editing = gnumeric_expr_entry_start_editing;
}


static void
gnumeric_expr_entry_start_editing (GtkCellEditable *cell_editable,
				   GdkEvent *event)
{
  GNUMERIC_EXPR_ENTRY (cell_editable)->is_cell_renderer = TRUE;

  g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (GNUMERIC_EXPR_ENTRY (cell_editable))),
		    "activate",
		    G_CALLBACK (gnumeric_cell_editable_entry_activated), cell_editable);

  gtk_widget_grab_focus (GTK_WIDGET (gnm_expr_entry_get_entry (GNUMERIC_EXPR_ENTRY
							       (cell_editable))));

}

static void
gnumeric_cell_editable_entry_activated (gpointer data, GnumericExprEntry *entry)
{
  gtk_cell_editable_editing_done (GTK_CELL_EDITABLE (entry));
}

static void
gee_rangesel_reset (GnumericExprEntry *gee)
{
	Rangesel *rs = &gee->rangesel;

	rs->sheet = NULL;
	rs->text_start = 0;
	rs->text_end = 0;
	memset (&rs->range, 0, sizeof (Range));

	/* restore the default based on the flags */
	gee->rangesel.abs_col = (gee->flags & GNUM_EE_ABS_COL) != 0;
	gee->rangesel.abs_row = (gee->flags & GNUM_EE_ABS_ROW) != 0;
	gee->rangesel.is_valid = FALSE;
}

static void
gee_make_display_range (GnumericExprEntry *gee, Range *dst)
{
	*dst = gee->rangesel.range;

	if (gee->flags & GNUM_EE_FULL_COL) {
		dst->start.row = 0;
		dst->end.row   = SHEET_MAX_ROWS - 1;
	}
	if (gee->flags & GNUM_EE_FULL_ROW) {
		dst->start.col = 0;
		dst->end.col   = SHEET_MAX_COLS - 1;
	}
}

static char *
gee_rangesel_make_text (GnumericExprEntry *gee)
{
	char *buffer;
	gboolean inter_sheet;
	Range display_range;
	Range const *m;
	Rangesel const *rs = &gee->rangesel;

	inter_sheet = (rs->sheet != gee->target_sheet);
	gee_make_display_range (gee, &display_range);
	buffer = g_strdup_printf (
		"%s%s%s%d",
		rs->abs_col ? "$" : "",
		col_name (display_range.start.col),
		rs->abs_row ? "$" : "",
		display_range.start.row+1);

	m = sheet_merge_is_corner (rs->sheet, &display_range.start);
	if (!range_is_singleton (&display_range) &&
	    ((m == NULL) || !range_equal (m, &display_range))) {
		char *tmp = g_strdup_printf (
			"%s:%s%s%s%d",buffer,
			rs->abs_col ? "$" : "",
			col_name (display_range.end.col),
			rs->abs_row ? "$" : "",
			display_range.end.row+1);
		g_free (buffer);
		buffer = tmp;
	}
	if (inter_sheet || !(gee->flags & GNUM_EE_SHEET_OPTIONAL)) {
		char *tmp = g_strdup_printf ("%s!%s", rs->sheet->name_quoted,
					     buffer);
		g_free (buffer);
		buffer = tmp;
	}

	return buffer;
}

static void
gee_rangesel_update_text (GnumericExprEntry *gee)
{
	GtkEditable *editable = GTK_EDITABLE (gee->entry);
	Rangesel *rs = &gee->rangesel;
	int len;

	char *text = gee_rangesel_make_text (gee);

	if (rs->text_end > rs->text_start) {
		if (text == NULL)
			gtk_editable_delete_text (editable,
						  rs->text_start,
						  rs->text_end);
		else
			/* We don't call gtk_editable_delete_text since we don't want */
			/* to emit a signal yet */
			GTK_EDITABLE_GET_CLASS (gee->entry)->delete_text (editable,
									  rs->text_start,
									  rs->text_end);
		rs->text_end = rs->text_start;
		gtk_editable_set_position (GTK_EDITABLE (gee->entry), rs->text_end);
	} else
		rs->text_start = rs->text_end =
			gtk_editable_get_position (GTK_EDITABLE (gee->entry));

	if (text == NULL)
		return;

	/* Set the cursor at the end.  It looks nicer */
	len = strlen (text);
	gtk_editable_insert_text (editable, text, len, &rs->text_end);
	gtk_editable_set_position (editable, rs->text_end);
	g_free (text);
}

/**
 * gnm_expr_entry_rangesel_start
 * @gee:   a #GnumericExprEntry
 *
 * Look at the current selection to see how much of it needs to be changed when
 * selecting a range.
 *
 **/
void
gnm_expr_entry_rangesel_start (GnumericExprEntry *gee)
{
	int cursor, start, last, end;
	int from, to;
	char const *text;
	RangeRef *range;
	Rangesel *rs;
	gboolean single = (gee->flags & (GNUM_EE_SINGLE_RANGE != 0));

	rs = &gee->rangesel;
	text = gtk_entry_get_text (gee->entry);
	cursor = gtk_editable_get_position (GTK_EDITABLE (gee->entry));
	rs->abs_col = (gee->flags & GNUM_EE_ABS_COL) != 0;
	rs->abs_row = (gee->flags & GNUM_EE_ABS_ROW) != 0;
	rs->sheet = gee->target_sheet;
	rs->is_valid = FALSE;
	if (text == NULL)
		return;
	last = strlen (text);

	if (parse_surrounding_ranges  (text, cursor, rs->sheet, 
				       !single, &from, &to,
				       &range))
	{
		Sheet *end_sheet;
		EvalPos ep;
		
		/* Note:
		 * If single is not true, we just have one range here!
		 **/
		rs->abs_col = !range->a.col_relative;
		rs->abs_row = !range->a.row_relative;
		ep.eval.col = 0;
		ep.eval.row = 0;
		ep.sheet = rs->sheet;
		
		rangeref_normalize (&ep, range, &rs->sheet, &end_sheet,
				    &rs->range);
		rs->is_valid = TRUE;
		g_free (range);
		rs->text_start = from;
		rs->text_end = to;
		if (single) {
			rs->text_start = 0;
			rs->text_end = last;
		}
		return;
	}

	if (single) {
		rs->text_start = 0;
		rs->text_end = last;
	} else {
		for (start = cursor; start > 0; start = g_utf8_prev_char (text + start) - text) {
			gunichar c = g_utf8_get_char (text + start);
			if (!isalnum (c)) {
				start = g_utf8_next_char (text + start) - text;
				break;
			}
		}
		rs->text_start = (cursor < start) ? cursor : start;
		for (end = cursor; end < last; end = g_utf8_next_char (text + end) - text) {
			gunichar c = g_utf8_get_char (text + end);
			if (!g_unichar_isalnum (c)) {
				break;
			}
		}
		rs->text_end = (cursor < end) ? end : cursor;
	}
	return;
}

/**
 * gnm_expr_entry_rangesel_stop
 * @gee:   a #GnumericExprEntry
 * @clear_string: clear string flag
 *
 * Perform the appropriate action when a range selection has been completed.
 **/
void
gnm_expr_entry_rangesel_stop (GnumericExprEntry *gee,
			      gboolean clear_string)
{
	Rangesel *rs;

	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	rs = &gee->rangesel;
	if (clear_string && rs->text_end > rs->text_start)
		gtk_editable_delete_text (GTK_EDITABLE (gee->entry),
					  rs->text_start, rs->text_end);

	if (!(gee->flags & GNUM_EE_SINGLE_RANGE) || clear_string)
		gee_rangesel_reset (gee);
}

/***************************************************************************/

static void
cb_scg_destroy (GnumericExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (scg == gee->scg);

	gee_rangesel_reset (gee);
	gee->scg = NULL;
	gee->target_sheet = NULL;
}

static void
gee_detach_scg (GnumericExprEntry *gee)
{
	if (gee->scg != NULL) {
		g_object_weak_unref (G_OBJECT (gee->scg),
				     (GWeakNotify) cb_scg_destroy, gee);
		gee->scg = NULL;
	}
}

/***************************************************************************/

/**
 * gnm_expr_entry_end_of_drag:
 * @gee :
 *
 * Signal the expression entry clients that a drag selection has just finished.
 * This is useful for knowing when to attempt to parse and update things that
 * depend on the modified expression without doing it in real time.
 **/
void
gnm_expr_entry_end_of_drag (GnumericExprEntry *gee)
{
	g_signal_emit (G_OBJECT (gee), signals [UPDATE], 0);
}

static gboolean
gee_update_timeout (gpointer data)
{
	GnumericExprEntry *gee = GNUMERIC_EXPR_ENTRY (data);
	gee->update_timeout_id = 0;
	g_signal_emit (G_OBJECT (gee), signals [UPDATE], 0);
	return FALSE;
}

static void
gee_remove_update_timer (GnumericExprEntry *range)
{
	if (range->update_timeout_id != 0) {
		g_source_remove (range->update_timeout_id);
		range->update_timeout_id = 0;
	}
}
static void
gee_reset_update_timer (GnumericExprEntry *gee)
{
	gee_remove_update_timer (gee);

	gee->update_timeout_id = g_timeout_add (300,
		gee_update_timeout, gee);
}

static void
gee_destroy (GtkObject *object)
{
	gee_detach_scg (GNUMERIC_EXPR_ENTRY (object));
	GTK_OBJECT_CLASS (gnumeric_expr_entry_parent_class)->destroy (object);
}

static gboolean
cb_gee_button_press_event (GtkEntry *entry, GdkEventButton *event, GnumericExprEntry *gee)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);

	if (gee->scg) {
		scg_rangesel_stop (gee->scg, FALSE);
		gnm_expr_entry_rangesel_start (gee);
		g_signal_emit (G_OBJECT (gee), signals [CHANGED], 0);
	}
	
	return FALSE;
}


static gboolean
cb_gee_key_press_event (GtkEntry	  *entry,
			GdkEventKey	  *event,
			GnumericExprEntry *gee)
{
	WorkbookControlGUI *wbcg  = gee->wbcg;
	int state = gnumeric_filter_modifiers (event->state);

	switch (event->keyval) {
	case GDK_Up:	case GDK_KP_Up:
	case GDK_Down:	case GDK_KP_Down:
		if (gee->is_cell_renderer)
			return FALSE;
		/* Ignore these keys */
		return TRUE;

	case GDK_F4: {
		/* Cycle absolute reference mode through the sequence rel/rel,
		 * abs/abs, rel/abs, abs/rel and back to rel/rel. Update text
		 * displayed in entry.
		 */
		Rangesel *rs = &gee->rangesel;
		gboolean abs_cols = (gee->flags & GNUM_EE_ABS_COL);
		gboolean abs_rows = (gee->flags & GNUM_EE_ABS_ROW);

		/* FIXME: since the range can't have changed we should just be able to */
		/*        look it up rather than reparse */

		/* Look for a range */
		if (rs->text_start >= rs->text_end)
			gnm_expr_entry_rangesel_start (gee);

		/* no range found */
		if (rs->text_start >= rs->text_end)
			return TRUE;

		/* rows must be absolute */
		if (abs_rows) {
			if (abs_cols)
				return TRUE;
			rs->abs_col = !rs->abs_col;
		} else if (abs_cols)
			rs->abs_row = !rs->abs_row;
		else {
			/* It's late. I'm doing this the straightforward way. */
			rs->abs_row = (rs->abs_row == rs->abs_col);
			rs->abs_col = !rs->abs_col;
		}

		gee_rangesel_update_text (gee);

		return TRUE;
	}

	case GDK_Escape:
		if (gee->is_cell_renderer) {
			entry->editing_canceled = TRUE;
			gtk_cell_editable_editing_done (GTK_CELL_EDITABLE (gee));
			gtk_cell_editable_remove_widget (GTK_CELL_EDITABLE (gee));
			return TRUE;
		} else
			wbcg_edit_finish (wbcg, FALSE);
		return TRUE;

	case GDK_KP_Enter:
	case GDK_Return: {
		SheetView *sv;

		if (gee->is_cell_renderer)
			return FALSE;
		/* Is this the right way to append a newline ?? */
		if (state == GDK_MOD1_MASK) {
			gint pos = gtk_editable_get_position (GTK_EDITABLE (entry));
			gtk_editable_insert_text (GTK_EDITABLE (entry), "\n", 1, &pos);
			gtk_editable_set_position (GTK_EDITABLE (entry), pos+1);
			return TRUE;
		}

		/* Ctrl-enter is only applicable for the main entry */
		if (!wbcg_is_editing (wbcg))
			break;

		/* Be careful to use the editing sheet */
		sv = sheet_get_view (wbcg->editing_sheet,
			wb_control_view (WORKBOOK_CONTROL (wbcg)));

		if (state == GDK_CONTROL_MASK ||
		    state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
			gboolean const is_array = (state & GDK_SHIFT_MASK);
			char const *text = gtk_entry_get_text (
				wbcg_get_entry (wbcg));

			cmd_area_set_text (WORKBOOK_CONTROL (wbcg), sv,
				text, is_array);

			/* do NOT store the results If the assignment was
			 * successful it will have taken care of that.
			 */
			wbcg_edit_finish (wbcg, FALSE);
		} else if (wbcg_edit_finish (wbcg, TRUE)) {
			/* move the edit pos */
			gboolean const direction = (event->state & GDK_SHIFT_MASK) ? FALSE : TRUE;
			sv_selection_walk_step (sv, direction, FALSE);
		}
		return TRUE;
	}
	default:
		break;
	}

	return FALSE;
}

static void
gee_notify_cursor_position (GObject *object, GParamSpec *pspec, GnumericExprEntry *gee)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	if (!gnm_expr_entry_can_rangesel (gee))
		scg_rangesel_stop (gee->scg, FALSE);
}

/**
 * gnumeric_expr_entry_set_update_policy:
 * @range: a #GnumericExprEntry
 * @policy: update policy
 *
 * Sets the update policy for the expr-entry. #GTK_UPDATE_CONTINUOUS means that
 * anytime the entry's content changes, the update signal will be emitted.
 * #GTK_UPDATE_DELAYED means that the signal will be emitted after a brief
 * timeout when no changes occur, so updates are spaced by a short time rather
 * than continuous. #GTK_UPDATE_DISCONTINUOUS means that the signal will only
 * be emitted when the user releases the button and ends the rangeselection.
 *
 **/
void
gnumeric_expr_entry_set_update_policy (GnumericExprEntry *gee,
				       GtkUpdateType  policy)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	if (gee->update_policy == policy)
		return;
	gee->update_policy = policy;
	g_object_notify (G_OBJECT (gee), "update_policy");
}

static void
gee_set_property (GObject      *object,
		  guint         prop_id,
		  GValue const *value,
		  GParamSpec   *pspec)
{
	GnumericExprEntry *gee = GNUMERIC_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		gnumeric_expr_entry_set_update_policy (gee, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gee_get_property (GObject      *object,
		  guint         prop_id,
		  GValue       *value,
		  GParamSpec   *pspec)
{
	GnumericExprEntry *gee = GNUMERIC_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		g_value_set_enum (value, gee->update_policy);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
cb_entry_changed (GtkEntry *ignored, GnumericExprEntry *gee)
{
	if (!gee->is_cell_renderer &&
	    !gnm_expr_entry_can_rangesel (gee) &&
	    gee->scg != NULL)
		scg_rangesel_stop (gee->scg, FALSE);

	g_signal_emit (G_OBJECT (gee), signals [CHANGED], 0);
}

/**
 * gnumeric_expr_entry_new:
 *
 * Creates a new #GnumericExprEntry, which is an entry widget with support
 * for range selections.
 * The entry is created with default flag settings which are suitable for use
 * in many dialogs, but see #gnm_expr_entry_set_flags.
 *
 * Return value: a new #GnumericExprEntry.
 **/
GnumericExprEntry *
gnumeric_expr_entry_new (WorkbookControlGUI *wbcg, gboolean with_icon)
{
	GnumericExprEntry *gee;

	gee = gtk_type_new (gnumeric_expr_entry_get_type ());

	gee->entry = GTK_ENTRY (gtk_entry_new ());
	g_signal_connect (G_OBJECT (gee->entry),
		"changed",
		G_CALLBACK (cb_entry_changed), gee);
	g_signal_connect (G_OBJECT (gee->entry),
			  "key_press_event",
			  G_CALLBACK (cb_gee_key_press_event), gee);
	g_signal_connect (G_OBJECT (gee->entry),
			  "button_press_event",
			  G_CALLBACK (cb_gee_button_press_event), gee);
	g_signal_connect (G_OBJECT (gee->entry),
		"notify::cursor-position",
		G_CALLBACK (gee_notify_cursor_position), gee);
	gtk_box_pack_start (GTK_BOX (gee), GTK_WIDGET (gee->entry),
		TRUE, TRUE, 0);

	if (with_icon) {
		GtkWidget *icon = gtk_image_new_from_stock (
			"Gnumeric_ExprEntry", GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start (GTK_BOX (gee), icon, FALSE, FALSE, 0);
		gtk_widget_show (icon);
	}
	gtk_widget_show (GTK_WIDGET (gee->entry));

	gee->flags = 0;
	gee->wbcg = wbcg;
	gee->freeze_count = 0;
	gee->update_timeout_id = 0;

	return gee;
}

void
gnm_expr_entry_freeze (GnumericExprEntry *gee)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	gee->freeze_count++;
}

void
gnm_expr_entry_thaw (GnumericExprEntry *gee)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	if (gee->freeze_count > 0 && (--gee->freeze_count) == 0)
		gee_rangesel_update_text (gee);
}

/**
 * gnm_expr_entry_set_flags:
 * @gee: a #GnumericExprEntry
 * @flags:      bitmap of flag values
 * @mask:       bitmap with ones for flags to be changed
 *
 * Changes the flags specified in @mask to values given in @flags.
 *
 * Flags (%FALSE by default, with exceptions given below):
 * %GNUM_EE_SINGLE_RANGE      Entry will only hold a single range.
 *                            %TRUE by default
 * %GNUM_EE_ABS_COL           Column reference must be absolute.
 * %GNUM_EE_ABS_ROW           Row reference must be absolute.
 * %GNUM_EE_FULL_COL          Range consists of full columns.
 * %GNUM_EE_FULL_ROW          Range consists of full rows.
 * %GNUM_EE_SHEET_OPTIONAL    Current sheet name not auto-added.
 **/
void
gnm_expr_entry_set_flags (GnumericExprEntry *gee,
			       GnumericExprEntryFlags flags,
			       GnumericExprEntryFlags mask)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	gee->flags = (gee->flags & ~mask) | (flags & mask);
	if (mask & GNUM_EE_ABS_COL)
		gee->rangesel.abs_col = (gee->flags & GNUM_EE_ABS_COL) != 0;
	if (mask & GNUM_EE_ABS_ROW)
		gee->rangesel.abs_row = (gee->flags & GNUM_EE_ABS_ROW) != 0;
}

/**
 * gnm_expr_entry_set_scg
 * @gee: a #GnumericExprEntry
 * @scg: a #SheetControlGUI
 *
 * Associates the entry with a SheetControlGUI. The entry widget
 * automatically removes the association when the SheetControlGUI is
 * destroyed.
 **/
void
gnm_expr_entry_set_scg (GnumericExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));
	g_return_if_fail (scg == NULL || IS_SHEET_CONTROL_GUI (scg));

	if ((gee->flags & GNUM_EE_SINGLE_RANGE) || scg != gee->scg)
		gee_rangesel_reset (gee);

	gee_detach_scg (gee);
	gee->scg = scg;
	if (scg) {
		g_object_weak_ref (G_OBJECT (gee->scg),
				   (GWeakNotify) cb_scg_destroy, gee);
		gee->target_sheet = sc_sheet (SHEET_CONTROL (scg));
	} else
		gee->target_sheet = NULL;
}

/**
 * gnm_expr_entry_load_from_text :
 * @gee :
 * @txt :
 */
void
gnm_expr_entry_load_from_text (GnumericExprEntry *gee, char const *txt)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	gee_rangesel_reset (gee);
	gtk_entry_set_text (gee->entry, txt);
}

/**
 * gnm_expr_entry_load_from_dep
 * @gee: a #GnumericExprEntry
 * @dep: A dependent
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_dep (GnumericExprEntry *gee, Dependent const *dep)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));
	g_return_if_fail (dep != NULL);
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (dep->expression != NULL) {
		ParsePos pp;
		char *text = gnm_expr_as_string (dep->expression,
			parse_pos_init_dep (&pp, dep));

		gee_rangesel_reset (gee);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);
		g_free (text);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_expr
 * @gee: a #GnumericExprEntry
 * @expr: An expression
 * @pp  : The parse position
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_expr (GnumericExprEntry *gee,
			       GnmExpr const *expr, ParsePos const *pp)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (expr != NULL) {
		char *text = gnm_expr_as_string (expr, pp);
		gee_rangesel_reset (gee);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);
		g_free (text);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_range
 * @gee: a #GnumericExprEntry
 * @r:          a #Range
 * @sheet:      a #sheet
 * @pos:        position
 *
 * Returns: true if displayed range is different from input range. false
 * otherwise.
 *
 * Sets the range selection and displays it in the entry text. If the widget
 * already contains a range selection, the new text replaces the
 * old. Otherwise, it is inserted at @pos.
 **/
gboolean
gnm_expr_entry_load_from_range (GnumericExprEntry *gee,
				Sheet *sheet, Range const *r)
{
	Rangesel *rs;
	gboolean needs_change = FALSE;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (r)
		needs_change =  (gee->flags & GNUM_EE_FULL_COL &&
 				 !range_is_full (r, TRUE)) ||
 				(gee->flags & GNUM_EE_FULL_ROW &&
 				 !range_is_full (r, FALSE));

	rs = &gee->rangesel;
	if (range_equal (r, &rs->range) && rs->sheet == sheet)
		return needs_change; /* FIXME ??? */

	if (r)
		rs->range = *r;
	else
		memset (&rs->range, 0, sizeof (Range));

	rs->sheet = sheet;

	if (gee->freeze_count == 0)
		gee_rangesel_update_text (gee);

	return needs_change;
}

/**
 * gnm_expr_entry_get_rangesel
 * @gee: a #GnumericExprEntry
 * @r:          address to receive #Range
 * @sheet:      address to receive #sheet
 *
 * Get the range selection. Range is copied, Sheet is not. If sheet
 * argument is NULL, the corresponding value is not returned.
 * Returns TRUE if the returned range is indeed valid.
 **/
gboolean
gnm_expr_entry_get_rangesel (GnumericExprEntry *gee,
			     Range *r, Sheet **sheet)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	if (r)
		gee_make_display_range (gee, r);
	if (sheet)
		*sheet = gee->rangesel.sheet;

	return gee->rangesel.is_valid;
}

/**
 * gnm_expr_entry_set_absolute
 * @gee:   a #GnumericExprEntry
 *
 * Select absolute reference mode for rows and columns. Do not change
 * displayed text. This is a convenience function which wraps
 * gnm_expr_entry_set_flags.
 **/
void
gnm_expr_entry_set_absolute (GnumericExprEntry *gee)
{
	GnumericExprEntryFlags flags;

	flags = GNUM_EE_ABS_ROW | GNUM_EE_ABS_COL;
	gnm_expr_entry_set_flags (gee, flags, flags);
}


#warning  FIXME: this is definitely not utf8 clean

static gboolean
split_char_p (unsigned char c)
{
	switch (c) {
	case ',': case '=':
	case '(': case '<': case '>':
	case '+': case '-': case '*': case '/':
	case '^': case '&': case '%': case '!':
		return TRUE;
	default :
		return FALSE;
	}
}

/**
 * gnm_expr_entry_can_rangesel
 * @gee:   a #GnumericExprEntry
 *
 * Returns TRUE if a range selection is meaningful at current position.
 * eg it isn't at '=sum', or 'bob', but it is at '=sum('.
 *
 * NOTE:
 * Removed ')' and ':' from the set of characters where a range selection
 * may start. This is to fix bug report 54828:
 * "1) Start Gnumeric
 *  2) Navigate to cell C5
 *  3) Enter something like "=SUOM(A1:A10)"
 *  4) Try to move backwards with the left arrow key (which normally takes
 *     you backwards through the text you entered)
 *  for some reason you start navigating on the sheet with the
 *  rangesel cursor. I think it's sensible to start rangesel mode if we had
 * typed "=SUOM(", But in this case there is no reason at all to jump into
 * rangesel mode because the expression is closed/finished."
 * 2000-05-22 Jon Kåre Hellan <hellan@acm.org>
 **/
gboolean
gnm_expr_entry_can_rangesel (GnumericExprEntry *gee)
{
	int cursor_pos;
	char const *text;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);

	if (wbcg_edit_has_guru (gee->wbcg) != NULL && gee == gee->wbcg->edit_line.entry)
		return FALSE;

	text = gtk_entry_get_text (gee->entry);

	/* We need to be editing an expression */
	if (wbcg_edit_has_guru (gee->wbcg) == NULL &&
	    gnumeric_char_start_expr_p (text) == NULL)
		return FALSE;

	gnm_expr_entry_rangesel_start (gee);
	if (gee->rangesel.is_valid)
		return TRUE;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (gee->entry));
	return (cursor_pos <= 0) || split_char_p (text [cursor_pos-1]);
}

/**
 * gnm_expr_entry_parse :
 * @gee : the entry
 * @pp : a parse position
 * @start_sel : start range selection when things change.
 *
 * Attempts to parse the content of the entry line honouring
 * the flags.
 */
GnmExpr const *
gnm_expr_entry_parse (GnumericExprEntry *gee, ParsePos const *pp,
		      ParseError *perr, gboolean start_sel)
{
	char const *text;
	char *str;
	int flags;
	GnmExpr const *expr;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);

	text = gtk_entry_get_text (gee->entry);

	if (text == NULL || text[0] == '\0')
		return NULL;

	flags = GNM_EXPR_PARSE_DEFAULT;
	if (gee->flags & GNUM_EE_ABS_COL)
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES;
	if (gee->flags & GNUM_EE_ABS_ROW)
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES;
	if (!(gee->flags & GNUM_EE_SHEET_OPTIONAL))
		flags |= GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	expr = gnm_expr_parse_str (text, pp, flags, perr);
	if (expr == NULL)
		return NULL;

	if (gee->flags & GNUM_EE_SINGLE_RANGE) {
		Value *range = gnm_expr_get_range (expr) ;
		if (range == NULL) {
			if (perr != NULL) {
				perr->id         = PERR_SINGLE_RANGE;
				perr->message    = g_strdup (_("Expecting a single range"));
				perr->begin_char = perr->end_char   = 0;
			}
			gnm_expr_unref (expr);
			return NULL;
		}
		value_release (range);
	}

	/* Reset the entry in case something changed */
	str = gnm_expr_as_string (expr, pp);
	if (strcmp (str, text)) {
		SheetControlGUI *scg = wbcg_cur_scg (gee->wbcg);
		if (start_sel && sc_sheet (SHEET_CONTROL (scg)) == gee->rangesel.sheet) {
			Range const *r = &gee->rangesel.range;
			scg_rangesel_bound (scg,
				r->start.col, r->start.row,
				r->end.col, r->end.row);
		} else
			gtk_entry_set_text (gee->entry, str);
	}
	g_free (str);

	return expr;
}

/**
 * gnm_expr_entry_get_text
 * @ee :
 *
 * A small convenience routine.  Think long and hard before using this.
 * There are lots of parse routines that serve the common case.
 */
char const *
gnm_expr_entry_get_text	(GnumericExprEntry const *gee)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);
	return gtk_entry_get_text (gee->entry);
}

/**
 * gnm_expr_entry_parse_as_value :
 *
 * @gee: GnumericExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (Value *) of type VALUE_CELLRANGE if the @range was
 *	succesfully parsed or NULL on failure.
 */
Value *
gnm_expr_entry_parse_as_value (GnumericExprEntry *gee, Sheet *sheet)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);

	return global_range_parse (sheet,
		gtk_entry_get_text (gnm_expr_entry_get_entry (gee)));
}

/**
 * gnm_expr_entry_parse_as_list:
 *
 * @gee: GnumericExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (GSList *)
 *	or NULL on failure.
 */
GSList *
gnm_expr_entry_parse_as_list (GnumericExprEntry *gee, Sheet *sheet)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);

	return global_range_list_parse (sheet,
		gtk_entry_get_text (gnm_expr_entry_get_entry (gee)));
}

GtkEntry *
gnm_expr_entry_get_entry (GnumericExprEntry *gee)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);

	return gee->entry;
}

gboolean
gnm_expr_entry_is_cell_ref (GnumericExprEntry *gee, Sheet *sheet,
			    gboolean allow_multiple_cell)
{
        Value *val;
	gboolean res;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);

	val = gnm_expr_entry_parse_as_value (gee, sheet);
        if (val == NULL)
		return FALSE;

	res = ((val->type == VALUE_CELLRANGE) &&
	       (allow_multiple_cell ||
		((val->v_range.cell.a.col == val->v_range.cell.b.col) &&
		 (val->v_range.cell.a.row == val->v_range.cell.b.row))));
	value_release (val);
	return res;

}

gboolean
gnm_expr_entry_is_blank	(GnumericExprEntry *gee)
{
	GtkEntry *entry = gnm_expr_entry_get_entry (gee);
	char const *text = gtk_entry_get_text (entry);
	char *new_text;
	int len;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), FALSE);

	if (text == NULL)
		return TRUE;

	new_text = g_strdup (text);
	len = strlen (g_strstrip(new_text));
	g_free (new_text);

	return (len == 0);
}

char *
gnm_expr_entry_global_range_name (GnumericExprEntry *gee, Sheet *sheet)
{
	Value *val;
	char *text = NULL;

	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), NULL);

	val = gnm_expr_entry_parse_as_value (gee, sheet);
	if (val != NULL) {
		if (val->type == VALUE_CELLRANGE)
			text = value_get_as_string (val);
		value_release (val);
	}

	return text;
}

void
gnm_expr_entry_grab_focus (GnumericExprEntry *gee, gboolean select_all)
{
	g_return_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee));

	gtk_widget_grab_focus (GTK_WIDGET (gee->entry));
	if (select_all) {
		gtk_entry_set_position (gee->entry, 0);
		gtk_entry_select_region (gee->entry, 0, gee->entry->text_length);
	}
}

gboolean
gnm_expr_entry_editing_canceled (GnumericExprEntry *gee)
{
	g_return_val_if_fail (IS_GNUMERIC_EXPR_ENTRY (gee), TRUE);

	return gee->editing_canceled;
}
