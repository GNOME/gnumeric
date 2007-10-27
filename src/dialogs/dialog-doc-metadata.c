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
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <value.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-timestamp.h>
#include <gsf/gsf-docprop-vector.h>

#include <goffice/app/go-doc.h>
#include <goffice/utils/go-file.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <glib/gprintf.h>

#include <string.h>

#define DOC_METADATA_KEY "dialog-doc-metadata"

typedef struct {
	GladeXML		*gui;
	GtkWidget		*dialog;

	/*pointer to the document metadata*/
	GsfDocMetaData		*metadata;

	gboolean		permissions_changed;
	GOFilePermissions	*file_permissions;

	WBCGtk	*wbcg;
	Workbook                *wb;
	GODoc			*doc;

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

	GtkCheckButton		*owner_read;
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

/******************************************************************************
 * G_VALUE TRANSFORM FUNCTIONS 
 ******************************************************************************/

/*
 * G_TYPE_STRING TO OTHER
 */
static void
dialog_doc_metadata_transform_str_to_timestamp (const GValue *string_value,
						GValue       *timestamp_value)
{
	g_return_if_fail (G_VALUE_HOLDS_STRING (string_value));
	g_return_if_fail (VAL_IS_GSF_TIMESTAMP (timestamp_value));

	/* TODO */
}

static void
dialog_doc_metadata_transform_str_to_docprop_vect (const GValue *string_value,
						   GValue       *docprop_value)
{
	g_return_if_fail (G_VALUE_HOLDS_STRING (string_value));
	g_return_if_fail (VAL_IS_GSF_DOCPROP_VECTOR (docprop_value));

	/* TODO */
}

/*
 * OTHER TO G_TYPE_STRING
 */
static void
dialog_doc_metadata_transform_timestamp_to_str (const GValue *timestamp_value,
						GValue       *string_value)
{
	GsfTimestamp *timestamp = NULL;

	g_return_if_fail (VAL_IS_GSF_TIMESTAMP (timestamp_value));
	g_return_if_fail (G_VALUE_HOLDS_STRING (string_value));

	timestamp = (GsfTimestamp *) g_value_get_boxed (timestamp_value);

	if (timestamp != NULL)
		g_value_set_string (string_value,
				    gsf_timestamp_as_string (timestamp));
}

static void
dialog_doc_metadata_transform_docprop_vect_to_str (const GValue *docprop_value,
						   GValue       *string_value)
{
	GsfDocPropVector * docprop_vector = NULL;

	g_return_if_fail (VAL_IS_GSF_DOCPROP_VECTOR (docprop_value));
	g_return_if_fail (G_VALUE_HOLDS_STRING (string_value));

	docprop_vector = gsf_value_get_docprop_vector (docprop_value);

	if (docprop_vector != NULL)
		g_value_set_string (string_value,
				    gsf_docprop_vector_as_string (docprop_vector));
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'FILE' PAGE
 ******************************************************************************/

static void
cb_dialog_doc_metadata_change_permission (GtkCheckButton    *bt,
					  DialogDocMetaData *state)
{
	g_return_if_fail (state->file_permissions != NULL);

	/* Check which button was toggled */
	if (bt == state->owner_read)
		state->file_permissions->owner_read = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else if (bt == state->owner_write)
		state->file_permissions->owner_write = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else if (bt == state->group_read)
		state->file_permissions->group_read = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else if (bt == state->group_write)
		state->file_permissions->group_write = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else if (bt == state->others_read)
		state->file_permissions->others_read = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else if (bt == state->others_write)
		state->file_permissions->others_write = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bt));
	else
		return;
	
	state->permissions_changed = TRUE;
}

