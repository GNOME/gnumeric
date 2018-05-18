/*
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
#ifndef GNUMERIC_TOOL_DIALOGS_H
#define GNUMERIC_TOOL_DIALOGS_H

#include <gnumeric.h>
#include <numbers.h>
#include <widgets/gnm-expr-entry.h>

typedef struct _scenario_state scenario_state_t;

typedef void (*state_destroy_t) (GnmGenericToolState *state);

struct _GenericToolState {
	GtkBuilder  *gui;
	GtkWidget *dialog;
	GnmExprEntry *input_entry;
	GnmExprEntry *input_entry_2;
	GtkWidget *gdao;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char const *help_link;
	Sheet	  *sheet;
	SheetView *sv;
	Workbook  *wb;
	WBCGtk  *wbcg;
	GtkWidget *warning_dialog;
	GtkWidget *warning;
	state_destroy_t state_destroy;
} ;

void     tool_load_selection (GnmGenericToolState *state, gboolean allow_multiple);
void     error_in_entry (GnmGenericToolState *state, GtkWidget *entry, char const *err_str);
gboolean dialog_tool_init (GnmGenericToolState *state,
			   WBCGtk *wbcg,
			   Sheet *sheet,
			   char const *help_file,
			   char const *gui_name,
			   char const *dialog_name,
			   char const *error_str,
			   char const *key,
			   GCallback ok_function,
			   GCallback close_function,
			   GCallback sensitivity_cb,
			   GnmExprEntryFlags flags);

GtkWidget *tool_setup_update (GnmGenericToolState* state,
			      char const *name,
			      GCallback cb,
			      gpointer closure);

#endif
