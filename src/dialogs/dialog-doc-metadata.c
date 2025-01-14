/*
 * dialog-doc-metadata.c: Edit document metadata
 *
 * Copyright (C) 2005 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2011 Andreas J. Guelzow (aguelzow@pyrshep.ca)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook-priv.h>
#include <gui-util.h>
#include <parse-util.h>
#include <value.h>
#include <expr.h>
#include <commands.h>
#include <number-match.h>
#include <dead-kittens.h>

#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-timestamp.h>
#include <gsf/gsf-docprop-vector.h>

#include <goffice/goffice.h>


#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <glib/gprintf.h>

#include <string.h>


#define DOC_METADATA_KEY "dialog-doc-metadata"

enum {
	ITEM_ICON,
	ITEM_NAME,
	PAGE_NUMBER,
	NUM_COLUMNS
};

typedef struct {
	GtkBuilder		*gui;
	GtkWidget		*dialog;

	/*pointer to the document metadata*/
	GsfDocMetaData		*metadata;

	gboolean		permissions_changed;
	GOFilePermissions	*file_permissions;

	WBCGtk	*wbcg;
	Workbook                *wb;
	GODoc			*doc;

	GtkTreeStore            *store;
	GtkTreeView             *view;

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

	GtkTextView		*comments;

	/* Properties Page */
	GtkTreeView		*properties;
	GtkTreeStore		*properties_store;

	GtkEntry	        *ppt_name;
	GtkEntry	        *ppt_value;
	GtkComboBox		*ppt_type;
	GtkListStore            *type_store;
	GtkTreeModelFilter      *type_store_filter;

	GtkButton		*add_button;
	GtkButton		*remove_button;

	GtkLabel                *instruction;
	GtkLabel                *warning;

	/* Keyword Page */
	GtkTreeView             *key_tree_view;
	GtkListStore            *key_store;
	GtkButton               *key_add_button;
	GtkButton               *key_remove_button;

	/* Statistics Page */
	GtkLabel		*sheets;
	GtkLabel		*cells;
	GtkLabel		*pages;

	/* Calculation Page */
	GtkCheckButton		*recalc_auto;
	GtkCheckButton		*recalc_manual;
	GtkCheckButton		*recalc_iteration;
	GtkEntry		*recalc_max;
	GtkEntry		*recalc_tolerance;
	GtkWidget               *recalc_iteration_grid;

} DialogDocMetaData;

#define trim_string(s_) g_strstrip (g_strdup ((s_)))

static gchar *dialog_doc_metadata_get_prop_val (DialogDocMetaData *state, char const *prop_name,
						GValue *prop_value);

static gboolean cb_dialog_doc_metadata_ppt_changed (G_GNUC_UNUSED GtkEntry      *entry,
						    G_GNUC_UNUSED GdkEventFocus *event,
						    DialogDocMetaData *state);


static GType
dialog_doc_metadata_get_value_type_from_name (gchar const *name, GType def)
{
	/* shared by all instances and never freed */
	static GHashTable *dialog_doc_metadata_name_to_type = NULL;
	gpointer res;

	if (NULL == dialog_doc_metadata_name_to_type) {
		static struct {
			char const *name;
			GType type;
		} const map [] = {
			{GSF_META_NAME_GENERATOR,            G_TYPE_STRING},
			{GSF_META_NAME_INITIAL_CREATOR,      G_TYPE_STRING},
			{GSF_META_NAME_CREATOR,              G_TYPE_STRING},
			{GSF_META_NAME_TITLE,                G_TYPE_STRING},
			{GSF_META_NAME_SUBJECT,              G_TYPE_STRING},
			{GSF_META_NAME_MANAGER,              G_TYPE_STRING},
			{GSF_META_NAME_COMPANY,              G_TYPE_STRING},
			{GSF_META_NAME_CATEGORY,             G_TYPE_STRING},
			{GSF_META_NAME_DESCRIPTION,          G_TYPE_STRING},
			{GSF_META_NAME_LAST_SAVED_BY,        G_TYPE_STRING},
			{GSF_META_NAME_TEMPLATE,             G_TYPE_STRING},
			{GSF_META_NAME_EDITING_DURATION,     G_TYPE_STRING}, /* special */
			{GSF_META_NAME_SPREADSHEET_COUNT,    G_TYPE_INT},
			{GSF_META_NAME_TABLE_COUNT,          G_TYPE_INT},
			{GSF_META_NAME_CELL_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_CHARACTER_COUNT,      G_TYPE_INT},
			{GSF_META_NAME_BYTE_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_SECURITY,             G_TYPE_INT},
			{GSF_META_NAME_HIDDEN_SLIDE_COUNT,   G_TYPE_INT},
			{GSF_META_NAME_LINE_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_SLIDE_COUNT,          G_TYPE_INT},
			{GSF_META_NAME_WORD_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_MM_CLIP_COUNT,        G_TYPE_INT},
			{GSF_META_NAME_NOTE_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_PARAGRAPH_COUNT,      G_TYPE_INT},
			{GSF_META_NAME_PAGE_COUNT,           G_TYPE_INT},
			{GSF_META_NAME_CODEPAGE,             G_TYPE_INT},
			{GSF_META_NAME_LOCALE_SYSTEM_DEFAULT,G_TYPE_INT},
			{GSF_META_NAME_OBJECT_COUNT,         G_TYPE_INT},
			{"xlsx:HyperlinksChanged",           G_TYPE_BOOLEAN},
			{GSF_META_NAME_LINKS_DIRTY,          G_TYPE_BOOLEAN},
			{"xlsx:SharedDoc",                   G_TYPE_BOOLEAN},
			{GSF_META_NAME_SCALE,                G_TYPE_BOOLEAN}
		};
		static char const *map_vector[] =
			{GSF_META_NAME_KEYWORDS,
			 GSF_META_NAME_DOCUMENT_PARTS,
			 GSF_META_NAME_HEADING_PAIRS};
		static char const *map_timestamps[] =
			{GSF_META_NAME_DATE_CREATED,
			 GSF_META_NAME_DATE_MODIFIED};

		/*Missing:GSF_META_NAME_THUMBNAIL */

		int i = G_N_ELEMENTS (map);
		dialog_doc_metadata_name_to_type = g_hash_table_new (g_str_hash, g_str_equal);
		while (i-- > 0)
			g_hash_table_insert (dialog_doc_metadata_name_to_type,
					     (gpointer)map[i].name,
					     GINT_TO_POINTER (map[i].type));

		i = G_N_ELEMENTS (map_vector);
		while (i-- > 0)
			g_hash_table_insert (dialog_doc_metadata_name_to_type,
					     (gpointer)map_vector[i],
					     GINT_TO_POINTER (GSF_DOCPROP_VECTOR_TYPE));

		i = G_N_ELEMENTS (map_timestamps);
		while (i-- > 0)
			g_hash_table_insert (dialog_doc_metadata_name_to_type,
					     (gpointer)map_timestamps[i],
					     GINT_TO_POINTER (GSF_TIMESTAMP_TYPE));

	}

	res = g_hash_table_lookup (dialog_doc_metadata_name_to_type, name);

	if (res != NULL)
		return GPOINTER_TO_INT (res);
	else
		return def;
}

static GType
dialog_doc_metadata_get_value_type (GValue *value)
{
	GType val_type = G_VALUE_TYPE (value);

	switch (val_type) {
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
	case G_TYPE_STRING:
	case G_TYPE_BOOLEAN:
		/* Just leave it as is */
		break;

	default:
		/* Check if it is a GsfDocPropVector or GsfTimeStamp */
		if (VAL_IS_GSF_TIMESTAMP (value))
			val_type = GSF_TIMESTAMP_TYPE;
		else if (VAL_IS_GSF_DOCPROP_VECTOR (value))
			val_type = GSF_DOCPROP_VECTOR_TYPE;
		else {
			g_printerr ("GType %s (%i) not handled in metadata dialog.\n",
				    g_type_name (val_type), (int) val_type);
			val_type = G_TYPE_INVALID;
		}

		break;
	}
	return val_type;
}