static void
dialog_doc_metadata_set_up_permissions (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	state->file_permissions = go_get_file_permissions (
		go_doc_get_uri (state->doc));

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

static char *
time2str (time_t t)
{
	char buffer[4000];
	gsize len;
	char const *format = "%c";

	if (t == -1)
		return NULL;
	
	len = strftime (buffer, sizeof (buffer), format, localtime (&t));
	if (len == 0)
		return NULL;

	return g_locale_to_utf8 (buffer, len, NULL, NULL, NULL);
}

 /* @auto_fill : if TRUE and the text is NULL, try to set the label text with an automatic value. */
static void 
dialog_doc_metadata_set_label (DialogDocMetaData *state,
			       GtkLabel          *label,
			       char        const *text,
			       gboolean          auto_fill)
{
	Workbook *wb = state->wb;
	gchar    *str_value = NULL;

	g_return_if_fail (label != NULL);

	if (text != NULL)
		str_value = g_strdup (text);

	if (str_value == NULL && auto_fill == TRUE) {
		/* File Name */
		if (label == state->file_name) {
			str_value = go_basename_from_uri (go_doc_get_uri (state->doc));
		}

		/* File Location */
		else if (label == state->location) {
			str_value = go_dirname_from_uri (go_doc_get_uri (state->doc), TRUE);
		}

		/* Date Created */
		else if (label == state->created) {
			/* Nothing to do ATM */
		}

		/* Date Modified */
		else if (label == state->modified) {
			str_value = time2str (go_file_get_date_modified (go_doc_get_uri (state->doc)));
		}

		/* Date Accessed */
		else if (label == state->accessed) {
			str_value = time2str (go_file_get_date_accessed (go_doc_get_uri (state->doc)));
		}

		/* Owner */
		else if (label == state->owner) {
			str_value = go_file_get_owner_name (go_doc_get_uri (state->doc));
		}

		/* Group */
		else if (label == state->group) {
			str_value = go_file_get_group_name (go_doc_get_uri (state->doc));
		}

		/* Number of Sheets */
		else if (label == state->sheets) {
			str_value = g_strdup_printf ("%d",  workbook_sheet_count (wb));
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
		gtk_label_set_text (label, _("Unknown"));
}

static void
dialog_doc_metadata_init_file_page (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	dialog_doc_metadata_set_label (state, state->file_name, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->location, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->created, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->modified, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->accessed, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->owner, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->group, NULL, TRUE);

	/* Set up check buttons */
	state->permissions_changed = FALSE;
	dialog_doc_metadata_set_up_permissions (state);

	/* Signals */

	/* Owner */
	g_signal_connect (G_OBJECT (state->owner_read),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);

	g_signal_connect (G_OBJECT (state->owner_write),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);

	/* Group */
	g_signal_connect (G_OBJECT (state->group_read),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);
	
	g_signal_connect (G_OBJECT (state->group_write),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);

	/* Others */
	g_signal_connect (G_OBJECT (state->others_read),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);
	
	g_signal_connect (G_OBJECT (state->others_write),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_change_permission),
			  state);

	/* Help and Close buttons */
	gnumeric_init_help_button (GTK_WIDGET (state->help_button),
				   GNUMERIC_HELP_LINK_METADATA);

	g_signal_connect_swapped (G_OBJECT (state->close_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  state->dialog);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'DESCRIPTION' PAGE 
 ******************************************************************************/

/* @activate_property : if TRUE, sets the tree view row which was added active. */
static void
dialog_doc_metadata_add_prop (DialogDocMetaData *state,
			      const gchar       *name,
			      const gchar       *value,
			      const gchar       *link,
			      gboolean          activate_property)
{
	GtkTreeIter tree_iter;
	GtkTreeIter list_iter;

	if (value == NULL)
		value = "";

	if (link == NULL)
		link = "";

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

static GType
dialog_doc_metadata_get_gsf_prop_val_type (DialogDocMetaData *state,
					   const gchar       *name)
{
	GsfDocProp *prop = NULL;
	GValue     *value = NULL;
	GType       val_type = G_TYPE_INVALID;

	g_return_val_if_fail (state->metadata != NULL, G_TYPE_INVALID);

	/* First we try the clean way */
	prop = gsf_doc_meta_data_lookup (state->metadata, name);

	if (prop != NULL) 
		value = (GValue *) gsf_doc_prop_get_val (prop);

	if (value != NULL) {
		val_type = G_VALUE_TYPE (value);

		switch (val_type) {
			case G_TYPE_INT:
			case G_TYPE_UINT:
			case G_TYPE_STRING:
				/* Just leave it as is */
				break;

			case G_TYPE_BOXED: 
				{
				/* Check if it is really a GsfTimeStamp */
					GsfTimestamp *timestamp;
					timestamp = (GsfTimestamp *) g_value_get_boxed (value);

					if (VAL_IS_GSF_TIMESTAMP (timestamp))
						val_type = GSF_TIMESTAMP_TYPE;
					else
						val_type = G_TYPE_INVALID;

					break;
				}

			case G_TYPE_OBJECT:
				/* Check if it is a GsfDocPropVector */
				{
					GsfDocPropVector *vect;
					vect = gsf_value_get_docprop_vector (value);

					if (VAL_IS_GSF_DOCPROP_VECTOR (vect))
						val_type = GSF_DOCPROP_VECTOR_TYPE;
					else
						val_type = G_TYPE_INVALID;

					break;
				}

			default:
				/* Anything else is invalid */
				{
					val_type = G_TYPE_INVALID;
					
					break;
				}
		}
	} 
	else {
		/* FIXME: At this moment, we will assume a G_TYPE_STRING */
		val_type = G_TYPE_STRING;
	}

	return val_type;
}

static void
dialog_doc_metadata_set_gsf_prop_val (DialogDocMetaData *state,
				      GValue            *prop_value,
				      const gchar       *str_val)
{
	GValue string_value = { 0 };
	g_value_init (&string_value, G_TYPE_STRING);

	g_value_set_string (&string_value, g_strdup (str_val));
	g_value_transform (&string_value, prop_value);
}

/**
 * dialog_doc_metadata_set_gsf_prop
 *
 * @state : dialog main struct
 * @name  : property name
 * @value : property value
 * @link  : property linked to
 *
 * Sets a new value to the property in the GsfDocMetaData struct
 *
 **/
static void
dialog_doc_metadata_set_gsf_prop (DialogDocMetaData *state,
				  const gchar       *name,
				  const gchar       *value,
				  const gchar       *link)
{
	GsfDocProp *doc_prop;
	GValue     *doc_prop_value;
	GType       val_type;
	
	/* Create a new GsfDocProp */
	doc_prop = gsf_doc_prop_new (g_strdup (name));

	/* Create a new Value */
	doc_prop_value = g_new0 (GValue, 1);

	val_type = dialog_doc_metadata_get_gsf_prop_val_type (state, name);

	if (val_type != G_TYPE_INVALID) {
		g_value_init (doc_prop_value, val_type);
		dialog_doc_metadata_set_gsf_prop_val (state, doc_prop_value, value);
		gsf_doc_prop_set_val (doc_prop, doc_prop_value);
	}

	if (link != NULL)
		gsf_doc_prop_set_link (doc_prop, g_strdup (link));

	gsf_doc_meta_data_store (state->metadata, doc_prop);
}

/**
 * dialog_doc_metadata_set_prop
 *
 * @state      : dialog main struct
 * @prop_name  : property name
 * @prop_value : property value
 *
 * Tries to update the property value in all the dialog and in the GsfDocMetaData
 * struct. If the property was not found, creates a new one.
 *
 **/
static void
dialog_doc_metadata_set_prop (DialogDocMetaData *state,
			      const gchar       *prop_name,
			      const gchar       *prop_value, 
			      const gchar       *link_value)
{
	GtkTreeIter tree_iter;
	GtkTreeIter list_iter;
	GValue      *value;
	gboolean    ret;
	gboolean    found;
	
	g_return_if_fail (state->metadata != NULL);

	found = FALSE;

	/* Search tree view for property */
	value = g_new0 (GValue, 1);

	ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (state->properties_store),
					     &tree_iter);

	while (ret == TRUE) {
		
		gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
					  &tree_iter,
					  0,
					  value);

		if (strcmp (prop_name, g_value_get_string (value)) == 0) {
			/* Set new value */
			gtk_tree_store_set (state->properties_store, 
					    &tree_iter,
					    1, prop_value,
					    -1);

			if (link_value != NULL)
				gtk_tree_store_set (state->properties_store, 
						    &tree_iter,
						    2, link_value,
						    -1);

			g_value_unset (value);

			/* Update entry value if necessary */
			ret = gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->ppt_name),
							     &list_iter);

			if (ret == TRUE) {
				gtk_tree_model_get_value (GTK_TREE_MODEL (state->ppt_name_store),
							  &list_iter,
							  0,
							  value);

				if (strcmp (prop_name, g_value_get_string (value)) == 0) {
					gtk_entry_set_text (state->ppt_value, prop_value);

					if (link_value != NULL)
						gtk_entry_set_text (state->ppt_link, link_value);
				}

				g_value_unset (value);
			}

			found = TRUE;
			break;
		}

		g_value_unset (value);
		ret = gtk_tree_model_iter_next (GTK_TREE_MODEL (state->properties_store),
						&tree_iter);
	}

	/* If the property was not found create it */
	if (found == FALSE)
		dialog_doc_metadata_add_prop (state, prop_name, prop_value, "", FALSE);

	dialog_doc_metadata_set_gsf_prop (state, prop_name, prop_value, link_value);

	/* Free all data */
	g_free (value);
}

/**
 * CALLBACKS for 'Description' page entries 
 **/
static void
cb_dialog_doc_metadata_title_changed (GtkEntry          *entry,
				      DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_TITLE,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_subject_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_SUBJECT,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_author_changed (GtkEntry          *entry,
				       DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_INITIAL_CREATOR,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_manager_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_MANAGER,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_company_changed (GtkEntry          *entry,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_COMPANY,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_category_changed (GtkEntry          *entry,
					 DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_CATEGORY,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_keywords_changed (GtkEntry          *entry,
					 DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_KEYWORDS,
				      gtk_entry_get_text (entry),
				      NULL);
}

static void
cb_dialog_doc_metadata_comments_changed (GtkTextBuffer     *buffer,
					 DialogDocMetaData *state)
{
	GtkTextIter start_iter;
	GtkTextIter end_iter;
	gchar *text;

	gtk_text_buffer_get_start_iter (buffer, &start_iter);
	gtk_text_buffer_get_end_iter   (buffer, &end_iter);

	text = gtk_text_buffer_get_text (buffer, &start_iter, &end_iter, TRUE);

	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_DESCRIPTION,
				      text,
				      NULL);
}

/**
 * dialog_doc_metadata_init_description_page
 *
 * @state : dialog main struct
 *
 * Initializes the widgets and signals for the 'Description' page.
 *
 **/
static void
dialog_doc_metadata_init_description_page (DialogDocMetaData *state)
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

	g_signal_connect (G_OBJECT (gtk_text_view_get_buffer (state->comments)),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_comments_changed),
			  state);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'PROPERTIES' PAGE
 ******************************************************************************/

/**
 * cb_dialog_doc_metadata_add_clicked
 *
 * @w     : widget 
 * @state : dialog main struct
 *
 * Adds a new "empty" property to the tree view.
 *
 **/
static void
cb_dialog_doc_metadata_add_clicked (GtkWidget         *w,
				    DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Create a new entry in Tree View and Combo Box */
	dialog_doc_metadata_add_prop (state, "<Name>", "<Value>", "", TRUE);
}

/**
 * dialog_doc_metadata_update_prop
 *
 * @state      : dialog main struct
 * @prop_name  : property name
 * @prop_value : property value
 *
 * Updates a label or a entry text with the new value.
 *
 **/
static void
dialog_doc_metadata_update_prop (DialogDocMetaData *state,
				 const gchar       *prop_name,
				 const gchar       *prop_value)
{
	/* Labels */
	if (strcmp (prop_name, GSF_META_NAME_DATE_CREATED) == 0) {
		dialog_doc_metadata_set_label (state,
					       state->created,
					       prop_value,
					       TRUE);
	}
	else if (strcmp (prop_name, GSF_META_NAME_DATE_MODIFIED) == 0) {
		dialog_doc_metadata_set_label (state,
					       state->modified,
					       prop_value,
					       TRUE);
	}
	else if (strcmp (prop_name, GSF_META_NAME_SPREADSHEET_COUNT) == 0) {
		dialog_doc_metadata_set_label (state,
					       state->sheets,
					       prop_value,
					       TRUE);
	}
	else if (strcmp (prop_name, GSF_META_NAME_CELL_COUNT) == 0) {
		dialog_doc_metadata_set_label (state,
					       state->cells,
					       prop_value,
					       TRUE);
	}
	else if (strcmp (prop_name, GSF_META_NAME_PAGE_COUNT) == 0) {
		dialog_doc_metadata_set_label (state,
					       state->pages,
					       prop_value,
					       TRUE);
	}

	/* Entries */
	if (prop_value == NULL)
		prop_value = "";

	if (strcmp (prop_name, GSF_META_NAME_TITLE) == 0) {
		gtk_entry_set_text (state->title, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_SUBJECT) == 0) {
		gtk_entry_set_text (state->subject, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_INITIAL_CREATOR) == 0) {
		gtk_entry_set_text (state->author, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_MANAGER) == 0) {
		gtk_entry_set_text (state->manager, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_COMPANY) == 0) {
		gtk_entry_set_text (state->company, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_CATEGORY) == 0) {
		gtk_entry_set_text (state->category, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_KEYWORDS) == 0) {
		gtk_entry_set_text (state->keywords, prop_value);
	}

	else if (strcmp (prop_name, GSF_META_NAME_DESCRIPTION) == 0) {
		gtk_text_buffer_set_text (gtk_text_view_get_buffer (state->comments), 
					  prop_value,
					  -1);
	}
}

/**
 * cb_dialog_doc_metadata_remove_clicked
 *
 * @remove_bt : widget 
 * @state     : dialog main struct
 *
 * Removes a property from the tree view and updates all the dialog and
 * the GsfDocMetaData struct.
 *
 **/
static void
cb_dialog_doc_metadata_remove_clicked (GtkWidget         *remove_bt,
				       DialogDocMetaData *state)
{
	GtkTreeIter list_iter;
	GtkTreeIter tree_iter;
	gboolean    has_iter;
	GtkTreePath *path;
	GtkEntry    *entry;
	GValue      *prop_name;

	g_return_if_fail (state->metadata != NULL);

	/* Get tree and list iter */
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->ppt_name),
				       &list_iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (state->ppt_name_store),
					&list_iter);

	has_iter = gtk_tree_model_get_iter (GTK_TREE_MODEL (state->properties_store), 
					    &tree_iter, path);
	gtk_tree_path_free (path);
	g_return_if_fail (has_iter);

	/* Get the property name */
	prop_name = g_new0 (GValue, 1);
	gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
				  &tree_iter,
				  0,
				  prop_name);

	/* Update other pages */
	dialog_doc_metadata_update_prop (state, 
					 g_value_get_string (prop_name),
					 NULL);

	/* Remove property from GsfMetadata */
	gsf_doc_meta_data_remove (state->metadata, g_value_get_string (prop_name));
	
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
	gtk_widget_set_sensitive (remove_bt, FALSE);

	/* Free all data */
	g_value_unset (prop_name);
	g_free (prop_name);
}

