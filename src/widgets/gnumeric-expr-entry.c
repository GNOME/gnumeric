/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges.
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "gnumeric-expr-entry.h"

#include <workbook-edit.h>
#include <workbook-control-gui-priv.h>
#include <sheet-control-gui-priv.h>
#include <sheet-merge.h>
#include <parse-util.h>
#include <gui-util.h>
#include <ranges.h>
#include <value.h>
#include <expr.h>
#include <dependent.h>
#include <sheet.h>
#include <sheet-view.h>
#include <selection.h>
#include <commands.h>
#include <gnm-marshalers.h>

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcelleditable.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>

typedef struct {
	int text_start;
	int text_end;
	RangeRef ref;
	gboolean  is_valid;
} Rangesel;

struct _GnmExprEntry {
	GtkHBox	parent;

	GtkEntry		*entry;
	GtkWidget		*icon;
	SheetControlGUI		*scg;	/* the source of the edit */
	Sheet			*sheet;	/* from scg */
	WorkbookControlGUI	*wbcg;	/* from scg */
	Rangesel		 rangesel;

	GnmExprEntryFlags	 flags;
	int			 freeze_count;

	GtkUpdateType		 update_policy;
	guint			 update_timeout_id;

	gboolean                 is_cell_renderer;  /* as cell_editable */
	gboolean                 editing_canceled;  /* as cell_editable */
	gboolean                 ignore_changes; /* internal mutex */
};

typedef struct _GnmExprEntryClass {
	GtkHBoxClass base;

	void (* update)   (GnmExprEntry *gee, gboolean user_requested_update);
	void (* changed)  (GnmExprEntry *gee);
	void (* activate) (GnmExprEntry *gee);
} GnmExprEntryClass;

/* Signals */
enum {
	UPDATE,
	CHANGED,
	ACTIVATE,
	LAST_SIGNAL
};

/* Properties */
enum {
	PROP_0,
	PROP_UPDATE_POLICY,
	PROP_WITH_ICON,
	PROP_SCG,
	PROP_WBCG
};

static GQuark signals [LAST_SIGNAL] = { 0 };

/* Internal routines
 */
static void     gee_rangesel_reset (GnmExprEntry *gee);
static void     gee_rangesel_update_text (GnmExprEntry *gee);
static void     gee_detach_scg (GnmExprEntry *gee);
static void     gee_remove_update_timer (GnmExprEntry *range);
static void     gee_notify_cursor_position (GObject *object, GParamSpec *pspec,
					    GnmExprEntry *gee);

static GtkObjectClass *parent_class = NULL;

static gboolean
split_char_p (unsigned char const *c)
{
	switch (*c) {
	case ',': case '=':
	case '(': case '<': case '>':
	case '+': case '-': case '*': case '/':
	case '^': case '&': case '%': case '!':
		return TRUE;
	default :
		return FALSE;
	}
}

static void
gee_rangesel_reset (GnmExprEntry *gee)
{
	Rangesel *rs = &gee->rangesel;

	rs->text_start = 0;
	rs->text_end = 0;
	memset (&rs->ref, 0, sizeof (Range));

	/* restore the default based on the flags */
	rs->ref.a.col_relative = rs->ref.b.col_relative = (gee->flags & GNM_EE_ABS_COL) != 0;
	rs->ref.a.row_relative = rs->ref.b.row_relative = (gee->flags & GNM_EE_ABS_ROW) != 0;

	gee->rangesel.is_valid = FALSE;
}

static void
gee_destroy (GtkObject *object)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (object);
	gee_remove_update_timer (gee);
	gee_detach_scg (gee);
	parent_class->destroy (object);
}

static void
gee_set_property (GObject      *object,
		  guint         prop_id,
		  GValue const *value,
		  GParamSpec   *pspec)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		gnm_expr_entry_set_update_policy (gee, g_value_get_enum (value));
		break;

	case PROP_WITH_ICON:
		if (g_value_get_boolean (value)) {
			if (gee->icon == NULL) {
				gee->icon = gtk_image_new_from_stock (
					"Gnumeric_ExprEntry", GTK_ICON_SIZE_BUTTON);
				gtk_box_pack_start (GTK_BOX (gee), gee->icon, FALSE, FALSE, 0);
				gtk_widget_show (gee->icon);
			}
		} else if (gee->icon != NULL)
			gtk_object_destroy (GTK_OBJECT (gee->icon));
		break;

	case PROP_SCG:
		gnm_expr_entry_set_scg (gee, 
			SHEET_CONTROL_GUI (g_value_get_object (value)));
		break;
	case PROP_WBCG:
		g_return_if_fail (gee->wbcg == NULL);
		gee->wbcg = WORKBOOK_CONTROL_GUI (g_value_get_object (value));
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
	GnmExprEntry *gee = GNM_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		g_value_set_enum (value, gee->update_policy);
		break;
	case PROP_WITH_ICON:
		g_value_set_boolean (value, gee->icon != NULL);
		break;
	case PROP_SCG:
		g_value_set_object (value, G_OBJECT (gee->scg));
		break;
	case PROP_WBCG:
		g_value_set_object (value, G_OBJECT (gee->wbcg));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