/******************************************************************************
 * G_VALUE TRANSFORM FUNCTIONS
 ******************************************************************************/

/*
 * G_TYPE_STRING TO OTHER
 */

static void
dialog_doc_metadata_transform_str_to_timestamp (const char *str,
						GValue       *timestamp_value)
{
	time_t s;
	gnm_float serial;
	gint int_serial;
	GsfTimestamp *gt;
	GnmValue *conversion;
	GOFormat *fmt;

	g_return_if_fail (VAL_IS_GSF_TIMESTAMP (timestamp_value));

	fmt = go_format_new_from_XL ("yyyy-mm-dd hh:mm:ss");
	conversion = format_match_number (str, fmt, NULL);
	go_format_unref (fmt);
	if (conversion) {
		serial = value_get_as_float (conversion);
		value_release (conversion);

		/* Convert from Gnumeric time to unix time */
		int_serial = (int)serial;
		s = go_date_serial_to_timet (int_serial, NULL);

		if (gnm_abs (serial - int_serial) >= 1 || s == (time_t)-1) {
			s = time (NULL);
		} else
			s += (gnm_fake_round (3600 * 24 * (serial - int_serial)));

	} else
		s  = time (NULL);

	gt = gsf_timestamp_new ();
	gsf_timestamp_set_time (gt, s);
	gsf_timestamp_to_value (gt, timestamp_value);
}

static void
dialog_doc_metadata_transform_str_to_docprop_vect (const char *str,
						   GValue     *docprop_value)
{
	char const *s;
	GsfDocPropVector *gdpv;
	GValue *value;

	g_return_if_fail (VAL_IS_GSF_DOCPROP_VECTOR (docprop_value));

	gdpv = gsf_docprop_vector_new ();

	while (*str == ' ') {str++;}

	while (*str == '"') {
		char *key;
		s = str += 1;
		while (*s != '"' && *s != '\0') {
			if (*(s++) == '\\') {
				if (*s == '\0')
					goto str_done;
				s++;
			}
		}
		if (*s == '\0')
			goto str_done;
		/* s == '"' */
		key = g_strndup (str, s - str);
		value = g_new0 (GValue, 1);
		g_value_take_string (g_value_init (value, G_TYPE_STRING),
				     g_strcompress (key));
		gsf_docprop_vector_append (gdpv, value);
		g_free (key);
		str = s + 1;
		while (*str == ' ') {str++;}
		if (str[0] != ',')
			goto str_done;
		str++;
		while (*str == ' ') {str++;}
	}
 str_done:
	g_value_set_object (docprop_value, gdpv);
	g_object_unref (gdpv);
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

static char *
time2str_go (time_t t)
{
	/* We need to create a format that is also parsable */
	char *str;
	GOFormat *fmt;
	gnm_float t_gnm;

	if (t == -1)
		return NULL;

	t_gnm = go_date_timet_to_serial_raw (t, NULL);

	fmt = go_format_new_from_XL ("yyyy-mm-dd hh:mm:ss");
	str = go_format_value (fmt, t_gnm);
	go_format_unref (fmt);
	return str;
}

/*
 * OTHER TO G_TYPE_STRING
 */
static void
dialog_doc_metadata_transform_timestamp_to_str (const GValue *timestamp_value,
						GValue       *string_value)
{
	GsfTimestamp const *timestamp = NULL;

	g_return_if_fail (VAL_IS_GSF_TIMESTAMP (timestamp_value));
	g_return_if_fail (G_VALUE_HOLDS_STRING (string_value));

	timestamp = g_value_get_boxed (timestamp_value);
	if (timestamp != NULL)
		g_value_take_string (string_value,
				     time2str_go (timestamp->timet));
}

static gchar*
gnm_docprop_vector_as_string (GsfDocPropVector *vector)
{
	GString         *rstring;
	guint		 i;
	guint		 num_values;
	GValueArray	*gva;
	GValue           vl = G_VALUE_INIT;

	g_return_val_if_fail (vector != NULL, NULL);

	g_value_set_object (g_value_init (&vl, GSF_DOCPROP_VECTOR_TYPE), vector);
	gva = gsf_value_get_docprop_varray (&vl);

	g_return_val_if_fail (gva != NULL, NULL);

	num_values = gva->n_values;
	rstring = g_string_sized_new (num_values * 8);

	for (i = 0; i < num_values; i++) {
		char       *str;
		GValue	   *v;

		v = g_value_array_get_nth (gva, i);

		if (G_VALUE_TYPE(v) == G_TYPE_STRING)
			str = g_strescape (g_value_get_string (v), "");
		else {
			char *b_str = g_strdup_value_contents (v);
			str = g_strescape (b_str, "");
			g_free (b_str);
		}
		g_string_append_c (rstring, '"');
		g_string_append (rstring, str);
		g_string_append (rstring, "\", ");
		g_free (str);
	}
	if (rstring->len > 0)
		g_string_truncate (rstring, rstring->len - 2);

	g_value_unset (&vl);

	return g_string_free (rstring, FALSE);
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
				    gnm_docprop_vector_as_string (docprop_vector));
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

 /* @auto_fill : if %TRUE and the text is %NULL, try to set the label text with an automatic value. */
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

}

/******************************************************************************
 * FUNCTIONS RELATED TO 'DESCRIPTION' PAGE
 ******************************************************************************/

/* @activate_property : if TRUE, sets the tree view row which was added active. */
static void
dialog_doc_metadata_add_prop (DialogDocMetaData *state,
			      const gchar       *name,
			      const gchar       *value,
			      const gchar       *lnk,
			      GType              val_type)
{
	gboolean editable = (val_type != G_TYPE_INVALID)
		&& (val_type != GSF_DOCPROP_VECTOR_TYPE);
	if (value == NULL)
		value = "";

	if (lnk == NULL)
		lnk = "";

	/* Append new values in tree view */
	gtk_tree_store_insert_with_values (state->properties_store, NULL, NULL, G_MAXINT,
					   0, name,
					   1, value,
					   2, lnk,
					   3, editable,
					   4, val_type,
					   -1);
}

static GType
dialog_doc_metadata_get_gsf_prop_val_type (DialogDocMetaData *state,
					   const gchar       *name)
{
	GsfDocProp *prop = NULL;
	GValue     *value = NULL;

	g_return_val_if_fail (state->metadata != NULL, G_TYPE_INVALID);

	/* First we try the clean way */
	prop = gsf_doc_meta_data_lookup (state->metadata, name);

	if (prop != NULL)
		value = (GValue *) gsf_doc_prop_get_val (prop);

	if (value != NULL)
		return dialog_doc_metadata_get_value_type (value);
	else
		return dialog_doc_metadata_get_value_type_from_name (name, G_TYPE_STRING);
}