static void
cb_dialog_doc_metadata_apply_clicked (GtkWidget         *w,
				      DialogDocMetaData *state)
{
	gtk_widget_set_sensitive (GTK_WIDGET (state->apply_button), FALSE);
}

/**
 * cb_dialog_doc_metadata_combo_prop_selected
 *
 * @combo_box : widget 
 * @state     : dialog main struct
 *
 * Update the highlited item in the tree view and the 'Properties' page entry values.
 *
 **/
static void
cb_dialog_doc_metadata_combo_prop_selected (GtkComboBox       *combo_box,
					    DialogDocMetaData *state)
{
	GtkTreeIter list_iter;
	GtkTreeIter tree_iter;
	GtkTreePath *path;
	GValue      *value;
	gchar       *link_value;

	g_return_if_fail (state->metadata != NULL);

	/* Get list store path */
	if (gtk_combo_box_get_active_iter (combo_box, &list_iter)) {
		path = gtk_tree_model_get_path 
			(GTK_TREE_MODEL (state->ppt_name_store), &list_iter);

		if (gtk_tree_model_get_iter 
		    (GTK_TREE_MODEL (state->properties_store), 
		     &tree_iter, path)) {

			/* Get value on the second column */
			value = g_new0 (GValue, 1);
			gtk_tree_model_get_value 
				(GTK_TREE_MODEL (state->properties_store),
				 &tree_iter, 1, value);

			gtk_entry_set_text (state->ppt_value, 
					    g_value_get_string (value));

			/* Get link value on the 3rd column */
			g_value_unset (value);
			gtk_tree_model_get_value 
				(GTK_TREE_MODEL (state->properties_store),
				 &tree_iter, 2, value);

			link_value = (gchar *) g_value_get_string (value);

			if (link_value != NULL)
				gtk_entry_set_text (state->ppt_link, 
						    (const gchar *) link_value);

			/* Update tree view cursor */
			gtk_tree_view_set_cursor (state->properties,
						  path, NULL, FALSE);

			/* Set 'Remove' button sensitive */
			gtk_widget_set_sensitive
				(GTK_WIDGET (state->remove_button), TRUE);

			g_value_unset (value);
			g_free (value);
		} else {
			g_warning ("Did not get a valid iterator");
		}

		gtk_tree_path_free (path);
	}
}

