/*
 * fn-misc.c:  Miscelaneous built-in functions
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static Value *
gnumeric_char (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	int i;
	char buffer [2];
	
	v->type = VALUE_STRING;
	i = value_get_as_double (argv [0]);
	buffer [0] = i;
	buffer [1] = 0;
	v->v.str = string_get (buffer);
	
	return v;
}

static Value *
gnumeric_clean (Value *argv [], char **error_string)
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

static Value *
gnumeric_exact (Value *argv [], char **error_string)
{
	Value *res;
	
	if (argv [0]->type != VALUE_STRING || argv [1]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	res = g_new (Value, 1);
	res->type = VALUE_INTEGER;
	res->v.v_int = !strcmp (argv [0]->v.str->str, argv [1]->v.str->str);
	return res;
}

FunctionDefinition misc_functions [] = {
	{ "char",  "f",  "number",           NULL, NULL, gnumeric_char },
	{ "clean", "s",  "text",             NULL, NULL, gnumeric_clean },
	{ "exact", "ss", "text1,text2",      NULL, NULL, gnumeric_exact },
	{ NULL, NULL }
};