cb_entry_activate (G_GNUC_UNUSED GtkWidget *w, GnmExprEntry *gee)
{
	g_signal_emit (G_OBJECT (gee), signals [ACTIVATE], 0);
	gnm_expr_entry_signal_update (gee, TRUE);
}

static void
cb_entry_changed (G_GNUC_UNUSED GtkEntry *ignored,
		  GnmExprEntry *gee)
{
	if (!gee->ignore_changes) {
		if (!gee->is_cell_renderer &&
		    !gnm_expr_entry_can_rangesel (gee) &&
		    gee->scg != NULL)
			scg_rangesel_stop (gee->scg, FALSE);
	}

	g_signal_emit (G_OBJECT (gee), signals [CHANGED], 0);
}

static gboolean
cb_gee_key_press_event (GtkEntry	  *entry,
			GdkEventKey	  *event,
			GnmExprEntry *gee)
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
		gboolean abs_cols = (gee->flags & GNM_EE_ABS_COL);
		gboolean abs_rows = (gee->flags & GNM_EE_ABS_ROW);

		/* FIXME: since the range can't have changed we should just be able to */
		/*        look it up rather than reparse */

		/* Look for a range */
		if (rs->text_start >= rs->text_end)
			gnm_expr_expr_find_range (gee);

		/* no range found */
		if (rs->text_start >= rs->text_end)
			return TRUE;

		/* rows must be absolute */
		if (abs_rows) {
			if (abs_cols)
				return TRUE;
			rs->ref.b.col_relative = rs->ref.a.col_relative =
				!rs->ref.a.col_relative;
		} else if (abs_cols)
			rs->ref.b.row_relative = rs->ref.a.row_relative =
				!rs->ref.a.row_relative;
		else {
			/* It's late. I'm doing this the straightforward way. */
			rs->ref.b.row_relative = rs->ref.a.row_relative =
				(rs->ref.a.row_relative != rs->ref.a.col_relative);
			rs->ref.b.col_relative = rs->ref.a.col_relative =
				!rs->ref.a.col_relative;
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
			gtk_editable_set_position (GTK_EDITABLE (entry), pos);
			return TRUE;
		}

		/* Ctrl-enter is only applicable for the main entry */
		if (!wbcg_is_editing (wbcg))
			break;

		/* Be careful to use the editing sheet */
		sv = sheet_get_view (wbcg->wb_control.editing_sheet,
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
			sv_update (sv);
		}
		return TRUE;
	}
	default:
		break;
	}

	return FALSE;
}

static gboolean
cb_gee_button_press_event (G_GNUC_UNUSED GtkEntry *entry,
			   G_GNUC_UNUSED GdkEventButton *event,
			   GnmExprEntry *gee)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);

	if (gee->scg) {
		scg_rangesel_stop (gee->scg, FALSE);
		gnm_expr_expr_find_range (gee);
		g_signal_emit (G_OBJECT (gee), signals [CHANGED], 0);
	}

	return FALSE;
}

static gboolean
gee_mnemonic_activate (GtkWidget *w, gboolean group_cycling)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (w);
	gtk_widget_grab_focus (GTK_WIDGET (gee->entry));
	return TRUE;
}

static void
gee_init (GnmExprEntry *gee)
{
	gee->editing_canceled = FALSE;
	gee->is_cell_renderer = FALSE;
	gee->ignore_changes = FALSE;
	gee->flags = 0;
	gee->scg = NULL;
	gee->sheet = NULL;
	gee->wbcg = NULL;
	gee->freeze_count = 0;
	gee->update_timeout_id = 0;
	gee->update_policy = GTK_UPDATE_CONTINUOUS;
	gee_rangesel_reset (gee);

	gee->entry = GTK_ENTRY (gtk_entry_new ());
	g_signal_connect (G_OBJECT (gee->entry),
		"activate",
		G_CALLBACK (cb_entry_activate), gee);
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
	gtk_widget_show (GTK_WIDGET (gee->entry));
}

