/* vim: set sw=8: */

/*
 * hlink.c: hyperlink support
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
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
#include "gnumeric.h"
#include "hlink.h"
#include "hlink-impl.h"
#include <gsf/gsf-impl-utils.h>

/**
 * gnm_hlink_activate :
 * @link :
 *
 * Return TRUE if the link successfully activated.
 **/
gboolean
gnm_hlink_activate (GnmHLink *link)
{
	g_return_val_if_fail (GNM_IS_HLINK (link), FALSE);

	return FALSE;
}

GnmHLink *
sheet_hlink_find (Sheet const *sheet, CellPos const *pos)
{
	return NULL;
}

/*
GType gnm_hlink_url_type ();
GType gnm_hlink_gnumeric_type ();
GType gnm_hlink_newbook_type ();
GType gnm_hlink_email_type ();

 * Use this for hyperlinks gnome_url_show (url);
 */
