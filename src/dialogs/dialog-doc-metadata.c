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
#include <gtk/gtkcomboboxentry.h>
#include <gtk/gtktable.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkcellrenderertext.h>
#include <string.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-timestamp.h>
#include <gsf/gsf-docprop-vector.h>

#include <goffice/utils/go-file.h>

#define DOC_METADATA_KEY "dialog-doc-metadata"

typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;

	/*pointer to the document metadata*/
	GsfDocMetaData 		*metadata;

	gboolean 		permissions_changed;
	GOFilePermissions 	*file_permissions;

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

	GtkComboBoxEntry	*ppt_name;
	GtkListStore		*ppt_name_store;
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

/*
 * Convenience functions--helpful for breaking up the work into more manageable
 * pieces.
 */
static void fetchfrom_page_1 (DialogDocMetaData *state);
static void fetchfrom_page_2 (DialogDocMetaData *state);
static void fetchfrom_page_3 (DialogDocMetaData *state);
static void fetchfrom_page_4 (DialogDocMetaData *state);

static void populate_page_1 (DialogDocMetaData *state);
static void populate_page_2 (DialogDocMetaData *state);
static void populate_page_3 (DialogDocMetaData *state);
static void populate_page_4 (DialogDocMetaData *state);

void dialog_doc_metadata_set_gsf_prop_value			(GsfDocMetaData *metadata,
								 const char     *prop_name,
								 GValue         *value);

gchar * dialog_doc_metadata_get_prop_value_as_str		(const char     *prop_name,
								 GValue         *prop_value);

void dialog_doc_metadata_set_up_file_permissions	 	(DialogDocMetaData *state);

void dialog_doc_metadata_populate_tree_view			(gchar             *name,
								 GsfDocProp 	   *prop,
								 DialogDocMetaData *state);

void dialog_doc_metadata_append_property			(DialogDocMetaData *state,
								 const gchar       *name,
								 const gchar       *value,
								 const gchar       *link,
								 gboolean          activate_property);

void dialog_doc_metadata_update_property_value			(DialogDocMetaData *state,
								 const gchar       *property_name,
								 const gchar       *property_value);

void dialog_doc_metadata_set_label_text				(DialogDocMetaData *state,
								 GtkLabel          *label,
								 const char        *value,
								 gboolean          auto_fill);
/*
 * Signal Handlers.
 */
static void cb_dialog_doc_metadata_owner_read_clicked		(GtkWidget         *w, 
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_owner_write_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_group_read_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_group_write_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_others_read_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_others_write_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);

