/*
 * dialog-validate.c : implementation of the data validation dialog.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#include <gnome.h>

#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"

#define GLADE_FILE "validate.glade"

typedef struct {
	WorkbookControlGUI *wbcg;
	Sheet              *sheet;
	
	GnomeDialog     *dialog;

	GtkOptionMenu   *set_constraint_type;
	GtkLabel        *set_operator_label;
	GtkOptionMenu   *set_operator;
	GtkLabel        *set_bound1_name;
	GtkLabel        *set_bound2_name;
	GtkEntry        *set_bound1_entry;
	GtkEntry        *set_bound2_entry;
	GtkCheckButton  *set_apply_shared;
	GtkCheckButton  *set_ignore_blank;
	GtkCheckButton  *set_in_dropdown;

	GtkToggleButton *input_flag;
	GtkEntry        *input_title;
	GtkText         *input_msg;
	
	GtkToggleButton *error_flag;
	GtkOptionMenu   *error_action;
	GtkEntry        *error_title;
	GtkText         *error_msg;
	GnomePixmap     *error_image;
} ValidateState;


static void
cb_set_constraint_type_deactivate (GtkMenuShell *shell, ValidateState *state)
{
	gboolean flag = (gnumeric_option_menu_get_selected_index (state->set_constraint_type) != 0);
	
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_operator), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_bound1_entry), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_bound2_entry), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_apply_shared), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_ignore_blank), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_in_dropdown), flag);

	gtk_widget_set_sensitive (GTK_WIDGET (state->set_operator_label), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_bound1_name), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->set_bound2_name), flag);
}

static void
cb_set_operator_deactivate (GtkMenuShell *shell, ValidateState *state)
{
	int index = gnumeric_option_menu_get_selected_index (state->set_operator);
	
	if (index > 1) {
		gtk_widget_hide (GTK_WIDGET (state->set_bound2_name));
		gtk_widget_hide (GTK_WIDGET (state->set_bound2_entry));
	} else {
		gtk_widget_show (GTK_WIDGET (state->set_bound2_name));
		gtk_widget_show (GTK_WIDGET (state->set_bound2_entry));
	}

	switch (index) {
	case 0 : /* Fall through */
	case 1 : {
		gtk_label_set_text (state->set_bound1_name, _("Minimum :"));
		gtk_label_set_text (state->set_bound2_name, _("Maximum :"));
	} break;
	case 2 : /* Fall through */
	case 3 : {
		gtk_label_set_text (state->set_bound1_name, _("Value :"));
	} break;
	case 4 : /* Fall through */
	case 6 : {
		gtk_label_set_text (state->set_bound1_name, _("Minimum :"));
	} break;
	case 5 : /* Fall through */
	case 7 : {
		gtk_label_set_text (state->set_bound1_name, _("Maximum :"));
	} break;
	}
}

static void
cb_error_action_deactivate (GtkMenuShell *shell, ValidateState *state)
{
	int   index = gnumeric_option_menu_get_selected_index (state->error_action);
	char *s     = NULL;

	switch (index) {
	case 0 :
		s = gnome_pixmap_file("gnome-error.png");
		break;
	case 1 :
		s = gnome_pixmap_file("gnome-warning.png");
		break;
	case 2 :
		s = gnome_pixmap_file("gnome-info.png");
		break;
	}
	
	if (s != NULL) {
		gnome_pixmap_load_file (state->error_image, s);
		g_free (s);
	}
}

static void
cb_input_flag_toggled (GtkToggleButton *button, ValidateState *state)
{
	gboolean flag = gtk_toggle_button_get_active (button);
	
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->input_msg), flag);
}

static void
cb_error_flag_toggled (GtkToggleButton *button, ValidateState *state)
{
	gboolean flag = gtk_toggle_button_get_active (button);
	
	gtk_widget_set_sensitive (GTK_WIDGET (state->error_action), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->error_title), flag);
	gtk_widget_set_sensitive (GTK_WIDGET (state->error_msg), flag);
}