static void
gee_class_init (GObjectClass *gobject_class)
{
	GtkObjectClass *gtk_object_class = (GtkObjectClass *)gobject_class;
	GtkWidgetClass *widget_class = (GtkWidgetClass *)gobject_class;

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gee_set_property;
	gobject_class->get_property	= gee_get_property;
	gtk_object_class->destroy	= gee_destroy;
	widget_class->mnemonic_activate = gee_mnemonic_activate;

	signals [UPDATE] = g_signal_new ("update",
		GNM_EXPR_ENTRY_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmExprEntryClass, update),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__BOOLEAN,
		G_TYPE_NONE,
		1, G_TYPE_BOOLEAN);
	signals [CHANGED] = g_signal_new ("changed",
		GNM_EXPR_ENTRY_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmExprEntryClass, changed),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE, 0);
	signals[ACTIVATE] =
		g_signal_new ("activate",
		G_OBJECT_CLASS_TYPE (gobject_class),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (GnmExprEntryClass, activate),
		(GSignalAccumulator) NULL, NULL,
		gnm__VOID__VOID,
		G_TYPE_NONE, 0);

	g_object_class_install_property (gobject_class,
		PROP_UPDATE_POLICY,
		g_param_spec_enum ("update_policy", "Update policy",
			"How frequently changes to the entry should be applied",
			GTK_TYPE_UPDATE_TYPE, GTK_UPDATE_CONTINUOUS,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		PROP_WITH_ICON,
		g_param_spec_boolean ("with_icon", "With icon",
			"Should there be an icon to the right of the entry",
			TRUE,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		PROP_SCG,
		g_param_spec_object ("scg", "SheetControlGUI",
			"The GUI container associated with the entry.",
			SHEET_CONTROL_GUI_TYPE,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		PROP_WBCG,
		g_param_spec_object ("wbcg", "WorkbookControlGUI",
			"The toplevel GUI container associated with the entry.",
			WORKBOOK_CONTROL_GUI_TYPE,
			G_PARAM_READWRITE));
}

static void
cb_entry_activated (G_GNUC_UNUSED gpointer data,
		    GnmExprEntry *entry)
{
	gtk_cell_editable_editing_done (GTK_CELL_EDITABLE (entry));
}

static void
gee_start_editing (GtkCellEditable *cell_editable,
		   G_GNUC_UNUSED GdkEvent *event)
{
	GtkEntry *entry = gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (cell_editable));

	GNM_EXPR_ENTRY (cell_editable)->is_cell_renderer = TRUE;

	g_signal_connect (G_OBJECT (entry),
		"activate",
		G_CALLBACK (cb_entry_activated), cell_editable);

	gtk_widget_grab_focus (GTK_WIDGET (entry));
}

static void
gee_cell_editable_init (GtkCellEditableIface *iface)
{
	iface->start_editing = gee_start_editing;
}

GSF_CLASS_FULL (GnmExprEntry, gnm_expr_entry,
		gee_class_init, gee_init,
		GTK_TYPE_HBOX, 0,
		GSF_INTERFACE (gee_cell_editable_init, GTK_TYPE_CELL_EDITABLE))

/**
 * gee_prepare_range :
 * @gee :
 * @dst :
 *
 * Adjust @dst as necessary to conform to @gee's requirements
 **/
static void
gee_prepare_range (GnmExprEntry const *gee, RangeRef *dst)
{
	Rangesel const *rs = &gee->rangesel;

	*dst = rs->ref;

	if (gee->flags & GNM_EE_FULL_ROW) {
		dst->a.col = 0;
		dst->b.col = SHEET_MAX_COLS - 1;
	}
	if (gee->flags & GNM_EE_FULL_COL) {
		dst->a.row = 0;
		dst->b.row = SHEET_MAX_ROWS - 1;
	}

	/* special case a single merge to be only corner */
	if (!(gee->flags & (GNM_EE_FULL_ROW|GNM_EE_FULL_COL))) {
		Range const *merge;
		CellPos  corner;

		corner.col = MIN (dst->a.col, dst->b.col);
		corner.row = MIN (dst->a.row, dst->b.row);
		merge = sheet_merge_is_corner (gee->sheet, &corner);
		if (merge != NULL &&
		    merge->end.col == MAX (dst->a.col, dst->b.col) &&
		    merge->end.row == MAX (dst->a.row, dst->b.row)) {
			dst->a.col = dst->b.col;
			dst->a.row = dst->b.row;
		}
	}

	if (dst->a.sheet == NULL && !(gee->flags & GNM_EE_SHEET_OPTIONAL))
		dst->a.sheet = gee->sheet;
}

static char *
gee_rangesel_make_text (GnmExprEntry const *gee)
{
	RangeRef ref;
	ParsePos pp;
	GString *target = g_string_new (NULL);

	gee_prepare_range (gee, &ref);
	rangeref_as_string (target, gnm_expr_conventions_default,
			    &ref, parse_pos_init_sheet (&pp, gee->sheet));
	return g_string_free (target, FALSE);
}

static void
gee_rangesel_update_text (GnmExprEntry *gee)
{
	GtkEditable *editable = GTK_EDITABLE (gee->entry);
	Rangesel *rs = &gee->rangesel;
	int len;
	char *text = gee_rangesel_make_text (gee);

	g_return_if_fail (!gee->ignore_changes);

	gee->ignore_changes = TRUE;
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

	if (text != NULL) {
		/* Set the cursor at the end.  It looks nicer */
		len = strlen (text);
		gtk_editable_insert_text (editable, text, len, &rs->text_end);
		gtk_editable_set_position (editable, rs->text_end);
		g_free (text);
	}

	gee->ignore_changes = FALSE;
}

/**
 * gnm_expr_entry_rangesel_start
 * @gee:   a #GnmExprEntry
 *
 * Look at the current selection to see how much of it needs to be changed when
 * selecting a range.
 **/
void
gnm_expr_expr_find_range (GnmExprEntry *gee)
{
	gboolean  single;
	char const *text, *cursor, *tmp, *ptr;
	RangeRef  range;
	Rangesel *rs;
	ParsePos  pp;
	int len;

	g_return_if_fail (gee != NULL);

	rs     = &gee->rangesel;
	single = (gee->flags & (GNM_EE_SINGLE_RANGE != 0));

	text = gtk_entry_get_text (gee->entry);
	cursor = text + gtk_editable_get_position (GTK_EDITABLE (gee->entry));

	rs->ref.a.col_relative = rs->ref.b.col_relative = (gee->flags & GNM_EE_ABS_COL) == 0;
	rs->ref.a.row_relative = rs->ref.b.row_relative = (gee->flags & GNM_EE_ABS_ROW) == 0;
	rs->ref.a.sheet = rs->ref.b.sheet = NULL;
	rs->is_valid = FALSE;
	if (text == NULL)
		return;
	len = strlen (text);

	parse_pos_init_sheet (&pp, gee->sheet);
	ptr = gnm_expr_char_start_p (text);
	if (ptr == NULL)
		ptr = text;

	while (ptr != NULL && *ptr && ptr <= cursor) {
		tmp = rangeref_parse (&range, ptr, &pp);
		if (tmp != ptr) {
			if (tmp >= cursor) {
				rs->is_valid = TRUE;
				rs->ref = range;
				if (single) {
					rs->text_start = 0;
					rs->text_end = len;
				} else {
					rs->text_start = ptr - text;
					rs->text_end = tmp - text;
				}
				return;
			}
			ptr = tmp;
		} else if (*ptr == '\'' || *ptr == '\"') {
			char const quote = *ptr;
			ptr = g_utf8_next_char (ptr);
			for (; *ptr && *ptr != quote; ptr = g_utf8_next_char (ptr))
				if (*ptr == '\\' && ptr[1])
					ptr = g_utf8_next_char (ptr+1);
			if (*ptr == quote)
				ptr = g_utf8_next_char (ptr+1);

		} else {
			/* rangerefs cannot start in the middle of a sequence
			 * of alphanumerics */
			if (g_unichar_isalnum (g_utf8_get_char (ptr))) {
				do
					ptr = g_utf8_next_char (ptr);
				while (ptr <= cursor && g_unichar_isalnum (g_utf8_get_char (ptr)));
			} else
				ptr = g_utf8_next_char (ptr);
		}
	}

	if (single) {
		rs->text_start = 0;
		rs->text_end = len;
	} else {
		for (tmp = cursor; tmp > text; tmp = g_utf8_prev_char (tmp)) {
			gunichar c = g_utf8_get_char (tmp);
			if (!g_unichar_isalnum (c)) {
				tmp = g_utf8_next_char (tmp);
				break;
			}
		}

		rs->text_start = ((cursor < tmp) ? cursor : tmp) - text;
		for (tmp = cursor; tmp < (text + len); tmp = g_utf8_next_char (tmp)) {
			gunichar c = g_utf8_get_char (tmp);
			if (!g_unichar_isalnum (c))
				break;
		}
		rs->text_end = ((cursor < (text+len)) ? tmp : cursor) - text;
	}
}

/**
 * gnm_expr_entry_rangesel_stop
 * @gee:   a #GnmExprEntry
 * @clear_string: clear string flag
 *
 * Perform the appropriate action when a range selection has been completed.
 **/
void
gnm_expr_entry_rangesel_stop (GnmExprEntry *gee,
			      gboolean clear_string)
{
	Rangesel *rs;

	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	rs = &gee->rangesel;
	if (clear_string && rs->text_end > rs->text_start)
		gtk_editable_delete_text (GTK_EDITABLE (gee->entry),
					  rs->text_start, rs->text_end);

	if (!(gee->flags & GNM_EE_SINGLE_RANGE) || clear_string)
		gee_rangesel_reset (gee);
}

/***************************************************************************/

static void
cb_scg_destroy (GnmExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (scg == gee->scg);

	gee_rangesel_reset (gee);
	gee->scg = NULL;
	gee->sheet = NULL;
}

static void
gee_detach_scg (GnmExprEntry *gee)
{
	if (gee->scg != NULL) {
		g_object_weak_unref (G_OBJECT (gee->scg),
				     (GWeakNotify) cb_scg_destroy, gee);
		gee->scg = NULL;
		gee->sheet = NULL;
	}
}

/***************************************************************************/

typedef struct {
	GnmExprEntry *gee;
	gboolean user_requested;
} GEETimerClosure;

static gboolean
cb_gee_update_timeout (GEETimerClosure const *info)
{
	info->gee->update_timeout_id = 0;
	g_signal_emit (G_OBJECT (info->gee), signals [UPDATE], 0,
		       info->user_requested);
	return FALSE;
}

static void
gee_remove_update_timer (GnmExprEntry *gee)
{
	if (gee->update_timeout_id != 0) {
		g_source_remove (gee->update_timeout_id);
		gee->update_timeout_id = 0;
	}
}

static void
gee_reset_update_timer (GnmExprEntry *gee, gboolean user_requested)
{
	GEETimerClosure *dat = g_new (GEETimerClosure, 1);
	gee_remove_update_timer (gee);
	dat->gee = gee;
	dat->user_requested = user_requested;
	gee->update_timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT, 300,
		(GSourceFunc) cb_gee_update_timeout, dat, g_free);
}

