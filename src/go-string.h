/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-string.h : ref counted shared strings with richtext and phonetic support
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

#ifndef GO_STRING_H
#define GO_STRING_H

#include "goffice-utils.h"	/* remove after move to goffice */
#include <pango/pango-attributes.h>

G_BEGIN_DECLS

typedef struct _GOStringPhonetic  GOStringPhonetic;	/* TODO : move */

struct _GOString {
	char const *str;	/* utf-8 */
	/* <private data> */
};

GType go_string_get_type (void);	/* GBoxed support */

GOString   *go_string_new		(char const *str);
GOString   *go_string_new_len		(char const *str, guint32 len);
GOString   *go_string_new_nocopy	(char *str);
GOString   *go_string_new_nocopy_len	(char *str, guint32 len);
GOString   *go_string_new_rich		(char const *str,
					 int byte_len,
					 gboolean copy,
					 PangoAttrList *markup,
					 GOStringPhonetic *phonetic);

GOString *go_string_ref		(GOString *gstr);
void	  go_string_unref	(GOString *gstr);

char const	 *go_string_get_cstr	  (GOString const *gstr);
guint32		  go_string_get_len	  (GOString const *gstr);
unsigned int	  go_string_get_ref_count (GOString const *gstr);
char const	 *go_string_get_collation (GOString const *gstr);
char const	 *go_string_get_casefold  (GOString const *gstr);

PangoAttrList	 *go_string_get_markup	  (GOString const *gstr);
GOStringPhonetic *go_string_get_phonetic  (GOString const *gstr);
guint32	  go_string_hash		(gconstpointer gstr);
int	  go_string_cmp			(gconstpointer gstr_a, gconstpointer gstr_b);
int	  go_string_cmp_ignorecase	(gconstpointer gstr_a, gconstpointer gstr_b);
gboolean  go_string_equal		(gconstpointer gstr_a, gconstpointer gstr_b);
gboolean  go_string_equal_ignorecase	(gconstpointer gstr_a, gconstpointer gstr_b);
gboolean  go_string_equal_rich		(gconstpointer gstr_a, gconstpointer gstr_b);

GOString *go_string_ERROR (void);

/*< private >*/
void go_string_init     (void);
void go_string_shutdown (void);
void go_string_dump     (void);
void go_string_foreach_base (GHFunc callback, gpointer data);

G_END_DECLS

#endif /* GO_STRING_H */
