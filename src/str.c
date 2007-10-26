/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * String management for the Gnumeric Spreadsheet
 *
 * Authors:
 *  Miguel de Icaza (miguel@kernel.org)
 *  Copyright 2007 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "str.h"
#include "gutils.h"

#include <string.h>
#include <goffice/utils/go-glib-extras.h>

#ifndef USE_STRING_POOLS
#ifdef HAVE_G_SLICE_ALLOC
#define USE_STRING_POOLS 0
#else
#define USE_STRING_POOLS 1
#endif
#endif

#if USE_STRING_POOLS
/* Memory pool for strings.  */
static GOMemChunk *string_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#ifdef HAVE_G_SLICE_ALLOC
#define CHUNK_ALLOC(T,c) g_slice_new (T)
#define CHUNK_FREE(p,v) g_slice_free1 (sizeof(*v),(v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif
#endif

static GHashTable *string_hash_table;

static GnmString *
gnm_string_lookup (char const *s)
{
	g_return_val_if_fail (s != NULL, NULL);
	return g_hash_table_lookup (string_hash_table, s);
}

GnmString *
gnm_string_get (char const *s)
{
	GnmString *string = gnm_string_lookup (s);
	if (string) {
		gnm_string_ref (string);
		return string;
	}

	/* If non-existant, create */
	string = CHUNK_ALLOC (GnmString, string_pool);
	string->ref_count = 1;
	string->str = g_strdup (s);

	g_hash_table_insert (string_hash_table, string->str, string);

	return string;
}

/*
 * gnm_string_get_nocopy :
 *
 * Take control of the supplied string.
 * delete it if it is already available.
 */
GnmString *
gnm_string_get_nocopy (char *s)
{
	GnmString *string = gnm_string_lookup (s);
	if (string) {
		gnm_string_ref (string);
		g_free (s);
		return string;
	}

	/* If non-existant, create */
	string = CHUNK_ALLOC (GnmString, string_pool);
	string->ref_count = 1;
	string->str = s;

	g_hash_table_insert (string_hash_table, string->str, string);

	return string;
}

GnmString *
gnm_string_ref (GnmString *string)
{
	g_return_val_if_fail (string != NULL, NULL);

	string->ref_count++;

	return string;
}

void
gnm_string_unref (GnmString *string)
{
	g_return_if_fail (string != NULL);
	g_return_if_fail (string->ref_count > 0);

	if (--(string->ref_count) == 0) {
		g_hash_table_remove (string_hash_table, string->str);
		g_free (string->str);
		CHUNK_FREE (string_pool, string);
	}
}

static void
cb_collect_strings (G_GNUC_UNUSED gpointer key, gpointer str,
		    gpointer user_data)
{
	GSList **pstrs = user_data;
	*pstrs = g_slist_prepend (*pstrs, str);
}

static gint
cb_by_refcount_str (gconstpointer a_, gconstpointer b_)
{
	const GnmString *a = a_;
	const GnmString *b = b_;

	if (a->ref_count == b->ref_count)
		return strcmp (a->str, b->str);
	return (a->ref_count - b->ref_count);
}

void
gnm_string_dump (void)
{
	GSList *strs = NULL;
	GSList *l;
	int refs = 0, len = 0;
	int count;

	g_hash_table_foreach (string_hash_table, cb_collect_strings, &strs);
	strs = g_slist_sort (strs, cb_by_refcount_str);
	count = g_slist_length (strs);
	for (l = strs; l; l = l->next) {
		const GnmString *s = l->data;
		refs += s->ref_count;
		len += strlen (s->str);
	}

	for (l = g_slist_nth (strs, MAX (0, count - 100)); l; l = l->next) {
		const GnmString *s = l->data;
		g_print ("%8d \"%s\"\n", s->ref_count, s->str);
	}
	g_print ("String table contains %d different strings.\n", count);
	g_print ("String table contains a total of %d characters.\n", len);
	g_print ("String table contains a total of %d refs.\n", refs);
	g_slist_free (strs);
}

void
gnm_string_init (void)
{
	string_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
#if USE_STRING_POOLS
	string_pool =
		go_mem_chunk_new ("string pool",
				   sizeof (GnmString),
				   16 * 1024 - 128);
#endif
}

static gboolean
cb_string_pool_leak (G_GNUC_UNUSED gpointer key,
		     gpointer value,
		     G_GNUC_UNUSED gpointer user)
{
	const GnmString *string = value;
	g_printerr ("Leaking string [%s] with ref_count=%d.\n",
		 string->str, string->ref_count);
	return TRUE;
}

void
gnm_string_shutdown (void)
{
	g_hash_table_foreach_remove (string_hash_table,
				     cb_string_pool_leak,
				     NULL);
	g_hash_table_destroy (string_hash_table);
	string_hash_table = NULL;
#if USE_STRING_POOLS
	go_mem_chunk_destroy (string_pool, FALSE);
	string_pool = NULL;
#endif
}

/**
 * gnm_string_concat:
 * @a : #GnmString
 * @b : #GnmString
 *
 * A place holder for when we add rich text and phonetics
 * to GnmString
 **/
GnmString *
gnm_string_concat (GnmString const *a, GnmString const *b)
{
	return gnm_string_get_nocopy (g_strconcat (a->str, b->str, NULL));
}

/**
 * gnm_string_concat_str:
 * @a : #GnmString
 * @b :
 *
 * A place holder for when we add rich text and phonetics
 * to GnmString
 **/
GnmString *
gnm_string_concat_str (GnmString const *a, char const *b)
{
	return gnm_string_get_nocopy (g_strconcat (a->str, b, NULL));
}
