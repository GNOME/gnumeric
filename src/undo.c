/*
 * undo.c:
 *
 * Authors:
 * Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>

#include <undo.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-control-gui.h>
#include <wbc-gtk.h>
#include <wbc-gtk-impl.h>
#include <colrow.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_undo_colrow_restore_state_group_parent_class;

static void
gnm_undo_colrow_restore_state_group_finalize (GObject *o)
{
	GnmUndoColrowRestoreStateGroup *ua = (GnmUndoColrowRestoreStateGroup *)o;

	colrow_state_group_destroy (ua->saved_state);
	ua->saved_state = NULL;
	colrow_index_list_destroy (ua->selection);
	ua->selection = NULL;

	G_OBJECT_CLASS (gnm_undo_colrow_restore_state_group_parent_class)->finalize (o);
}

static void
gnm_undo_colrow_restore_state_group_undo (GOUndo *u, G_GNUC_UNUSED gpointer data)
{
	GnmUndoColrowRestoreStateGroup *ua = (GnmUndoColrowRestoreStateGroup *)u;

	colrow_restore_state_group (ua->sheet, ua->is_cols, ua->selection, ua->saved_state);
}

static void
gnm_undo_colrow_restore_state_group_class_init (GObjectClass *gobject_class)
{
	GOUndoClass *uclass = (GOUndoClass *)gobject_class;

	gnm_undo_colrow_restore_state_group_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_undo_colrow_restore_state_group_finalize;
	uclass->undo = gnm_undo_colrow_restore_state_group_undo;
}


GSF_CLASS (GnmUndoColrowRestoreStateGroup, gnm_undo_colrow_restore_state_group,
	   gnm_undo_colrow_restore_state_group_class_init, NULL, GO_TYPE_UNDO)

/**
 * gnm_undo_colrow_restore_state_group_new:
 * @sheet:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @selection:
 * @saved_state:
 *
 * Returns: (transfer full): a new undo object.
 **/