static void cb_dialog_doc_metadata_combo_property_selected	(GtkComboBox       *combo_box,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_tree_property_selected	(GtkTreeView       *tree_view,
								 DialogDocMetaData *state);

static void cb_dialog_doc_metadata_add_button_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_remove_button_clicked	(GtkWidget         *w,
								 DialogDocMetaData *state);
static void cb_dialog_doc_metadata_apply_button_clicked		(GtkWidget         *w,
								 DialogDocMetaData *state);

static void
dialog_doc_metadata_free (DialogDocMetaData *state) 
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
	wbcg_edit_detach_guru (state->wbcg);

	/*
	 * If state->gui is NULL, we are done with it already.
	 * Else, we need to fetch final values from the dialog and free it again
	 */
	if (state->gui != NULL) {
		fetchfrom_page_1 (state);
		fetchfrom_page_2 (state);
		fetchfrom_page_3 (state);
		fetchfrom_page_4 (state);

		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	if (state->file_permissions != NULL) {
		g_free (state->file_permissions);
		state->file_permissions = NULL;
	}

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);

	state->dialog = NULL;

	g_free (state);
	state = NULL;
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

	state->ppt_name  = GTK_COMBO_BOX_ENTRY (glade_xml_get_widget (state->gui, "ppt_name"));
	state->ppt_value = GTK_ENTRY (glade_xml_get_widget (state->gui, "ppt_value"));
	state->ppt_link  = GTK_ENTRY (glade_xml_get_widget (state->gui, "ppt_link"));
	
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
	state->wbcg     = wbcg;
	state->wb       = wb_control_workbook (WORKBOOK_CONTROL(wbcg));
	state->metadata = g_object_get_data (G_OBJECT (state->wb), "GsfDocMetaData");

	/* If workbook's metadata is NULL, create it! */
	if (state->metadata  == NULL) {
		state->metadata = gsf_doc_meta_data_new ();

		if (state->metadata == NULL) {
			/* Do something panicky! */
			return TRUE;
		}

		/* Set the metadata pointer in workbook */
		g_object_set_data (G_OBJECT (state->wb), "GsfDocMetaData", G_OBJECT (state->metadata));
	}

	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"doc-meta-data.glade", NULL, NULL);

        if (state->gui == NULL)
                return TRUE;

	dialog_doc_metadata_init_widgets (state);

	/* Populate the signal handlers and initial values. */
	/* IMPORTANT: OBEY THE ORDER 1 - 4 - 3 - 2 */
	populate_page_1 (state);
	populate_page_4 (state);
	populate_page_3 (state);
	populate_page_2 (state);

	/* A candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg,
			       GTK_WINDOW (state->dialog),
			       DOC_METADATA_KEY);
	
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state",
				state,
				(GDestroyNotify) dialog_doc_metadata_free);
	
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
		go_gtk_notice_dialog (wbcg_toplevel (wbcg),
				      GTK_MESSAGE_ERROR,
				      _("Could not create the Name Guru."));

		g_free (state);
		return;
	}
}

static void
fetchfrom_page_1 (DialogDocMetaData *state)
{
	if (state->file_permissions != NULL && 
	    state->permissions_changed == TRUE)
		go_set_file_permissions (workbook_get_uri (state->wb),
				         state->file_permissions);
}

static void
fetchfrom_page_2 (DialogDocMetaData *state)
{
}

static void
fetchfrom_page_3 (DialogDocMetaData *state)
{
}

static void
fetchfrom_page_4 (DialogDocMetaData *state)
{
}

static void
dialog_doc_metadata_change_file_permission (DialogDocMetaData *state,
					    gboolean	         *permission,
					    gboolean          value)
{
	g_return_if_fail (state->file_permissions != NULL);

	state->permissions_changed = TRUE;
	*permission = value;
}

static void
cb_dialog_doc_metadata_owner_read_clicked (GtkWidget         *w, 
					   DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->owner_read),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_owner_write_clicked (GtkWidget         *w,
					    DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->owner_write),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_group_read_clicked (GtkWidget         *w,
					   DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->group_read),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_group_write_clicked (GtkWidget         *w,
					    DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->group_write),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_others_read_clicked (GtkWidget         *w,
					    DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->others_read),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

static void
cb_dialog_doc_metadata_others_write_clicked (GtkWidget         *w,
					     DialogDocMetaData *state)
{
	dialog_doc_metadata_change_file_permission (state, 
						    &(state->file_permissions->others_write),
						    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));
}

void dialog_doc_metadata_set_up_file_permissions (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	state->file_permissions = go_get_file_permissions (workbook_get_uri (state->wb));

	if (state->file_permissions != NULL) {
		/* Set Check buttons */

		/* Owner */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->owner_read),
					      state->file_permissions->owner_read);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->owner_write),
					      state->file_permissions->owner_write);

		/* Group */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->group_read),
						state->file_permissions->group_read);
		
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->group_write),
					      state->file_permissions->group_write);

		/* Others */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->others_read),
					      state->file_permissions->others_read);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->others_write),
					      state->file_permissions->others_write);
	}

	/* At this moment we don't let user change file permissions */
	gtk_widget_set_sensitive (GTK_WIDGET (state->owner_read), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->owner_write), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->group_read), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->group_write), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->others_read), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->others_write), FALSE);
}

static void
populate_page_1 (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	dialog_doc_metadata_set_label_text (state, state->file_name, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->location, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->created, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->modified, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->accessed, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->owner, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->group, NULL, TRUE);

	/* Set up check buttons */
	state->permissions_changed = FALSE;
	dialog_doc_metadata_set_up_file_permissions (state);

	/* Signals */

	/* Owner */
	g_signal_connect (G_OBJECT (state->owner_read),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_owner_read_clicked),
			  state);

	g_signal_connect (G_OBJECT (state->owner_write),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_owner_write_clicked),
			  state);

	/* Group */
	g_signal_connect (G_OBJECT (state->group_read),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_group_read_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->group_write),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_group_write_clicked),
			  state);

	/* Others */
	g_signal_connect (G_OBJECT (state->others_read),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_others_read_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->others_write),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_others_write_clicked),
			  state);

	/* Help and Close buttons */
	gnumeric_init_help_button (GTK_WIDGET (state->help_button),
				   GNUMERIC_HELP_LINK_METADATA);

	g_signal_connect_swapped (G_OBJECT (state->close_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  state->dialog);
}