static void
dialog_doc_metadata_set_gsf_prop_val (DialogDocMetaData *state,
				      GValue            *prop_value,
				      const gchar       *str_val)
{
	GType t = G_VALUE_TYPE (prop_value);

	/* The preinstalled transform functions do not handle simple transformations */
	/* such as from string to double, so we do that ourselves */
	switch (t) {
	case G_TYPE_STRING:
		g_value_set_string (prop_value, str_val);
		return;
	case G_TYPE_DOUBLE:
	case G_TYPE_FLOAT: {
		GnmValue *val = format_match_number (str_val, NULL, workbook_date_conv (state->wb));
		if (val != NULL) {
			gnm_float fl = value_get_as_float (val);
			value_release (val);
			if (t == G_TYPE_DOUBLE)
				g_value_set_double (prop_value, fl);
			else
				g_value_set_float (prop_value, fl);
		}
		return;
	}
	case G_TYPE_INT:
		g_value_set_int (prop_value, strtol (str_val, NULL, 10));
		return;
	case G_TYPE_UINT:
		g_value_set_uint (prop_value, strtoul (str_val, NULL, 10));
		return;
	case G_TYPE_BOOLEAN: {
		GnmValue *val = format_match_number (str_val, NULL, workbook_date_conv (state->wb));
		gboolean b = FALSE;
		if (val != NULL) {
			gboolean err;
			b = value_get_as_bool (val, &err);
			value_release (val);
		}
		g_value_set_boolean (prop_value, b);
		return;
	}
	}

	if (t == GSF_TIMESTAMP_TYPE) {
		dialog_doc_metadata_transform_str_to_timestamp (str_val, prop_value);
	} else if (t == GSF_DOCPROP_VECTOR_TYPE) {
		dialog_doc_metadata_transform_str_to_docprop_vect (str_val, prop_value);
	} else {
		g_printerr (_("Transform function of G_TYPE_STRING to %s is required!\n"),
			    g_type_name (t));
	}
}

/**
 * dialog_doc_metadata_set_gsf_prop
 * @state: dialog main struct
 * @name: property name
 * @value: property value
 * @lnk: property linked to
 *
 * Sets a new value to the property in the GsfDocMetaData struct
 *
 **/
static GType
dialog_doc_metadata_set_gsf_prop (DialogDocMetaData *state,
				  const gchar       *name,
				  const gchar       *value,
				  const gchar       *lnk,
				  GType              type)
{
	GsfDocProp *existing_prop = NULL;
	GsfDocProp *doc_prop;
	GValue     *existing_value = NULL;
	char const *existing_link  = NULL;
	GType       val_type;

	existing_prop = gsf_doc_meta_data_lookup (state->metadata, name);
	if (existing_prop != NULL) {
		existing_value = (GValue *) gsf_doc_prop_get_val (existing_prop);
		existing_link  = gsf_doc_prop_get_link (existing_prop);
	}

	if (lnk != NULL && *lnk == 0)
		lnk = NULL;
	if (value != NULL && *value == 0)
		value = NULL;
	if ((value == NULL) && (lnk == NULL)) {
		if ((existing_prop == NULL) ||
		    ((existing_value == NULL) && (existing_link == NULL)))
			return G_TYPE_INVALID;
		else {
			cmd_change_meta_data (GNM_WBC (state->wbcg), NULL,
					      g_slist_prepend (NULL, g_strdup (name)));
			return G_TYPE_INVALID;
		}
	}

	if (existing_prop != NULL) {
		gboolean    link_changed;
		gboolean    value_changed = TRUE;

		if (existing_link!= NULL && *existing_link == 0)
			existing_link = NULL;
		if (lnk == existing_link)
			link_changed = FALSE;
		else if (lnk == NULL || existing_link == NULL)
			link_changed = TRUE;
		else
			link_changed = (0 != strcmp (lnk, existing_link));

		if (existing_value == NULL)
			value_changed = (value != NULL);
		else if (G_VALUE_HOLDS_STRING (existing_value) && (type == 0 || type == G_TYPE_STRING)) {
			char const * existing_val_str = g_value_get_string (existing_value);
			if (existing_val_str != NULL && *existing_val_str == 0)
				existing_val_str = NULL;
			if (value == existing_val_str)
				value_changed = FALSE;
			else if (value == NULL || existing_val_str == NULL)
				value_changed = TRUE;
			else
				value_changed = (0 != strcmp (value, existing_val_str));
			if (!link_changed && !value_changed)
				return G_TYPE_STRING;
		}
	}


	/* Create a new GsfDocProp */
	doc_prop = gsf_doc_prop_new (g_strdup (name));

	if (type == 0)
		val_type = dialog_doc_metadata_get_gsf_prop_val_type (state, name);
	else
		val_type = type;

	if (val_type != G_TYPE_INVALID) {
		GValue     *doc_prop_value;

		/* Create a new Value */
		doc_prop_value = g_new0 (GValue, 1);

		g_value_init (doc_prop_value, val_type);
		dialog_doc_metadata_set_gsf_prop_val (state, doc_prop_value, value);
		gsf_doc_prop_set_val (doc_prop, doc_prop_value);
	}

	if (lnk != NULL)
		gsf_doc_prop_set_link (doc_prop, g_strdup (lnk));

	cmd_change_meta_data (GNM_WBC (state->wbcg),
			      g_slist_prepend (NULL, doc_prop), NULL);

	return val_type;
}

/**
 * dialog_doc_metadata_set_prop
 * @state: dialog main struct
 * @prop_name: property name
 * @prop_value: property value
 *
 * Tries to update the property value in all the dialog and in the GsfDocMetaData
 * struct. If the property was not found, creates a new one.
 *
 **/
static void
dialog_doc_metadata_set_prop (DialogDocMetaData *state,
			      const gchar       *prop_name,
			      const gchar       *prop_value,
			      const gchar       *link_value,
			      GType type)
{
	GtkTreeIter tree_iter;
	GValue      *value;
	gboolean    ret;
	gboolean    found;
	GType       val_type;
	GsfDocProp *updated_prop;
	gchar      *new_prop_value = NULL;

	g_return_if_fail (state->metadata != NULL);

	val_type = dialog_doc_metadata_set_gsf_prop (state, prop_name, prop_value,
						     link_value, type);

	/* Due to changes in type, prop_value may have changed */
	updated_prop = gsf_doc_meta_data_lookup (state->metadata, prop_name);
	if (updated_prop != NULL) {
		GValue *new_value = (GValue *) gsf_doc_prop_get_val (updated_prop);
		if (new_value != NULL)
			new_prop_value = dialog_doc_metadata_get_prop_val (state, prop_name, new_value);
		if (new_prop_value == NULL)
			new_prop_value = g_strdup ("");
	}

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
			if (updated_prop != NULL) {
				/* Set new value */
				gtk_tree_store_set (state->properties_store,
						    &tree_iter,
						    1, new_prop_value,
					    -1);

				if (link_value != NULL)
					gtk_tree_store_set (state->properties_store,
							    &tree_iter,
							    2, link_value,
							    -1);
			} else
				/* Clear value */
				gtk_tree_store_remove (state->properties_store,
						       &tree_iter);

			g_value_unset (value);

			found = TRUE;
			break;
		}

		g_value_unset (value);
		ret = gtk_tree_model_iter_next (GTK_TREE_MODEL (state->properties_store),
						&tree_iter);
	}

	if (val_type != G_TYPE_INVALID && found == FALSE)
		dialog_doc_metadata_add_prop (state, prop_name, new_prop_value, "", val_type);

	/* Free all data */
	g_free (value);
	g_free (new_prop_value );
}

/*
 * CALLBACKS for 'Description' page entries
 */