/**
 * gnm_expr_entry_signal_update:
 * @gee :
 * @user_requested : is the update requested by the user (eg activation)
 *
 * Higher level operations know when they are logically complete and can notify
 * ExperEntry clients.  eg button up after a drag selection indicates a logical
 * end to the change and offers a good time to update.
 **/
void
gnm_expr_entry_signal_update (GnmExprEntry *gee, gboolean user_requested)
{
	gee_reset_update_timer (gee, user_requested);
}

static void
gee_notify_cursor_position (G_GNUC_UNUSED GObject *object,
			    G_GNUC_UNUSED GParamSpec *pspec,
			    GnmExprEntry *gee)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	if (gee->ignore_changes)
		return;
	if (!gnm_expr_entry_can_rangesel (gee))
		scg_rangesel_stop (gee->scg, FALSE);
}

/**
 * gnm_expr_entry_set_update_policy:
 * @gee: a #GnmExprEntry
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
gnm_expr_entry_set_update_policy (GnmExprEntry *gee,
				       GtkUpdateType  policy)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	if (gee->update_policy == policy)
		return;
	gee->update_policy = policy;
	g_object_notify (G_OBJECT (gee), "update_policy");
}

/**
 * gnm_expr_entry_new:
 *
 * Creates a new #GnmExprEntry, which is an entry widget with support
 * for range selections.
 * The entry is created with default flag settings which are suitable for use
 * in many dialogs, but see #gnm_expr_entry_set_flags.
 *
 * Return value: a new #GnmExprEntry.
 **/
