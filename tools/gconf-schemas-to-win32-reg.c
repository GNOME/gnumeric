/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * schemas-to-reg.c : Convert gconf's .schemas file into Win32 Registry's .reg file
 *
 * Copyright (C) 2005 Ivan, Wong Yat Cheung  email@ivanwong.info
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

#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <glib-2.0/glib.h>

#define GCONF_APP_ROOT "/apps"
#define WIN32_USERS_APP_ROOT "[HKEY_USERS\\.DEFAULT\\Software"
#define WIN32_CURRENT_USER_APP_ROOT "[HKEY_CURRENT_USER\\Software"

static gboolean current_user = FALSE;

static GOptionEntry entries[] = 
{
	{ "current-user-only", 'c', 0, G_OPTION_ARG_NONE, &current_user,
	  "Output HKEY_CURRENT_USER instead of HKEY_USERS.", NULL },
	{ NULL }
};

enum {
	KEY_TYPE_NONE = 0,
	KEY_TYPE_INT,
	KEY_TYPE_BOOL,
	KEY_TYPE_STRING,
	KEY_TYPE_FLOAT,
	KEY_TYPE_LIST
};

typedef struct _KeyInfo KeyInfo;
struct _KeyInfo {
	gchar *name;
	gint type;
	gint list_type;
	gchar *def;
};

typedef struct _NodeInfo NodeInfo;
struct _NodeInfo {
	gchar *name;
	GSList *values;
};

static gint
get_type_from_str (const gchar *str)
{
	gint type;

	if (!xmlStrcmp (str, "int"))
		type = KEY_TYPE_INT;
	else if (!xmlStrcmp (str, "bool"))
		type = KEY_TYPE_BOOL;
	else if (!xmlStrcmp (str, "string"))
		type = KEY_TYPE_STRING;
	else if (!xmlStrcmp (str, "float"))
		type = KEY_TYPE_FLOAT;
	else if (!xmlStrcmp (str, "list"))
		type = KEY_TYPE_LIST;
	else
		type = KEY_TYPE_NONE;

	return type;
}

static void
print_string (gchar *str)
{
	for (; *str; ++str) {
		switch (*str) {
		case '\\':
		case '"':
			putchar ('\\');
			break;
		}
		putchar (*str);
	}
}

static void
convert_key (KeyInfo *info)
{
	gchar *ptr;

	g_print ("\"%s\"=", info->name);

	switch (info->type) {
	case KEY_TYPE_INT:
		g_print ("dword:%08x", (gint) atol (info->def));
		break;
	case KEY_TYPE_BOOL:
		g_print ("hex:0%d", g_ascii_strncasecmp (info->def, "TRUE", 4) ? 0 : 1);
		break;
	case KEY_TYPE_STRING:
		putchar ('"');
		if (info->def)
			print_string (info->def);
		putchar ('"');
		break;
	case KEY_TYPE_FLOAT:
		g_print ("\"%s\"", info->def);
		break;
	case KEY_TYPE_LIST:
		putchar ('"');
		if ((ptr = info->def) != NULL && *(ptr++) == '[') {
			GString *temp_str;
			gchar c, *str;
			gboolean end = FALSE;

			temp_str = g_string_new ("");
			for (; (c = *ptr) != '\0'; ++ptr) {
				if (c == '\\') {
					if (!(c = *(++ptr)))
						break;
				}
				else if (c == ',' || (end = c == ']')) {
					str = g_strescape (temp_str->str, NULL);
					print_string (temp_str->str);
					putchar ('\n');
					g_free (str);
					temp_str->len = 0;
					temp_str->str[0] = '\0';
					if (end)
						break;
					else
						continue;
				}
				g_string_append_c (temp_str, c);
			}
			if (!end)
				g_warning ("String List %s is not properly ended", info->name);
			g_string_free (temp_str, TRUE);
		}
		putchar ('"');
		break;
	default:
		g_printerr ("General type not implemented: %d\n", info->type);
	}

	putchar ('\n');
}