/**
 * cb_dialog_doc_metadata_tree_prop_selected
 *
 * @combo_box : widget 
 * @state     : dialog main struct
 *
 * Update the highlited item in the 'Properties' page combo box.
 *
 **/
static void 
cb_dialog_doc_metadata_tree_prop_selected (GtkTreeView       *tree_view,
					   DialogDocMetaData *state)
{
	GtkTreeIter list_iter;
	GtkTreePath *path;

	g_return_if_fail (state->metadata != NULL);

	gtk_tree_view_get_cursor (tree_view, &path, NULL);
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->ppt_name_store),
				     &list_iter,
				     path)) {

		/* Activate item on combo box */
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->ppt_name),
					       &list_iter);

		/* Set remove button sensitive */
		gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), 
					  TRUE);
	} else {
		g_warning ("Did not get a valid iterator");
	}
	gtk_tree_path_free (path);
}

/**
 * dialog_doc_metadata_get_prop_val
 *
 * @prop_name  : property name 
 * @prop_value : property value 
 *
 * Retrieves an arbitrary property value always as string.
 *
 **/
static gchar * 
dialog_doc_metadata_get_prop_val (char const *prop_name,
				  GValue     *prop_value)
{
	GValue str_value = { 0 };
	gboolean ret = FALSE;

	g_return_val_if_fail (prop_value != NULL, NULL);

	g_value_init (&str_value, G_TYPE_STRING);

	ret = g_value_transform (prop_value, &str_value);

	if (ret == FALSE) {
		g_warning ("Metadata tag '%s' holds unrecognized value type.", prop_name);
		return NULL;
	}

	return g_value_dup_string (&str_value);
}