static gboolean
cb_dialog_doc_metadata_title_changed (GtkEntry          *entry,
				      G_GNUC_UNUSED GdkEventFocus *event,
				      DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_TITLE,
				      gtk_entry_get_text (entry),
				      NULL, G_TYPE_STRING);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_subject_changed (GtkEntry          *entry,
					G_GNUC_UNUSED GdkEventFocus *event,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_SUBJECT,
				      gtk_entry_get_text (entry),
				      NULL, G_TYPE_STRING);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_author_changed (GtkEntry          *entry,
				       G_GNUC_UNUSED GdkEventFocus *event,
				       DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_INITIAL_CREATOR,
				      gtk_entry_get_text (entry),
				      NULL,  G_TYPE_STRING);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_manager_changed (GtkEntry          *entry,
					G_GNUC_UNUSED GdkEventFocus *event,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_MANAGER,
				      gtk_entry_get_text (entry),
				      NULL,  G_TYPE_STRING);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_company_changed (GtkEntry          *entry,
					G_GNUC_UNUSED GdkEventFocus *event,
					DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_COMPANY,
				      gtk_entry_get_text (entry),
				      NULL,  G_TYPE_STRING);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_category_changed (GtkEntry          *entry,
					 G_GNUC_UNUSED GdkEventFocus *event,
					 DialogDocMetaData *state)
{
	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_CATEGORY,
				      gtk_entry_get_text (entry),
				      NULL, G_TYPE_STRING );
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_comments_changed (GtkTextView     *view,
					 G_GNUC_UNUSED GdkEventFocus *event,
					 DialogDocMetaData *state)
{
	GtkTextIter start_iter;
	GtkTextIter end_iter;
	gchar *text;
	GtkTextBuffer     *buffer = gtk_text_view_get_buffer (view);

	gtk_text_buffer_get_start_iter (buffer, &start_iter);
	gtk_text_buffer_get_end_iter   (buffer, &end_iter);

	text = gtk_text_buffer_get_text (buffer, &start_iter, &end_iter, TRUE);

	dialog_doc_metadata_set_prop (state,
				      GSF_META_NAME_DESCRIPTION,
				      text,
				      NULL,  G_TYPE_STRING);
	return FALSE;
}

/**
 * dialog_doc_metadata_init_description_page
 * @state: dialog main struct
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
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_title_changed),
			  state);

	g_signal_connect (G_OBJECT (state->subject),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_subject_changed),
			  state);

	g_signal_connect (G_OBJECT (state->author),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_author_changed),
			  state);

	g_signal_connect (G_OBJECT (state->manager),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_manager_changed),
			  state);

	g_signal_connect (G_OBJECT (state->company),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_company_changed),
			  state);

	g_signal_connect (G_OBJECT (state->category),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_category_changed),
			  state);

	g_signal_connect (G_OBJECT (state->comments),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_comments_changed),
			  state);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'KEYWORDS' PAGE
 ******************************************************************************/

static void
dialog_doc_metadata_update_keywords_changed (DialogDocMetaData *state)
{
	GValue val = G_VALUE_INIT;
	GtkTreeIter iter;
	GsfDocPropVector *vector = gsf_docprop_vector_new ();

	g_value_init (&val, GSF_DOCPROP_VECTOR_TYPE);

	if (gtk_tree_model_get_iter_first
	    (GTK_TREE_MODEL (state->key_store), &iter)) {
		do {
			GValue *value = g_new0 (GValue, 1);
			gtk_tree_model_get_value
				(GTK_TREE_MODEL (state->key_store), &iter,
				 0, value);
			gsf_docprop_vector_append (vector, value);
			g_value_unset (value);
			g_free (value);
		} while (gtk_tree_model_iter_next
			 (GTK_TREE_MODEL (state->key_store), &iter));
	}
	g_value_set_object (&val, vector);
	g_object_unref (vector);

	dialog_doc_metadata_set_prop
		(state, GSF_META_NAME_KEYWORDS,
		 dialog_doc_metadata_get_prop_val (state, GSF_META_NAME_KEYWORDS, &val),
		 NULL, GSF_DOCPROP_VECTOR_TYPE);

	g_value_unset (&val);
}

static void
cb_dialog_doc_metadata_keywords_sel_changed (GtkTreeSelection *treeselection,
					     DialogDocMetaData *state)
{
	gtk_widget_set_sensitive
		(GTK_WIDGET (state->key_remove_button),
		 gtk_tree_selection_get_selected (treeselection, NULL, NULL));
}

static void
dialog_doc_metadata_update_keyword_list (DialogDocMetaData *state, GsfDocProp *prop)
{
	guint i;
	GtkTreeSelection *sel;

	gtk_list_store_clear (state->key_store);

	if (prop != NULL) {
		GValueArray *array;
		array = gsf_value_get_docprop_varray (gsf_doc_prop_get_val (prop));
		if (array != NULL) {
			for (i = 0; i < array->n_values; i++) {
				GValue *val = g_value_array_get_nth (array, i);
				gtk_list_store_insert_with_values
					(state->key_store, NULL, G_MAXINT,
					 0, g_value_get_string (val), -1);
			}
		}
	}

	sel = gtk_tree_view_get_selection (state->key_tree_view);
	cb_dialog_doc_metadata_keywords_sel_changed (sel, state);
}

static void
cb_dialog_doc_metadata_keywords_add_clicked (G_GNUC_UNUSED GtkWidget *w,
					     DialogDocMetaData       *state)
{
	gtk_list_store_insert_with_values (state->key_store, NULL, G_MAXINT,
					   0, "<?>", -1);
	dialog_doc_metadata_update_keywords_changed (state);
}

static void
cb_dialog_doc_metadata_keywords_remove_clicked (G_GNUC_UNUSED GtkWidget *w,
						DialogDocMetaData       *state)
{
	GtkTreeIter iter;
	GtkTreeSelection *sel = gtk_tree_view_get_selection (state->key_tree_view);

	if (gtk_tree_selection_get_selected (sel, NULL, &iter)) {
		gtk_list_store_remove (state->key_store, &iter);
		dialog_doc_metadata_update_keywords_changed (state);
	}
}

static void
cb_dialog_doc_metadata_keyword_edited (G_GNUC_UNUSED GtkCellRendererText *renderer,
				       gchar                             *path,
				       gchar                             *new_text,
				       DialogDocMetaData                 *state)
{
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (state->key_store), &iter, path)) {
		gtk_list_store_set (state->key_store, &iter, 0, new_text, -1);
		dialog_doc_metadata_update_keywords_changed (state);
	}
}


/**
 * dialog_doc_metadata_init_keywords_page
 * @state: dialog main struct
 *
 * Initializes the widgets and signals for the 'Description' page.
 *
 **/
static void
dialog_doc_metadata_init_keywords_page (DialogDocMetaData *state)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer   *renderer;
	GtkTreeSelection *sel;

	g_return_if_fail (state->metadata != NULL);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Keywords"),
							   renderer,
							   "text", 0,
							   NULL);
	gtk_tree_view_insert_column (state->key_tree_view, column, -1);

	gtk_widget_set_sensitive (GTK_WIDGET (state->key_add_button), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->key_remove_button), FALSE);

	sel = gtk_tree_view_get_selection (state->key_tree_view);
	g_signal_connect (G_OBJECT (sel),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_keywords_sel_changed),
			  state);

	g_signal_connect (G_OBJECT (state->key_add_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_keywords_add_clicked),
			  state);
	g_signal_connect (G_OBJECT (state->key_remove_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_keywords_remove_clicked),
			  state);
	g_signal_connect (G_OBJECT (renderer),
			  "edited",
			  G_CALLBACK (cb_dialog_doc_metadata_keyword_edited),
			  state);

	cb_dialog_doc_metadata_keywords_sel_changed (sel, state);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'PROPERTIES' PAGE
 ******************************************************************************/