static void
dialog_doc_metadata_update_tree_property (DialogDocMetaData *state,
					  const gchar       *name,
					  const gchar       *value)
{
	GtkTreeIter tree_iter;
	GtkTreeIter list_iter;
	GValue      *property_name;
	gboolean    ret;
	gboolean    found;
	
	g_return_if_fail (state->metadata != NULL);

	found = FALSE;

	/* Search tree view for property */
	property_name = g_new0 (GValue, 1);

	ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (state->properties_store),
					     &tree_iter);

	while (ret == TRUE) {
		
		gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
					  &tree_iter,
					  0,
					  property_name);

		if (strcmp (name, g_value_get_string (property_name)) == 0) {
			/* Set new value */
			gtk_tree_store_set (state->properties_store, 
					    &tree_iter,
					    1, value,
					    -1);

			g_value_unset (property_name);

			/* Update entry value if necessary */
			ret = gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->ppt_name),
							     &list_iter);

			if (ret == TRUE) {
				gtk_tree_model_get_value (GTK_TREE_MODEL (state->ppt_name_store),
							  &list_iter,
							  0,
							  property_name);

				if (strcmp (name, g_value_get_string (property_name)) == 0)
					gtk_entry_set_text (state->ppt_value, value);

				g_value_unset (property_name);
			}

			found = TRUE;
			break;
		}

		g_value_unset (property_name);
		ret = gtk_tree_model_iter_next (GTK_TREE_MODEL (state->properties_store),
						&tree_iter);
	}

	/* If the property was not found create it */
	if (found == FALSE)
		dialog_doc_metadata_append_property (state, name, value, "", FALSE);

	/* Free all data */
	g_free (property_name);
}

static void
cb_dialog_doc_metadata_title_changed (GtkEntry          *entry,
				      DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_TITLE,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_subject_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_SUBJECT,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_author_changed (GtkEntry          *entry,
				       DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_INITIAL_CREATOR,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_manager_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_MANAGER,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_company_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_COMPANY,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_category_changed (GtkEntry          *entry,
					 DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_CATEGORY,
						  gtk_entry_get_text (entry));
}

static void
cb_dialog_doc_metadata_keywords_changed (GtkEntry          *entry,
					 DialogDocMetaData *state)
{
	dialog_doc_metadata_update_tree_property (state,
						  GSF_META_NAME_KEYWORDS,
						  gtk_entry_get_text (entry));
}

static void
populate_page_2 (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* At this point, the entry values were already filled */

	/* Set up the signals */
	g_signal_connect (G_OBJECT (state->title),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_title_changed),
			  state);

	g_signal_connect (G_OBJECT (state->subject),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_subject_changed),
			  state);

	g_signal_connect (G_OBJECT (state->author),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_author_changed),
			  state);

	g_signal_connect (G_OBJECT (state->manager),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_manager_changed),
			  state);

	g_signal_connect (G_OBJECT (state->company),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_company_changed),
			  state);

	g_signal_connect (G_OBJECT (state->category),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_category_changed),
			  state);

	g_signal_connect (G_OBJECT (state->keywords),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_keywords_changed),
			  state);
}

static void
cb_dialog_doc_metadata_add_button_clicked (GtkWidget         *w,
					   DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Create a new entry in Tree View and Combo Box */
	dialog_doc_metadata_append_property (state, "<Name>", "<Value>", "", TRUE);
}

