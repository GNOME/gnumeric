/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-theme.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
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
#include <goffice/graph/gog-theme.h>
#include <goffice/graph/gog-style.h>
#include <goffice/utils/go-color.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <string.h>

typedef void (GogThemeStyleMap) (GogStyle *style, unsigned ind);
typedef struct {
	GogStyle *style;
	GogThemeStyleMap	*map;
} GogThemeElement;
struct _GogTheme {
	GObject	base;

	char 		*name;
	char 		*load_from_file; /* optionally NULL */
	GHashTable	*elem_hash_by_class;
	GHashTable	*elem_hash_by_name;
};
typedef GObjectClass GogThemeClass;

static GObjectClass *parent_klass;
static GSList *themes;
static GogTheme *default_theme = NULL;

static void
gog_theme_finalize (GObject *obj)
{
	GogTheme *theme = GOG_THEME (obj);

	g_free (theme->name); theme->name = NULL;
	g_free (theme->load_from_file); theme->load_from_file = NULL;
	if (theme->elem_hash_by_class)
		g_hash_table_destroy (theme->elem_hash_by_class);
	if (theme->elem_hash_by_name)
		g_hash_table_destroy (theme->elem_hash_by_name);

	if (parent_klass != NULL && parent_klass->finalize != NULL)
		(parent_klass->finalize) (obj);
}

static void
gog_theme_class_init (GogThemeClass *klass)
{
	GObjectClass *gobject_klass   = (GObjectClass *) klass;

	parent_klass = g_type_class_peek_parent (klass);
	gobject_klass->finalize	    = gog_theme_finalize;
}

static void
gog_theme_init (GogTheme *theme)
{
	theme->elem_hash_by_class =
		g_hash_table_new (g_direct_hash, g_direct_equal);
	theme->elem_hash_by_name =
		g_hash_table_new (g_str_hash, g_str_equal);
}

GSF_CLASS (GogTheme, gog_theme,
	   gog_theme_class_init, gog_theme_init,
	   G_TYPE_OBJECT)

void
gog_theme_init_style (GogTheme *theme, GogStyle *style,
		      GObjectClass *klass, int ind)
{
	GogThemeElement *elem;

	if (theme == NULL)
		theme = default_theme;

	g_return_if_fail (theme != NULL);

	if (theme->load_from_file != NULL) {
#warning TODO parse some xml
	}

	elem = g_hash_table_lookup (theme->elem_hash_by_class, klass);
	if (elem == NULL) {
		while (1) {
			elem = g_hash_table_lookup (theme->elem_hash_by_name,
				G_OBJECT_CLASS_NAME (klass));
			if (elem != NULL)
				break;
			klass = g_type_class_peek_parent (klass);
			g_return_if_fail (klass != NULL);
		}
		g_hash_table_insert (theme->elem_hash_by_class, klass, elem);
	}

	gog_style_copy (style, elem->style);
	if (ind >= 0 && elem->map)
		(elem->map) (style, ind);
}

void
gog_theme_register (GogTheme *theme, gboolean is_default)
{
	g_return_if_fail (GOG_THEME (theme) != NULL);

	if (is_default) {
		g_object_ref (theme);
		if (default_theme != NULL)
			g_object_unref (default_theme);
		default_theme = theme;
	}

	themes = g_slist_prepend (themes, theme);
}

static GogTheme *
gog_theme_new (char const *name)
{
	GogTheme *theme = g_object_new (GOG_THEME_TYPE, NULL);
	theme->name = g_strdup (name);
	return theme;
}

void
gog_theme_register_file (char const *name, char const *file)
{
	GogTheme *theme = gog_theme_new (name);
	theme->load_from_file = g_strdup (file);
}

static void
gog_theme_element_add_applies_to (GogTheme *theme, GogThemeElement *elem,
				  char const *applies_to)
{
	g_hash_table_insert (theme->elem_hash_by_name,
		(gpointer)applies_to, elem);
}

static GogThemeElement *
gog_theme_add_element_full (GogTheme *theme, GogStyle *style,
			    GogThemeStyleMap	*map)
{
	GogThemeElement *res = g_new0 (GogThemeElement, 1);
	res->style = style;
	res->map = map;
	return res;
}

static void
gog_theme_add_element (GogTheme *theme, GogStyle *style,
		       char const *applies_to)
{
	gog_theme_element_add_applies_to (theme, 
		gog_theme_add_element_full (theme, style, NULL),
		applies_to);
}

/**************************************************************************/