static void
setup_widgets (ValidateState *state, GladeXML *gui)
{
	g_return_if_fail (state != NULL);
	g_return_if_fail (gui != NULL);

	state->dialog              = GNOME_DIALOG     (glade_xml_get_widget (gui, "dialog"));

	state->set_constraint_type = GTK_OPTION_MENU   (glade_xml_get_widget (gui, "set_constraint_type"));
	state->set_operator_label  = GTK_LABEL         (glade_xml_get_widget (gui, "set_operator_label"));
	state->set_operator        = GTK_OPTION_MENU   (glade_xml_get_widget (gui, "set_operator"));
	state->set_bound1_name     = GTK_LABEL         (glade_xml_get_widget (gui, "set_bound1_name"));
	state->set_bound2_name     = GTK_LABEL         (glade_xml_get_widget (gui, "set_bound2_name"));
	state->set_bound1_entry    = GTK_ENTRY         (glade_xml_get_widget (gui, "set_bound1_entry"));
	state->set_bound2_entry    = GTK_ENTRY         (glade_xml_get_widget (gui, "set_bound2_entry"));
	state->set_apply_shared    = GTK_CHECK_BUTTON  (glade_xml_get_widget (gui, "set_apply_shared"));
	state->set_ignore_blank    = GTK_CHECK_BUTTON  (glade_xml_get_widget (gui, "set_ignore_blank"));
	state->set_in_dropdown     = GTK_CHECK_BUTTON  (glade_xml_get_widget (gui, "set_in_dropdown"));

	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->set_bound1_entry));
	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->set_bound2_entry));

	state->input_flag          = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "input_flag"));
	state->input_title         = GTK_ENTRY         (glade_xml_get_widget (gui, "input_title"));
	state->input_msg           = GTK_TEXT          (glade_xml_get_widget (gui, "input_msg"));

	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->input_title));
	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->input_msg));

	state->error_flag          = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "error_flag"));
	state->error_action        = GTK_OPTION_MENU   (glade_xml_get_widget (gui, "error_action"));
	state->error_title         = GTK_ENTRY         (glade_xml_get_widget (gui, "error_title"));
	state->error_msg           = GTK_TEXT          (glade_xml_get_widget (gui, "error_msg"));
	state->error_image         = GNOME_PIXMAP      (glade_xml_get_widget (gui, "error_image"));
	
	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->error_title));
	gnome_dialog_editable_enters (state->dialog, GTK_EDITABLE (state->error_msg));
}

static void
connect_signals (ValidateState *state)
{
	gtk_signal_connect (GTK_OBJECT (gtk_option_menu_get_menu (state->set_constraint_type)), "deactivate",
			    GTK_SIGNAL_FUNC (cb_set_constraint_type_deactivate), state);
	gtk_signal_connect (GTK_OBJECT (gtk_option_menu_get_menu (state->set_operator)), "deactivate",
			    GTK_SIGNAL_FUNC (cb_set_operator_deactivate), state);
	gtk_signal_connect (GTK_OBJECT (gtk_option_menu_get_menu (state->error_action)), "deactivate",
			    GTK_SIGNAL_FUNC (cb_error_action_deactivate), state);

	gtk_signal_connect (GTK_OBJECT (state->input_flag), "toggled",
			    GTK_SIGNAL_FUNC (cb_input_flag_toggled), state);
	gtk_signal_connect (GTK_OBJECT (state->error_flag), "toggled",
			    GTK_SIGNAL_FUNC (cb_error_flag_toggled), state);
}

void
dialog_validate (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML *gui;
	ValidateState *state;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);
	
	gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	state = g_new0 (ValidateState, 1);
	state->wbcg  = wbcg;
	state->sheet = sheet;
	
	setup_widgets (state, gui);
	connect_signals (state);

	/* Initialize */
	cb_set_constraint_type_deactivate (GTK_MENU_SHELL (gtk_option_menu_get_menu (state->set_constraint_type)), state);
	cb_set_operator_deactivate (GTK_MENU_SHELL (gtk_option_menu_get_menu (state->set_operator)), state);
	cb_error_action_deactivate (GTK_MENU_SHELL (gtk_option_menu_get_menu (state->error_action)), state);
	cb_input_flag_toggled (state->input_flag, state);
	cb_error_flag_toggled (state->error_flag, state);
	
	/* Run the dialog */
	gnumeric_dialog_run (wbcg, GNOME_DIALOG (state->dialog));
		
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
	gtk_object_unref (GTK_OBJECT (gui));
	g_free (state);
}

