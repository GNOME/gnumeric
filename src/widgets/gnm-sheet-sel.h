/*
 * gnm-sheet-sel.h: A selector for sheets.
 *
 * Copyright (c) 2018 Morten Welinder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 **/

#ifndef GNM_SHEET_SEL_H
#define GNM_SHEET_SEL_H

#include <gnumeric.h>
#include <widgets/gnm-workbook-sel.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

GType gnm_sheet_sel_get_type (void);

#define GNM_TYPE_SHEET_SEL             (gnm_sheet_sel_get_type ())
#define GNM_SHEET_SEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_SHEET_SEL, GnmSheetSel))
#define GNM_IS_SHEET_SEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_SHEET_SEL))

typedef struct GnmSheetSel_ GnmSheetSel;

GtkWidget *gnm_sheet_sel_new (void);

void       gnm_sheet_sel_set_sheets (GnmSheetSel *ss, GPtrArray *sheets);

void       gnm_sheet_sel_link (GnmSheetSel *ss, GnmWorkbookSel *wbs);

void       gnm_sheet_sel_set_sheet (GnmSheetSel *ss, Sheet *sheet);
Sheet     *gnm_sheet_sel_get_sheet (GnmSheetSel *ss);

G_END_DECLS

#endif /* GNM_SHEET_SEL_H */