void
dialog_doc_metadata_update_property_value (DialogDocMetaData *state,
					   const gchar       *property_name,
					   const gchar       *property_value)
{
	/* Labels */
	if (strcmp (property_name, GSF_META_NAME_DATE_CREATED) == 0) {
		dialog_doc_metadata_set_label_text (state,
						    state->created,
						    property_value,
						    TRUE);
	}
	else if (strcmp (property_name, GSF_META_NAME_DATE_MODIFIED) == 0) {
		dialog_doc_metadata_set_label_text (state,
						    state->modified,
						    property_value,
						    TRUE);
	}
	else if (strcmp (property_name, GSF_META_NAME_SPREADSHEET_COUNT) == 0) {
		dialog_doc_metadata_set_label_text (state,
						    state->sheets,
						    property_value,
						    TRUE);
	}
	else if (strcmp (property_name, GSF_META_NAME_CELL_COUNT) == 0) {
		dialog_doc_metadata_set_label_text (state,
						    state->cells,
						    property_value,
						    TRUE);
	}
	else if (strcmp (property_name, GSF_META_NAME_PAGE_COUNT) == 0) {
		dialog_doc_metadata_set_label_text (state,
						    state->pages,
						    property_value,
						    TRUE);
	}

	/* Entries */
	if (property_value == NULL)
		property_value = "";

	if (strcmp (property_name, GSF_META_NAME_TITLE) == 0) {
		gtk_entry_set_text (state->title, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_SUBJECT) == 0) {
		gtk_entry_set_text (state->subject, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_INITIAL_CREATOR) == 0) {
		gtk_entry_set_text (state->author, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_MANAGER) == 0) {
		gtk_entry_set_text (state->manager, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_COMPANY) == 0) {
		gtk_entry_set_text (state->company, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_CATEGORY) == 0) {
		gtk_entry_set_text (state->category, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_KEYWORDS) == 0) {
		gtk_entry_set_text (state->keywords, property_value);
	}

	else if (strcmp (property_name, GSF_META_NAME_DESCRIPTION) == 0) {
		gtk_text_buffer_set_text (gtk_text_view_get_buffer (state->comments), 
					  property_value,
					  -1);
	}
}

static void
cb_dialog_doc_metadata_remove_button_clicked (GtkWidget         *remove_button,
					      DialogDocMetaData *state)
{
	GtkTreeIter list_iter;
	GtkTreeIter tree_iter;
	GtkTreePath *path;
	GtkEntry    *entry;
	GValue      *property_name;

	g_return_if_fail (state->metadata != NULL);

	/* Get tree and list iter */
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->ppt_name),
				       &list_iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->ppt_name_store),
					&list_iter);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->properties_store),
				 &tree_iter,
				 path);

	/* Get the property name */
	property_name = g_new0 (GValue, 1);
	gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
				  &tree_iter,
				  0,
				  property_name);

	/* Update other pages */
	dialog_doc_metadata_update_property_value (state, 
						   g_value_get_string (property_name),
						   NULL);

	/* REMOVE FROM GSF DOC METADATA */
	
	
	/* Remove from Tree View */
	gtk_tree_store_remove (state->properties_store,
			       &tree_iter);

	/* Remove from Combo Box */
	gtk_list_store_remove (state->ppt_name_store,
			       &list_iter);

	/* Clear entries on 'Properties' page */
	entry = GTK_ENTRY (GTK_BIN (state->ppt_name)->child);
	gtk_entry_set_text (entry, "");

	gtk_entry_set_text (state->ppt_value, "");
	gtk_entry_set_text (state->ppt_link, "");

	/* Set remove button insensitive */
	gtk_widget_set_sensitive (remove_button, FALSE);

	/* Free all data */
	g_value_unset (property_name);
	g_free (property_name);
}

static void
cb_dialog_doc_metadata_apply_button_clicked (GtkWidget         *w,
					     DialogDocMetaData *state)
{
	gtk_widget_set_sensitive (GTK_WIDGET (state->apply_button), FALSE);
}

static void
cb_dialog_doc_metadata_combo_property_selected (GtkComboBox       *combo_box,
						DialogDocMetaData *state)
{
	GtkTreeIter list_iter;
	GtkTreeIter tree_iter;
	GtkTreePath *path;
	GValue      *value;
	gboolean    ret;
	gchar       *link_value;

	g_return_if_fail (state->metadata != NULL);

	/* Get list store path */
	ret = gtk_combo_box_get_active_iter (combo_box, &list_iter);

	if (ret == TRUE) {
		path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->ppt_name_store),
						&list_iter);

		/* Get value on the second column */
		gtk_tree_model_get_iter (GTK_TREE_MODEL (state->properties_store),
					 &tree_iter,
					 path);

		value = g_new0 (GValue, 1);
		gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
					  &tree_iter,
					  1,
					  value);

		gtk_entry_set_text (state->ppt_value, g_value_get_string (value));

		/* Get link value on the 3rd column */
		g_value_unset (value);
		gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
					  &tree_iter,
					  2,
					  value);

		link_value = (gchar *) g_value_get_string (value);

		if (link_value != NULL)
			gtk_entry_set_text (state->ppt_link, (const gchar *) link_value);

		/* Update tree view cursor */
		gtk_tree_view_set_cursor (state->properties, path, NULL, FALSE);

		/* Set 'Remove' button sensitive */
		gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), TRUE);

		/* Free all data */
		gtk_tree_path_free (path);
		g_value_unset (value);
		g_free (value);
	}
}