static void
cb_dialog_doc_metadata_value_edited (G_GNUC_UNUSED GtkCellRendererText *renderer,
				     gchar                             *path,
				     gchar                             *new_text,
				     DialogDocMetaData                 *state)
{
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_from_string
	    (GTK_TREE_MODEL (state->properties_store), &iter, path)) {
		gchar       *prop_name;
		gchar       *link_value;
		GType        type;

		gtk_tree_model_get (GTK_TREE_MODEL (state->properties_store),
				    &iter,
				    0, &prop_name,
				    2, &link_value,
				    4, &type,
				    -1);
		dialog_doc_metadata_set_prop (state, prop_name, new_text, link_value, type);
		g_free (prop_name);
		g_free (link_value);
	}
}

/**
 * cb_dialog_doc_metadata_add_clicked
 * @w: widget
 * @state: dialog main struct
 *
 * Adds a new "empty" property to the tree view.
 *
 **/
static void
cb_dialog_doc_metadata_add_clicked (G_GNUC_UNUSED GtkWidget *w,
				    DialogDocMetaData       *state)
{
	const gchar *name = gtk_entry_get_text (state->ppt_name);
	const gchar *value = gtk_entry_get_text (state->ppt_value);
	gchar *name_trimmed = trim_string (name);
	GType t = G_TYPE_INVALID; /* we need to initialize that one since
								gtk_tree_model_get() will only set the low bytes */
	GtkTreeIter filter_iter;

	if (gtk_combo_box_get_active_iter (state->ppt_type, &filter_iter)) {
		GtkTreeIter child_iter;
		gtk_tree_model_filter_convert_iter_to_child_iter
			(state->type_store_filter, &child_iter, &filter_iter);
		gtk_tree_model_get (GTK_TREE_MODEL (state->type_store), &child_iter,
				    1, &t, -1);
	} else
		t = dialog_doc_metadata_get_value_type_from_name (name_trimmed, G_TYPE_STRING);
	dialog_doc_metadata_set_prop (state, name_trimmed, value, NULL, t);

	g_free (name_trimmed);

	cb_dialog_doc_metadata_ppt_changed (NULL, NULL, state);
	gtk_label_set_text (state->warning, "");
}

/**
 * dialog_doc_metadata_update_prop
 * @state: dialog main struct
 * @prop_name: property name
 * @prop_value: property value
 *
 * Updates a label or a entry text with the new value.
 *
 **/

static void
dialog_doc_metadata_update_prop (DialogDocMetaData *state,
				 const gchar       *prop_name,
				 const gchar       *prop_value,
				 GsfDocProp	   *prop)
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
		dialog_doc_metadata_update_keyword_list (state, prop);
	}

	else if (strcmp (prop_name, GSF_META_NAME_DESCRIPTION) == 0) {
		gtk_text_buffer_set_text (gtk_text_view_get_buffer (state->comments),
					  prop_value,
					  -1);
	}
}

/**
 * cb_dialog_doc_metadata_remove_clicked
 * @remove_bt: widget
 * @state: dialog main struct
 *
 * Removes a property from the tree view and updates all the dialog and
 * the GsfDocMetaData struct.
 *
 **/
static void
cb_dialog_doc_metadata_remove_clicked (GtkWidget         *remove_bt,
				       DialogDocMetaData *state)
{
	GtkTreeIter tree_iter;
	GValue      *prop_name;
	GtkTreeSelection *sel = gtk_tree_view_get_selection (state->properties);

	g_return_if_fail (state->metadata != NULL);

	if (gtk_tree_selection_get_selected (sel, NULL, &tree_iter)) {

		/* Get the property name */
		prop_name = g_new0 (GValue, 1);
		gtk_tree_model_get_value (GTK_TREE_MODEL (state->properties_store),
					  &tree_iter,
					  0,
					  prop_name);

		/* Update other pages */
		dialog_doc_metadata_update_prop (state,
						 g_value_get_string (prop_name),
						 NULL, NULL);

		/* Remove property from GsfMetadata */
		cmd_change_meta_data (GNM_WBC (state->wbcg), NULL,
				      g_slist_prepend (NULL, g_value_dup_string (prop_name)));

		/* Remove from Tree View */
		gtk_tree_store_remove (state->properties_store,
				       &tree_iter);

		/* Free all data */
		g_value_unset (prop_name);
		g_free (prop_name);
	}

	/* Set remove button insensitive */
	gtk_widget_set_sensitive (remove_bt, FALSE);
}


/**
 * cb_dialog_doc_metadata_tree_prop_selected
 * @combo_box: widget
 * @state: dialog main struct
 *
 * Update the highlited item in the 'Properties' page combo box.
 *
 **/
static void
cb_dialog_doc_metadata_tree_prop_selected (GtkTreeSelection  *selection,
					   DialogDocMetaData *state)
{
	GtkTreeIter iter;
	gboolean selected;
	gchar const *text = "";

	g_return_if_fail (state->metadata != NULL);

	selected = gtk_tree_selection_get_selected (selection, NULL, &iter);

	/* Set remove button sensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), selected);

	if (selected) {
		GType val_type = G_TYPE_INVALID;
		gchar *prop_name = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL (state->properties_store),
				    &iter,
				    0, &prop_name,
				    4, &val_type,
				    -1);
		switch (val_type) {
		case G_TYPE_STRING:
			text = _("Edit string value directly in above listing.");
			break;
		case G_TYPE_UINT:
			text = _("Edit positive integer value directly in above listing.");
			break;
		case G_TYPE_INT:
			text = _("Edit integer value directly in above listing.");
			break;
		case G_TYPE_FLOAT:
		case G_TYPE_DOUBLE:
			text = _("Edit decimal number value directly in above listing.");
			break;
		case G_TYPE_BOOLEAN:
			text = _("Edit TRUE/FALSE value directly in above listing.");
			break;
		default:
			if (val_type == GSF_DOCPROP_VECTOR_TYPE) {
				if (0 == strcmp (prop_name, "dc:keywords"))
					text = _("To edit, use the keywords tab.");
				else
					text = _("This property value cannot be edited.");
			} else if (val_type == GSF_TIMESTAMP_TYPE)
				text= _("Edit timestamp directly in above listing.");
			break;
		}
		g_free (prop_name);
	}

	gtk_label_set_text (state->instruction, text);
}

/**
 * dialog_doc_metadata_get_prop_val:
 * @state:
 * @prop_name: property name
 * @prop_value: property value
 *
 * Retrieves an arbitrary property value always as string.
 *
 **/
static gchar *
dialog_doc_metadata_get_prop_val (G_GNUC_UNUSED DialogDocMetaData *state,
				  char const                      *prop_name,
				  GValue                          *prop_value)
{
	GValue str_value = G_VALUE_INIT;
	gboolean ret = FALSE;
	GType t;
	char *s;

	g_return_val_if_fail (prop_value != NULL, NULL);

	g_value_init (&str_value, G_TYPE_STRING);
	t = G_VALUE_TYPE (prop_value);
	switch (t) {
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_STRING:
		ret = g_value_transform (prop_value, &str_value);
		break;

	case G_TYPE_BOOLEAN: {
		gboolean b = g_value_get_boolean (prop_value);
		g_value_set_string (&str_value, go_locale_boolean_name (b));
		ret = TRUE;
		break;
	}

	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE: {
		double d = (t == G_TYPE_FLOAT)
			? g_value_get_float (prop_value)
			: g_value_get_double (prop_value);
		GString *res = g_string_new (NULL);
		go_dtoa (res, "!g", d);
		g_value_set_string (&str_value, res->str);
		g_string_free (res, TRUE);
		ret = TRUE;
		break;
	}
	}

	if (t == GSF_TIMESTAMP_TYPE) {
		dialog_doc_metadata_transform_timestamp_to_str (prop_value, &str_value);
		ret = TRUE;
	} else if (t == GSF_DOCPROP_VECTOR_TYPE) {
		dialog_doc_metadata_transform_docprop_vect_to_str (prop_value, &str_value);
		ret = TRUE;
	}

	if (ret == FALSE) {
		g_warning ("Metadata tag '%s' holds unrecognized value type.", prop_name);
		return NULL;
	}

	s = g_value_dup_string (&str_value);
	g_value_unset (&str_value);
	return s;
}

/**
 * dialog_doc_metadata_populate_tree_view
 * @name: property name
 * @prop: property stored in GsfDocMetaData
 * @state: dialog main struct
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
	GValue *value;

	g_return_if_fail (state->metadata != NULL);

	value = (GValue *) gsf_doc_prop_get_val (prop);
	/* Get the prop value as string */
	str_value = dialog_doc_metadata_get_prop_val (state, name, value);

	link_value = (char *) gsf_doc_prop_get_link (prop);

	dialog_doc_metadata_add_prop
		(state,
		 gsf_doc_prop_get_name (prop),
		 str_value == NULL ? "" : str_value,
		 link_value == NULL ? "" : link_value,
		 dialog_doc_metadata_get_value_type (value));

	dialog_doc_metadata_update_prop (state, gsf_doc_prop_get_name (prop), str_value, prop);

	g_free (str_value);
}

