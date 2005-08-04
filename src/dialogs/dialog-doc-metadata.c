/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-doc-metadata.c: Edit document metadata
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <workbook.h>
#include <workbook-control.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <value.h>

#include <glade/glade.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcellrenderertext.h>
#include <string.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>

#define DOC_METADATA_KEY "dialog-doc-metadata"

//typedef struct dialog_doc_meta_data{
typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;

	/*pointer to the document metadata*/
	GsfDocMetaData *metadata;

	WorkbookControlGUI	*wbcg;
	Workbook                *wb;
} DialogDocMetaData;

/*Convenience functions--helpful for breaking up the work into more manageable
 * pieces.
 */
void fetchfrom_page_1(DialogDocMetaData *state);
void fetchfrom_page_2(DialogDocMetaData *state);
void fetchfrom_page_3(DialogDocMetaData *state);
void fetchfrom_page_4(DialogDocMetaData *state);
void fetchfrom_page_5(DialogDocMetaData *state);
void populate_page_1(DialogDocMetaData *state);
void populate_page_2(DialogDocMetaData *state);
void populate_page_3(DialogDocMetaData *state);
void populate_page_4(DialogDocMetaData *state);
void populate_page_5(DialogDocMetaData *state);



static void
dialog_doc_metadata_free (DialogDocMetaData *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
	wbcg_edit_detach_guru (state->wbcg);
	/*If state->gui is NULL, we are done with it already.
	 *Else, we need to fetch final values from the dialog and free it again
	 */
	if (state->gui != NULL) {
		fetchfrom_page_1(state);
		fetchfrom_page_2(state);
		fetchfrom_page_3(state);
		fetchfrom_page_4(state);
		fetchfrom_page_5(state);
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	g_free (state);
}

static gboolean
dialog_doc_metadata_init (DialogDocMetaData *state, WorkbookControlGUI *wbcg)
{
	state->wbcg  = wbcg;
	state->wb = wb_control_workbook(WORKBOOK_CONTROL(wbcg));
	/*Get the workbook's metadata*/
	/*This could be NULL; if so, create it!*/
	if((state->metadata = g_object_get_data(G_OBJECT(state->wb), "GsfDocMetaData")) == NULL) {
		/*Yep.  Doesn't exist yet.  Create it.*/
		if((state->metadata = gsf_doc_meta_data_new()) == NULL) {
			/*Do something panicky!*/
			return TRUE;
		}
		/*Woot.  It exists!  Tell the workbook about it; this is a pointer, so we
		 * just need to give the workbook the new pointer*/
		g_object_set_data(G_OBJECT(state->wb), "GsfDocMetaData", G_OBJECT(state->metadata));
	}
	/*Now get the gui in*/
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"doc-meta-data.glade", NULL, NULL);
        if (state->gui == NULL)
                return TRUE;

	/*We populate the signal handlers*/
	/*We populate initial values.
	 *This has been split into pages, for easier maintenance.
	 */
	populate_page_1(state);
	populate_page_2(state);
	populate_page_3(state);
	populate_page_4(state);
	populate_page_5(state);

	state->dialog = glade_xml_get_widget (state->gui, "MetadataDialog");

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_DEFINE_NAMES);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DOC_METADATA_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)dialog_doc_metadata_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	


	return FALSE;
}

void
dialog_doc_metadata_new (WorkbookControlGUI *wbcg)
{
	DialogDocMetaData *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, DOC_METADATA_KEY))
		return;

	state = g_new0 (DialogDocMetaData, 1);
	if (dialog_doc_metadata_init (state, wbcg)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Name Guru."));
		g_free (state);
		return;
	}
}

/*Signal handlers for the different features*/

/*These two synchronize between the two Document Title parts*/
/*Sync it back to the Description page*/
static void
sync_general_title(GtkCellEditable *cell_editable, gpointer userdata) {
	gtk_entry_set_text(GTK_ENTRY(glade_xml_get_widget(((DialogDocMetaData*)userdata)->gui, "title")), gtk_entry_get_text(GTK_ENTRY(glade_xml_get_widget(((DialogDocMetaData*)userdata)->gui, "general_title"))));
}
/*Sync it back to the General page*/
static void
sync_description_title(GtkCellEditable *cell_editable, gpointer userdata) {
	gtk_entry_set_text(GTK_ENTRY(glade_xml_get_widget(((DialogDocMetaData*)userdata)->gui, "general_title")), gtk_entry_get_text(GTK_ENTRY(glade_xml_get_widget(((DialogDocMetaData*)userdata)->gui, "title"))));
}


/*The convenience functions*/
/*Fetchors!*/
void fetchfrom_page_1(DialogDocMetaData *state) {
}
void fetchfrom_page_2(DialogDocMetaData *state) {
}
void fetchfrom_page_3(DialogDocMetaData *state) {
}
void fetchfrom_page_4(DialogDocMetaData *state) {
}
void fetchfrom_page_5(DialogDocMetaData *state) {
}
/*Populators!*/
/*NOTE: populate_page_1 also sets handlers for the cancel and OK buttons*/
void populate_page_1(DialogDocMetaData *state) {
	/*Set up the labels*/
	gtk_label_set_text(GTK_LABEL(glade_xml_get_widget(state->gui, "location")), "Unknown");
	gtk_label_set_text(GTK_LABEL(glade_xml_get_widget(state->gui, "created")), "Unknown");
	gtk_label_set_text(GTK_LABEL(glade_xml_get_widget(state->gui, "modified")), "Unknown");
	gtk_label_set_text(GTK_LABEL(glade_xml_get_widget(state->gui, "accessed")), "Unknown");
	/*Set up the handlers*/
	glade_xml_signal_connect_data(state->gui, "on_general_title_editing_done", G_CALLBACK (*sync_general_title), (gpointer)state);
	glade_xml_signal_connect_data(state->gui, "on_title_editing_done", G_CALLBACK (*sync_description_title), (gpointer)state);
}
void populate_page_2(DialogDocMetaData *state) {
}
void populate_page_3(DialogDocMetaData *state) {
}
void populate_page_4(DialogDocMetaData *state) {
}
void populate_page_5(DialogDocMetaData *state) {
}

