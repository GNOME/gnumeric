/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef GNUMERIC_TOOL_DIALOGS_H
#define GNUMERIC_TOOL_DIALOGS_H

#include "gnumeric.h"
#include "numbers.h"
#include "widgets/gnumeric-expr-entry.h"
#include <glade/glade-xml.h>

typedef struct {
	GtkWidget *show_button;
	GtkWidget *delete_button;
	GtkWidget *summary_button;

        GtkWidget *scenarios_treeview;
	GSList    *new_report_sheets;
	GList     *old_values;
} scenario_state_t;

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *input_entry;
	GnumericExprEntry *input_entry_2;
	GnumericExprEntry *output_entry;
        GtkWidget *clear_outputrange_button;
        GtkWidget *retain_format_button;
        GtkWidget *retain_comments_button;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *help_button;
	char const *help_link;
	char const *input_var1_str;
	char const *input_var2_str;
	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	Sheet	  *sheet;
	SheetView *sv;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	GtkAccelGroup *accel;
	GtkWidget *warning_dialog;
	GtkWidget *warning;

        scenario_state_t *scenario_state;
        GtkWidget *name_entry;
} GenericToolState;

void     tool_load_selection (GenericToolState *state, gboolean allow_multiple);
gboolean tool_destroy (GtkObject *w, GenericToolState  *state);
void     dialog_tool_init_buttons (GenericToolState *state,
				   GCallback ok_function, 
				   GCallback close_function);
void     error_in_entry (GenericToolState *state, GtkWidget *entry, const char *err_str);
gboolean dialog_tool_init (GenericToolState *state, 
			   WorkbookControlGUI *wbcg,
			   Sheet *sheet,
			   char const *help_file,
			   char const *gui_name,
			   char const *dialog_name,
			   char const *input_var1_str,
			   char const *input_var2_str,
			   char const *error_str,
			   char const *key,
			   GtkSignalFunc ok_function, 
			   GtkSignalFunc close_function, 
			   GtkSignalFunc sensitivity_cb,
			   GnumericExprEntryFlags flags);

#endif
