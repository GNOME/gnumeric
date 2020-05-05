/*
 * dialog-sheetobject-size.c:
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@pyrshep.ca>
 *
 * (c) Copyright 2009 Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <string.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <sheet-view.h>
#include <application.h>
#include <workbook-cmd-format.h>
#include <sheet-object-widget.h>
#include <sheet-object-impl.h>
#include <sheet-control-gui.h>
#include <widgets/gnm-so-anchor-mode-chooser.h>


#define SO_SIZE_DIALOG_KEY "so-size-dialog"

typedef struct {
	GtkBuilder         *gui;
	WBCGtk *wbcg;
	Sheet              *sheet;
	SheetView	   *sv;
	SheetControlGUI    *scg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *apply_button;
	GtkWidget          *cancel_button;
	GtkWidget          *wpoints;
	GtkSpinButton      *wspin;
	GtkWidget          *hpoints;
	GtkSpinButton      *hspin;
	GtkWidget          *xpoints;
	GtkSpinButton      *xspin;
	GtkWidget          *ypoints;
	GtkSpinButton      *yspin;
	GtkEntry           *nameentry;
	GtkWidget          *print_check;
	GnmSOAnchorModeChooser *modecombo;

	SheetObject        *so;
	SheetObjectAnchor  *old_anchor;
	SheetObjectAnchor  *active_anchor;
	double              coords[4];
	gchar              *old_name;
	gboolean            so_size_needs_restore;
	gboolean            so_pos_needs_restore;
	gboolean            so_name_changed;
	gboolean            so_print_check_changed;
	gboolean            so_mode_changed;
} SOSizeState;

static void
cb_dialog_so_size_value_changed_update_points (GtkSpinButton *spinbutton,
					       GtkLabel *points)
{
	gint value = gtk_spin_button_get_value_as_int (spinbutton);
	double size_points = value *  72./gnm_app_display_dpi_get (FALSE);
	gchar *pts = g_strdup_printf ("%.2f",size_points);
	gtk_label_set_text (points, pts);
	g_free (pts);
}

static void
dialog_so_size_button_sensitivity (SOSizeState *state)
{
	gboolean sensitive = state->so_size_needs_restore ||
		state->so_pos_needs_restore ||
		state->so_name_changed ||
		state->so_print_check_changed ||
		state->so_mode_changed;
	gtk_widget_set_sensitive
		(state->ok_button, sensitive);
	gtk_widget_set_sensitive
		(state->apply_button, sensitive);
}

static void
cb_dialog_so_size_destroy (SOSizeState *state)
{
	if (state->so_size_needs_restore || state->so_pos_needs_restore)
		sheet_object_set_anchor	(state->so, state->old_anchor);
	g_free (state->old_anchor);
	g_free (state->active_anchor);
	g_free (state->old_name);
	if (state->so!= NULL)
		g_object_unref (state->so);
	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_free (state);
}

static void
cb_dialog_so_size_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				    SOSizeState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_so_size_value_changed (G_GNUC_UNUSED GtkSpinButton *spinbutton,
				   SOSizeState *state)
{
	int width, height;
	int new_width, new_height;
	int dx, dy;

	width = state->coords[2] - state->coords[0];
	height = state->coords[3] - state->coords[1];
	if (width < 0) width = - width;
	if (height < 0) height = - height;

	new_width = gtk_spin_button_get_value_as_int (state->wspin);
	new_height = gtk_spin_button_get_value_as_int (state->hspin);
	dx =  gtk_spin_button_get_value_as_int (state->xspin);
	dy =  gtk_spin_button_get_value_as_int (state->yspin);

	state->so_size_needs_restore = (new_width != width) || (new_height != height);
	state->so_pos_needs_restore = (dx != 0) || (dy != 0);

	*(state->active_anchor) = *(state->old_anchor);

	if (state->so_size_needs_restore || state->so_pos_needs_restore) {
		gdouble new_coords[4];

		new_coords[0] = state->coords[0] + dx;
		new_coords[1] = state->coords[1] + dy;
		new_coords[2] = state->coords[2] + dx;
		new_coords[3] = state->coords[3] + dy;
		if (new_coords[0] < new_coords[2])
			new_coords[2] = new_coords[0] + new_width;
		else
			new_coords[0] = new_coords[2] + new_width;
		if (new_coords[1] < new_coords[3])
			new_coords[3] = new_coords[1] + new_height;
		else
			new_coords[1] = new_coords[3] + new_height;

		scg_object_coords_to_anchor (state->scg, new_coords,
					     state->active_anchor);
	}

	sheet_object_set_anchor	(state->so, state->active_anchor);

	dialog_so_size_button_sensitivity (state);
}

static void
dialog_so_size_load (SOSizeState *state)
{
	g_free (state->old_anchor);
	state->old_anchor = sheet_object_anchor_dup
		(sheet_object_get_anchor (state->so));
	scg_object_anchor_to_coords (state->scg,
				     state->old_anchor,
				     state->coords);
	state->so_size_needs_restore = FALSE;
	state->so_pos_needs_restore = FALSE;
}

static GOUndo *
set_params (SheetObject *so, char *name)
{
	return go_undo_binary_new
		(g_object_ref (so), name,
		 (GOUndoBinaryFunc)sheet_object_set_name,
		 g_object_unref, g_free);
}

static GOUndo *
set_print_flag (SheetObject *so, gboolean print)
{
	gboolean *p_print = g_new (gboolean, 1);

	*p_print = print;
	return go_undo_binary_new
		(g_object_ref (so), p_print,
		 (GOUndoBinaryFunc)sheet_object_set_print_flag,
		 g_object_unref, g_free);
}

static GOUndo *
set_mode (SheetObject *so, GnmSOAnchorMode mode)
{
	GnmSOAnchorMode *p_mode = g_new (GnmSOAnchorMode, 1);

	*p_mode = mode;
	return go_undo_binary_new
		(g_object_ref (so), p_mode,
		 (GOUndoBinaryFunc)sheet_object_set_anchor_mode,
		 g_object_unref, g_free);
}

static void
cb_dialog_so_size_apply_clicked (G_GNUC_UNUSED GtkWidget *button,
				   SOSizeState *state)
{
	char const *name;
	GOUndo *undo = NULL, *redo = NULL;
	char const *undo_name = NULL;
	int cnt = 0;

	if (state->so_size_needs_restore || state->so_pos_needs_restore) {
		char const *label = state->so_pos_needs_restore ?
			_("Move Object") : _("Resize Object");
		sheet_object_set_anchor	(state->so, state->old_anchor);
		if (!cmd_objects_move (GNM_WBC (state->wbcg),
				       g_slist_prepend (NULL, state->so),
				       g_slist_prepend
				       (NULL, sheet_object_anchor_dup
					(state->active_anchor)),
				       FALSE, label))
			dialog_so_size_load (state);
	}

	name = gtk_entry_get_text (state->nameentry);
	if (name == NULL)
		name = "";
	if (strcmp (name, state->old_name) != 0) {
		char *old_name, *new_name;

		g_object_get (G_OBJECT (state->so), "name", &old_name, NULL);
		undo = go_undo_combine (undo, set_params (state->so, old_name));

		new_name = (*name == '\0') ? NULL : g_strdup (name);
		redo = go_undo_combine (redo, set_params (state->so, new_name));

		undo_name = _("Set Object Name");
		cnt++;
	}
	if (state->so_print_check_changed) {
		gboolean val = sheet_object_get_print_flag (state->so);
		undo = go_undo_combine (undo, set_print_flag
					(state->so,  val));
		redo = go_undo_combine (redo, set_print_flag
					(state->so, !val));
		undo_name =  _("Set Object Print Property");
		cnt++;
	}
	if (state->so_mode_changed) {
		int new_mode = gnm_so_anchor_mode_chooser_get_mode (state->modecombo);
		int old_mode = state->so->anchor.mode;
		undo = go_undo_combine (undo, set_mode (state->so, old_mode));
		redo = go_undo_combine (redo, set_mode (state->so, new_mode));
		undo_name = _("Set Object Anchor Mode");
		cnt++;
	}

	if (cnt > 0) {
		if (cnt > 1)
			undo_name =  _("Set Object Properties");
		state->so_name_changed = state->so_print_check_changed = state->so_mode_changed =
			cmd_generic (GNM_WBC (state->wbcg),
				     undo_name, undo, redo);
	}
	dialog_so_size_button_sensitivity (state);

	return;
}

static void
cb_dialog_so_size_ok_clicked (GtkWidget *button, SOSizeState *state)
{
	cb_dialog_so_size_apply_clicked (button, state);
	if (!state->so_size_needs_restore && !state->so_pos_needs_restore &&
	    !state->so_name_changed && !state->so_print_check_changed)
		gtk_widget_destroy (state->dialog);
	return;
}

static gboolean
cb_dialog_so_size_name_changed (GtkEntry *entry,
				GdkEventFocus *event,
				SOSizeState *state)
{
	char const *name = gtk_entry_get_text (entry);
	if (name == NULL)
		name = "";
	state->so_name_changed
		= (strcmp (name, state->old_name) != 0);
	dialog_so_size_button_sensitivity (state);
	return FALSE;
}

static void
cb_dialog_so_size_print_check_toggled (GtkToggleButton *togglebutton,
				       SOSizeState *state)
{
	gboolean new_print = !gtk_toggle_button_get_active (togglebutton);
	gboolean old_print = sheet_object_get_print_flag (state->so);

	state->so_print_check_changed
		= (new_print != old_print);
	dialog_so_size_button_sensitivity (state);
	return;
}

static void
cb_dialog_so_size_mode_changed (GnmSOAnchorModeChooser *chooser, SOSizeState *state)
{
	GnmSOAnchorMode new_mode = gnm_so_anchor_mode_chooser_get_mode (chooser);
	GnmSOAnchorMode old_mode = state->so->anchor.mode;
	double coords[4];

	scg_object_anchor_to_coords (state->scg, state->active_anchor, coords);
	state->active_anchor->mode = new_mode;
	scg_object_coords_to_anchor (state->scg, coords, state->active_anchor);
	state->so_mode_changed = new_mode != old_mode;
	dialog_so_size_button_sensitivity (state);
}

void
dialog_so_size (WBCGtk *wbcg, GObject *so)
{
	GtkBuilder *gui;
	SOSizeState *state;
	GtkGrid *grid;
	int width, height;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, SO_SIZE_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/sheetobject-size.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (SOSizeState, 1);
	state->wbcg  = wbcg;
	state->sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->scg = wbcg_get_nth_scg (wbcg, state->sheet->index_in_wb);
	state->gui    = gui;
	state->dialog = go_gtk_builder_get_widget (state->gui, "object-size");

	state->so = GNM_SO (so);
	g_object_ref (so);

	state->nameentry = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "name-entry"));
	state->old_anchor = NULL;
	state->old_name = NULL;
	g_object_get (so, "name", &state->old_name, NULL);
	if (state->old_name == NULL)
		state->old_name = g_strdup ("");
	gtk_entry_set_text (state->nameentry, state->old_name);
	state->so_name_changed = FALSE;
	g_signal_connect (G_OBJECT (state->nameentry),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_so_size_name_changed),
			  state);
	state->so_print_check_changed = FALSE;

	state->wpoints = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "w-pts-label"));
	state->wspin  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "w-spin"));
	state->hpoints = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "h-pts-label"));
	state->hspin  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "h-spin"));
	state->xpoints = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "x-pts-label"));
	state->xspin  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "x-spin"));
	state->ypoints = GTK_WIDGET (go_gtk_builder_get_widget (state->gui, "y-pts-label"));
	state->yspin  = GTK_SPIN_BUTTON (go_gtk_builder_get_widget (state->gui, "y-spin"));
	state->print_check = GTK_WIDGET (go_gtk_builder_get_widget (state->gui,
							       "print-check"));
	state->modecombo = GNM_SO_ANCHOR_MODE_CHOOSER (gnm_so_anchor_mode_chooser_new (sheet_object_can_resize (state->so)));
	dialog_so_size_load (state);
	state->active_anchor = sheet_object_anchor_dup (sheet_object_get_anchor
							(state->so));
	width = state->coords[2] - state->coords[0];
	height = state->coords[3] - state->coords[1];

	gtk_spin_button_set_value (state->wspin, (width < 0) ? - width : width);
	gtk_spin_button_set_value (state->hspin, (height < 0) ? - height : height);
	gtk_spin_button_set_value (state->xspin, 0.);
	gtk_spin_button_set_value (state->yspin, 0.);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->print_check),
				      !sheet_object_get_print_flag (state->so));
	gnm_so_anchor_mode_chooser_set_mode (state->modecombo,
	                                     state->so->anchor.mode);
	grid = GTK_GRID (gtk_builder_get_object (state->gui, "main-grid"));
	gtk_grid_insert_row (grid, 7);
	gtk_grid_attach (grid, GTK_WIDGET (state->modecombo), 0, 7, 2, 1);
	gtk_widget_set_halign (GTK_WIDGET (state->modecombo), GTK_ALIGN_START);
	gtk_widget_show (GTK_WIDGET (state->modecombo));
	g_signal_connect (G_OBJECT (state->wspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->wpoints);
	g_signal_connect (G_OBJECT (state->hspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->hpoints);
	g_signal_connect (G_OBJECT (state->xspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->xpoints);
	g_signal_connect (G_OBJECT (state->yspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->ypoints);
	g_signal_connect (G_OBJECT (state->print_check),
			  "toggled",
			  G_CALLBACK (cb_dialog_so_size_print_check_toggled),
			  state);

	cb_dialog_so_size_value_changed_update_points (state->wspin, GTK_LABEL (state->wpoints));
	cb_dialog_so_size_value_changed_update_points (state->hspin, GTK_LABEL (state->hpoints));
	cb_dialog_so_size_value_changed_update_points (state->xspin, GTK_LABEL (state->xpoints));
	cb_dialog_so_size_value_changed_update_points (state->yspin, GTK_LABEL (state->ypoints));


	g_signal_connect (G_OBJECT (state->wspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);
	g_signal_connect (G_OBJECT (state->hspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);
	g_signal_connect (G_OBJECT (state->xspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);
	g_signal_connect (G_OBJECT (state->yspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);

	g_signal_connect (G_OBJECT (state->modecombo),
	    "changed",
	    G_CALLBACK (cb_dialog_so_size_mode_changed), state);

		state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_ok_clicked), state);
	state->apply_button = go_gtk_builder_get_widget (state->gui, "apply_button");
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_apply_clicked), state);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_cancel_clicked), state);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_SIZE);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_so_size_destroy);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       SO_SIZE_DIALOG_KEY);
	dialog_so_size_button_sensitivity (state);
	gtk_widget_show (state->dialog);
}