/**
 * dialog_doc_metadata_populate_tree_view
 *
 * @name  : property name 
 * @prop  : property stored in GsfDocMetaData 
 * @state : dialog main struct
 *
 * Populates the tree view in 'Properties' page. 
 *
 **/
static void
dialog_doc_metadata_populate_tree_view (gchar             *name,
					GsfDocProp	  *prop,
					DialogDocMetaData *state)
{
	gchar  *str_value;
	char   *link_value;

	g_return_if_fail (state->metadata != NULL);

	/* Get the prop value as string */
	str_value = dialog_doc_metadata_get_prop_val (name, (GValue *) gsf_doc_prop_get_val (prop));

	link_value = (char *) gsf_doc_prop_get_link (prop);

	dialog_doc_metadata_add_prop (state,
				      gsf_doc_prop_get_name (prop),
				      str_value == NULL ? "" : str_value,
				      link_value == NULL ? "" : link_value,
				      FALSE);

	dialog_doc_metadata_update_prop (state, gsf_doc_prop_get_name (prop), str_value);

	g_free (str_value);
}

/**
 * dialog_doc_metadata_init_properties_page
 *
 * @state : dialog main struct
 *
 * Initializes the widgets and signals for the 'Properties' page.
 *
 **/
static void
dialog_doc_metadata_init_properties_page (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);
	g_return_if_fail (state->properties != NULL);

	/* Set Remove and Apply buttons insensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (state->add_button), FALSE);
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
			  G_CALLBACK (cb_dialog_doc_metadata_tree_prop_selected),
			  state);

	/* Combo Box */
	g_signal_connect (G_OBJECT (state->ppt_name),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_combo_prop_selected),
			  state);

	/* Entries */
	
	/* 'Add', 'Remove' and 'Apply' Button Signals */
	g_signal_connect (G_OBJECT (state->add_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_add_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->remove_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_remove_clicked),
			  state);
	
	g_signal_connect (G_OBJECT (state->apply_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_apply_clicked),
			  state);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'STATISTICS' PAGE
 ******************************************************************************/