static void
map_area_series_solid_default (GogStyle *style, unsigned ind)
{
	static GOColor palette [] = {
		0x9c9cffff, 0x9c3163ff, 0xffffceff, 0xceffffff, 0x630063ff,
		0xff8484ff, 0x0063ceff, 0xceceffff, 0x000084ff, 0xff00ffff,
		0xffff00ff, 0x00ffffff, 0x840084ff, 0x840000ff, 0x008484ff,
		0x0000ffff, 0x00ceffff, 0xceffffff, 0xceffceff, 0xffff9cff,
		0x9cceffff, 0xff9cceff, 0xce9cffff, 0xffce9cff, 0x3163ffff,
		0x31ceceff, 0x9cce00ff, 0xffce00ff, 0xff9c00ff, 0xff6300ff,
		0x63639cff, 0x949494ff, 0x003163ff, 0x319c63ff, 0x003100ff,
		0x313100ff, 0x9c3100ff, 0x9c3163ff, 0x31319cff, 0x313131ff,
		0xffffffff, 0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xffff00ff,
		0xff00ffff, 0x00ffffff, 0x840000ff, 0x008400ff, 0x000084ff,
		0x848400ff, 0x840084ff, 0x008484ff, 0xc6c6c6ff, 0x848484ff
	};
	if (ind >= G_N_ELEMENTS (palette))
		ind %= G_N_ELEMENTS (palette);
	style->fill.u.solid.color = palette [ind];
}

static void
map_area_series_solid_guppi (GogStyle *style, unsigned ind)
{
	static GOColor palette[] = {
		0xff3000ff, 0x80ff00ff, 0x00ffcfff, 0x2000ffff,
		0xff008fff, 0xffbf00ff, 0x00ff10ff, 0x009fffff,
		0xaf00ffff, 0xff0000ff, 0xafff00ff, 0x00ff9fff,
		0x0010ffff, 0xff00bfff, 0xff8f00ff, 0x20ff00ff,
		0x00cfffff, 0x8000ffff, 0xff0030ff, 0xdfff00ff,
		0x00ff70ff, 0x0040ffff, 0xff00efff, 0xff6000ff,
		0x50ff00ff, 0x00ffffff, 0x5000ffff, 0xff0060ff,
		0xffef00ff, 0x00ff40ff, 0x0070ffff, 0xdf00ffff
	};
	if (ind >= G_N_ELEMENTS (palette))
		ind %= G_N_ELEMENTS (palette);
	style->fill.u.solid.color = palette [ind];
}

/**************************************************************************/

void
gog_themes_init	(void)
{
	GogTheme *theme;
	GogStyle *style;
	GogThemeElement *elem;

	/* An MS Excel-ish theme * TODO : have a look at apple's themes */
	theme = gog_theme_new (_("Default"));
	style = gog_style_new (); /* graph */
		style->outline.width = -1;
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.is_auto = FALSE;
		style->fill.u.solid.color = RGBA_WHITE;
		gog_theme_add_element (theme, style, "GogGraph");
	style = gog_style_new (); /* chart */
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.type = GOG_FILL_STYLE_NONE;
		gog_theme_add_element (theme, style, "GogChart");
	style = gog_style_new (); /* legend */
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.is_auto = FALSE;
		style->fill.u.solid.color = RGBA_WHITE;
		gog_theme_add_element (theme, style, "GogLegend");
	style = gog_style_new (); /* series */
		style->outline.width = 0.; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.is_auto = TRUE;
		elem = gog_theme_add_element_full (theme, style,
			map_area_series_solid_default);
		/* FIXME : not really true, will want to split area from line */
		gog_theme_element_add_applies_to (theme, elem, "GogSeries");
	gog_theme_register (theme, TRUE);

	/* guppi theme */
	theme = gog_theme_new (_("Guppi"));
	style = gog_style_new (); /* graph */
		style->outline.width = -1;
		style->fill.type = GOG_FILL_STYLE_GRADIENT;
		style->fill.u.gradient.start = RGBA_BLUE;
		style->fill.u.gradient.end = RGBA_BLACK;
		style->fill.u.gradient.type = GOG_GRADIENT_N_TO_S;
		gog_theme_add_element (theme, style, "GogGraph");
	style = gog_style_new (); /* chart */
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.type = GOG_FILL_STYLE_NONE;
		gog_theme_add_element (theme, style, "GogChart");
	style = gog_style_new (); /* legend */
		style->outline.width = 0; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.u.solid.color = RGBA_GREY (0xB0);
		gog_theme_add_element (theme, style, "GogLegend");
	style = gog_style_new (); /* series */
		style->outline.width = 0.; /* hairline */
		style->outline.color = RGBA_BLACK;
		style->fill.type = GOG_FILL_STYLE_SOLID;
		style->fill.u.solid.is_auto = FALSE;
		style->fill.u.solid.color = RGBA_GREY (0xB0);
		elem = gog_theme_add_element_full (theme, style,
			map_area_series_solid_guppi);
		/* FIXME : not really true, will want to split area from line */
		gog_theme_element_add_applies_to (theme, elem, "GogSeries");
	gog_theme_register (theme, FALSE);
}

GogTheme *
gog_theme_lookup (char const *name)
{
	GSList *ptr;
	GogTheme *theme;

	if (name != NULL) {
		for (ptr = themes ; ptr != NULL ; ptr = ptr->next) {
			theme = ptr->data;
			if (!strcmp (theme->name, name))
				return theme;
		}
		g_warning ("No theme named '%s' found, using default", name);
	}
	return default_theme;
}

char const *
gog_theme_get_name (GogTheme const *theme)
{
	g_return_val_if_fail (GOG_THEME (theme) != NULL, "");
	return theme->name;
}
