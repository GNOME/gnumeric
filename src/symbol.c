#include <config.h>
#include <glib.h>
#include <string.h>
#include "symbol.h"

static GHashTable *symbol_hash_table;

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
	
	g_hash_table_insert (symbol_hash_table, str, sym);
	
	return sym;
}

void
symbol_ref (Symbol *sym)
{
	g_return_if_fail (sym != NULL);

	sym->ref_count++;
}

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

void
symbol_unref (Symbol *sym)
{
	g_return_if_fail (sym != NULL);
	g_return_if_fail (sym->ref_count == 0);
	
	if (--(sym->ref_count) == 0){
		g_free (sym->str);
		g_free (sym);
	}
}

void
symbol_init (void)
{
	symbol_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
}
