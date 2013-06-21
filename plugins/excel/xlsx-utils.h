/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-utils.h : utilities shared between xlsx import and export
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

/*****************************************************************************/

#ifndef GNM_XLSX_UTILS_H
#define GNM_XLSX_UTILS_H

#include <gnumeric.h>

enum {
	XL_NS_SS,
	XL_NS_SS_DRAW,
	XL_NS_CHART,
	XL_NS_CHART_DRAW,
	XL_NS_DRAW,
	XL_NS_DOC_REL,
	XL_NS_PKG_REL,
	XL_NS_LEG_OFF,
	XL_NS_LEG_XL,
	XL_NS_LEG_VML,
	XL_NS_PROP_CP,
	XL_NS_PROP_DC,
	XL_NS_PROP_DCMITYPE,
	XL_NS_PROP_DCTERMS,
	XL_NS_PROP_XSI,
	XL_NS_PROP,
	XL_NS_PROP_VT,
	XL_NS_PROP_CUSTOM
};

#define XLSX_MaxCol	16384
#define XLSX_MaxRow	1048576

GnmConventions	*xlsx_conventions_new  (gboolean output);
void		 xlsx_conventions_free (GnmConventions *conv);
Workbook	*xlsx_conventions_add_extern_ref (GnmConventions *conv,
						  char const *path);
GOFormat        *xlsx_pivot_date_fmt   (void);

GOGradientDirection xlsx_get_gradient_direction (double ang);

#endif /* GNM_XLSX_UTILS_H */
