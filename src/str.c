/*
 * String management for the Gnumeric Spreadsheet
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "str.h"
#include "gutils.h"
#include "stdio.h"

#include <string.h>

#ifndef USE_STRING_POOLS
#define USE_STRING_POOLS 1
#endif

#if USE_STRING_POOLS
/* Memory pool for strings.  */
static gnm_mem_chunk *string_pool;
#define CHUNK_ALLOC(T,p) ((T*)gnm_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) gnm_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif

static GHashTable *string_hash_table;

String *
string_lookup (const char *s)
{
	g_return_val_if_fail (s != NULL, NULL);
	return g_hash_table_lookup (string_hash_table, s);
}

String *
string_get (const char *s)
{
	String *string = string_lookup (s);
	if (string){
		string_ref (string);
		return string;
	}

	/* If non-existant, create */
	string = CHUNK_ALLOC (String, string_pool);
	string->ref_count = 1;
	string->str = g_strdup (s);

	g_hash_table_insert (string_hash_table, string->str, string);

	return string;
}

/*
 * string_get_nocopy :
 *
 * Take control of the supplied string.
 * delete it if it is already availabl.e
 */
String *
string_get_nocopy (char *s)
{
	String *string = string_lookup (s);
	if (string) {
		string_ref (string);
		g_free (s);
		return string;
	}

	/* If non-existant, create */
	string = CHUNK_ALLOC (String, string_pool);
	string->ref_count = 1;
	string->str = s;

	g_hash_table_insert (string_hash_table, string->str, string);

	return string;
}

String *
string_ref (String *string)
{
	g_return_val_if_fail (string != NULL, NULL);

	string->ref_count++;

	return string;
}

void
string_unref (String *string)
{
	g_return_if_fail (string != NULL);
	g_return_if_fail (string->ref_count > 0);

	if (--(string->ref_count) == 0){
		g_hash_table_remove (string_hash_table, string->str);
		g_free (string->str);
		CHUNK_FREE (string_pool, string);
	}
}

void
string_init (void)
{
	string_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
#if USE_STRING_POOLS
	string_pool =
		gnm_mem_chunk_new ("string pool",
				   sizeof (String),
				   16 * 1024 - 128);
#endif
}

#if USE_STRING_POOLS
static void
cb_string_pool_leak (gpointer data, gpointer user)
{
	String *string = data;
	fprintf (stderr, "Leaking string at %p: [%s].\n", string, string->str);
}

#endif

void
string_shutdown (void)
{
	g_hash_table_destroy (string_hash_table);
	string_hash_table = NULL;
#if USE_STRING_POOLS
	gnm_mem_chunk_foreach_leak (string_pool, cb_string_pool_leak, NULL);
	gnm_mem_chunk_destroy (string_pool, FALSE);
	string_pool = NULL;
#endif
}
