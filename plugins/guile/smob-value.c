/* -*- mode: c; c-basic-offset: 8 -*- */

/*
  Author: Ariel Rios <ariel@arcavia.com>
	   
	   Copyright Ariel Rios 2000
*/
#include <libguile.h>
#include <gtk/gtk.h>
#include <guile/gh.h>

#include "value.h"

static long value_tag;

typedef struct _SCM_Value
{
	SCM name;
	SCM scm;
	Value *v;
	SCM update_func;
} SCM_Value;

SCM
make_value (SCM name, SCM scm)
{
	Value *v;
	SCM val_smob;
	SCM_Value value;

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

	value->name = name;
	value->scm = scm;
	value = (SCM_value *) scm_must_malloc (sizeof (SCM_VALUE), "value");
	value->v = v;
	value->update_func = SCM_BOOL_F;
	
	SCM_RETURN_NEWSMOB (value_tag, value);
}	 

static SCM
mark_value (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	scm_gc_mark (v->name);
	scm_gc_mark (v->scm);
	
	return v->update_func;
}

static scm_size_t
free_value (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
	scm_sizet size  = sizeof (SCM_Value);
	value_release (v);

	return size;
}

static int
print_value (SCM value_smob, SCM port, scm_print_state *pstate)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	scm_puts ("#<Value ", port);
	scm_display (v->scm, port);
	scm_puts (">", port);

	return 1;
}
	
static SCM
equalp_value (SCM value_smob_1, SCM value_smob_2)
{
	SCM_Value *v1 = (SCM_Value *) SCM_CDR (value_smob_1);
	SCM_Value *v2 = (SCM_Value *) SCM_CDR (value_smob_2);
	SCM flag;
	
	flag = (value_compare (v1, v2, TRUE))? SCM_BOOL_T : SCM_BOOL_F;

	return flag;
}
	
void
init_value_type ()
{
	
	value_tag = scm_make_smob_type ("value", sizeof (SCM_Value));
	scm_set_smob_mark (value_tag, mark_value);
	scm_set_smob_free (value_tag, free_value);
	scm_set_smob_print (value_tag, print_value);
	scm_set_smob_print (value_tag, equalp_value);
	
  scm_make_gsubr ("make-value", 3, 0, 0, make_value);
}

