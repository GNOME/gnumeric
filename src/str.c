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

#include <string.h>

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

/*
 * string_get_nocopy :
 *
 * Take control of the supplied string.
 * delete it if it is already availabl.e
 */
String *
string_get_nocopy (char *s)
{
	String *string;

	string = string_lookup (s);
	if (string){
		string_ref (string);
		g_free (s);
		return string;
	}

	/* If non-existant, create */
	string = g_new (String, 1);
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
		g_free (string);
	}
}

void
string_init (void)
{
	string_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
}