GOUndo *
gnm_undo_colrow_restore_state_group_new (Sheet *sheet, gboolean is_cols,
					ColRowIndexList *selection,
					ColRowStateGroup *saved_state)
{
	GnmUndoColrowRestoreStateGroup *ua = g_object_new (GNM_TYPE_UNDO_COLROW_RESTORE_STATE_GROUP, NULL);

	ua->sheet = sheet;
	ua->is_cols = is_cols;
	ua->selection = selection;
	ua->saved_state = saved_state;

	return (GOUndo *)ua;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_undo_colrow_set_sizes_parent_class;

static void
gnm_undo_colrow_set_sizes_finalize (GObject *o)
{
	GnmUndoColrowSetSizes *ua = (GnmUndoColrowSetSizes *)o;

	colrow_index_list_destroy (ua->selection);
	ua->selection = NULL;

	G_OBJECT_CLASS (gnm_undo_colrow_set_sizes_parent_class)->finalize (o);
}

static void
gnm_undo_colrow_set_sizes_undo (GOUndo *u, G_GNUC_UNUSED gpointer data)
{
	GnmUndoColrowSetSizes *ua = (GnmUndoColrowSetSizes *)u;
	ColRowStateGroup *group;

	group = colrow_set_sizes (ua->sheet, ua->is_cols, ua->selection, ua->new_size,
				  ua->from, ua->to);
	colrow_state_group_destroy (group);
}

static void
gnm_undo_colrow_set_sizes_class_init (GObjectClass *gobject_class)
{
	GOUndoClass *uclass = (GOUndoClass *)gobject_class;

	gnm_undo_colrow_set_sizes_parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize = gnm_undo_colrow_set_sizes_finalize;
	uclass->undo = gnm_undo_colrow_set_sizes_undo;
}


GSF_CLASS (GnmUndoColrowSetSizes, gnm_undo_colrow_set_sizes,
	   gnm_undo_colrow_set_sizes_class_init, NULL, GO_TYPE_UNDO)

/**
 * gnm_undo_colrow_set_sizes_new:
 * @sheet:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @selection:
 * @new_size:
 * @r: (nullable):
 *
 * If r is non-null and new_size < 0, selection is ignored.
 *
 * Returns: (transfer full): a new undo object.
 **/

GOUndo *
gnm_undo_colrow_set_sizes_new (Sheet *sheet, gboolean is_cols,
			       ColRowIndexList *selection,
			       int new_size, GnmRange const *r)
{
	GnmUndoColrowSetSizes *ua;

	g_return_val_if_fail (selection != NULL || (r != NULL && new_size == -1), NULL);

	ua = g_object_new (GNM_TYPE_UNDO_COLROW_SET_SIZES, NULL);

	ua->sheet = sheet;
	ua->is_cols = is_cols;
	ua->new_size = new_size;

	if (r == NULL || new_size >= 0) {
		ua->selection = selection;
		ua->from = 0;
		ua->to = -1;
	} else {
		int first, last;

		if (is_cols) {
			first = r->start.col;
			last = r->end.col;
			ua->from =  r->start.row;
			ua->to = r->end.row;
		} else {
			first = r->start.row;
			last = r->end.row;
			ua->from =  r->start.col;
			ua->to = r->end.col;
		}
		ua->selection = colrow_get_index_list (first, last, NULL);
	}

	return (GOUndo *)ua;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_undo_filter_set_condition_parent_class;

static void
gnm_undo_filter_set_condition_finalize (GObject *o)
{
	GnmUndoFilterSetCondition *ua = (GnmUndoFilterSetCondition *)o;

	gnm_filter_condition_free (ua->cond);
	ua->cond = NULL;

	G_OBJECT_CLASS (gnm_undo_filter_set_condition_parent_class)->finalize (o);
}

static gboolean
cb_filter_set_condition_undo (GnmColRowIter const *iter, gint *count)
{
	if (iter->cri->visible)
		(*count)++;
	return FALSE;
}

static void
cb_filter_set_condition_undo_set_pb (SheetControl *control, char *text)
{
	SheetControlGUI *scg = (SheetControlGUI *) control;
	WBCGtk *wbcg = scg_wbcg (scg);
	if (wbcg != NULL)
		gtk_progress_bar_set_text
			(GTK_PROGRESS_BAR (wbcg->progress_bar), text);
}

static void
gnm_undo_filter_set_condition_undo (GOUndo *u, G_GNUC_UNUSED gpointer data)
{
	GnmUndoFilterSetCondition *ua = (GnmUndoFilterSetCondition *)u;
	gint count = 0;
	char const *format;
	char *text;

	gnm_filter_set_condition (ua->filter, ua->i,
				  gnm_filter_condition_dup (ua->cond), TRUE);
	sheet_update (ua->filter->sheet);

	sheet_colrow_foreach (ua->filter->sheet, FALSE,
			ua->filter->r.start.row + 1,
			ua->filter->r.end.row,
			(ColRowHandler) cb_filter_set_condition_undo,
			&count);
	if (ua->filter->r.end.row - ua->filter->r.start.row > 10) {
		/* xgettext: The first %d gives the number of rows that match. */
		/* The second %d gives the total number of rows. Assume that the */
		/* total number of rows is always large (>10). */
		/* Note that the english "matches" or "match" is the verb of this sentence! */
		/* There is no explicit noun associated with the second %d in english, the */
		/* meaning is really "%d rows of all %d rows match" */
		/* This is input to ngettext. */
		format = ngettext ("%d row of %d matches",
				   "%d rows of %d match",
				   count);
		text = g_strdup_printf (format, count,
					ua->filter->r.end.row -
					ua->filter->r.start.row);
	} else {
		/* xgettext: The %d gives the number of rows that match. */
		/* This is input to ngettext. */
		format = ngettext ("%d row matches",
				   "%d rows match",
				   count);
		text = g_strdup_printf (format, count);
	}

	SHEET_FOREACH_CONTROL (ua->filter->sheet, view, control, cb_filter_set_condition_undo_set_pb (control, text););

	g_free (text);
}

static void
gnm_undo_filter_set_condition_class_init (GObjectClass *gobject_class)
{
	GOUndoClass *uclass = (GOUndoClass *)gobject_class;

	gnm_undo_filter_set_condition_parent_class = g_type_class_peek_parent
		(gobject_class);

	gobject_class->finalize = gnm_undo_filter_set_condition_finalize;
	uclass->undo = gnm_undo_filter_set_condition_undo;
}


GSF_CLASS (GnmUndoFilterSetCondition, gnm_undo_filter_set_condition,
	   gnm_undo_filter_set_condition_class_init, NULL, GO_TYPE_UNDO)

/**
 * gnm_undo_filter_set_condition_new:
 * @filter:
 * @i:
 * @cond: (nullable):
 * @retrieve_from_filter:
 *
 * if (retrieve_from_filter), cond is ignored
 *
 * Returns: a new undo object.
 **/
GOUndo *
gnm_undo_filter_set_condition_new (GnmFilter *filter, unsigned i,
				   GnmFilterCondition *cond,
				   gboolean retrieve_from_filter)
{
	GnmUndoFilterSetCondition *ua;

	g_return_val_if_fail (filter != NULL, NULL);
	g_return_val_if_fail (i < filter->fields->len , NULL);

	ua = g_object_new (GNM_TYPE_UNDO_FILTER_SET_CONDITION, NULL);

	ua->filter = filter;
	ua->i = i;

	if (retrieve_from_filter)
		ua->cond = gnm_filter_condition_dup
			(gnm_filter_get_condition (filter, i));
	else
		ua->cond = cond;

	return (GOUndo *)ua;
}

/* ------------------------------------------------------------------------- */
