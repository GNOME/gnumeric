/*
 * stf.c : reads sheets using CSV/Fixed encoding while allowing
 *         fine-tuning of the import process 
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

#include "plugin.h"
#include "gnumeric.h"
#include "file.h"
#include "mstyle.h"
#include "formats.h"

#include "stf.h"
#include "dialog-stf.h"

#include "stf-separated.h"
#include "stf-fixed.h"

#define STF_NO_FILE_DESCRIPTOR -1
#define STF_NO_DATA 0

/**
 * stf_is_valid_input
 * @src : a structure containing file information
 *
 * returns weather the input data is valid to import.
 * (meaning it checks weather it is text only)
 *
 * returns : true if valid, false otherwise
 **/
static gboolean
stf_is_valid_input (FileSource_t *src)
{
	gboolean valid;
	const char *iterator = src->data;

	if (src->len == 0)
		return FALSE;

	valid = TRUE;
	while (*iterator) {
		if (!isprint (*iterator) && *iterator != '\n' && *iterator != '\t') {
			valid = FALSE;
			break;
		}
		iterator++;
	} 
	
	return valid;
}

/**
 * stf_convert_to_unix
 * @src : a structure containing file information
 *
 * This function will convert the @src->data into a
 * unix line-terminated format. this means that CRLF (windows) will be converted to LF
 * and CR (Macintosh) to LF
 * NOTE : This will not resize the buffer
 * 
 * returns : TRUE on success, FALSE otherwise.
 **/
static gboolean
stf_convert_to_unix (FileSource_t *src)
{
	const char *iterator = src->data;
	char *dest = (char *) src->data;
	int len = 0;

	if (iterator == STF_NO_DATA)
		return FALSE;
	
	while (*iterator) {
	
		if (*iterator == '\r') {
			const char *temp = iterator;
			temp++;

			if (*temp != '\n') {
				*dest = '\n';
				dest++;
				len++;
			}
			iterator++;
		}

		*dest = *iterator;
		
		iterator++;
		dest++;
		len++;
	}
	*dest = '\0';
	
	return TRUE;
}

/**
 * get_lines_count
 * @text : a multi-line string
 * 
 * returns : the number of lines the string consists of
 **/
static unsigned long 
stf_get_lines_count (const char *text)
{
	const char *cur = text-1;
	int linecount = 0;

	do {
		cur++;
		while (*cur != '\n' && *cur != '\r'&& *cur != 0) {
			cur++;
		}
		linecount++;
	} while (*cur != 0);

	return linecount;

}


/**
 * stf_close_and_free
 * @src : struct containing information about the file to close&buffer to free
 *
 * Will close the file in @src and will free the buffer in @src
 *
 * returns : nothing
 **/
static void
stf_close_and_free (FileSource_t *src)
{
	if (src->data != STF_NO_DATA) {
		g_free ( (char *) src->data);
		src->data = STF_NO_DATA;
	}

	if (src->fd != STF_NO_FILE_DESCRIPTOR) {
		close (src->fd);
		src->fd = STF_NO_FILE_DESCRIPTOR;
	}
}

/**
 * stf_open_and_read
 * @filename : name of the file to open&read
 * @src : struct to store the file information/memory locations in
 * 
 * Will open filename, read the file into a g_alloced memory buffer
 * and fill a FileSource_t structure accordingly
 *
 * returns : true if successfully opend and read, false otherwise.
 **/
static gboolean
stf_open_and_read (FileSource_t *src)
{
	struct stat sbuf;
	const char *data;
	int const fd = open (src->filename, O_RDONLY);

	if (fd < 0)
		return FALSE;

	if (fstat(fd, &sbuf) < 0) {
		close (fd);
		return FALSE;
	}


	data = g_malloc0 (sbuf.st_size + 1);
	
	if (read (fd, (char *) data, sbuf.st_size) != sbuf.st_size)
		return FALSE;
		
	src->len  = sbuf.st_size;
	src->data = data;
	src->cur  = data;

	return TRUE;
}

/**
 * stf_read_workbook
 * @book : workbook
 * @filename : file to read from+convert
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 *
 * returns : NULL on success or an error message otherwise
 **/
static char*
stf_read_workbook (Workbook *book, char const *filename)
{        
	FileSource_t src;
	char *success = NULL;
	char *name = g_strdup_printf (_("Imported %s"), g_basename (filename));
	src.sheet  = sheet_new (book, name);
	g_free (name);

	src.fd = STF_NO_FILE_DESCRIPTOR;
	src.data = STF_NO_DATA;
	src.filename = filename;
	src.rowcount = 0;
	src.colcount = 0;

	if (!stf_open_and_read (&src)) {
		stf_close_and_free (&src);
		success = g_strdup (_("Error while trying to memory map file"));
		return success;
	}

	if (!stf_convert_to_unix (&src)) {
		stf_close_and_free (&src);
		success = g_strdup (_("Error while trying to pre-convert file"));
		return success;
		}

	if (!stf_is_valid_input (&src)) {
		stf_close_and_free (&src);
		success = g_strdup (_("This file does not seem to be a valid text file"));
		return success;
	}
		
	src.totallines = stf_get_lines_count (src.data);
	src.lines      = src.totallines;
	
	workbook_attach_sheet (book, src.sheet);
	
	success = dialog_stf (&src);

	if (success == NULL) {
		Range range = sheet_get_extent (src.sheet);

		sheet_style_optimize (src.sheet, range);
		sheet_cells_update (src.sheet, range, TRUE);
	}
	else {
		workbook_detach_sheet (book, src.sheet, TRUE);
	}

	stf_close_and_free (&src);
	
	return (success);
}

/**
 * stf_can_unload
 * @pd: a plugin data struct
 *
 * returns weather the plugin can unload, something which can be always
 * done currently as this plug-in does not keep dynamically allocated 
 * stuff in memory all the time.
 *
 * returns : always TRUE
 **/
static int
stf_can_unload (PluginData *pd)
{
	/* We can always unload */
	return TRUE;
}

/**
 * stf_cleanup_plugin
 * @pd: a plugin data struct
 * 
 * returns : nothing
 **/
static void
stf_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (NULL, stf_read_workbook);
/*	file_format_unregister_save (stf_write_workbook); */
}

/**
 * init_plugin
 * @pd : a plugin data struct
 *
 * Registers some plug-in related things like the name displayed
 * in the plug-in manager and the name in the import screen and
 * "connects" some callback routines
 *
 * returns : PLUGIN_OK normally or PLUGIN_QUIET_ERROR on a version mismatch
 **/
PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	char *desc;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;
	
	desc = _("Text File import");
	file_format_register_open (1, desc, NULL, stf_read_workbook);

	/*    desc = _("Structured Text File format (*.stf)");
	      file_format_register_save (".stf", desc, stf_write_workbook);*/
	
	desc = _("Structured Text File (STF) module");
	pd->title = g_strdup (desc);
	pd->can_unload = stf_can_unload;
	pd->cleanup_plugin = stf_cleanup_plugin;

	glade_gnome_init ();

	return PLUGIN_OK;
}
