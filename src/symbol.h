#ifndef SYMBOL_H
#define SYMBOL_H

typedef enum {
	SYMBOL_FUNCTION,
	SYMBOL_STRING
} SymbolType;

typedef struct {
	int        ref_count;
	SymbolType type;
	char       *str;
	void       *data;
} Symbol;

void    symbol_init    (void);

Symbol *symbol_lookup        (char *str);
Symbol *symbol_lookup_substr (char *buffer, int len);
Symbol *symbol_install       (char *str, SymbolType type, void *data);
void    symbol_ref           (Symbol *);
void    symbol_unref         (Symbol *);
Symbol *symbol_ref_string    (char *str);

#endif