/**
 * dialog_doc_metadata_init_statistics_page
 *
 * @state : dialog main struct
 *
 * Initializes the widgets and signals for the 'Statistics' page.
 *
 **/
static void
dialog_doc_metadata_init_statistics_page (DialogDocMetaData *state)
{
	g_return_if_fail (state->metadata != NULL);

	/* Set up the labels */
	dialog_doc_metadata_set_label (state, state->sheets, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->cells, NULL, TRUE);
	dialog_doc_metadata_set_label (state, state->pages, NULL, TRUE);
}

/******************************************************************************
 * DIALOG INITIALIZE/FINALIZE FUNCTIONS
 ******************************************************************************/

/**
 * dialog_doc_metadata_set_file_permissions
 *
 * @state : dialog main struct
 *
 * Writes the new file permissions if there were any changes.
 *
 **/
static void
dialog_doc_metadata_set_file_permissions (DialogDocMetaData *state)
{
	if (state->file_permissions != NULL && 
	    state->permissions_changed == TRUE)
		go_set_file_permissions (go_doc_get_uri (state->doc),
					 state->file_permissions);
}

static void
dialog_doc_metadata_free (DialogDocMetaData *state) 
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);

	if (state->gui != NULL) {
		dialog_doc_metadata_set_file_permissions (state);

		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	g_free (state->file_permissions);
	state->file_permissions = NULL;

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
dialog_doc_metadata_init (DialogDocMetaData *state,
			  WBCGtk *wbcg)
{
	state->wbcg     = wbcg;
	state->wb       = wb_control_get_workbook (WORKBOOK_CONTROL(wbcg));
	state->doc      = GO_DOC (state->wb);
	state->metadata = g_object_get_data (G_OBJECT (state->wb), "GsfDocMetaData");

	/* If workbook's metadata is NULL, create it! */
	if (state->metadata  == NULL) {
		state->metadata = gsf_doc_meta_data_new ();

		if (state->metadata == NULL) {
			/* Do something panicky! */
			return TRUE;
		}

		/* Set the metadata pointer in workbook */
		g_object_set_data (G_OBJECT (state->wb),
				   "GsfDocMetaData",
				   G_OBJECT (state->metadata));
	}

	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
					"doc-meta-data.glade", 
					NULL, 
					NULL);

        if (state->gui == NULL)
                return TRUE;

	dialog_doc_metadata_init_widgets (state);

	/* Register g_value_transform functions */
	g_value_register_transform_func (G_TYPE_STRING,
					 GSF_TIMESTAMP_TYPE,
					 dialog_doc_metadata_transform_str_to_timestamp);

	g_value_register_transform_func (G_TYPE_STRING,
					 GSF_DOCPROP_VECTOR_TYPE,
					 dialog_doc_metadata_transform_str_to_docprop_vect);

	g_value_register_transform_func (GSF_TIMESTAMP_TYPE,
					 G_TYPE_STRING,
					 dialog_doc_metadata_transform_timestamp_to_str);

	g_value_register_transform_func (GSF_DOCPROP_VECTOR_TYPE,
					 G_TYPE_STRING,
					 dialog_doc_metadata_transform_docprop_vect_to_str);

	/* Populate the signal handlers and initial values. */
	/* IMPORTANT: OBEY THE ORDER 1 - 4 - 3 - 2 */
	dialog_doc_metadata_init_file_page (state);
	dialog_doc_metadata_init_statistics_page (state);
	dialog_doc_metadata_init_properties_page (state);
	dialog_doc_metadata_init_description_page (state);

	/* A candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg,
			       GTK_WINDOW (state->dialog),
			       DOC_METADATA_KEY);


	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				GTK_WINDOW (state->dialog));

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog), "state",
		state, (GDestroyNotify) dialog_doc_metadata_free);

	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

/******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************/

/**
 * dialog_doc_metadata_new
 *
 * @wbcg  : WBCGtk 
 *
 * Creates a new instance of the dialog.
 *
 **/
void
dialog_doc_metadata_new (WBCGtk *wbcg)
{
	DialogDocMetaData *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
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