static void 
cb_dialog_doc_metadata_tree_property_selected (GtkTreeView       *tree_view,
					       DialogDocMetaData *state)
{
	GtkTreeIter tree_iter;
	GtkTreeIter list_iter;
	GtkTreePath *path;

	g_return_if_fail (state->metadata != NULL);

	gtk_tree_view_get_cursor (tree_view, &path, NULL);
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->properties_store),
				 &tree_iter,
				 path);

	/* Activate item on combo box */
	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->ppt_name_store),
				 &list_iter,
				 path);

	gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->ppt_name),
				       &list_iter);

	gtk_tree_path_free (path);

	/* Set remove button sensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), TRUE);
}

void
dialog_doc_metadata_append_property (DialogDocMetaData *state,
				     const gchar       *name,
				     const gchar       *value,
				     const gchar       *link,
				     gboolean          activate_property)
{
	GtkTreeIter tree_iter;
	GtkTreeIter list_iter;

	/* Append new values in tree view */
	gtk_tree_store_append (state->properties_store, &tree_iter, NULL);
	gtk_tree_store_set (state->properties_store, 
			    &tree_iter,
			    0, name,
			    1, value,
			    2, link,
			    -1);

	/* Append new values in combo box */
	gtk_list_store_append (state->ppt_name_store, &list_iter);
	gtk_list_store_set (state->ppt_name_store, 
			    &list_iter,
			    0, name,
			    -1);

	if (activate_property == TRUE)
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->ppt_name),
					       &list_iter);
}

void
dialog_doc_metadata_populate_tree_view (gchar             *name,
					GsfDocProp 	  *prop,
					DialogDocMetaData *state)
{
	gchar  *str_value;
	char   *link_value;

	g_return_if_fail (state->metadata != NULL);

	/* Get the prop value as string */
	str_value = dialog_doc_metadata_get_prop_value_as_str (name, (GValue *) gsf_doc_prop_get_val (prop));

	link_value = (char *) gsf_doc_prop_get_link (prop);

	dialog_doc_metadata_append_property (state,
					     gsf_doc_prop_get_name (prop),
					     str_value == NULL ? "" : str_value,
					     link_value == NULL ? "" : gsf_doc_prop_get_link (prop),
					     FALSE);

	dialog_doc_metadata_update_property_value (state, gsf_doc_prop_get_name (prop), str_value);

	if (str_value != NULL)
		g_free (str_value);

}

static void
populate_page_3 (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);
	g_return_if_fail (state->properties != NULL);

	/* Set Remove and Apply buttons insensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->apply_button), FALSE);
	
	/* Intialize Combo Box */
	state->ppt_name_store = gtk_list_store_new (1, G_TYPE_STRING);
	
	gtk_combo_box_set_model (GTK_COMBO_BOX (state->ppt_name),
				 GTK_TREE_MODEL (state->ppt_name_store));
	
	gtk_combo_box_entry_set_text_column (state->ppt_name, 0);

	/* Populate Treeview */
	state->properties_store = gtk_tree_store_new (3,
						      G_TYPE_STRING,
						      G_TYPE_STRING,
						      G_TYPE_STRING);
	
	gtk_tree_view_set_model (state->properties, 
				 GTK_TREE_MODEL (state->properties_store));
	
	/* Append Columns */
	gtk_tree_view_insert_column_with_attributes (state->properties, 
						     0, _("Name"),
						     gtk_cell_renderer_text_new(),
						     "text", 0,
						     NULL);
						     
	gtk_tree_view_insert_column_with_attributes (state->properties, 
						     1, _("Value"),
						     gtk_cell_renderer_text_new(),
						     "text", 1,
						     NULL);

	gtk_tree_view_insert_column_with_attributes (state->properties, 
						     2, _("Linked To"),
						     gtk_cell_renderer_text_new(),
						     "text", 2,
						     NULL);

	/* Read all metadata */
	gsf_doc_meta_data_foreach (state->metadata,
				   (GHFunc) dialog_doc_metadata_populate_tree_view,
				   state);

	/* Set up signals */
	/* Tree View */
	g_signal_connect (G_OBJECT (state->properties),
			  "cursor-changed",
			  G_CALLBACK (cb_dialog_doc_metadata_tree_property_selected),
			  state);

	/* Combo Box */
	g_signal_connect (G_OBJECT (state->ppt_name),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_combo_property_selected),
			  state);

	/* Entries */
	
	/* 'Add', 'Remove' and 'Apply' Button Signals */
	g_signal_connect (G_OBJECT (state->add_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_add_button_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->remove_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_remove_button_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->apply_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_apply_button_clicked),
			  state);
}

