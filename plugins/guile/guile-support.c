/* -*- mode: c; c-basic-offset: 8 -*- */

/*
  Authors: Ariel Rios <ariel@arcavia.com>
*/             

#include <libguile.h>
#include <gtk/gtk.h>
#include <guile/gh.h>
#include "guile-support.h"
#include "smob-value.h"


SCM
gnumeric_list2scm (GList *list, SCM (*data_to_scm)(void*))
{
  SCM res, *tail = &res;

  while (list)
    {
      *tail = scm_cons (data_to_scm (&list->data), *tail); 
      tail = SCM_CDRLOC (*tail);
      list = list->next;
    }

  *tail = SCM_EOL;

  return res;

}

GList *
gnumeric_scm2list (SCM obj , void (*fromscm)(SCM, void*))
{
   GList *res = NULL, *tail = NULL;

  if (obj == SCM_BOOL_F)
    return NULL;
  else if (obj == SCM_EOL || (SCM_NIMP(obj) && SCM_CONSP(obj)))
    {
      while (SCM_NIMP(obj) && SCM_CONSP(obj))
      {
        GList *n = g_list_alloc ();
	if (res == NULL)
	  res = tail = n;
	else 
	  {
	    g_list_concat (tail, n);
	    tail = n;
	  }
	if (fromscm)
	  fromscm (SCM_CAR (obj), &(n->data));
	else
	  n->data = NULL;
	obj = SCM_CDR(obj);
      }
    }
  else if (SCM_NIMP(obj) && SCM_VECTORP(obj))
    {
      int len = SCM_LENGTH (obj), i;
      SCM *elts = SCM_VELTS (obj);
      for (i = 0; i < len; i++)
	{
	  GList *n = g_list_alloc ();
	  if (res == NULL)
	    res = tail = n;
	  else 
	    {
	      g_list_concat (tail, n);
	      tail = n;
	    }
	  if (fromscm)
	    fromscm (elts[i], &(n->data));
	  else
	    n->data = NULL;
	}
    }

  return res;
}

SCM
value_to_scm (Value const *val, CellRef cell_ref)
{
	Value *v = (Value *) val;
	return make_new_smob (v);
}

Value*
scm_to_value (SCM value_smob)
{
	return get_value_from_smob (value_smob);
}
	
