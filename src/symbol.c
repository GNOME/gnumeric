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
#include <stdio.h>
#include "symbol.h"
#include "utils.h"

SymbolTable *global_symbol_table;

/**
 * symbol_lookup:
 * @st: The symbol table where lookup takes place
 * @str: string to be looked up in the symbol table
 */
Symbol *
symbol_lookup (SymbolTable *st, const char *str)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (st != NULL, NULL);
	
	sym = (Symbol *) g_hash_table_lookup (st->hash, str);
	return sym;
}

Symbol *
symbol_lookup_substr (SymbolTable *st, const char *buffer, int len)
{
	char *str;
	Symbol *sym;
	
	g_return_val_if_fail (buffer != NULL, NULL);
	g_return_val_if_fail (st != NULL, NULL);
	
	str = g_new (char, len + 1);
	strncpy (str, buffer, len);
	str [len] = 0;

	sym = symbol_lookup (st, str);
	g_free (str);
	
	return sym;
}

/**
 * symbol_install:
 *
 * @st: The symbol table
 * @str: the string name
 * @SymbolType: in which hash table we perform the lookup
 * @data: information attached to the symbol
 */
Symbol *
symbol_install (SymbolTable *st, const char *str, SymbolType type, void *data)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (st != NULL, NULL);

	sym = (Symbol *) g_hash_table_lookup (st->hash, str);
	if (sym) printf ("(leak) Symbol [%s] redefined.\n", str);

	sym = g_new (Symbol, 1);
	sym->ref_count = 1;
	sym->type = type;
	sym->data = data;
	sym->str  = g_strdup (str);
	sym->st   = st;
	
	g_hash_table_insert (st->hash, sym->str, sym);
	
	return sym;
}

gboolean
symbol_is_unused (Symbol *sym)
{
	g_return_val_if_fail (sym != NULL, FALSE);
	g_return_val_if_fail (sym->ref_count > 0, FALSE);

	return sym->ref_count <= 1;
}

void
symbol_remove (SymbolTable *st, Symbol *sym)
{
	g_return_if_fail (st != NULL);
	g_return_if_fail (sym != NULL);
	g_return_if_fail (st->hash != NULL);
	g_return_if_fail (sym->ref_count > 0);
	g_return_if_fail (symbol_is_unused (sym));

	g_hash_table_remove (st->hash, sym);

	if (sym->str)
		g_free (sym->str);
	sym->str  = NULL;
	sym->data = NULL;
	sym->type = 0;
	sym->ref_count = -1;
	sym->st = NULL;

	g_free (sym);
}


/**
 * symbol_ref:
 * @sym: The symbol to reference
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
symbol_ref_string (SymbolTable *st, const char *str)
{
	Symbol *sym;

	g_return_val_if_fail (st != NULL, NULL);
	g_return_val_if_fail (str != NULL, NULL);
	
	sym = symbol_lookup (st, str);
	if (sym){
		symbol_ref (sym);
		return sym;
	}
	return symbol_install (st, str, SYMBOL_STRING, 0);
}

/*
 * symbol_unref:
 * @Sym:  The symbol to remove the reference from
 *
 * Unreferences a symbol.  If the count reaches zero, the symbol
 * Is deallocated
 */
void
symbol_unref (Symbol *sym)
{
	g_return_if_fail (sym != NULL);
	g_return_if_fail (sym->ref_count > 0);
	
	if (--(sym->ref_count) == 0){
		g_hash_table_remove (sym->st->hash, sym->str);
		g_free (sym->str);
		g_free (sym);
	}
}

SymbolTable *
symbol_table_new (void)
{
	SymbolTable *st = g_new (SymbolTable, 1);

	st->hash = g_hash_table_new (gnumeric_strcase_hash, gnumeric_strcase_equal);

	return st;
}

/**
 * symbol_table_destroy:
 * @st: The symbol table to destroy
 *
 * This only releases the resources associated with a SymbolTable.
 * Note that the symbols on the SymbolTable are not unrefed, it is
 * up to the caller to unref them.
 */
void
symbol_table_destroy (SymbolTable *st)
{
	g_return_if_fail (st != NULL);

	g_hash_table_destroy (st->hash);
	g_free (st);
}

void
global_symbol_init (void)
{
	global_symbol_table = symbol_table_new ();
}
