#ifndef GNUMERIC_SYMBOL_H
#define GNUMERIC_SYMBOL_H

typedef enum {
	SYMBOL_FUNCTION,
	SYMBOL_VALUE,
	SYMBOL_STRING
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

Symbol      *symbol_lookup         (SymbolTable *st, const char *str);
Symbol      *symbol_lookup_substr  (SymbolTable *st, const char *buffer, int len);
Symbol      *symbol_install        (SymbolTable *st, const char *str,
				    SymbolType type, void *data);
void         symbol_remove         (SymbolTable *st, Symbol *);
gboolean     symbol_is_unused      (Symbol *);

void         symbol_ref            (Symbol *);
void         symbol_unref          (Symbol *);
void         symbol_unref_ptr      (Symbol **);

Symbol      *symbol_ref_string     (SymbolTable *st, const char *str);

void         global_symbol_init    (void);

extern SymbolTable *global_symbol_table;

#endif /* GNUMERIC_SYMBOL_H */
