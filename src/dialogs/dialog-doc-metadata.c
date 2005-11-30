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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
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
#include <gtk/gtktreeview.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcellrenderertext.h>
#include <string.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>

#include <goffice/utils/go-file.h>

#define DOC_METADATA_KEY "dialog-doc-metadata"

//typedef struct dialog_doc_meta_data{
typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;

	/*pointer to the document metadata*/
	GsfDocMetaData *metadata;

	WorkbookControlGUI	*wbcg;
	Workbook                *wb;

	/* Dialog Widgets */
	GtkNotebook		*notebook;
	GtkButton		*help_button;
	GtkButton		*close_button;

	/* File Information Page */
	GtkLabel		*file_name;
	GtkLabel		*location;
	GtkLabel		*created;
	GtkLabel		*modified;
	GtkLabel		*accessed;
	GtkLabel		*owner;
	GtkLabel		*group;

	GtkCheckButton 		*owner_read;
	GtkCheckButton		*owner_write;

	GtkCheckButton		*group_read;
	GtkCheckButton		*group_write;
	
	GtkCheckButton		*others_read;
	GtkCheckButton		*others_write;

	/* Description Page */
	GtkEntry		*title;
	GtkEntry		*subject;
	GtkEntry		*author;
	GtkEntry		*manager;
	GtkEntry		*company;
	GtkEntry		*category;
	GtkEntry		*keywords;

	GtkTextView		*comments;

	/* Properties Page */
	GtkTreeView		*properties;
	GtkTreeStore		*properties_store;

	GtkComboBox		*ppt_name;
	GtkEntry		*ppt_value;
	GtkEntry		*ppt_link;
	
	GtkButton		*add_button;
	GtkButton		*remove_button;
	GtkButton		*apply_button;

	/* Statistics Page */
	GtkLabel		*sheets;
	GtkLabel		*cells;
	GtkLabel		*pages;
	
} DialogDocMetaData;

/*Convenience functions--helpful for breaking up the work into more manageable
 * pieces.
 */
void fetchfrom_page_1 (DialogDocMetaData *state);
void fetchfrom_page_2 (DialogDocMetaData *state);
void fetchfrom_page_3 (DialogDocMetaData *state);
void fetchfrom_page_4 (DialogDocMetaData *state);

void populate_page_1 (DialogDocMetaData *state);
void populate_page_2 (DialogDocMetaData *state);
void populate_page_3 (DialogDocMetaData *state);
void populate_page_4 (DialogDocMetaData *state);

void dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (GtkEntry * entry, GsfDocMetaData * metadata, const char * prop_name);
void dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (GtkLabel * label, GsfDocMetaData * metadata, const char * prop_name);
void dialog_doc_metadata_set_label_text_with_gsf_int_prop_value (GtkLabel * label, GsfDocMetaData * metadata, const char * prop_name);
const GValue * dialog_doc_metadata_get_gsf_prop_value (GsfDocMetaData * metadata, const char * prop_name);
const char * dialog_doc_metadata_get_gsf_prop_value_as_str (GsfDocMetaData * metadata, const char * prop_name);
int dialog_doc_metadata_get_gsf_prop_value_as_int (GsfDocMetaData * metadata, const char * prop_name);

/*
 * Signal Handlers.
 */

static void cb_dialog_doc_metadata_owner_read_clicked (GtkWidget *w, DialogDocMetaData *state);
static void cb_dialog_doc_metadata_owner_write_clicked (GtkWidget *w, DialogDocMetaData *state);

static void cb_dialog_doc_metadata_group_read_clicked (GtkWidget *w, DialogDocMetaData *state);
static void cb_dialog_doc_metadata_group_write_clicked (GtkWidget *w, DialogDocMetaData *state);

static void cb_dialog_doc_metadata_others_read_clicked (GtkWidget *w, DialogDocMetaData *state);
static void cb_dialog_doc_metadata_others_write_clicked (GtkWidget *w, DialogDocMetaData *state);

static void cb_dialog_doc_metadata_close_clicked (GtkWidget *w, DialogDocMetaData *state);
static gboolean cb_dialog_doc_metadata_destroy (GtkObject *w, DialogDocMetaData *state);

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
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	g_free (state);
}