static void
convert_schema (xmlDocPtr doc, xmlNodePtr schema, GHashTable *table)
{
	xmlNodePtr n, node = schema->xmlChildrenNode;
	xmlChar *str;
	KeyInfo info = {0};
	gboolean is_locale;

	while (node) {
		if (node->type != XML_ELEMENT_NODE) {
			node = node->next;
			continue;
		}

		if (!xmlStrcmp (node->name, "applyto")) {
			str = xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
			if (info.name)
				xmlFree (info.name);
			info.name = str;
		}
		else if (!xmlStrcmp (node->name, "type")) {
			str = xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
			info.type = get_type_from_str (str);
			xmlFree (str);
		}
		else if (!xmlStrcmp (node->name, "list_type")) {
			str = xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
			info.list_type = get_type_from_str (str);
			xmlFree (str);
		}
		else if (!(is_locale = xmlStrcmp (node->name, "default")) ||
			 !xmlStrcmp (node->name, "locale")) {
			if (is_locale) {
				n = node->xmlChildrenNode;
				while (n) {
					if (n->type == XML_ELEMENT_NODE &&
					    !xmlStrcmp (n->name, "default"))
						break;
					n = n->next;
				}
			}
			else
				n = node;

			if (n) {
				str = xmlNodeListGetString (doc, n->xmlChildrenNode, 1);
				g_free (info.def);
				info.def = g_strdup (str ? (gchar *) str : "");
				if (str)
					xmlFree (str);
			}
		}

		node = node->next;
	}

	if (info.name && info.type &&
	    (info.type != KEY_TYPE_LIST || info.list_type) &&
	    (info.type == KEY_TYPE_LIST || info.def)) {
		GNode *node = NULL;
		gchar *end;
		KeyInfo *new_info;
		gint i, cnt = 0;
		GSList **values;

		if (xmlStrncmp (info.name, GCONF_APP_ROOT "/", 6)) {
			g_printerr ("Don't know how to handle this key: %s\n", info.name);
			return;
		}

		do {
			if (!(end = strrchr (info.name, '/')))
				break;
			++cnt;
			*end = '\0';
		} while (!(node = g_hash_table_lookup (table, info.name)));

		if (!node) {
			g_printerr ("Can find the root node?\n");
			return;
		}

		for (i = 1; i < cnt; ++i) {
			NodeInfo *node_info = g_new (NodeInfo, 1);
			node_info->name = g_strdup (end + 1);
			node_info->values = NULL;
			*end = '/';
			end += strlen (node_info->name) + 1;
			node = g_node_insert_data (node, -1, node_info);
			g_hash_table_insert (table, g_strdup (info.name), node);
		}
	
		new_info = g_new (KeyInfo, 1);
		*new_info = info;
		new_info->name = g_strdup (end + 1);
		values = &((NodeInfo *) node->data)->values;
		*values = g_slist_prepend (*values, new_info);
			
		xmlFree (info.name);
	}
	else
		g_warning ("Invalid key %s", info.name ? info.name : "(unknown name)");
}

static gboolean
free_node (GNode *node, gpointer data)
{
	GSList *values;
	KeyInfo *key_info;
	NodeInfo *info = (NodeInfo *) node->data;

	g_free (info->name);
	for (values = info->values; values; values = values->next) {
		key_info = (KeyInfo *) values->data;
		g_free (key_info->name);
		g_free (key_info->def);
		g_free (key_info);
	}
	g_slist_free (info->values);
	
	return FALSE;
}

static gboolean
print_node (GNode *node, gpointer data)
{
	GNode *n, *root = (GNode *) data;
	GSList *values;
	NodeInfo *info = (NodeInfo *) node->data;
	GString *path = g_string_new ("");

	if (node == root)
		return FALSE;

	g_print (current_user ? WIN32_CURRENT_USER_APP_ROOT : WIN32_USERS_APP_ROOT);

	for (n = node; n != root; n = n->parent) {
		g_string_prepend (path, ((NodeInfo *) n->data)->name);
		g_string_prepend_c (path, '\\');
	}
	g_print ("%s]\n", path->str);
	g_string_free (path, TRUE);
	
	for (values = info->values; values; values = values->next)
		convert_key ((KeyInfo *) values->data);
	
	putchar ('\n');
	
	return FALSE;
}

static void
convert_schemalist (xmlDocPtr doc, xmlNodePtr schemalist)
{
	xmlNodePtr node = schemalist->xmlChildrenNode;
	GHashTable *table;
	GNode *root;
	NodeInfo *info;

	table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	info = g_new (NodeInfo, 1);
	info->name = g_strdup (GCONF_APP_ROOT);
	info->values = NULL;
	root = g_node_new (info);
	g_hash_table_insert (table, g_strdup (GCONF_APP_ROOT), root);

	g_print ("REGEDIT4\n\n");

	while (node) {
		if (node->type == XML_ELEMENT_NODE &&
		    !xmlStrcmp (node->name, "schema"))
			convert_schema (doc, node, table);
		node = node->next;
	}

	g_hash_table_destroy (table);
	g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_ALL, -1, print_node, root);
	g_node_traverse (root, G_POST_ORDER, G_TRAVERSE_ALL, -1, free_node, NULL);
}

static void
convert (xmlDocPtr doc, xmlNodePtr root)
{
	xmlNodePtr node = NULL;

	if (!root) {
		g_printerr ("Empty document\n");
		return;
	}

	if (xmlStrcmp (root->name, "gconfschemafile")) {
		g_printerr ("Document of the wrong type, root node != gconfschemafile\n");
		return;
	}

	node = root->xmlChildrenNode;

	while (node) {
		if (node->type == XML_ELEMENT_NODE &&
		    !xmlStrcmp (node->name, "schemalist"))
			convert_schemalist (doc, node);
		node = node->next;
	}
}

int main (int argc, char **argv)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GOptionContext *context;

	context = g_option_context_new ("inputfile\nConvert gconf .schemas file into win32 registry .reg file");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (argc != 2) {
		g_printerr ("Please provide exactly one input filename\n");
		return 1;
	}

	LIBXML_TEST_VERSION

	doc = xmlReadFile (argv[1], NULL, 0);
	if (!doc) {
		g_printerr ("Failed to parse %s\n", argv[1]);
		return 2;
	}

	root = xmlDocGetRootElement (doc);
	convert (doc, root);

	xmlFreeDoc (doc);
	xmlCleanupParser ();

	return 0;
}
