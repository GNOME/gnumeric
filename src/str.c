/*
 * String management for the Gnumeric Spreadsheet
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <config.h>
#include <glib.h>
#include <string.h>
#include "str.h"

static GHashTable *string_hash_table;

String *
string_lookup (const char *s)
{
	String *string;

	g_return_val_if_fail (s != NULL, NULL);
	string = (String *) g_hash_table_lookup (string_hash_table, s);
	return string;
}

String *
string_get (const char *s)
{
	String *string;

	string = string_lookup (s);
	if (string){
		string_ref (string);
		return string;
	}

	/* If non-existant, create */
	string = g_new (String, 1);
	string->ref_count = 1;
	string->str = g_strdup (s);

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
		g_free (string);
	}
}

/*
 * Decrements the reference count on *string, and if
 * it reaches zero, it also clears the value pointed
 * by string_ptr
 */
void
string_unref_ptr (String **string_ptr)
{
	g_return_if_fail (string_ptr != NULL);
	g_return_if_fail (*string_ptr != NULL);
	g_return_if_fail ((*string_ptr)->ref_count > 0);
	
	if (--((*string_ptr)->ref_count) == 0){
		g_hash_table_remove (string_hash_table, (*string_ptr)->str);
		g_free ((*string_ptr)->str);
		g_free (*string_ptr);
		*string_ptr = NULL;
	}
}

void
string_init (void)
{
	string_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
}