static gboolean
dialog_doc_metadata_show_all_types (GtkTreeModel *model,
				    G_GNUC_UNUSED GtkTreePath *path,
				    GtkTreeIter *iter,
				    G_GNUC_UNUSED gpointer data)
{
	gtk_list_store_set (GTK_LIST_STORE (model), iter, 2, TRUE, -1);
	return FALSE;
}

static gboolean
dialog_doc_metadata_show_this_type (GtkTreeModel *model,
				    G_GNUC_UNUSED GtkTreePath *path,
				    GtkTreeIter *iter,
				    gpointer data)
{
	GType t, type = GPOINTER_TO_INT (data);

	gtk_tree_model_get (model, iter, 1, &t, -1);
	gtk_list_store_set (GTK_LIST_STORE (model), iter, 2, t == type, -1);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_ppt_changed (G_GNUC_UNUSED GtkEntry          *entry,
				    G_GNUC_UNUSED GdkEventFocus     *event,
				    DialogDocMetaData *state)
{
	const gchar *name;
	const gchar *value;
	gchar *name_trimmed;
	gboolean enable = FALSE;
	gchar *str = NULL;
	GsfDocProp *prop = NULL;
	GtkTreeIter iter;

	name = gtk_entry_get_text (state->ppt_name);
	value = gtk_entry_get_text (state->ppt_value);
	name_trimmed = trim_string (name);

	enable = strlen (name_trimmed) > 0 && strlen (value) > 0;

	if (enable)
		enable = gtk_combo_box_get_active_iter (state->ppt_type, &iter);

	if (enable) {
		prop = gsf_doc_meta_data_lookup (state->metadata, name_trimmed);
		if (prop != NULL) {
			str = g_strdup_printf
				(_("A document property with the name \'%s\' already exists."),
				 name_trimmed);
			enable = FALSE;
		}
	}
	g_free (name_trimmed);
	gtk_widget_set_sensitive (GTK_WIDGET (state->add_button), enable);
	gtk_label_set_text (state->warning, str ? str : "");
	g_free (str);
	return FALSE;
}

static void
cb_dialog_doc_metadata_ppt_type_changed (G_GNUC_UNUSED GtkComboBox *widget,
					 DialogDocMetaData *state)
{
	cb_dialog_doc_metadata_ppt_changed (NULL, NULL, state);
}

static gboolean
cb_dialog_doc_metadata_ppt_name_changed (G_GNUC_UNUSED GtkEntry          *entry,
					 G_GNUC_UNUSED GdkEventFocus     *event,
					 DialogDocMetaData *state)
{
	const gchar *name;
	gchar *name_trimmed;
	gboolean enable = FALSE;
	GtkTreeIter iter;
	gchar *str = NULL;

	name = gtk_entry_get_text (state->ppt_name);
	name_trimmed = trim_string (name);

	enable = strlen (name_trimmed) > 0;

	if (enable) {
		GType t = dialog_doc_metadata_get_value_type_from_name (name_trimmed, G_TYPE_INVALID);

		if (t == GSF_DOCPROP_VECTOR_TYPE) {
			str = g_strdup_printf
					(_("Use the keywords tab to create this property."));
			enable = FALSE;
		}

		if (t == G_TYPE_INVALID) {
			g_signal_handlers_block_by_func(G_OBJECT (state->ppt_type),
							cb_dialog_doc_metadata_ppt_type_changed,
							state);
			gtk_tree_model_foreach (GTK_TREE_MODEL (state->type_store),
						dialog_doc_metadata_show_all_types, NULL);
			gtk_tree_model_filter_refilter (state->type_store_filter);
			g_signal_handlers_unblock_by_func(G_OBJECT (state->ppt_type),
							  cb_dialog_doc_metadata_ppt_type_changed,
							  state);
		} else {
			gtk_combo_box_set_active_iter (state->ppt_type, NULL);
			g_signal_handlers_block_by_func(G_OBJECT (state->ppt_type),
							cb_dialog_doc_metadata_ppt_type_changed,
							state);
			gtk_tree_model_foreach (GTK_TREE_MODEL (state->type_store),
						dialog_doc_metadata_show_this_type,
						GINT_TO_POINTER (t));
			gtk_tree_model_filter_refilter (state->type_store_filter);
			g_signal_handlers_unblock_by_func(G_OBJECT (state->ppt_type),
							  cb_dialog_doc_metadata_ppt_type_changed,
							  state);
			if (gtk_tree_model_get_iter_first
			    (GTK_TREE_MODEL (state->type_store_filter), &iter))
				gtk_combo_box_set_active_iter (state->ppt_type, &iter);
		}
	}
	g_free (name_trimmed);

	if (enable)
		return cb_dialog_doc_metadata_ppt_changed (NULL, NULL, state);
	else {
		gtk_widget_set_sensitive (GTK_WIDGET (state->add_button), FALSE);
		gtk_label_set_text (state->warning, str ? str : "");
		g_free (str);
	}

	return FALSE;
}



/**
 * dialog_doc_metadata_init_properties_page
 * @state: dialog main struct
 *
 * Initializes the widgets and signals for the 'Properties' page.
 *
 **/
static void
dialog_doc_metadata_init_properties_page (DialogDocMetaData *state)
{
	GtkTreeSelection *sel;
	GtkCellRenderer  *cell;
	guint i;

	struct types {
		char const *type_name;
		GType type;
	} ppt_types[] = {
		{N_("String"), G_TYPE_STRING},
		{N_("Integer"), G_TYPE_INT},
		{N_("Decimal Number"), G_TYPE_FLOAT},
		{N_("TRUE/FALSE"), G_TYPE_BOOLEAN}
	};

	g_return_if_fail (state->metadata != NULL);
	g_return_if_fail (state->properties != NULL);

	/* Set Remove and Apply buttons insensitive */
	gtk_widget_set_sensitive (GTK_WIDGET (state->add_button), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove_button), FALSE);

	/* Intialize Combo Box */
	/* gtk_combo_box_set_id_column (state->ppt_type, 0); */
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(state->ppt_type), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(state->ppt_type), cell, "text", 0, NULL);

	for (i = 0; i < G_N_ELEMENTS (ppt_types); i++)
		gtk_list_store_insert_with_values (state->type_store, NULL, G_MAXINT,
						   0, _(ppt_types[i].type_name),
						   1, ppt_types[i].type,
						   2, TRUE,
						   -1);
	gtk_list_store_insert_with_values (state->type_store, NULL, G_MAXINT,
					   0, _("Date & Time"),
					   1, GSF_TIMESTAMP_TYPE,
					   2, TRUE,
					   -1);
	gtk_tree_model_filter_set_visible_column (state->type_store_filter, 2);
	gtk_tree_model_filter_refilter (state->type_store_filter);

	/* Populate Treeview */
	state->properties_store = gtk_tree_store_new (5,
						      G_TYPE_STRING,
						      G_TYPE_STRING,
						      G_TYPE_STRING,
						      G_TYPE_BOOLEAN,
						      G_TYPE_GTYPE);

	gtk_tree_view_set_model (state->properties,
				 GTK_TREE_MODEL (state->properties_store));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (state->properties_store),
					      0, GTK_SORT_ASCENDING);
	g_object_unref (state->properties_store);

	/* Append Columns */
	gtk_tree_view_insert_column_with_attributes (state->properties,
						     0, _("Name"),
						     gtk_cell_renderer_text_new(),
						     "text", 0,
						     NULL);

	cell =  gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes (state->properties,
						     1, _("Value"),
						     cell,
						     "text", 1,
						     "editable", 3,
						     NULL);
	g_signal_connect (G_OBJECT (cell),
			  "edited",
			  G_CALLBACK (cb_dialog_doc_metadata_value_edited),
			  state);

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
	sel = gtk_tree_view_get_selection (state->properties);
	g_signal_connect (G_OBJECT (sel),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_tree_prop_selected),
			  state);

	/* Entries */
	g_signal_connect (G_OBJECT (state->ppt_name),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_ppt_name_changed),
			  state);
	g_signal_connect (G_OBJECT (state->ppt_value),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_ppt_changed),
			  state);
	/* ComboBox */
	g_signal_connect (G_OBJECT (state->ppt_type),
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_ppt_type_changed),
			  state);

	/* 'Add', 'Remove' and 'Apply' Button Signals */
	g_signal_connect (G_OBJECT (state->add_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_add_clicked),
			  state);

	g_signal_connect (G_OBJECT (state->remove_button),
			  "clicked",
			  G_CALLBACK (cb_dialog_doc_metadata_remove_clicked),
			  state);

	cb_dialog_doc_metadata_tree_prop_selected (sel, state);
	gtk_combo_box_set_active (state->ppt_type, 0);
}

