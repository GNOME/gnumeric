/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-font.c :
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
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

#include <goffice/goffice-config.h>
#include "go-font.h"
#include <pango/pango.h>
#include <pango/pangoft2.h>
#include <gdk/gdkscreen.h>
#include <gdk/gdkpango.h>
#include <gtk/gtkmain.h>
#include <stdlib.h>
#include <gmodule.h>

static GHashTable   *font_hash;
static GPtrArray    *font_array;
static GSList	    *font_watchers;
static GOFont const *font_default;
static PangoFontFamily	**pango_families;

GSList *go_font_family_list = NULL;
unsigned const go_font_sizes [] = {
	4, 8, 9, 10, 11, 12, 14, 16, 18,
	20, 22, 24, 26, 28, 36, 48, 72,
	0
};

#if 0
#define ref_debug(x)	x
#else
#define ref_debug(x)	do { } while (0)
#endif

static void
go_font_free (GOFont *font)
{
	g_return_if_fail (font->ref_count == 1);
	pango_font_description_free (font->desc);
	g_free (font);
}

/**
 * go_font_new_by_desc:
 * @desc : #PangoFontDescription
 *
 * Aborbs @desc and returns a ref to a font that matches it.
 **/
GOFont const *
go_font_new_by_desc (PangoFontDescription *desc)
{
	GOFont *font = g_hash_table_lookup (font_hash, desc);

	if (font == NULL) {
		int i = font_array->len;

		while (i-- > 0 && g_ptr_array_index (font_array, i) != NULL)
			;

		font = g_new0 (GOFont, 1);
		font->desc = desc; /* absorb it */
		font->ref_count = 1; /* one for the hash */
		ref_debug (g_warning ("created %p = 1", font););
		if (i < 0) {
			i = font_array->len;
			g_ptr_array_add (font_array, font);
		} else
			g_ptr_array_index (font_array, i) = font;
		font->font_index = i;
		g_hash_table_insert (font_hash, font->desc, font);
	} else
		pango_font_description_free (desc);	/* free it */

	return go_font_ref (font); /* and another ref for the result */
}

GOFont const *
go_font_new_by_name  (char const *str)
{
	return go_font_new_by_desc (pango_font_description_from_string (str));
}

GOFont const *
go_font_new_by_index (unsigned i)
{
	g_return_val_if_fail (i < font_array->len, NULL);
	return go_font_ref (g_ptr_array_index (font_array, i));
}

char *
go_font_as_str (GOFont const *font)
{
	g_return_val_if_fail (font != NULL, g_strdup (""));
	return pango_font_description_to_string (font->desc);
}

GOFont const *
go_font_ref (GOFont const *font)
{
	g_return_val_if_fail (font != NULL, NULL);
	((GOFont *)font)->ref_count++;
	ref_debug (g_warning ("ref added %p = %d", font, font->ref_count););
	return font;
}

void
go_font_unref (GOFont const *font)
{
	g_return_if_fail (font != NULL);

	if (--((GOFont *)font)->ref_count == 1) {
		GValue instance_and_params[2];
		GSList *ptr;

		for (ptr = font_watchers; ptr != NULL ; ptr = ptr->next) {
			GClosure *watcher = ptr->data;
			gpointer data = watcher->is_invalid ? 
				NULL : watcher->data;
			
			instance_and_params[0].g_type = 0;
			g_value_init (&instance_and_params[0], G_TYPE_POINTER);
			g_value_set_pointer (&instance_and_params[0], (gpointer)font);

			instance_and_params[1].g_type = 0;
			g_value_init (&instance_and_params[1], G_TYPE_POINTER);
			g_value_set_pointer (&instance_and_params[1], data);

			g_closure_invoke (watcher, NULL, 2,
				instance_and_params, NULL);
		}
		g_ptr_array_index (font_array, font->font_index) = NULL;
		g_hash_table_remove (font_hash, font->desc);
		ref_debug (g_warning ("unref removed %p = 1 (and deleted)", font););
	} else
		ref_debug (g_warning ("unref removed %p = %d", font, font->ref_count););
}

