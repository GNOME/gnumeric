/*
 * gnm-commands-slicer.c: undo & redo for data slicer manipulation
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

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnm-commands-slicer.h>
#include <gnm-sheet-slicer.h>
#include <gnm-command-impl.h>
#include <command-context.h>
#include <workbook-control.h>
#include <sheet-view.h>
#include <sheet.h>
#include <ranges.h>
#include <clipboard.h>

#define CMD_SLICER_REFRESH_TYPE        (cmd_slicer_refresh_get_type ())
#define CMD_SLICER_REFRESH(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CMD_SLICER_REFRESH_TYPE, CmdSlicerRefresh))

typedef struct {
	GnmCommand cmd;

	GnmSheetSlicer *slicer;
	GnmCellRegion  *orig_content;
	GnmRange	orig_size;
} CmdSlicerRefresh;

MAKE_GNM_COMMAND (CmdSlicerRefresh, cmd_slicer_refresh, NULL)

static gboolean
cmd_slicer_refresh_undo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSlicerRefresh *me = CMD_SLICER_REFRESH (cmd);
	GnmRange const *new_size = gnm_sheet_slicer_get_range (me->slicer);
	GnmPasteTarget pt;
	sheet_clear_region (me->cmd.sheet,
		new_size->start.col, new_size->start.row,
		new_size->end.col, new_size->end.row,
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GO_CMD_CONTEXT (wbc));
	clipboard_paste_region (me->orig_content,
		paste_target_init (&pt, me->cmd.sheet, &me->orig_size, PASTE_DEFAULT),
		GO_CMD_CONTEXT (wbc));
	cellregion_unref (me->orig_content);
	me->orig_content = NULL;
	return FALSE;
}

static gboolean
cmd_slicer_refresh_redo (GnmCommand *cmd, WorkbookControl *wbc)
{
	CmdSlicerRefresh *me = CMD_SLICER_REFRESH (cmd);
	GnmRange const *new_size = gnm_sheet_slicer_get_range (me->slicer);

	me->orig_size	 = *gnm_sheet_slicer_get_range (me->slicer);
	me->orig_content = clipboard_copy_range (me->cmd.sheet, &me->orig_size);

	sheet_clear_region (me->cmd.sheet,
		new_size->start.col, new_size->start.row,
		new_size->end.col, new_size->end.row,
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_MERGES | CLEAR_NOCHECKARRAY | CLEAR_RECALC_DEPS,
		GO_CMD_CONTEXT (wbc));

	gnm_sheet_slicer_regenerate (me->slicer);

	return FALSE;
}

static void
cmd_slicer_refresh_finalize (GObject *cmd)
{
	CmdSlicerRefresh *me = CMD_SLICER_REFRESH (cmd);
	if (NULL != me->orig_content)
		cellregion_unref (me->orig_content);
	gnm_command_finalize (cmd);
}

/**
 * cmd_slicer_refresh:
 * @wbc: the workbook control.
 **/
gboolean
cmd_slicer_refresh (WorkbookControl *wbc)
{
	CmdSlicerRefresh *me;
	char *r_name;
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmSheetSlicer *slicer;

	slicer = gnm_sheet_slicers_at_pos (sv->sheet, &sv->edit_pos);
	if (NULL == slicer)
		return FALSE;

	me = g_object_new (CMD_SLICER_REFRESH_TYPE, NULL);
	me->cmd.sheet = sv_sheet (sv);
	me->cmd.size = 1;  /* Updated below.  */
	me->orig_content = NULL;
	me->slicer = slicer;

	r_name = undo_range_name (me->cmd.sheet,
		gnm_sheet_slicer_get_range (slicer));
	me->cmd.cmd_descriptor = g_strdup_printf (_("Refreshing DataSlicer in %s"), r_name);
	g_free (r_name);

	return gnm_command_push_undo (wbc, G_OBJECT (me));
}