GnmExprEntry *
gnm_expr_entry_new (WorkbookControlGUI *wbcg, gboolean with_icon)
{
	return g_object_new (GNM_EXPR_ENTRY_TYPE,
			     "scg",	 wbcg_cur_scg (wbcg),
			     "with_icon", with_icon,
			     NULL);
}

void
gnm_expr_entry_freeze (GnmExprEntry *gee)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	gee->freeze_count++;
}

void
gnm_expr_entry_thaw (GnmExprEntry *gee)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	if (gee->freeze_count > 0 && (--gee->freeze_count) == 0) {
		gee_rangesel_update_text (gee);
		switch (gee->update_policy) {
		case GTK_UPDATE_DELAYED :
			gee_reset_update_timer (gee, FALSE);
			break;

		default :
		case GTK_UPDATE_DISCONTINUOUS :
			if (gee->scg->rangesel.active)
				break;
		case GTK_UPDATE_CONTINUOUS:
			g_signal_emit (G_OBJECT (gee), signals [UPDATE], 0, FALSE);
		};
	}
}

/**
 * gnm_expr_entry_set_flags:
 * @gee: a #GnmExprEntry
 * @flags:      bitmap of flag values
 * @mask:       bitmap with ones for flags to be changed
 *
 * Changes the flags specified in @mask to values given in @flags.
 *
 * Flags (%FALSE by default):
 * %GNM_EE_SINGLE_RANGE      Entry will only hold a single range.
 * %GNM_EE_ABS_COL           Column reference must be absolute.
 * %GNM_EE_ABS_ROW           Row reference must be absolute.
 * %GNM_EE_FULL_COL          Range consists of full columns.
 * %GNM_EE_FULL_ROW          Range consists of full rows.
 * %GNM_EE_SHEET_OPTIONAL    Current sheet name not auto-added.
 **/