static void 
dialog_doc_metadata_init_widgets (DialogDocMetaData *state) 
{
	state->dialog = glade_xml_get_widget (state->gui, "GOMetadataDialog");

	state->notebook     = GTK_NOTEBOOK (glade_xml_get_widget (state->gui, "notebook"));
	state->help_button  = GTK_BUTTON (glade_xml_get_widget (state->gui, "help_button"));
	state->close_button = GTK_BUTTON (glade_xml_get_widget (state->gui, "close_button"));

	/* File Information Page */
	state->file_name = GTK_LABEL (glade_xml_get_widget (state->gui, "file_name"));
	state->location  = GTK_LABEL (glade_xml_get_widget (state->gui, "location"));
	state->created   = GTK_LABEL (glade_xml_get_widget (state->gui, "created"));
	state->modified  = GTK_LABEL (glade_xml_get_widget (state->gui, "modified"));
	state->accessed  = GTK_LABEL (glade_xml_get_widget (state->gui, "accessed"));
	state->owner     = GTK_LABEL (glade_xml_get_widget (state->gui, "owner"));
	state->group     = GTK_LABEL (glade_xml_get_widget (state->gui, "group"));

	state->owner_read  = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "owner_read"));
	state->owner_write = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "owner_write"));

	state->group_read  = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "group_read"));
	state->group_write = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "group_write"));
	
	state->others_read  = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "others_read"));
	state->others_write = GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "others_write"));

	/* Description Page */
	state->title    = GTK_ENTRY (glade_xml_get_widget (state->gui, "title"));
	state->subject  = GTK_ENTRY (glade_xml_get_widget (state->gui, "subject"));
	state->author   = GTK_ENTRY (glade_xml_get_widget (state->gui, "author"));
	state->manager  = GTK_ENTRY (glade_xml_get_widget (state->gui, "manager"));
	state->company  = GTK_ENTRY (glade_xml_get_widget (state->gui, "company"));
	state->category = GTK_ENTRY (glade_xml_get_widget (state->gui, "category"));
	state->keywords = GTK_ENTRY (glade_xml_get_widget (state->gui, "keywords"));

	state->comments = GTK_TEXT_VIEW (glade_xml_get_widget (state->gui, "comments"));

	/* Properties Page */
	state->properties = GTK_TREE_VIEW (glade_xml_get_widget (state->gui, "properties"));

	state->ppt_name  = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "name"));
	state->ppt_value = GTK_ENTRY (glade_xml_get_widget (state->gui, "value"));
	state->ppt_link  = GTK_ENTRY (glade_xml_get_widget (state->gui, "link"));
	
	state->add_button    = GTK_BUTTON (glade_xml_get_widget (state->gui, "add_button"));
	state->remove_button = GTK_BUTTON (glade_xml_get_widget (state->gui, "remove_button"));
	state->apply_button  = GTK_BUTTON (glade_xml_get_widget (state->gui, "apply_button"));

	/* Statistics Page */
	state->sheets = GTK_LABEL (glade_xml_get_widget (state->gui, "sheets"));
	state->cells  = GTK_LABEL (glade_xml_get_widget (state->gui, "cells"));
	state->pages  = GTK_LABEL (glade_xml_get_widget (state->gui, "pages"));
}

static gboolean
dialog_doc_metadata_init (DialogDocMetaData *state, WorkbookControlGUI *wbcg)
{
	state->wbcg = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL(wbcg));
	/*Get the workbook's metadata*/
	/*This could be NULL; if so, create it!*/
	if ((state->metadata = g_object_get_data (G_OBJECT (state->wb), "GsfDocMetaData")) == NULL) {
		/*Yep.  Doesn't exist yet.  Create it.*/
		if ((state->metadata = gsf_doc_meta_data_new ()) == NULL) {
			/*Do something panicky!*/
			return TRUE;
		}
		/*Woot.  It exists!  Tell the workbook about it; this is a pointer, so we
		 * just need to give the workbook the new pointer*/
		g_object_set_data (G_OBJECT (state->wb), "GsfDocMetaData", G_OBJECT (state->metadata));
	}

	/*Now get the gui in*/
	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"doc-meta-data.glade", NULL, NULL);

        if (state->gui == NULL)
                return TRUE;

	dialog_doc_metadata_init_widgets (state);

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_dialog_doc_metadata_destroy), state);
	/*We populate the signal handlers*/
	/*We populate initial values.
	 *This has been split into pages, for easier maintenance.
	 */
	populate_page_1 (state);
	populate_page_2 (state);
	populate_page_3 (state);
	populate_page_4 (state);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DOC_METADATA_KEY);
	
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) dialog_doc_metadata_free);
	
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