static void
populate_page_4 (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	dialog_doc_metadata_set_label_text (state, state->sheets, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->cells, NULL, TRUE);
	dialog_doc_metadata_set_label_text (state, state->pages, NULL, TRUE);
}

static char *
time2str (time_t t)
{
	char buffer[4000];
	gsize len;
	const char *format = "%c";

	if (t == -1)
		return NULL;

	
	len = strftime (buffer, sizeof (buffer), format, localtime (&t));
	if (len == 0)
		return NULL;

	return g_locale_to_utf8 (buffer, len, NULL, NULL, NULL);
}

void 
dialog_doc_metadata_set_label_text (DialogDocMetaData *state,
				    GtkLabel          *label,
				    const char        *value,
				    gboolean          auto_fill)
{
	Workbook *wb = state->wb;
	gchar    *str_value = NULL;

	g_return_if_fail (label != NULL);

	if (value != NULL)
		str_value = g_strdup (value);

	if (str_value == NULL && auto_fill == TRUE) {
		/* File Name */
		if (label == state->file_name) {
			str_value = go_basename_from_uri (workbook_get_uri (wb));
		}

		/* File Location */
		else if (label == state->location) {
			str_value = go_dirname_from_uri (workbook_get_uri (wb), TRUE);
		}

		/* Date Created */
		else if (label == state->created) {
			/* Nothing to do ATM */
		}

		/* Date Modified */
		else if (label == state->modified) {
			str_value = time2str (go_file_get_date_modified (workbook_get_uri (wb)));
		}

		/* Date Accessed */
		else if (label == state->accessed) {
			str_value = time2str (go_file_get_date_accessed (workbook_get_uri (wb)));
		}

		/* Owner */
		else if (label == state->owner) {
			str_value = go_file_get_owner_name (workbook_get_uri (wb));
		}

		/* Group */
		else if (label == state->group) {
			str_value = go_file_get_group_name (workbook_get_uri (wb));
		}

		/* Number of Sheets */
		else if (label == state->sheets) {
			str_value = g_strdup_printf ("%d",  workbook_sheet_count (state->wb));
		}
		
		/* Number of cells */
		else if (label == state->cells) {
			/* Nothing to do ATM */
		}

		/* Number of pages */
		else if (label == state->pages) {
			/* Nothing to do ATM */
		}
	}

	if (str_value != NULL) {
		gtk_label_set_text (label, str_value);
		g_free (str_value);
	} else
		gtk_label_set_text (label, "Unknwon");
}

void 
dialog_doc_metadata_set_gsf_prop_value (GsfDocMetaData *metadata,
					const char     *prop_name,
					GValue         *value)
{
	GsfDocProp * prop = gsf_doc_meta_data_lookup (metadata,
						      prop_name);

	/* No need to check if prop is NULL. gsf already checks it */
	gsf_doc_prop_set_val (prop, value);
}

gchar * 
dialog_doc_metadata_get_prop_value_as_str (const char *prop_name,
					   GValue     *prop_value)
{
	gchar  *str_value = NULL;
	
	if (prop_value != NULL) {
		/* String */
		if (G_VALUE_HOLDS_STRING (prop_value)) {
			str_value = g_value_dup_string (prop_value);
		}

		/* Integer */
		else if (G_VALUE_HOLDS_INT (prop_value)) {
			str_value = g_strdup_printf ("%d", g_value_get_int (prop_value));
		}

		/* Unsigned Integer */
		else if (G_VALUE_HOLDS_UINT (prop_value)) {
			str_value = g_strdup_printf ("%u", g_value_get_uint (prop_value));
		}

		/* Timestamp */
		else if (VAL_IS_GSF_TIMESTAMP (prop_value)) {
			GsfTimestamp * timestamp = (GsfTimestamp *) g_value_get_boxed (prop_value);
			str_value = gsf_timestamp_as_string (timestamp);
		}

		/* Vector */
		else if (VAL_IS_GSF_DOCPROP_VECTOR (prop_value)) {
			GsfDocPropVector * prop_vector = gsf_value_get_docprop_vector (prop_value);
			str_value = gsf_docprop_vector_as_string (prop_vector);
		}

		/* Unrecognized type */
		else {
			g_warning ("Metadata tag '%s' holds unrecognized value type.", prop_name);
		}
	}
	
	return str_value;
}
