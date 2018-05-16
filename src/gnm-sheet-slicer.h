/*
 * gnm-sheet-slicer.h : Gnumeric specific display for goffice's DataSlicers
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
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
#ifndef GNM_SHEET_SLICER_H
#define GNM_SHEET_SLICER_H

#include <gnumeric.h>
#include <goffice-data.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_SHEET_SLICER_TYPE	(gnm_sheet_slicer_get_type ())
#define GNM_SHEET_SLICER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SHEET_SLICER_TYPE, GnmSheetSlicer))
#define GNM_IS_SHEET_SLICER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SHEET_SLICER_TYPE))

GType gnm_sheet_slicer_get_type (void);

typedef enum {
	GSS_LAYOUT_XL_OUTLINE	= 0,
	GSS_LAYOUT_XL_COMPACT	= 1,
	GSS_LAYOUT_XL_TABULAR	= 2,
	GSS_LAYOUT_MAX
} GnmSheetSlicerLayout;

void		 gnm_sheet_slicer_set_sheet      (GnmSheetSlicer *gss, Sheet *sheet);
void		 gnm_sheet_slicer_clear_sheet    (GnmSheetSlicer *gss);
GnmRange const	*gnm_sheet_slicer_get_range      (GnmSheetSlicer const *src);
void		 gnm_sheet_slicer_set_range      (GnmSheetSlicer *gss, GnmRange const *r);
GnmSheetSlicerLayout
		 gnm_sheet_slicer_get_layout     (GnmSheetSlicer const *src);
void		 gnm_sheet_slicer_set_layout     (GnmSheetSlicer *gss, GnmSheetSlicerLayout l);

#ifndef GOFFICE_NAMESPACE_DISABLE
GODataSlicerField *gnm_sheet_slicer_field_header_at_pos (GnmSheetSlicer const *gss, GnmCellPos const *pos);
#endif

void		   gnm_sheet_slicer_regenerate	 (GnmSheetSlicer *gss);

/* Convenience */
gboolean	 gnm_sheet_slicer_overlaps_range (GnmSheetSlicer const *gss, GnmRange const *r);

/* Scripting */
GType		 gnm_sheet_slicer_layout_get_type (void);

/* Sheet Utilities */
GnmSheetSlicer * gnm_sheet_slicers_at_pos (Sheet const *sheet, GnmCellPos const *pos);

G_END_DECLS

#endif /* GNM_SHEET_SLICER_H */
