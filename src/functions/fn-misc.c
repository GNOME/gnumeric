/*
 * fn-misc.c:  Miscelaneous built-in functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <ctype.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"

	   "@DESCRIPTION="
	   "Cleans the string from any non-printable characters."
	   "\n"
	   
	   "@SEEALSO=")
};
static Value *
gnumeric_clean (FunctionDefinition *fn, Value *argv [], char **error_string)
{
	Value *res;
	char *copy, *p, *q;
	
	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}
	p = argv [0]->v.str->str;
	copy = q = g_malloc (strlen (p) + 1);
	
	while (p){
		if (isprint (*p))
			*q++ = *p;
		p++;
	}
	*q = 0;

	res = g_new (Value, 1);
	res->type = VALUE_STRING;
	res->v.str = string_get (copy);
	g_free (copy);

	return res;
}

FunctionDefinition misc_functions [] = {
	{ "clean", "s",  "text",             &help_clean, NULL, gnumeric_clean },
	{ NULL, NULL }
};
