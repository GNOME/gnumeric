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

typedef struct _scenario_state scenario_state_t;

typedef struct _GenericToolState GenericToolState;
typedef void (*state_destroy_t) (GenericToolState *state);

struct _GenericToolState {
	GladeXML  *gui;
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

void     tool_load_selection (GenericToolState *state, gboolean allow_multiple);
void     dialog_tool_init_buttons (GenericToolState *state,
				   GCallback ok_function, 
				   GCallback close_function);
void     error_in_entry (GenericToolState *state, GtkWidget *entry, char const *err_str);
gboolean dialog_tool_init (GenericToolState *state, 
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

#endif
