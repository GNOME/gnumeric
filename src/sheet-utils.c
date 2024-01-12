/*
 * sheet-utils.c: Utility routines for Sheet content
 *
 * Copyright (C) 2002-2008 Jody Goldberg (jody@gnome.org)
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
#include <libgnumeric.h>
#include <sheet-utils.h>
#include <sheet.h>

static gboolean
sheet_cell_or_one_below_is_not_empty (Sheet *sheet, int col, int row)
{
	return !sheet_is_cell_empty (sheet, col, row) ||
		(row < gnm_sheet_get_last_row (sheet) &&
		 !sheet_is_cell_empty (sheet, col, row+1));
}

/**
 * gnm_sheet_guess_region:
 * @sheet: #Sheet
 * @region: #GnmRange
 *
 * Makes a guess at the logical containing @region and returns the possibly
 * expanded result in @region.
 **/
void
gnm_sheet_guess_region (Sheet *sheet, GnmRange *region)
{
	int col;
	int end_row;
	int offset;

	/* check in case only one cell selected */
	if (region->start.col == region->end.col) {
		int start = region->start.col;
		/* look for previous empty column */
		for (col = start - 1; col > 0; col--)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
				break;
		region->start.col = col + 1;

		/* look for next empty column */
		for (col = start + 1; col < gnm_sheet_get_max_cols (sheet); col++)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
				break;
		region->end.col = col - 1;
	}

	/* find first and last non-empty cells in region */
	for (col = region->start.col; col <= region->end.col; col++)
		if (sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
			break;

	if (col > region->end.col)
		return; /* all empty -- give up */
	region->start.col = col;

	for (col = region->end.col; col >= region->start.col; col--)
		if (sheet_cell_or_one_below_is_not_empty(sheet, col, region->start.row))
			break;
	region->end.col = col;

	/* now find length of longest column */
	for (col = region->start.col; col <= region->end.col; col++) {
		offset = 0;
		if (sheet_is_cell_empty(sheet, col, region->start.row))
			offset = 1;
		end_row = sheet_find_boundary_vertical (sheet, col,
			region->start.row + offset, col, 1, TRUE);
		if (end_row > region->end.row)
			region->end.row = end_row;
	}
}


/**
 * gnm_sheet_guess_data_range:
 * @sheet: #Sheet
 * @region: #GnmRange
 *
 * Makes a guess at the logical range containing @region and returns the possibly
 * expanded result in @region. The range is also expanded upwards.
 **/
void
gnm_sheet_guess_data_range (Sheet *sheet, GnmRange *region)
{
	int col;
	int row;
	int start = region->start.col;

	/* look for previous empty column */
	for (col = start - 1; col >= 0; col--)
		if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
			break;
	region->start.col = col + 1;

	/* look for next empty column */
	start = region->end.col;
	for (col = start + 1; col < gnm_sheet_get_max_cols (sheet); col++)
		if (!sheet_cell_or_one_below_is_not_empty (sheet, col, region->start.row))
			break;
	region->end.col = col - 1;

	for (col = region->start.col; col <= region->end.col; col++) {
		gboolean empties = FALSE;
		for (row = region->start.row - 2; row >= 0; row--)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, row)) {
				empties = TRUE;
				break;
			}
		region->start.row = empties ? row + 2 : 0;
		for (row = region->end.row + 1; row < gnm_sheet_get_max_rows (sheet); row++)
			if (!sheet_cell_or_one_below_is_not_empty (sheet, col, row))
				break;
		region->end.row = row - 1;
	}
	return;
}
