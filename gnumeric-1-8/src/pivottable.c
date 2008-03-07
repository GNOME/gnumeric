/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * pivottable.c:
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
#include "pivottable.h"
#include <sheet.h>

#include <glib/gi18n-lib.h>

GnmPivotTable *
gnm_pivottable_new (Sheet *src_sheet, GnmRange const *src,
		    Sheet *dst_sheet, GnmRange const *dst)
{
	GnmPivotTable *res;

	g_return_val_if_fail (IS_SHEET (src_sheet), NULL);
	g_return_val_if_fail (IS_SHEET (dst_sheet), NULL);
	g_return_val_if_fail (src != NULL && dst != NULL, NULL);

	res = g_new0 (GnmPivotTable, 1);
	res->src.sheet = src_sheet;
	res->src.range = *src;
	res->dst.sheet = src_sheet;
	res->dst.range = *dst;

	return res;
}

void
gnm_pivottable_free (GnmPivotTable *filter)
{
}
