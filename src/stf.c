/*
 * stf.c : Utilizes the stf-parse engine and the dialog-stf to provide a plug-in for
 *         importing text files with a structure (CSV/fixed width)
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <gnome.h>
#include <glade/glade.h>
#include <sys/types.h>
#include <unistd.h>

#include "plugin.h"
#include "gnumeric.h"
#include "file.h"
#include "style.h"
#include "mstyle.h"
#include "formats.h"
#include "workbook.h"
#include "command-context.h"
#include "xml-io.h"

#include "stf.h"
#include "dialog-stf.h"

/**
 * stf_open_and_read
 * @filename : name of the file to open&read
 * 
 * Will open filename, read the file into a g_alloced memory buffer
 *
 * NOTE : The returned buffer has to be freed by the calling routine.
 *
 * returns : a buffer containing the file contents
 **/
static char *
stf_open_and_read (const char *filename)
{
	struct stat sbuf;
	char *data;
	int const fd = open (filename, O_RDONLY);

	if (fd < 0)
		return NULL;

	if (fstat(fd, &sbuf) < 0) {
	
		close (fd);
		return NULL;
	}

	/*
	 * We use malloc instead of g_malloc because g_malloc aborts
	 * execution if there is not enough memory
	 */
	data = calloc (1, sbuf.st_size + 1);
	if (!data)
		return NULL;

	if (read (fd, data, sbuf.st_size) != sbuf.st_size) {

		free (data);
		close (fd);
		
		return NULL;
	}

	close (fd);
	
	return data;
}

/**
 * stf_read_workbook
 * @context  : command context
 * @book     : workbook
 * @filename : file to read from+convert
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 *
 * returns : NULL on success or an error message otherwise
 **/
static int
stf_read_workbook (CommandContext *context, Workbook *book, char const *filename)
{        
	DialogStfResult_t *dialogresult;
	char *name;
	char *data;
	Sheet *sheet;

	data = stf_open_and_read (filename);
	if (!data) {
	
		gnumeric_error_read (context,
				     _("Error while trying to memory map file"));
		return -1;
	}

	if (!stf_parse_convert_to_unix (data)) {
		/*
		 * Note this buffer was allocated with malloc, not g_malloc
		 */
		free (data);	
		gnumeric_error_read (context,
				     _("Error while trying to pre-convert file"));
		return -1;
	}

	if (!stf_parse_is_valid_data (data)) {
		/*
		 * Note this buffer was allocated with malloc, not g_malloc
		 */
		
		free (data);
		gnumeric_error_read (context,
				     _("This file does not seem to be a valid text file"));
		return -1;
	}

	/*
	 * Add Sheet
	 */
	name = g_strdup_printf (_("Imported %s"), g_basename (filename));
	sheet = sheet_new (book, name);
	g_free (name);

	workbook_attach_sheet (book, sheet);
	
	dialogresult = dialog_stf (context, filename, data);

	if (dialogresult != NULL) {
		Range range;
		GSList *iterator;
		int col, rowcount;

		if (!stf_parse_sheet (dialogresult->parseoptions, dialogresult->newstart, sheet)) {

			workbook_detach_sheet (book, sheet, TRUE);
			/*
			 * Note this buffer was allocated with malloc, not g_malloc
			 */
			free (data);
			gnumeric_error_read (context,
					     _("Parse error while trying to parse data into sheet"));
			return -1;
		}

		iterator = dialogresult->formats;
		col = 0;
		rowcount = stf_parse_get_rowcount (dialogresult->parseoptions, dialogresult->newstart);
		while (iterator) {
			MStyle *style = mstyle_new ();

			mstyle_set_format (style, iterator->data);

			range.start.col = col;
			range.start.row = 0;
			range.end.col   = col;
			range.end.row   = rowcount;
		
			sheet_style_attach (sheet, range, style);

			iterator = g_slist_next (iterator);

			col++;
		}
		
		range = sheet_get_extent (sheet);
		
		sheet_style_optimize (sheet, range);
		sheet_cells_update (sheet, range, TRUE);
		workbook_set_saveinfo (book, filename, FILE_FL_MANUAL,
				       gnumeric_xml_write_workbook);
	} else {
		workbook_detach_sheet (book, sheet, TRUE);
	}

	/*
	 * Note the buffer was allocated with malloc, not with g_malloc
	 * as g_malloc aborts if there is not enough memory
	 */
	free (data);

	if (dialogresult != NULL) {

		dialog_stf_result_free (dialogresult);
	
		return 0;
	} else
		return -1;
}

/**
 * stf_init
 *
 * Registers the file format
 *
 * returns : nothing
 **/
void
stf_init (void)
{
	char *desc;

	desc = _("Text File import");
	file_format_register_open (1, desc, NULL, stf_read_workbook);
}
