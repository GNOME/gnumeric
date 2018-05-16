/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 *
 *     Author: Ariel Rios <ariel@linuxppc.org>
 *	   Copyright Ariel Rios 2000, 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA  02110-1301  USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libguile.h>
#include <glib.h>
#include <stdlib.h>

#include "smob-value.h"
#include <value.h>

int scm_i_scm2bool (SCM obj);
static long value_tag;

typedef struct _SCM_Value
{
	GnmValue *v;
	SCM update_func;
} SCM_Value;

/**
 * The GnmValue in the smob has to be a clone of v, since the original may be
 * released in C-land, and there is no way to tell the Guile garbage
 * collector.
 */
SCM
make_new_smob (GnmValue *v)
{
	SCM_Value *value;

	value = (SCM_Value *) scm_must_malloc (sizeof (SCM_Value), "value");
	value->v = value_dup (v);
	value->update_func = SCM_BOOL_F;

	SCM_RETURN_NEWSMOB (value_tag, value);
}

/**
 * We also have to clone the value in the smob before returning it to
 * C-land, since the returned value may be released. There may be leaks, but
 * freing memory twice is worse.
 */
GnmValue *
get_value_from_smob (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	return value_dup (v->v);
}

int
scm_i_scm2bool (SCM obj)
{
  return (SCM_FALSEP (obj)) ? 0 : 1;
}

static SCM
make_value (SCM scm)
{
	GnmValue *v;
	SCM_Value *value;

	/*
	  FIXME:
	  Add support for array, null values, etc
	*/

	if (SCM_NIMP (scm) && SCM_STRINGP (scm))
		v = value_new_string (SCM_CHARS (scm));

	else if ((SCM_NFALSEP (scm_number_p(scm))))
		v = value_new_float ((gnm_float) scm_num2dbl(scm, 0));

	else if (SCM_BOOLP (scm))
		v = value_new_bool ((gboolean) scm_i_scm2bool (scm));

	else
		v = value_new_error (NULL,
				     _("Unable to convert value from Guile"));

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
#if 0
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
#endif

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
scm_value_new_bool (SCM scm)
{
	GnmValue *v;
	SCM_Value *value;

	if (SCM_BOOLP (scm))
		v = value_new_bool ((gboolean) scm_i_scm2bool (scm));
	else
		v = value_new_error (NULL, _("Not a Guile boolean"));

	value = (SCM_Value *) scm_must_malloc (sizeof (SCM_Value), "value");
	value->v = v;
	value->update_func = SCM_BOOL_F;

	SCM_RETURN_NEWSMOB (value_tag, value);
}

static SCM
scm_value_new_float (SCM scm)
{
	GnmValue *v;
	SCM_Value *value;

	if ((SCM_NFALSEP (scm_number_p(scm))))
		v = value_new_float ((gnm_float) scm_num2dbl(scm, 0));
	else
		v = value_new_error (NULL, _("Not a Guile number"));

	value = (SCM_Value *) scm_must_malloc (sizeof (SCM_Value), "value");
	value->v = v;
	value->update_func = SCM_BOOL_F;

	SCM_RETURN_NEWSMOB (value_tag, value);
}

static SCM
scm_value_new_string (SCM scm)
{
	GnmValue *v;
	SCM_Value *value;

	if (SCM_NIMP (scm) && SCM_STRINGP (scm))
		v = value_new_string (SCM_CHARS (scm));
	else
		v = value_new_error (NULL, _("Not a Guile string"));


	value = (SCM_Value *) scm_must_malloc (sizeof (SCM_Value), "value");
	value->v = v;
	value->update_func = SCM_BOOL_F;

	SCM_RETURN_NEWSMOB (value_tag, value);
}

static SCM
scm_value_get_as_string (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	if (VALUE_IS_STRING (v->v) || VALUE_IS_NUMBER (v->v))
		return scm_makfrom0str (value_get_as_string (v->v));

	return SCM_EOL;
}

static SCM
scm_value_get_as_int (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);
	if (VALUE_IS_NUMBER (v->v))
		return scm_long2num (value_get_as_int (v->v));

	return SCM_EOL;
}

static SCM
scm_value_get_as_float (SCM value_smob)
{
	SCM_Value *v = (SCM_Value *) SCM_CDR (value_smob);

	if (VALUE_IS_NUMBER (v->v))
		return scm_i_dbl2big (value_get_as_float (v->v));

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
						/*
						*ls = scm_cons (gh_double2scm (value_get_as_float (v->v->v_array.vals[i][ii])), *ls);
						*/
						*ls = scm_cons (scm_i_dbl2big (value_get_as_float (v->v->v_array.vals[i][ii])), *ls);
						/* FIXME */
						ls = SCM_CDRLOC (*ls);
					}
			*ls = SCM_EOL;
			*ls = scm_reverse (*ls);
			return list;
		}

	return SCM_EOL;
}

void
init_value_type (void)
{

	value_tag = scm_make_smob_type ((char *) "value", sizeof (SCM_Value));
	scm_set_smob_mark (value_tag, mark_value);
	scm_set_smob_free (value_tag, free_value);
	scm_set_smob_print (value_tag, print_value);
	scm_set_smob_equalp (value_tag, equalp_value);

	scm_c_define_gsubr ("make-value", 1, 0, 0, make_value);
	scm_c_define_gsubr ("value-new-bool", 1, 0, 0, scm_value_new_bool);
	scm_c_define_gsubr ("value-new-float", 1, 0, 0, scm_value_new_float);
	scm_c_define_gsubr ("value-new-string", 1, 0, 0, scm_value_new_string);
	scm_c_define_gsubr ("value-get-as-string", 1, 0, 0, scm_value_get_as_string);
	scm_c_define_gsubr ("value-get-as-int", 1, 0, 0, scm_value_get_as_int);
	scm_c_define_gsubr ("value-get-as-float", 1, 0, 0, scm_value_get_as_float);
	scm_c_define_gsubr ("value-get-as-list", 1, 0, 0, scm_value_get_as_list);
}
