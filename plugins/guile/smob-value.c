/* -*- mode: c; c-basic-offset: 8 -*- */

/*
  Author: Ariel Rios <ariel@arcavia.com>
	   
	   Copyright Ariel Rios 2000
*/
#include <libguile.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <guile/gh.h>
#include <stdlib.h>

#include "smob-value.h"
#include "value.h" 

static long value_tag;

typedef struct _SCM_Value
{
	Value *v;
	SCM update_func;
} SCM_Value;

static SCM
make_value (SCM scm)
{
	Value *v;
	SCM_Value *value;

	v = g_new (Value, 1);
	
	/*
	  FIXME:
	  Add support for array, null values, etc
	*/
	
	if (SCM_NIMP (scm) && SCM_STRINGP (scm))
		v = value_new_string (SCM_CHARS (scm));
	
	if ((SCM_NFALSEP (scm_number_p(scm))))
		v = value_new_float ((float_t) scm_num2dbl(scm, 0));
	
	if (gh_boolean_p (scm)) 
		v = value_new_bool ((gboolean) gh_scm2bool (scm));

	value = (SCM_Value *) scm_must_malloc (sizeof (SCM_Value), "value");
	value->v = v;
	value->update_func = SCM_BOOL_F;
	
	SCM_RETURN_NEWSMOB (value_tag, value);
}	 

static SCM
mark_value (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
	
	return v->update_func;
}

static scm_sizet
free_value (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
	scm_sizet size  = sizeof (SCM_Value);
	value_release (v->v);

	return size;
}

static int
print_value (SCM value_smob, SCM port, scm_print_state *pstate)
{
	//SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	scm_puts ("#<Value>", port);

	return 1;
}

	
static SCM
equalp_value (SCM value_smob_1, SCM value_smob_2)
{
	SCM_Value *v1 = (SCM_Value *) SCM_CDR (value_smob_1);
	SCM_Value *v2 = (SCM_Value *) SCM_CDR (value_smob_2);
	SCM flag;
	
	flag = (value_compare (v1->v, v2->v, TRUE))? SCM_BOOL_T : SCM_BOOL_F;

	return flag;
}

static SCM
scm_value_get_as_string (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
		
	if (v->v->type ==  VALUE_STRING)
		return scm_makfrom0str (value_get_as_string (v->v));

	return SCM_EOL;
}

static SCM
scm_value_get_as_int (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
	if (v->v->type ==  VALUE_INTEGER)
		return scm_long2num (value_get_as_int (v->v));
		
	return SCM_EOL;
}

static SCM
scm_value_get_as_float (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	if (v->v->type ==  VALUE_FLOAT)
		return gh_double2scm (value_get_as_float (v->v));

		return SCM_EOL;
}
	
static SCM
scm_value_get_as_list (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	if (v->v->type ==  VALUE_ARRAY)
		{
			int x, y, i, ii;
			SCM list, *ls = &list;
			
			x = v->v->v_array.x;
			y = v->v->v_array.y;

			for (i = 0; i < y; i++)
				for (ii = 0; i < x; i++)
					{
						*ls = scm_cons (value_get_as_float (v->v->v_array.vals[i][ii]), *ls); // FIXME
						ls = SCM_CDRLOC (*ls);
					}
			*ls = SCM_EOL;
			*ls = scm_reverse (*ls);
			return list;
		}

	return SCM_EOL;
}

void
init_value_type ()
{
	
	value_tag = scm_make_smob_type ("value", sizeof (SCM_Value));
	scm_set_smob_mark (value_tag, mark_value);
	scm_set_smob_free (value_tag, free_value);
	scm_set_smob_print (value_tag, print_value);
	scm_set_smob_equalp (value_tag, equalp_value);
	
	scm_make_gsubr ("make-value", 1, 0, 0, make_value);
	scm_make_gsubr ("value-get-as-string", 1, 0, 0, scm_value_get_as_string);
	scm_make_gsubr ("value-get-as-int", 1, 0, 0, scm_value_get_as_int);
	scm_make_gsubr ("value-get-as-float", 1, 0, 0, scm_value_get_as_float);
	scm_make_gsubr ("value-get-as-list", 1, 0, 0, scm_value_get_as_list);
}



