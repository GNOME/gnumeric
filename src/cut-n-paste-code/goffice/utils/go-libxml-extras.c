/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xml-io.c: save/read gnumeric workbooks using gnumeric-1.0 style xml.
 *
 * Authors:
 *   Daniel Veillard <Daniel.Veillard@w3.org>
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 */
#include <goffice/goffice-config.h>
#include "go-libxml-extras.h"
#include "go-color.h"
#include "go-locale.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>

#define CC2XML(s)	((const xmlChar *)(s))
#define CXML2C(s)	((const char *)(s))

/* Get an xmlChar * value for a node carried as an attibute
 * result must be xmlFree
 */
xmlChar *
xml_node_get_cstr (xmlNodePtr node, char const *name)
{
	if (name != NULL)
		return xmlGetProp (node, CC2XML (name));
	/* in libxml1 <foo/> would return NULL
	 * in libxml2 <foo/> would return ""
	 */
	if (node->xmlChildrenNode != NULL)
		return xmlNodeGetContent (node);
	return NULL;
}
void
xml_node_set_cstr (xmlNodePtr node, char const *name, char const *val)
{
	if (name)
		xmlSetProp (node, CC2XML (name), CC2XML (val));
	else
		xmlNodeSetContent (node, CC2XML (val));
}

gboolean
xml_node_get_bool (xmlNodePtr node, char const *name, gboolean *val)
{
	xmlChar *buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	*val = (!strcmp (buf, "1")
		|| 0 == g_ascii_strcasecmp (buf, "true"));
	g_free (buf);
	return TRUE;
}

void
xml_node_set_bool (xmlNodePtr node, char const *name, gboolean val)
{
	xml_node_set_cstr (node, name, val ? "true" : "false");
}

gboolean
xml_node_get_int (xmlNodePtr node, char const *name, int *val)
{
	xmlChar *buf;
	char *end;

	buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	errno = 0; /* strto(ld) sets errno, but does not clear it.  */
	*val = strtol (CXML2C (buf), &end, 10);
	xmlFree (buf);

	/* FIXME: it is, strictly speaking, not valid to use buf here.  */
	return (CXML2C (buf) != end) && (errno != ERANGE);
}

void
xml_node_set_int (xmlNodePtr node, char const *name, int val)
{
	char str[4 * sizeof (int)];
	sprintf (str, "%d", val);
	xml_node_set_cstr (node, name, str);
}

gboolean
xml_node_get_double (xmlNodePtr node, char const *name, double *val)
{
	xmlChar *buf;
	char *end;

	buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	errno = 0; /* strto(ld) sets errno, but does not clear it.  */
	*val = strtod (CXML2C (buf), &end);
	xmlFree (buf);

	/* FIXME: it is, strictly speaking, now valid to use buf here.  */
	return (CXML2C (buf) != end) && (errno != ERANGE);
}

void
xml_node_set_double (xmlNodePtr node, char const *name, double val,
		     int precision)
{
	char str[101 + DBL_DIG];

	if (precision < 0 || precision > DBL_DIG)
		precision = DBL_DIG;

	if (fabs (val) < 1e9 && fabs (val) > 1e-5)
		snprintf (str, 100 + DBL_DIG, "%.*g", precision, val);
	else
		snprintf (str, 100 + DBL_DIG, "%f", val);

	xml_node_set_cstr (node, name, str);
}


gboolean
xml_node_get_gocolor (xmlNodePtr node, char const *name, GOColor *res)
{
	xmlChar *color;
	int r, g, b;

	color = xmlGetProp (node, CC2XML (name));
	if (color == NULL)
		return FALSE;
	if (sscanf (CXML2C (color), "%X:%X:%X", &r, &g, &b) == 3) {
		r >>= 8;
		g >>= 8;
		b >>= 8;
		*res = RGBA_TO_UINT (r,g,b,0xff);
		xmlFree (color);
		return TRUE;
	}
	xmlFree (color);
	return FALSE;
}

#include <gdk/gdkcolor.h>
void
xml_node_set_gocolor (xmlNodePtr node, char const *name, GOColor val)
{
	char str[4 * sizeof (val)];
	GdkColor tmp;
	go_color_to_gdk (val, &tmp);
	sprintf (str, "%X:%X:%X", tmp.red, tmp.green, tmp.blue);
	xml_node_set_cstr (node, name, str);
}
/*************************************************************************/

xmlNode *
e_xml_get_child_by_name (xmlNode const *parent, char const *child_name)
{
	xmlNode *child;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (child_name != NULL, NULL);

	for (child = parent->xmlChildrenNode; child != NULL; child = child->next) {
		if (xmlStrcmp (child->name, child_name) == 0) {
			return child;
		}
	}
	return NULL;
}

xmlNode *
e_xml_get_child_by_name_no_lang (xmlNode const *parent, char const *name)
{
	xmlNodePtr node;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (node = parent->xmlChildrenNode; node != NULL; node = node->next) {
		xmlChar *lang;

		if (node->name == NULL || strcmp (node->name, name) != 0) {
			continue;
		}
		lang = xmlGetProp (node, "xml:lang");
		if (lang == NULL) {
			return node;
		}
		xmlFree (lang);
	}

	return NULL;
}


xmlNode *
e_xml_get_child_by_name_by_lang (const xmlNode *parent, const gchar *name)
{
	xmlNodePtr   best_node = NULL, node;
	gint         best_lang_score = INT_MAX;
	GList const *lang_list = go_locale_languages ();

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (node = parent->xmlChildrenNode; node != NULL; node = node->next) {
		xmlChar *lang;

		if (node->name == NULL || strcmp (node->name, name) != 0)
			continue;

		lang = xmlGetProp (node, "xml:lang");
		if (lang != NULL) {
			const GList *l;
			gint i;

			for (l = lang_list, i = 0;
			     l != NULL && i < best_lang_score;
			     l = l->next, i++) {
				if (strcmp ((gchar *) l->data, lang) == 0) {
					best_node = node;
					best_lang_score = i;
				}
			}
		} else if (best_node == NULL)
			best_node = node;

		xmlFree (lang);
		if (best_lang_score == 0) 
			return best_node;
	}

	return best_node;
}