/******************************************************************************
 * FUNCTIONS RELATED TO 'STATISTICS' PAGE
 ******************************************************************************/

/**
 * dialog_doc_metadata_init_statistics_page
 * @state: dialog main struct
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
 * FUNCTIONS RELATED TO 'CALCULATIONS' PAGE
 ******************************************************************************/

static gboolean
cb_dialog_doc_metadata_recalc_max_changed (GtkEntry          *entry,
					   G_GNUC_UNUSED GdkEventFocus *event,
					   DialogDocMetaData *state)
{
	int val;
	if (!entry_to_int (entry, &val, TRUE))
		/* FIXME: make undoable */
		workbook_iteration_max_number (state->wb, val);
	return FALSE;
}

static gboolean
cb_dialog_doc_metadata_recalc_tolerance_changed (GtkEntry          *entry,
						 G_GNUC_UNUSED GdkEventFocus *event,
						 DialogDocMetaData *state)
{
	gnm_float val;
	if (!entry_to_float (entry, &val, TRUE))
		/* FIXME: make undoable */
		workbook_iteration_tolerance (state->wb, val);
	return FALSE;
}

static void
cb_dialog_doc_metadata_recalc_auto_changed (GtkWidget *widget, DialogDocMetaData *state)
{
	/* FIXME: make undoable */
	workbook_set_recalcmode	(state->wb, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
}

static void
cb_dialog_doc_metadata_recalc_iteration_changed (G_GNUC_UNUSED GtkWidget *widget, DialogDocMetaData *state)
{
	/* FIXME: make undoable */
	workbook_iteration_enabled (state->wb, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
	gtk_widget_set_sensitive (state->recalc_iteration_grid, state->wb->iteration.enabled);
}

/**
 * dialog_doc_metadata_init_calculations_page
 * @state: dialog main struct
 *
 * Initializes the widgets and signals for the 'Calculations' page.
 *
 **/
static void
dialog_doc_metadata_init_calculations_page (DialogDocMetaData *state)
{
	char *buf;

	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON ( workbook_get_recalcmode (state->wb) ? state->recalc_auto : state->recalc_manual),
		 TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->recalc_iteration),
				      state->wb->iteration.enabled);
	gtk_widget_set_sensitive (state->recalc_iteration_grid, state->wb->iteration.enabled);

	buf = g_strdup_printf ("%d", state->wb->iteration.max_number);
	gtk_entry_set_text (state->recalc_max, buf);
	g_free (buf);

	{
		GnmValue *v = value_new_float (state->wb->iteration.tolerance);
		buf = value_get_as_string (v);
		value_release (v);
		gtk_entry_set_text (state->recalc_tolerance, buf);
		g_free (buf);
	}

	g_signal_connect (G_OBJECT (state->recalc_auto),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_recalc_auto_changed), state);
	g_signal_connect (G_OBJECT (state->recalc_iteration),
			  "toggled",
			  G_CALLBACK (cb_dialog_doc_metadata_recalc_iteration_changed), state);
	g_signal_connect (G_OBJECT (state->recalc_max),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_recalc_max_changed),
			  state);
	g_signal_connect (G_OBJECT (state->recalc_tolerance),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_doc_metadata_recalc_tolerance_changed),
			  state);

}

/******************************************************************************
 * DIALOG INITIALIZE/FINALIZE FUNCTIONS
 ******************************************************************************/

/**
 * dialog_doc_metadata_set_file_permissions
 * @state: dialog main struct
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
	WorkbookControl *wbc = GNM_WBC (state->wbcg);

	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);

	if (state->gui != NULL) {
		dialog_doc_metadata_set_file_permissions (state);

		g_object_unref (state->gui);
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
	state->dialog = go_gtk_builder_get_widget (state->gui, "GOMetadataDialog");

	state->notebook     = GTK_NOTEBOOK (go_gtk_builder_get_widget (state->gui, "notebook"));
	state->help_button  = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "help_button"));
	state->close_button = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "close_button"));

	/* File Information Page */
	state->file_name = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "file_name"));
	state->location  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "location"));
	state->created   = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "created"));
	state->modified  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "modified"));
	state->accessed  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "accessed"));
	state->owner     = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "owner"));
	state->group     = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "group"));

	state->owner_read  = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "owner_read"));
	state->owner_write = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "owner_write"));

	state->group_read  = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "group_read"));
	state->group_write = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "group_write"));

	state->others_read  = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "others_read"));
	state->others_write = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "others_write"));

	/* Description Page */
	state->title    = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "title"));
	state->subject  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "subject"));
	state->author   = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "author"));
	state->manager  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "manager"));
	state->company  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "company"));
	state->category = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "category"));

	state->comments = GTK_TEXT_VIEW (go_gtk_builder_get_widget (state->gui, "comments"));

	/* Properties Page */
	state->properties = GTK_TREE_VIEW (go_gtk_builder_get_widget (state->gui, "properties"));

	state->ppt_name  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "property-name"));
	state->ppt_value  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "property-value"));
	state->ppt_type  = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "type-combo"));
	state->type_store = GTK_LIST_STORE (gtk_builder_get_object (state->gui, "typestore"));
	state->type_store_filter = GTK_TREE_MODEL_FILTER (gtk_combo_box_get_model (state->ppt_type));

	state->add_button    = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "add_button"));
	state->remove_button = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "remove_button"));
	state->instruction   = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "instruction-label"));
	state->warning   = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "warning"));

	/* Keyword Page */
	state->key_tree_view = GTK_TREE_VIEW  (go_gtk_builder_get_widget (state->gui, "keyview"));
	state->key_store = GTK_LIST_STORE (gtk_tree_view_get_model (state->key_tree_view));
	state->key_add_button  = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "key-add-button"));
	state->key_remove_button  = GTK_BUTTON (go_gtk_builder_get_widget (state->gui, "key-remove-button"));

	/* Statistics Page */
	state->sheets = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "sheets"));
	state->cells  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "cells"));
	state->pages  = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "pages"));

	/* Calculations Page */
	state->recalc_auto = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "recalc_auto"));
	state->recalc_manual = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "recalc_manual"));
	state->recalc_iteration = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (state->gui, "iteration_enabled"));
	state->recalc_max  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "max_iterations"));
	state->recalc_tolerance  = GTK_ENTRY (go_gtk_builder_get_widget (state->gui, "iteration_tolerance"));
	state->recalc_iteration_grid = go_gtk_builder_get_widget (state->gui, "iteration-grid");
}

