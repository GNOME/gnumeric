#include <config.h>
#include <gbrun/libgbrun.h>

#include "common.h"
#include "str.h"

GBValue *
value_to_gb (Value *val)
{
	if (val == NULL)
		return NULL;

	switch (val->type) {
	case VALUE_EMPTY:
		/* FIXME ?? what belongs here */
		return gb_value_new_empty ();

	case VALUE_BOOLEAN:
		return gb_value_new_boolean (val->v_bool.val);

	case VALUE_ERROR:
		/* FIXME ?? what belongs here */
		return gb_value_new_string_chars (val->v_err.mesg->str);

	case VALUE_STRING:
		return gb_value_new_string_chars (val->v_str.val->str);

	case VALUE_INTEGER:
		return gb_value_new_long (val->v_int.val);

	case VALUE_FLOAT:
		return gb_value_new_double (val->v_float.val);

	default:
		g_warning ("Unimplemented %d -> GB translation", val->type);

		return gb_value_new_int (0);
	}
}

Value *
gb_to_value (GBValue *v)
{
	switch (gb_value_from_gtk_type (v->gtk_type)) {
	case GB_VALUE_EMPTY:
	case GB_VALUE_NULL:
		return value_new_empty ();

	case GB_VALUE_INT:
	case GB_VALUE_LONG:
		return value_new_int (gb_value_get_as_long (v));

	case GB_VALUE_SINGLE:
	case GB_VALUE_DOUBLE:
		return value_new_float (gb_value_get_as_double (v));

	case GB_VALUE_STRING:
		return value_new_string (v->v.s->str);

	default:
		g_warning ("Unimpflemented GB '%s' -> gnumeric type mapping",
			   gtk_type_name (v->gtk_type));

		return value_new_error (NULL, "Unknown mapping");
	}
}
