/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SYMBOL_H_
# define _GNM_SYMBOL_H_

G_BEGIN_DECLS

typedef enum {
	SYMBOL_FUNCTION
} SymbolType;

typedef struct {
	GHashTable *hash;
} SymbolTable;

typedef struct {
	int         ref_count;
	SymbolType  type;
	char        *str;
	SymbolTable *st;
	void        *data;
} Symbol;

SymbolTable *symbol_table_new      (void);
void         symbol_table_destroy  (SymbolTable *st);

Symbol      *symbol_lookup         (SymbolTable *st, char const *str);
Symbol      *symbol_install        (SymbolTable *st, char const *str,
				    SymbolType type, void *data);

void         symbol_ref            (Symbol *sym);
void         symbol_unref          (Symbol *sym);

G_END_DECLS

#endif /* _GNM_SYMBOL_H_ */
