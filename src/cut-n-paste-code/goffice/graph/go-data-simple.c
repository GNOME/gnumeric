/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * go-data-simple.c : 
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <goffice/graph/go-data-simple.h>
#include <goffice/graph/go-data-impl.h>

#include <gsf/gsf-impl-utils.h>
#include <src/gnumeric-i18n.h>
#include <src/mathfunc.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

struct _GODataScalarVal {
	GODataScalar	 base;
	double		 val;
	char		*str;
};
typedef GODataScalarClass GODataScalarValClass;

static GObjectClass *scalar_val_parent_klass;

static void
go_data_scalar_val_finalize (GObject *obj)
{
	GODataScalarVal *val = (GODataScalarVal *)obj;

	if (val->str != NULL) {
		g_free (val->str);
		val->str = NULL;
	}

	if (scalar_val_parent_klass->finalize)
		(*scalar_val_parent_klass->finalize) (obj);
}

static GOData *
go_data_scalar_val_dup (GOData const *src)
{
	GODataScalarVal *dst = g_object_new (G_OBJECT_TYPE (src), NULL);
	GODataScalarVal const *src_val = (GODataScalarVal const *)src;
	dst->val = src_val->val;
	return GO_DATA (dst);
}

static gboolean
go_data_scalar_val_eq (GOData const *a, GOData const *b)
{
	GODataScalarVal const *sval_a = (GODataScalarVal const *)a;
	GODataScalarVal const *sval_b = (GODataScalarVal const *)b;

	/* GOData::eq is used for identity, not arithamtic */
	return sval_a->val == sval_b->val;
}

static char *
go_data_scalar_val_as_str (GOData const *dat)
{
	return g_strdup (go_data_scalar_get_str (GO_DATA_SCALAR (dat)));
}

static gboolean
go_data_scalar_val_from_str (GOData *dat, char const *str)
{
	GODataScalarVal *sval = (GODataScalarVal *)dat;
	double tmp;
	char *end;
	errno = 0; /* strto(ld) sets errno, but does not clear it.  */
	tmp = strtod (str, &end);

	if (end == str || *end != '\0' || errno == ERANGE)
		return FALSE;

	g_free (sval->str);
	sval->str = NULL;
	sval->val = tmp;
	return TRUE;
}

static double
go_data_scalar_val_get_value (GODataScalar *dat)
{
	GODataScalarVal const *sval = (GODataScalarVal const *)dat;
	return sval->val;
}

static char const *
go_data_scalar_val_get_str (GODataScalar *dat)
{
	GODataScalarVal *sval = (GODataScalarVal *)dat;

	if (sval->str == NULL)
		sval->str = g_strdup_printf ("%g", sval->val);
	return sval->str;
}

static void
go_data_scalar_val_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataScalarClass *scalar_klass = (GODataScalarClass *) gobject_klass;

	scalar_val_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize = go_data_scalar_val_finalize;
	godata_klass->dup	= go_data_scalar_val_dup;
	godata_klass->eq	= go_data_scalar_val_eq;
	godata_klass->as_str	= go_data_scalar_val_as_str;
	godata_klass->from_str	= go_data_scalar_val_from_str;
	scalar_klass->get_value	= go_data_scalar_val_get_value;
	scalar_klass->get_str	= go_data_scalar_val_get_str;
}

GSF_CLASS (GODataScalarVal, go_data_scalar_val,
	   go_data_scalar_val_class_init, NULL,
	   GO_DATA_SCALAR_TYPE)

GOData *
go_data_scalar_val_new (double val)
{
	GODataScalarVal *res = g_object_new (GO_DATA_SCALAR_VAL_TYPE, NULL);
	res->val = val;
	return GO_DATA (res);
}

/*****************************************************************************/

struct _GODataScalarStr {
	GODataScalar	 base;
	char const *str;
	gboolean    needs_free;
};
typedef GODataScalarClass GODataScalarStrClass;

static GObjectClass *scalar_str_parent_klass;

static void
go_data_scalar_str_finalize (GObject *obj)
{
	GODataScalarStr *str = (GODataScalarStr *)obj;

	if (str->needs_free && str->str != NULL) {
		g_free ((char *)str->str);
		str->str = NULL;
	}
	if (scalar_str_parent_klass->finalize)
		(*scalar_str_parent_klass->finalize) (obj);
}

static GOData *
go_data_scalar_str_dup (GOData const *src)
{
	GODataScalarStr *dst = g_object_new (G_OBJECT_TYPE (src), NULL);
	GODataScalarStr const *src_val = (GODataScalarStr const *)src;
	dst->needs_free = TRUE;
	dst->str = g_strdup (src_val->str);
	return GO_DATA (dst);
}

static gboolean
go_data_scalar_str_eq (GOData const *a, GOData const *b)
{
	GODataScalarStr const *str_a = (GODataScalarStr const *)a;
	GODataScalarStr const *str_b = (GODataScalarStr const *)b;
	return 0 == strcmp (str_a->str, str_b->str);
}

static char *
go_data_scalar_str_as_str (GOData const *dat)
{
	GODataScalarStr const *str = (GODataScalarStr const *)dat;
	return g_strdup (str->str);
}

static gboolean
go_data_scalar_str_from_str (GOData *dat, char const *string)
{
	GODataScalarStr *str = (GODataScalarStr *)dat;

	if (str->str == string)
		return TRUE;
	if (str->needs_free)
		g_free ((char *)str->str);
	str->str = g_strdup (string);
	return TRUE;
}

static double
go_data_scalar_str_get_value (GODataScalar *dat)
{
	return gnm_nan;
}

static char const *
go_data_scalar_str_get_str (GODataScalar *dat)
{
	GODataScalarStr const *str = (GODataScalarStr const *)dat;
	return str->str;
}

static void
go_data_scalar_str_class_init (GObjectClass *gobject_klass)
{
	GODataClass *godata_klass = (GODataClass *) gobject_klass;
	GODataScalarClass *scalar_klass = (GODataScalarClass *) gobject_klass;

	scalar_str_parent_klass = g_type_class_peek_parent (gobject_klass);
	gobject_klass->finalize	= go_data_scalar_str_finalize;
	godata_klass->dup	= go_data_scalar_str_dup;
	godata_klass->eq	= go_data_scalar_str_eq;
	godata_klass->as_str	= go_data_scalar_str_as_str;
	godata_klass->from_str	= go_data_scalar_str_from_str;
	scalar_klass->get_value	= go_data_scalar_str_get_value;
	scalar_klass->get_str	= go_data_scalar_str_get_str;
}

static void
go_data_scalar_str_init (GObject *obj)
{
	GODataScalarStr *str = (GODataScalarStr *)obj;
	str->str = "";
	str->needs_free = FALSE;
}

GSF_CLASS (GODataScalarStr, go_data_scalar_str,
	   go_data_scalar_str_class_init, go_data_scalar_str_init,
	   GO_DATA_SCALAR_TYPE)

GOData *
go_data_scalar_str_new (char const *str, gboolean needs_free)
{
	GODataScalarStr *res = g_object_new (GO_DATA_SCALAR_STR_TYPE, NULL);
	res->str	= str;
	res->needs_free = needs_free;
	return GO_DATA (res);
}