void
gnm_expr_entry_set_flags (GnmExprEntry *gee,
			  GnmExprEntryFlags flags,
			  GnmExprEntryFlags mask)
{
	Rangesel *rs;

	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	gee->flags = (gee->flags & ~mask) | (flags & mask);
	rs = &gee->rangesel;
	if (mask & GNM_EE_ABS_COL)
		rs->ref.a.col_relative = rs->ref.b.col_relative = (gee->flags & GNM_EE_ABS_COL) != 0;
	if (mask & GNM_EE_ABS_ROW)
		rs->ref.a.row_relative = rs->ref.b.row_relative = (gee->flags & GNM_EE_ABS_ROW) != 0;
}

/**
 * gnm_expr_entry_set_scg
 * @gee: a #GnmExprEntry
 * @scg: a #SheetControlGUI
 *
 * Associates the entry with a SheetControlGUI. The entry widget
 * automatically removes the association when the SheetControlGUI is
 * destroyed.
 **/
void
gnm_expr_entry_set_scg (GnmExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));
	g_return_if_fail (scg == NULL || IS_SHEET_CONTROL_GUI (scg));

	if ((gee->flags & GNM_EE_SINGLE_RANGE) || scg != gee->scg)
		gee_rangesel_reset (gee);

	gee_detach_scg (gee);
	gee->scg = scg;
	if (scg) {
		g_object_weak_ref (G_OBJECT (gee->scg),
				   (GWeakNotify) cb_scg_destroy, gee);
		gee->sheet = sc_sheet (SHEET_CONTROL (scg));
		gee->wbcg = scg_get_wbcg (gee->scg);
	} else
		gee->sheet = NULL;
#if 0
	g_warning ("Setting gee (%p)->sheet = %p", gee, gee->sheet);
#endif
}

/**
 * gnm_expr_entry_load_from_text :
 * @gee :
 * @txt :
 */
void
gnm_expr_entry_load_from_text (GnmExprEntry *gee, char const *txt)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	gee_rangesel_reset (gee);
	gtk_entry_set_text (gee->entry, txt);
}

/**
 * gnm_expr_entry_load_from_dep
 * @gee: a #GnmExprEntry
 * @dep: A dependent
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_dep (GnmExprEntry *gee, Dependent const *dep)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));
	g_return_if_fail (dep != NULL);
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (dep->expression != NULL) {
		ParsePos pp;
		char *text = gnm_expr_as_string (dep->expression,
			parse_pos_init_dep (&pp, dep), gnm_expr_conventions_default);

		gee_rangesel_reset (gee);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);
		g_free (text);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_expr
 * @gee: a #GnmExprEntry
 * @expr: An expression
 * @pp  : The parse position
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_expr (GnmExprEntry *gee,
			       GnmExpr const *expr, ParsePos const *pp)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (expr != NULL) {
		char *text = gnm_expr_as_string (expr, pp,
				gnm_expr_conventions_default);
		gee_rangesel_reset (gee);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);
		g_free (text);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_range
 * @gee: a #GnmExprEntry
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
gnm_expr_entry_load_from_range (GnmExprEntry *gee,
				Sheet *sheet, Range const *r)
{
	Rangesel *rs;
	gboolean needs_change = FALSE;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	needs_change =  (gee->flags & GNM_EE_FULL_COL &&
			 !range_is_full (r, TRUE)) ||
			(gee->flags & GNM_EE_FULL_ROW &&
			 !range_is_full (r, FALSE));

	rs = &gee->rangesel;
	if (rs->ref.a.col == r->start.col &&
	    rs->ref.b.col == r->end.col &&
	    rs->ref.a.row == r->start.row &&
	    rs->ref.b.row == r->end.row &&
	    rs->ref.a.sheet == sheet &&
	    (rs->ref.b.sheet == NULL || rs->ref.b.sheet == sheet))
		return needs_change; /* FIXME ??? */

	if (r != NULL) {
		rs->ref.a.col = r->start.col;
		rs->ref.b.col = r->end.col;
		rs->ref.a.row = r->start.row;
		rs->ref.b.row = r->end.row;
	} else
		rs->ref.a.col = rs->ref.b.col = rs->ref.a.row = rs->ref.b.row = 0;

	rs->ref.a.sheet =
		(sheet != gee->sheet || !(gee->flags & GNM_EE_SHEET_OPTIONAL)) ? sheet : NULL;
	rs->ref.b.sheet = NULL;

	if (gee->freeze_count == 0)
		gee_rangesel_update_text (gee);

	rs->is_valid = TRUE; /* we just loaded it up */

	return needs_change;
}

