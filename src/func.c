/*
 * func.c:  Built in functions
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"

static void
gnumeric_sin (void)
{
	printf ("Sinning\n");
}

static struct {
	char *name;
	void (*fn)(void);
} internal_functions [] = {
	{ "sin", gnumeric_sin },
	{ NULL, NULL },
};

void
functions_init (void)
{
	int i;
	
	for (i = 0; internal_functions [i].name; i++){
		symbol_install (internal_functions [i].name, SYMBOL_FUNCTION,
				internal_functions [i].fn);
	}
}