/*The convenience functions*/
/*Fetchors!*/
void fetchfrom_page_1 (DialogDocMetaData *state)
{
}

void fetchfrom_page_2 (DialogDocMetaData *state)
{
}

void fetchfrom_page_3 (DialogDocMetaData *state)
{
}

void fetchfrom_page_4 (DialogDocMetaData *state)
{
}

static void
cb_dialog_doc_metadata_owner_read_clicked (GtkWidget *w, DialogDocMetaData *state)
{
	GtkCheckButton * button  = GTK_CHECK_BUTTON (w); 

	g_return_if_fail(button != NULL);
	fprintf(stderr, "%s() - state = %d\n", __FUNCTION__, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_owner_write_clicked (GtkWidget *w, DialogDocMetaData *state)
{
}

static void
cb_dialog_doc_metadata_group_read_clicked (GtkWidget *w, DialogDocMetaData *state)
{
}

static void
cb_dialog_doc_metadata_group_write_clicked (GtkWidget *w, DialogDocMetaData *state)
{
}

static void
cb_dialog_doc_metadata_others_read_clicked (GtkWidget *w, DialogDocMetaData *state)
{
}

static void
cb_dialog_doc_metadata_others_write_clicked (GtkWidget *w, DialogDocMetaData *state)
{
}

static void 
cb_dialog_doc_metadata_close_clicked (GtkWidget *w, DialogDocMetaData *state)
{
	gtk_widget_destroy (state->dialog);
}

static gboolean
cb_dialog_doc_metadata_destroy (GtkObject *w, DialogDocMetaData *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	
	state->dialog = NULL;

	return FALSE;
}

/*Populators!*/
/*NOTE: populate_page_1 also sets handlers for the cancel and OK buttons*/
void populate_page_1 (DialogDocMetaData *state)
{
	Workbook *wb = state->wb;
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	gtk_label_set_text (state->file_name, go_basename_from_uri (workbook_get_uri (wb)));
	gtk_label_set_text (state->location, go_dirname_from_uri (workbook_get_uri (wb), FALSE));

	dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (state->created, state->metadata, GSF_META_NAME_DATE_CREATED);
	dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (state->modified, state->metadata, GSF_META_NAME_DATE_MODIFIED);
	
	/* GSF_META_NAME_DATE_ACCESSED does not exist. Create it? 
	 * dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (state->accessed, state->metadata, GSF_META_NAME_DATE_ACCESSED);
	 */

	/*
	 * dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (state->owner, state->metadata, const char * prop_name);
	 * dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (state->group, state->metadata, const char * prop_name);
	 */

	/* Set up check buttons */
	//dialog_doc_metadata_set_file_permissions();

	/* Set up signals */
	g_signal_connect (G_OBJECT (state->owner_read), "clicked", G_CALLBACK (cb_dialog_doc_metadata_owner_read_clicked), (gpointer) state);
	g_signal_connect (G_OBJECT (state->owner_write), "clicked", G_CALLBACK (cb_dialog_doc_metadata_owner_write_clicked), state);

	g_signal_connect (G_OBJECT (state->group_read), "clicked", G_CALLBACK (cb_dialog_doc_metadata_group_read_clicked), state);
	g_signal_connect (G_OBJECT (state->group_write), "clicked", G_CALLBACK (cb_dialog_doc_metadata_group_write_clicked), state);

	g_signal_connect (G_OBJECT (state->others_read), "clicked", G_CALLBACK (cb_dialog_doc_metadata_others_read_clicked), state);
	g_signal_connect (G_OBJECT (state->others_write), "clicked", G_CALLBACK (cb_dialog_doc_metadata_others_write_clicked), state);

	/* Help and Close buttons */
	gnumeric_init_help_button (GTK_WIDGET (state->help_button), GNUMERIC_HELP_LINK_SUMMARY);
	g_signal_connect (G_OBJECT (state->close_button), "clicked", G_CALLBACK (cb_dialog_doc_metadata_close_clicked), state);
}

void populate_page_2(DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->title, state->metadata, GSF_META_NAME_TITLE);
	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->subject, state->metadata, GSF_META_NAME_SUBJECT);

	/* GSF_META_NAME_AUTHOR does not exist. Create it? 
	 * dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->author, state->metadata, GSF_META_NAME_);
	 */
	
	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->manager, state->metadata, GSF_META_NAME_MANAGER);
	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->company, state->metadata, GSF_META_NAME_COMPANY);
	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->category, state->metadata, GSF_META_NAME_CATEGORY);
	dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (state->keywords, state->metadata, GSF_META_NAME_KEYWORDS);

	/* GSF_META_NAME_COMMENTS does not exist. Create it?
	 * {
	 * 	char * value;
	 * 	value = dialog_doc_metadata_get_gsf_prop_value_as_str (state->metadata, GSF_META_NAME_COMMENTS);
	 * 	if (value != NULL)
	 * 		gtk_text_buffer_set_text (gtk_text_view_get_buffer (state->comments), value, -1); 
	 * 	else
	 * 		gtk_text_buffer_set_text (gtk_text_view_get_buffer (state->comments), "", 0); 
	 * }
	 */
}