/**
 * gnm_expr_entry_get_rangesel
 * @gee: a #GnmExprEntry
 * @r:          address to receive #Range
 * @sheet:      address to receive #sheet
 *
 * Get the range selection. Range is copied, Sheet is not. If sheet
 * argument is NULL, the corresponding value is not returned.
 * Returns TRUE if the returned range is indeed valid.
 * The resulting range is normalized.
 **/
gboolean
gnm_expr_entry_get_rangesel (GnmExprEntry *gee,
			     Range *r, Sheet **sheet)
{
	RangeRef ref;
	Rangesel const *rs = &gee->rangesel;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);

	gee_prepare_range (gee, &ref);
	if (r != NULL) {
		/* normalize but don't bother with rel vs absolute conversions
		 * we always work relative to A1 internally so there is no
		 * difference
		 */
		if (ref.a.col < ref.b.col) {
			r->start.col = ref.a.col;
			r->end.col   = ref.b.col;
		} else {
			r->start.col = ref.b.col;
			r->end.col   = ref.a.col;
		}
		if (ref.a.row < ref.b.row) {
			r->start.row = ref.a.row;
			r->end.row   = ref.b.row;
		} else {
			r->start.row = ref.b.row;
			r->end.row   = ref.a.row;
		}
	}

	/* TODO : does not handle 3d, neither does this interface
	 * should probably scrap the interface in favour of returning a
	 * rangeref.
	 */
	if (sheet != NULL)
		*sheet = eval_sheet (rs->ref.a.sheet, gee->sheet);

	return rs->is_valid;
}

/**
 * gnm_expr_entry_set_absolute
 * @gee:   a #GnmExprEntry
 *
 * Select absolute reference mode for rows and columns. Do not change
 * displayed text. This is a convenience function which wraps
 * gnm_expr_entry_set_flags.
 **/
void
gnm_expr_entry_set_absolute (GnmExprEntry *gee)
{
	GnmExprEntryFlags flags;

	flags = GNM_EE_ABS_ROW | GNM_EE_ABS_COL;
	gnm_expr_entry_set_flags (gee, flags, flags);
}


/**
 * gnm_expr_entry_can_rangesel
 * @gee:   a #GnmExprEntry
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
gnm_expr_entry_can_rangesel (GnmExprEntry *gee)
{
	int cursor_pos;
	char const *text;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);

	if (wbcg_edit_has_guru (gee->wbcg) != NULL && gee == gee->wbcg->edit_line.entry)
		return FALSE;

	text = gtk_entry_get_text (gee->entry);

	/* We need to be editing an expression */
	if (wbcg_edit_has_guru (gee->wbcg) == NULL &&
	    gnm_expr_char_start_p (text) == NULL)
		return FALSE;

	gnm_expr_expr_find_range (gee);
	if (gee->rangesel.is_valid)
		return TRUE;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (gee->entry));
	return (cursor_pos <= 0) || split_char_p (text + cursor_pos - 1);
}

/**
 * gnm_expr_entry_parse :
 * @gee : the entry
 * @pp : a parse position
 * @start_sel : start range selection when things change.
 * @flags : 
 *
 * Attempts to parse the content of the entry line honouring
 * the flags.
 */