gboolean
go_font_eq (GOFont const *a, GOFont const *b)
{
	return pango_font_description_equal (a->desc, b->desc);
}

void
go_font_cache_register (GClosure *watcher)
{
	g_return_if_fail (watcher != NULL);

	font_watchers = g_slist_prepend (font_watchers, watcher);
	g_closure_set_marshal (watcher,
		g_cclosure_marshal_VOID__POINTER);
}

void
go_font_cache_unregister (GClosure *watcher)
{
	font_watchers = g_slist_remove (font_watchers, watcher);
}

/* ancient holdover code to cache the available fonts
 * This is not a good idea.  We should pull this from pango as needed */
static int
compare_family_pointers_by_name (gconstpointer a, gconstpointer b)
{
	PangoFontFamily * const * const fa = a;
	PangoFontFamily * const * const fb = b;
	return g_utf8_collate (pango_font_family_get_name (*fa),
			       pango_font_family_get_name (*fb));
}

/**
 * go_pango_context_get :
 *
 * Simple wrapper to handle windowless operation
 **/
PangoContext *
go_pango_context_get (void)
{
	PangoContext *context;
	GdkScreen *screen = gdk_screen_get_default ();

	if (screen != NULL) {
		context = gdk_pango_context_get_for_screen (screen);
		/* FIXME: barf!  */
		gdk_pango_context_set_colormap (context,
			gdk_screen_get_default_colormap (screen));
	} else {
		PangoFontMap *fontmap = pango_ft2_font_map_new ();
		pango_ft2_font_map_set_resolution (PANGO_FT2_FONT_MAP (fontmap), 96, 96);
		context = pango_ft2_font_map_create_context (PANGO_FT2_FONT_MAP (fontmap));
	}
	pango_context_set_language (context, gtk_get_default_language ());
	pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);

	return context;
}

static void (*fake_pango_fc_font_map_cache_clear) (PangoFcFontMap *);

void
go_pango_fc_font_map_cache_clear (PangoFcFontMap *font_map)
{
	if (fake_pango_fc_font_map_cache_clear)
		fake_pango_fc_font_map_cache_clear (font_map);
}

/* private */
void
go_font_init (void)
{
	PangoContext *context;
	unsigned i, n_families;

	GModule *self = g_module_open (NULL, 0);
	if (self) {
		gpointer sym;
		if (g_module_symbol (self, "pango_fc_font_map_cache_clear", &sym))
			fake_pango_fc_font_map_cache_clear = sym;
		g_module_close (self);
	}

	font_array = g_ptr_array_new ();
	font_hash = g_hash_table_new_full (
		(GHashFunc)pango_font_description_hash,
		(GEqualFunc)pango_font_description_equal,
		NULL, (GDestroyNotify) go_font_free);
	font_default = go_font_new_by_desc (
		pango_font_description_from_string ("Sans 8"));

	context = go_pango_context_get ();
	pango_context_list_families (context,
		&pango_families, &n_families);
	qsort (pango_families, n_families, sizeof (*pango_families),
	       compare_family_pointers_by_name);

	for (i = 0 ; i < n_families ; i++)
		go_font_family_list = g_slist_prepend (
			go_font_family_list,
			(gpointer) pango_font_family_get_name (pango_families [i]));

	go_font_family_list = g_slist_reverse (go_font_family_list);

	g_object_unref (G_OBJECT (context));
}

void
go_font_shutdown (void)
{
	go_font_unref (font_default);
	font_default = NULL;
	g_ptr_array_free (font_array, TRUE);
	font_array = NULL;
	g_hash_table_destroy (font_hash);
	font_hash = NULL;

	if (font_watchers != NULL) {
		g_warning ("Missing calls to go_font_cache_unregister");
		/* be careful and _leak_ the closured in case they are already freed */
		g_slist_free (font_watchers);
	}

	g_free (pango_families);
	pango_families = NULL;
	g_slist_free (go_font_family_list);
	go_font_family_list = NULL;
}