void populate_page_3(DialogDocMetaData *state)
{
}

void populate_page_4(DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	dialog_doc_metadata_set_label_text_with_gsf_int_prop_value (state->sheets, state->metadata, GSF_META_NAME_SPREADSHEET_COUNT);
	dialog_doc_metadata_set_label_text_with_gsf_int_prop_value (state->cells, state->metadata, GSF_META_NAME_CELL_COUNT);
	dialog_doc_metadata_set_label_text_with_gsf_int_prop_value (state->pages, state->metadata, GSF_META_NAME_PAGE_COUNT);
}

void
dialog_doc_metadata_set_entry_text_with_gsf_str_prop_value (GtkEntry * entry, GsfDocMetaData * metadata, const char * prop_name)
{
	const char * value = dialog_doc_metadata_get_gsf_prop_value_as_str (metadata, prop_name);

	if (value != NULL)
		gtk_entry_set_text (entry, value);
	else
		gtk_entry_set_text (entry, "");
}

void
dialog_doc_metadata_set_label_text_with_gsf_str_prop_value (GtkLabel * label, GsfDocMetaData * metadata, const char * prop_name)
{
	const char * value = dialog_doc_metadata_get_gsf_prop_value_as_str (metadata, prop_name);

	if (value != NULL)
		gtk_label_set_text (label, value);
	else
		gtk_label_set_text (label, "");
}

void
dialog_doc_metadata_set_label_text_with_gsf_int_prop_value (GtkLabel * label, GsfDocMetaData * metadata, const char * prop_name)
{
	gchar * str_value = g_new0 (gchar, 64);
	int int_value = dialog_doc_metadata_get_gsf_prop_value_as_int (metadata, prop_name);

	if (int_value != -1)
		g_sprintf (str_value, "%d", int_value);
	
	gtk_label_set_text (label, str_value);

	g_free (str_value);
}

const GValue * 
dialog_doc_metadata_get_gsf_prop_value (GsfDocMetaData * metadata, const char * prop_name)
{
	GsfDocProp * prop = gsf_doc_meta_data_lookup (metadata, prop_name);
	
	if (prop != NULL)
		return gsf_doc_prop_get_val (prop);

	return NULL;
}

const char * 
dialog_doc_metadata_get_gsf_prop_value_as_str (GsfDocMetaData * metadata, const char * prop_name)
{
	GValue * value = (GValue *) dialog_doc_metadata_get_gsf_prop_value (metadata, prop_name);

	if (value != NULL)
		return g_value_get_string (value);
	
	return NULL;
}

int 
dialog_doc_metadata_get_gsf_prop_value_as_int (GsfDocMetaData * metadata, const char * prop_name)
{
	GValue * value = (GValue *) dialog_doc_metadata_get_gsf_prop_value (metadata, prop_name);

	if (value != NULL)
		return g_value_get_int (value);
	
	return -1;
}

