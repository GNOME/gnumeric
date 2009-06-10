/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-string.c : ref counted shared strings with richtext and phonetic support
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2007-2008 Morten Welinder (terra@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "go-string.h"

#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>

#include <string.h>

typedef struct {
	GOString	base;
	guint32		hash;
	guint32		flags;		/* len in bytes (not characters) */
	guint32		ref_count;
} GOStringImpl;
typedef struct _GOStringRichImpl {
	GOStringImpl		  base;
	PangoAttrList		 *markup;
	GOStringPhonetic	 *phonetic;
} GOStringRichImpl;

#define GO_STRING_HAS_CASEFOLD	(1u << 31)
#define GO_STRING_HAS_COLLATE	(1u << 30)
#define GO_STRING_IS_RICH	(1u << 29)
#define GO_STRING_IS_SHARED	(1u << 28) /* rich strings share this base */
#define GO_STRING_IS_DEPENDENT	(1u << 27) /* a rich string that shares an underlying */

/* mask off just the len */
#define GO_STRING_LEN(s)	(((GOStringImpl const *)(s))->flags & ((1u << 27) - 1u))

/* Collection of unique strings
 * : GOStringImpl.base.hash -> GOStringImpl * */
static GHashTable *go_strings_base;

/* Collection of gslist keyed the basic string
 * : str (directly on the pointer) -> GSList of GOStringRichImpl */
static GHashTable *go_strings_shared;

static inline GOStringImpl *
go_string_impl_new (char const *str, guint32 hash, guint32 flags, guint32 ref_count)
{
	GOStringImpl *res = g_slice_new (GOStringImpl);
	res->base.str	= str;
	res->hash	= hash;
	res->flags	= flags;
	res->ref_count	= ref_count;
	g_hash_table_replace (go_strings_base, res, res);
	return res;
}

static void
go_string_phonetic_unref (GOStringPhonetic *phonetic)
{
	/* TODO */
}

static GOString *
replace_rich_base_with_plain (GOStringRichImpl *rich)
{
	GOStringImpl *res = go_string_impl_new (rich->base.base.str, rich->base.hash,
		(rich->base.flags & (~GO_STRING_IS_RICH)) | GO_STRING_IS_SHARED, 2);

	rich->base.flags |= GO_STRING_IS_DEPENDENT;
	if ((rich->base.flags & GO_STRING_IS_SHARED)) {
		GSList *shares = g_hash_table_lookup (go_strings_shared, res->base.str);
		unsigned n = g_slist_length (shares);
		g_assert (rich->base.ref_count > n);
		rich->base.flags &= ~GO_STRING_IS_SHARED;
		rich->base.ref_count -= n;
		res->ref_count += n;
		/* ignore result, assignment is just to make the compiler shutup */
		shares = g_slist_insert (shares, res, 1);
	} else
		g_hash_table_insert (go_strings_shared, (gpointer) res->base.str,
			g_slist_prepend (NULL, rich));

	return &res->base;
}

/**
 * go_string_new_len :
 * @str : string (optionally %NULL)
 * @len : guint32
 *
 * GOString duplicates @str if no string already exists.
 *
 * Returns: a reference to a #GOString containing @str, or %NULL if @str is NULL
 **/
GOString *
go_string_new_len (char const *str, guint32 len)
{
	GOStringImpl key, *res;

	if (NULL == str)
		return NULL;

	key.base.str = str;
	key.flags    = len;
	key.hash     = g_str_hash (str);
	res = g_hash_table_lookup (go_strings_base, &key);
	if (NULL == res) {
		 /* Copy str */
		res = go_string_impl_new (g_strndup (str, len),
					  key.hash, key.flags, 1);
		return &res->base;
	} else if (G_UNLIKELY (res->flags & GO_STRING_IS_RICH))
		/* if rich was there first move it to the shared */
		return replace_rich_base_with_plain ((GOStringRichImpl *)res);
	else
		return go_string_ref (&res->base);
}

/**
 * go_string_new_nocopy :
 * @str : string (optionally %NULL)
 * @len : guint32
 *
 * GOString takes ownership of @str
 *
 * Returns: a reference to a #GOString containing @str
 **/
GOString *
go_string_new_nocopy_len (char *str, guint32 len)
{
	GOStringImpl key, *res;

	if (NULL == str)
		return NULL;

	key.base.str = str;
	key.flags    = len;
	key.hash     = g_str_hash (str);
	res = g_hash_table_lookup (go_strings_base, &key);
	if (NULL == res) {
		 /* NO copy str */
		res = go_string_impl_new (str, key.hash, key.flags, 1);
		return &res->base;
	}

	if (str != res->base.str) g_free (str); /* Be extra careful */

	/* if rich was there first move it to the shared */
	if (G_UNLIKELY (res->flags & GO_STRING_IS_RICH))
		return replace_rich_base_with_plain ((GOStringRichImpl *)res);

	return go_string_ref (&res->base);
}

/**
 * go_string_new :
 * @str : string (optionally %NULL)
 *
 * GOString duplicates @str if no string already exists.
 *
 * Returns: a reference to a #GOString containing @str, or %NULL if @str is NULL
 **/
GOString *
go_string_new (char const *str)
{
	return str ? go_string_new_len (str, strlen (str)) : NULL;
}

/**
 * go_string_new_nocopy :
 * @str : string
 *
 * GOString takes ownership of @str
 *
 * Returns: a reference to a #GOString containing @str
 **/
GOString *
go_string_new_nocopy (char *str)
{
	return str ? go_string_new_nocopy_len (str, strlen (str)) : NULL;
}

/**
 * go_string_new_rich :
 * @str :
 * @len : < 0 will call strlen
 * @copy : %TRUE @str should be copied when adding to the string table.
 * @markup : optionally %NULL list, GOString steals the ref
 * @phonetic : optionally %NULL list of phonetic extensions, GOString steals the ref.
 *
 * Returns : a string.
 **/
GOString *
go_string_new_rich (char const *str,
		    int len,
		    gboolean copy,
		    PangoAttrList *markup,
		    GOStringPhonetic *phonetic)
{
	GOStringImpl *base;
	GOStringRichImpl *rich;

	if (NULL == str) {
		if (NULL != markup) pango_attr_list_unref (markup);
		if (NULL != phonetic) go_string_phonetic_unref (phonetic);
		return NULL;
	}

	/* TODO : when we use a better representation for attributes (eg array
	 * of GOFont indicies) look into sharing rich strings */

	rich = g_slice_new (GOStringRichImpl);
	rich->base.base.str	= str;
	rich->base.hash		= g_str_hash (str);
	rich->base.flags		= ((len > 0) ? (guint32) len : strlen (str)) | GO_STRING_IS_RICH;
	rich->base.ref_count	= 1;
	rich->markup		= markup;
	rich->phonetic		= phonetic;
	base = g_hash_table_lookup (go_strings_base, rich);
	if (NULL == base) {
		if (copy) rich->base.base.str = g_strndup (str, len);
		g_hash_table_insert (go_strings_base, rich, rich);
	} else {
		go_string_ref (&base->base);
		if (str != rich->base.base.str) { /* watch for people doing something stupid */
			if (!copy) g_free ((char *)str);
			rich->base.base.str = base->base.str;
		}
		rich->base.flags |= GO_STRING_IS_DEPENDENT;
		if ((base->flags & GO_STRING_IS_SHARED)) {
			GSList *shares = g_hash_table_lookup (go_strings_shared, rich->base.base.str);
			/* ignore result, assignment is just to make the compiler shutup */
			shares = g_slist_insert (shares, rich, 1);
		} else {
			base->flags |= GO_STRING_IS_SHARED;
			g_hash_table_insert (go_strings_shared, (gpointer) rich->base.base.str,
				g_slist_prepend (NULL, rich));
		}
	}

	return &rich->base.base;
}

GOString *
go_string_ref (GOString *gstr)
{
	if (NULL != gstr)
		((GOStringImpl *)gstr)->ref_count++;
	return gstr;
}

void
go_string_unref (GOString *gstr)
{
	GOStringImpl *impl = (GOStringImpl *)gstr;
	if (NULL == gstr)
		return;

	g_return_if_fail (impl->ref_count > 0);

	if ((--(impl->ref_count)) == 0) {
		/* polite assertion failure */
		g_return_if_fail (!(impl->flags & GO_STRING_IS_SHARED));

		if ((impl->flags & GO_STRING_IS_RICH)) {
			GOStringRichImpl *rich = (GOStringRichImpl *)gstr;
			if (NULL != rich->markup) pango_attr_list_unref (rich->markup);
			if (NULL != rich->phonetic) go_string_phonetic_unref (rich->phonetic);
		}

		if (G_UNLIKELY (impl->flags & GO_STRING_IS_DEPENDENT)) {
			GOStringImpl *base = g_hash_table_lookup (go_strings_base, gstr);
			GSList *shares = g_hash_table_lookup (go_strings_shared, gstr->str);
			GSList *new_shares = g_slist_remove (shares, gstr);
			if (new_shares != shares) {
				if (new_shares == NULL) {
					base->flags &= ~GO_STRING_IS_SHARED;
					g_hash_table_remove (go_strings_shared, gstr->str);
				} else
					g_hash_table_replace (go_strings_shared, (gpointer)gstr->str, new_shares);
			}
			go_string_unref (&base->base);
		} else {
			g_hash_table_remove (go_strings_base, gstr);
			g_free ((gpointer)gstr->str);
		}
		g_slice_free1 (sizeof (GOStringImpl), gstr);
	}
}

unsigned int
go_string_get_ref_count (GOString const *gstr)
{
	return gstr ? ((GOStringImpl const *)gstr)->ref_count : 0;
}

guint32
go_string_hash (gconstpointer gstr)
{
	return gstr ? ((GOStringImpl const *)gstr)->hash : 0;
}

int
go_string_cmp (gconstpointer gstr_a, gconstpointer gstr_b)
{
	return (gstr_a == gstr_b)
		? 0
		: strcmp (go_string_get_collation (gstr_a),
			  go_string_get_collation (gstr_b));
}

int
go_string_cmp_ignorecase (gconstpointer gstr_a, gconstpointer gstr_b)
{
	return (gstr_a == gstr_b)
		? 0
		: strcmp (go_string_get_casefold (gstr_a),
			  go_string_get_casefold (gstr_b));
}

gboolean
go_string_equal (gconstpointer gstr_a, gconstpointer gstr_b)
{
	GOString const *a = gstr_a;
	GOString const *b = gstr_b;
	return a == b || (NULL != a && NULL != b && a->str == b->str);
}

static void
go_string_impl_append_extra (GOStringImpl *gstri, char *extra, unsigned int offset)
{
	guint32 len = strlen (extra);
	gchar *res = g_realloc ((gpointer)gstri->base.str, offset + 4 + len + 1);
	GSF_LE_SET_GUINT32(res + offset, len);
	memcpy ((gpointer)(res + offset + 4), extra, len + 1);
	g_free (extra);

	if (res != gstri->base.str) {
		/* update any shared strings */
		if ((gstri->flags & GO_STRING_IS_SHARED)) {
			GSList *shares = g_hash_table_lookup (go_strings_shared, gstri->base.str);
			g_hash_table_remove (go_strings_shared, gstri->base.str);
			g_hash_table_insert (go_strings_shared, res, shares);
			for (; shares != NULL ; shares = shares->next)
				((GOStringImpl *)(shares->data))->base.str = res;
		}
		((GOStringImpl *)gstri)->base.str = res;
	}
}

char const *
go_string_get_collation  (GOString const *gstr)
{
	GOStringImpl *gstri = (GOStringImpl *)gstr;
	unsigned int len;

	if (NULL == gstr)
		return "";

	len = GO_STRING_LEN (gstri);
	if (0 == (gstri->flags & GO_STRING_HAS_COLLATE)) {
		gchar *collate = g_utf8_collate_key (gstri->base.str, len);
		/* Keep it simple, drop the casefold to avoid issues with overlapping */
		gstri->flags &= ~GO_STRING_HAS_CASEFOLD;
		gstri->flags |=  GO_STRING_HAS_COLLATE;
		go_string_impl_append_extra (gstri, collate, len + 1);
	}
	return gstri->base.str + len + 1 + 4;
}

char const *
go_string_get_casefold (GOString const *gstr)
{
	GOStringImpl *gstri = (GOStringImpl *)gstr;
	unsigned int offset;

	if (NULL == gstr)
		return "";

	offset = GO_STRING_LEN (gstri) + 1;
	if (0 != (gstri->flags & GO_STRING_HAS_COLLATE))
		offset += GSF_LE_GET_GUINT32(gstri->base.str + offset) + 4 + 1;

	if (0 == (gstri->flags & GO_STRING_HAS_CASEFOLD)) {
		gchar *casefold = g_utf8_casefold (gstri->base.str, GO_STRING_LEN (gstri));
		gstri->flags |= GO_STRING_HAS_CASEFOLD;
		go_string_impl_append_extra (gstri, casefold, offset);
	}
	return gstri->base.str + offset + 4;
}

static GOString *go_string_ERROR_val = NULL;

/**
 * go_string_ERROR :
 *
 * A convenience for g_return_val to share one error string without adding a
 * reference to functions that do not add references to the result
 *
 * Returns : A string saying 'ERROR' but does not add a ref to it.
 **/
GOString *
go_string_ERROR (void)
{
	if (NULL == go_string_ERROR_val)
		go_string_ERROR_val = go_string_new ("<ERROR>");
	return go_string_ERROR_val;
}

/*******************************************************************************/

/* Internal comparison routine that actually does a strcmp, rather than
 * assuming that only str == str are equal  */
static gboolean
go_string_equal_internal (gconstpointer gstr_a, gconstpointer gstr_b)
{
	GOStringImpl const *a = gstr_a;
	GOStringImpl const *b = gstr_b;
	return a == b ||
		((a->hash == b->hash) &&
		 (GO_STRING_LEN (a) == GO_STRING_LEN (b)) &&
		 0 == strcmp (a->base.str, b->base.str));
}
void
go_string_init (void)
{
	go_strings_base   = g_hash_table_new (go_string_hash, go_string_equal_internal);
	go_strings_shared = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static gboolean
cb_string_pool_leak (G_GNUC_UNUSED gpointer key,
		     gpointer value,
		     G_GNUC_UNUSED gpointer user)
{
	GOString const *gstr = value;
	g_printerr ("Leaking string [%s] with ref_count=%d.\n",
		    gstr->str, go_string_get_ref_count (gstr));
	return TRUE;
}

void
go_string_shutdown (void)
{
	go_string_unref (go_string_ERROR_val);

	g_hash_table_destroy (go_strings_shared);
	go_strings_shared = NULL;

	g_hash_table_foreach_remove (go_strings_base,
				     cb_string_pool_leak,
				     NULL);
	g_hash_table_destroy (go_strings_base);
	go_strings_base = NULL;
}

static void
cb_collect_strings (G_GNUC_UNUSED gpointer key, gpointer str,
		    gpointer user_data)
{
	GSList **pstrs = user_data;
	*pstrs = g_slist_prepend (*pstrs, str);
}

static gint
cb_by_refcount_str (gconstpointer a_, gconstpointer b_)
{
	GOStringImpl const *a = a_;
	GOStringImpl const *b = b_;

	if (a->ref_count == b->ref_count)
		return strcmp (a->base.str, b->base.str);
	return (a->ref_count - b->ref_count);
}

/**
 * go_string_dump :
 *
 * Internal debugging utility to
 **/
void
go_string_dump (void)
{
	GSList *strs = NULL;
	GSList *l;
	int refs = 0, len = 0;
	int count;

	g_hash_table_foreach (go_strings_base, cb_collect_strings, &strs);
	strs = g_slist_sort (strs, cb_by_refcount_str);
	count = g_slist_length (strs);
	for (l = strs; l; l = l->next) {
		GOStringImpl const *s = l->data;
		refs += s->ref_count;
		len  += GO_STRING_LEN (s);
	}

	for (l = g_slist_nth (strs, MAX (0, count - 100)); l; l = l->next) {
		GOStringImpl const *s = l->data;
		g_print ("%8d \"%s\"\n", s->ref_count, s->base.str);
	}
	g_print ("String table contains %d different strings.\n", count);
	g_print ("String table contains a total of %d characters.\n", len);
	g_print ("String table contains a total of %d refs.\n", refs);
	g_slist_free (strs);
}

void
go_string_foreach_base (GHFunc callback, gpointer data)
{
	g_hash_table_foreach (go_strings_base, callback, data);
}

static void
value_transform_gostring_string (GValue const *src_val,
				 GValue       *dest)
{
	GOString *src_str = src_val->data[0].v_pointer;
	dest->data[0].v_pointer = src_str
		? g_strndup (src_str->str, GO_STRING_LEN (src_str))
		: NULL;
}

/**
 * go_string_get_type :
 * Register #GOString as a type for #GValue
 * Returns : #GType
 **/
GType
go_string_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GOString",
			 (GBoxedCopyFunc)go_string_ref,
			 (GBoxedFreeFunc)go_string_unref);
		g_value_register_transform_func (t, G_TYPE_STRING, value_transform_gostring_string);
	}
	return t;
}