GnmExpr const *
gnm_expr_entry_parse (GnmExprEntry *gee, ParsePos const *pp,
		      ParseError *perr, gboolean start_sel,
		      GnmExprParseFlags flags)
{
	char const *text;
	char *str;
	GnmExpr const *expr;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);

	text = gtk_entry_get_text (gee->entry);

	if (text == NULL || text[0] == '\0')
		return NULL;

	if (gee->flags & GNM_EE_ABS_COL)
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_COL_REFERENCES;
	if (gee->flags & GNM_EE_ABS_ROW)
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_ROW_REFERENCES;
	if (!(gee->flags & GNM_EE_SHEET_OPTIONAL))
		flags |= GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	expr = gnm_expr_parse_str (text, pp, flags, gnm_expr_conventions_default, perr);
	if (expr == NULL)
		return NULL;

	if (gee->flags & GNM_EE_SINGLE_RANGE) {
		Value *range = gnm_expr_get_range (expr) ;
		if (range == NULL) {
			if (perr != NULL) {
				perr->err = g_error_new (1, PERR_SINGLE_RANGE,
					_("Expecting a single range"));
				perr->begin_char = perr->end_char   = 0;
			}
			gnm_expr_unref (expr);
			return NULL;
		}
		value_release (range);
	}

	/* Reset the entry in case something changed */
	str = gnm_expr_as_string (expr, pp, gnm_expr_conventions_default);
	if (strcmp (str, text)) {
		SheetControlGUI *scg = wbcg_cur_scg (gee->wbcg);
		Rangesel const *rs = &gee->rangesel;
		if (start_sel && sc_sheet (SHEET_CONTROL (scg)) == rs->ref.a.sheet) {
			scg_rangesel_bound (scg,
				rs->ref.a.col, rs->ref.a.row,
				rs->ref.b.col, rs->ref.b.row);
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
gnm_expr_entry_get_text	(GnmExprEntry const *gee)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);
	return gtk_entry_get_text (gee->entry);
}

/**
 * gnm_expr_entry_parse_as_value :
 *
 * @gee: GnmExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (Value *) of type VALUE_CELLRANGE if the @range was
 *	succesfully parsed or NULL on failure.
 */
Value *
gnm_expr_entry_parse_as_value (GnmExprEntry *gee, Sheet *sheet)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);

	return global_range_parse (sheet,
		gtk_entry_get_text (gnm_expr_entry_get_entry (gee)));
}

/**
 * gnm_expr_entry_parse_as_list:
 *
 * @gee: GnmExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns a (GSList *)
 *	or NULL on failure.
 */
GSList *
gnm_expr_entry_parse_as_list (GnmExprEntry *gee, Sheet *sheet)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);

	return global_range_list_parse (sheet,
		gtk_entry_get_text (gnm_expr_entry_get_entry (gee)));
}

GtkEntry *
gnm_expr_entry_get_entry (GnmExprEntry *gee)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);

	return gee->entry;
}

gboolean
gnm_expr_entry_is_cell_ref (GnmExprEntry *gee, Sheet *sheet,
			    gboolean allow_multiple_cell)
{
        Value *val;
	gboolean res;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);

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
gnm_expr_entry_is_blank	(GnmExprEntry *gee)
{
	GtkEntry *entry = gnm_expr_entry_get_entry (gee);
	char const *text = gtk_entry_get_text (entry);
	char *new_text;
	int len;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), FALSE);

	if (text == NULL)
		return TRUE;

	new_text = g_strdup (text);
	len = strlen (g_strstrip(new_text));
	g_free (new_text);

	return (len == 0);
}

char *
gnm_expr_entry_global_range_name (GnmExprEntry *gee, Sheet *sheet)
{
	Value *val;
	char *text = NULL;

	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), NULL);

	val = gnm_expr_entry_parse_as_value (gee, sheet);
	if (val != NULL) {
		if (val->type == VALUE_CELLRANGE)
			text = value_get_as_string (val);
		value_release (val);
	}

	return text;
}

void
gnm_expr_entry_grab_focus (GnmExprEntry *gee, gboolean select_all)
{
	g_return_if_fail (IS_GNM_EXPR_ENTRY (gee));

	gtk_widget_grab_focus (GTK_WIDGET (gee->entry));
	if (select_all) {
		gtk_entry_set_position (gee->entry, 0);
		gtk_entry_select_region (gee->entry, 0, gee->entry->text_length);
	}
}

gboolean
gnm_expr_entry_editing_canceled (GnmExprEntry *gee)
{
	g_return_val_if_fail (IS_GNM_EXPR_ENTRY (gee), TRUE);

	return gee->editing_canceled;
}
