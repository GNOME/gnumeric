/*
 * Symbol management for the Gnumeric spreadsheet
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <glib.h>
#include <string.h>
#include <ctype.h>
#include "symbol.h"

GHashTable *symbol_hash_table;

Symbol *
symbol_lookup (char *str)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);
	sym = (Symbol *) g_hash_table_lookup (symbol_hash_table, str);
	return sym;
}

Symbol *
symbol_lookup_substr (char *buffer, int len)
{
	char *str;
	Symbol *sym;
	
	g_return_val_if_fail (buffer != NULL, NULL);
	str = g_new (char, len + 1);
	strncpy (str, buffer, len);
	str [len] = 0;

	sym = symbol_lookup (str);
	g_free (str);
	
	return sym;
}

/*
 * symbol_install
 *
 * @str: the string name
 * @SymbolType: in which hash table we perform the lookup
 * @data: information attached to the symbol
 */
Symbol *
symbol_install (char *str, SymbolType type, void *data)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);

	sym = g_new (Symbol, 1);
	sym->ref_count = 1;
	sym->type = type;
	sym->data = data;
	sym->str  = g_strdup (str);
	
	g_hash_table_insert (symbol_hash_table, sym->str, sym);
	
	return sym;
}

/*
 * symbol_ref:
 * @Sym: The symbol to reference
 *
 * Increments the reference count for the symbol
 */
void
symbol_ref (Symbol *sym)
{
	g_return_if_fail (sym != NULL);

	sym->ref_count++;
}

/*
 * symbol_ref_string:
 * @str:  string to be converted to a symbol
 *
 * This looks up the string on the symbol hash table,
 * if it is found, it is references, otherwise a new
 * symbol is created
 */
Symbol *
symbol_ref_string (char *str)
{
	Symbol *sym;

	sym = symbol_lookup (str);
	if (sym){
		symbol_ref (sym);
		return sym;
	}
	return symbol_install (str, SYMBOL_STRING, 0);
}

/*
 * symbol_unref:
 * @Sym:  The symbol to remove the reference from
 *
 * Unreferences a symbol.  If the count reaches zero, the symbol
 * is deallocated
 */
void
symbol_unref (Symbol *sym)
{
	g_return_if_fail (sym != NULL);
	g_return_if_fail (sym->ref_count > 0);
	
	if (--(sym->ref_count) == 0){
		g_hash_table_remove (symbol_hash_table, sym->str);
		g_free (sym->str);
		g_free (sym);
	}
}

static gint
g_strcase_equal (gconstpointer v1, gconstpointer v2)
{
	return strcasecmp ((const gchar*) v1, (const gchar*) v2) == 0;
}


static guint
g_strcase_hash (gconstpointer v)
{
	const char *s = (char *) v;
	const char *p;
	guint h = 0, g;
	
	for (p = s; *p != '\0'; p += 1){
		h = (h << 4) + toupper (*p);
		if ((g = h & 0xf0000000)){
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}
	
	return h /* % M */;
}

void
symbol_init (void)
{
	symbol_hash_table = g_hash_table_new (g_strcase_hash, g_strcase_equal);
}

