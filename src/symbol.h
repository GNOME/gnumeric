#ifndef GNUMERIC_SYMBOL_H
#define GNUMERIC_SYMBOL_H

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

#endif /* GNUMERIC_SYMBOL_H */