static void
dialog_doc_meta_data_add_item (DialogDocMetaData *state, char const *page_name,
			       char const *icon_name,
			       int page, char const* parent_path)
{
	GtkTreeIter iter, parent;
	GdkPixbuf * icon = NULL;

	if (icon_name != NULL)
		icon = gtk_widget_render_icon_pixbuf (state->dialog, icon_name,
					       GTK_ICON_SIZE_MENU);
	if ((parent_path != NULL) && gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (state->store),
									  &parent, parent_path))
		gtk_tree_store_append (state->store, &iter, &parent);
	else
		gtk_tree_store_append (state->store, &iter, NULL);

	gtk_tree_store_set (state->store, &iter,
			    ITEM_ICON, icon,
			    ITEM_NAME, _(page_name),
			    PAGE_NUMBER, page,
			    -1);
	if (icon != NULL)
		g_object_unref (icon);
}

typedef struct {
	char const *page_name;
	char const *icon_name;
	char const *parent_path;
	int  const page;
	void (*page_initializer) (DialogDocMetaData *state);
} page_info_t;

static page_info_t const page_info[] = {
	/* IMPORTANT: OBEY THE ORDER 0 - 3 - 2 - 1 */
	{N_("File"),        GTK_STOCK_FILE,   	  NULL, 0, &dialog_doc_metadata_init_file_page          },
	{N_("Statistics"),  "gnumeric-graphguru", NULL, 3 ,&dialog_doc_metadata_init_statistics_page    },
	{N_("Properties"),  GTK_STOCK_PROPERTIES, NULL, 2, &dialog_doc_metadata_init_properties_page    },
	{N_("Description"), GTK_STOCK_ABOUT,	  NULL, 1, &dialog_doc_metadata_init_description_page   },
	{N_("Keywords"),    GTK_STOCK_INDEX,	  NULL, 5, &dialog_doc_metadata_init_keywords_page   },
	{N_("Calculation"), GTK_STOCK_EXECUTE,    NULL, 4, &dialog_doc_metadata_init_calculations_page  },
	{NULL, NULL, NULL, -1, NULL},
};

typedef struct {
	int  const page;
	GtkTreePath *path;
} page_search_t;

static gboolean
dialog_doc_metadata_select_page_search (GtkTreeModel *model,
					GtkTreePath *path,
					GtkTreeIter *iter,
					page_search_t *pst)
{
	int page;
	gtk_tree_model_get (model, iter, PAGE_NUMBER, &page, -1);
	if (page == pst->page) {
		pst->path = gtk_tree_path_copy (path);
		return TRUE;
	} else
		return FALSE;
}

static void
dialog_doc_metadata_select_page (DialogDocMetaData *state, int page)
{
	page_search_t pst = {page, NULL};

	if (page >= 0)
		gtk_tree_model_foreach (GTK_TREE_MODEL (state->store),
					(GtkTreeModelForeachFunc) dialog_doc_metadata_select_page_search,
					&pst);

	if (pst.path == NULL)
		pst.path = gtk_tree_path_new_from_string ("0");

	if (pst.path != NULL) {
		gtk_tree_view_set_cursor (state->view, pst.path, NULL, FALSE);
		gtk_tree_view_expand_row (state->view, pst.path, TRUE);
		gtk_tree_path_free (pst.path);
	}
}

static void
cb_dialog_doc_metadata_selection_changed (GtkTreeSelection *selection,
					  DialogDocMetaData *state)
{
	GtkTreeIter iter;
	int page;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->store), &iter,
				    PAGE_NUMBER, &page,
				    -1);
		gtk_notebook_set_current_page (state->notebook, page);
	} else {
		dialog_doc_metadata_select_page (state, 0);
	}
}

static gboolean
dialog_doc_metadata_init (DialogDocMetaData *state,
			  WBCGtk *wbcg)
{
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	int i;

	state->wbcg     = wbcg;
	state->wb       = wb_control_get_workbook (GNM_WBC(wbcg));
	state->doc      = GO_DOC (state->wb);
	state->metadata = go_doc_get_meta_data (wb_control_get_doc (GNM_WBC (state->wbcg)));

	g_return_val_if_fail (state->metadata  != NULL, TRUE);

	state->gui = gnm_gtk_builder_load ("res:ui/doc-meta-data.ui", NULL,
	                                  GO_CMD_CONTEXT (wbcg));

        if (state->gui == NULL)
                return TRUE;

	dialog_doc_metadata_init_widgets (state);

	state->view = GTK_TREE_VIEW(go_gtk_builder_get_widget (state->gui, "itemlist"));
	state->store = gtk_tree_store_new (NUM_COLUMNS,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   G_TYPE_INT);
	gtk_tree_view_set_model (state->view, GTK_TREE_MODEL(state->store));
	g_object_unref (state->store);
	selection = gtk_tree_view_get_selection (state->view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_pixbuf_new (),
							   "pixbuf", ITEM_ICON,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	gtk_tree_view_set_expander_column (state->view, column);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_dialog_doc_metadata_selection_changed), state);

	for (i = 0; page_info[i].page > -1; i++) {
		const page_info_t *this_page =  &page_info[i];
		this_page->page_initializer (state);
		dialog_doc_meta_data_add_item (state, this_page->page_name, this_page->icon_name,
					       this_page->page, this_page->parent_path);
	}

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (state->store), ITEM_NAME, GTK_SORT_ASCENDING);

	/* A candidate for merging into attach guru */
	gnm_keyed_dialog (state->wbcg,
			       GTK_WINDOW (state->dialog),
			       DOC_METADATA_KEY);


	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				GTK_WINDOW (state->dialog));

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog), "state",
		state, (GDestroyNotify) dialog_doc_metadata_free);

	/* Help and Close buttons */
	gnm_init_help_button (GTK_WIDGET (state->help_button),
				   GNUMERIC_HELP_LINK_METADATA);

	g_signal_connect_swapped (G_OBJECT (state->close_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  state->dialog);

	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

/******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************/

/**
 * dialog_doc_metadata_new:
 * @wbcg: WBCGtk
 *
 * Creates a new instance of the dialog.
 **/
void
dialog_doc_metadata_new (WBCGtk *wbcg, int page)
{
	DialogDocMetaData *state;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, DOC_METADATA_KEY))
		return;

	state = g_new0 (DialogDocMetaData, 1);

	if (dialog_doc_metadata_init (state, wbcg)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg),
				      GTK_MESSAGE_ERROR,
				      _("Could not create the Properties dialog."));

		g_free (state);
		return;
	}

	dialog_doc_metadata_select_page (state, page);
}
