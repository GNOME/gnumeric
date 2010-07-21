/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Symbol management for the Gnumeric spreadsheet
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "symbol.h"
#include "func.h"

#include "gutils.h"

#include <string.h>
#include <goffice/goffice.h>

/**
 * symbol_lookup:
 * @st: The symbol table where lookup takes place
 * @str: string to be looked up in the symbol table
 */
Symbol *
symbol_lookup (SymbolTable *st, char const *str)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (st != NULL, NULL);

	sym = (Symbol *) g_hash_table_lookup (st->hash, str);
	return sym;
}

typedef struct {
	char const *prefix;
	GSList *list;
} symbol_names_cb_t;

static void
symbol_names_cb (char const * key, Symbol *symbol, symbol_names_cb_t *data)
{
	if (g_str_has_prefix (key, data->prefix)) {
		gnm_func_ref ((GnmFunc*)symbol->data);
		data->list = g_slist_prepend (data->list, symbol->data);
	}
}

/* returns a list of reffed GnmFunc* */
GSList *
symbol_names (SymbolTable *st, GSList *list, char const *prefix)
{
	symbol_names_cb_t data;

	data.list = list;
	data.prefix = prefix;
	g_hash_table_foreach (st->hash,
			      (GHFunc) symbol_names_cb,
			      &data);
	return data.list;
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
symbol_install (SymbolTable *st, char const *str, SymbolType type, void *data)
{
	Symbol *sym;

	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (st != NULL, NULL);

	sym = (Symbol *) g_hash_table_lookup (st->hash, str);
	if (sym) g_warning ("(leak) Symbol [%s] redefined.\n", str);

	sym = g_new (Symbol, 1);
	sym->ref_count = 1;
	sym->type = type;
	sym->data = data;
	sym->str  = g_strdup (str);
	sym->st   = st;

	g_hash_table_replace (st->hash, sym->str, sym);

	return sym;
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

	if (--(sym->ref_count) == 0) {
		g_hash_table_remove (sym->st->hash, sym->str);
		g_free (sym->str);
		g_free (sym);
	}
}

SymbolTable *
symbol_table_new (void)
{
	SymbolTable *st = g_new (SymbolTable, 1);

	st->hash = g_hash_table_new (go_ascii_strcase_hash,
				     go_ascii_strcase_equal);

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
