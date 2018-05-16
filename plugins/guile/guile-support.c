/* -*- mode: c; c-basic-offset: 8 -*- */

/*
  Authors: Ariel Rios <ariel@arcavia.com>
*/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libguile.h>
#include <guile/gh.h>
#include <position.h>
#include "guile-support.h"
#include "smob-value.h"


SCM
value_to_scm (GnmValue const *val, GnmCellRef cell_ref)
{
	GnmValue *v = (GnmValue *) val;
	return make_new_smob (v);
}

GnmValue*
scm_to_value (SCM value_smob)
{
	return get_value_from_smob (value_smob);
}

