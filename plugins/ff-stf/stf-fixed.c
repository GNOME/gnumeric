/*
 * stf-fixed.c : does fixed width importing of plaintext
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include "stf-util.h"
#include "stf-fixed.h"

/**
 * stf_fixed_parse_column
 * @src : struct containing info/memory locations of the input data to parse
 * @fixinfo : struct containing info on how to parse a cell and some internal
 *            position information
 *
 * returns : a string with the cell contents or null.
 **/
static char*
stf_fixed_parse_column (FileSource_t *src, ParseFixedInfo_t *fixinfo)
{
	GString *res;
	const char* cur = src->cur;
	int splitval, len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (fixinfo != NULL, NULL);
	
	cur = src->cur;
	
	res = g_string_new ("");
	if (fixinfo->rulepos < fixinfo->splitposcnt) 
		splitval = (int) g_array_index (fixinfo->splitpos, int, fixinfo->rulepos);
	else
		splitval = -1;

	while (*cur && *cur != '\n'  && splitval != fixinfo->linepos) {
		g_string_append_c (res, *cur);

		fixinfo->linepos++;
	        cur++;
		len++;
	}

	/*if (!stf_is_line_terminator (cur))
	  cur++;*/

	src->cur = cur;

	if (len != 0) {
		char *tmp = res->str;
		g_string_free (res, FALSE);
		return tmp;
	}

	g_string_free (res, TRUE);
	return NULL;
}


/**
 * stf_fixed_parse_sheet_partial
 * @src : struct containing info/memory locations of the input data to parse
 * @fixinfo : struct containing info on how to parse a cell and some internal
 *            position information
 * @fromline : the line to start parsing at
 * @toline : the line to stop parsing at
 *
 * This function will parse the input data into a gnumeric sheet using
 * fixed width sized cells
 * Note that it will not parse lines that have already been parsed before
 * be sure to call sheet_destroy_contents before calling if you want to parse
 * the *whole* sheet
 *
 * returns : true on success, false otherwise 
 **/
gboolean
stf_fixed_parse_sheet_partial (FileSource_t *src, ParseFixedInfo_t *fixinfo, int fromline, int toline)
{
	int row = 0, col;
	char *field;
	const char *cur = src->cur;

	g_return_val_if_fail (src != NULL, FALSE);
	g_return_val_if_fail (fixinfo != NULL, FALSE);

	if (fixinfo->splitposcnt - 1 >= SHEET_MAX_COLS) {
		g_warning (WARN_COLS_MSG, SHEET_MAX_COLS);
		return FALSE;
	}
	
	if (toline == -1)
		toline = src->lines + 1;
	
	while (*src->cur) {
		fixinfo->linepos = 0;
		fixinfo->rulepos = 0;
		col = 0;

		if (row >= fromline && !sheet_cell_get (src->sheet, 0, row)) {
			while (*src->cur && *src->cur != '\n') {
				Cell* newcell;
			
				field = stf_fixed_parse_column (src, fixinfo);

				/* This may look stupid but was done for
				   consistency with the stf_separated stuff */
				if (!field)
					field = g_strdup ("");
					
				newcell = sheet_cell_new (src->sheet, col, row);
				cell_set_text_simple (newcell, field);
				g_free(field);

				col++;
				fixinfo->rulepos++;
			}
		} else {
			while (*src->cur && *src->cur != '\n')
				src->cur++;
		}

		if (*src->cur) 
			src->cur++;

		if (++row >= SHEET_MAX_ROWS) {
			g_warning (WARN_ROWS_MSG, SHEET_MAX_ROWS);
			src->cur = cur;
			return FALSE;
		}
		
		if (row > toline) break;
	}

	src->cur      = cur;
	src->rowcount = src->lines;
	src->colcount = fixinfo->splitposcnt - 1;
	return TRUE;
}

/**
 * stf_fixed_parse_sheet_partial
 * @src : struct containing info/memory locations of the input data to parse
 * @fixinfo : struct containing info on how to parse a cell and some internal
 *            position information
 *
 * This function will parse the input data into a gnumeric sheet using
 * fixed width sized cells
 * It will call upon stf_fixed_parse_sheet_partial and will *always*
 * parse the whole sheet. (except already parsed lines ofcourse)
 *
 * returns : true on success, false otherwise 
 **/
gboolean
stf_fixed_parse_sheet (FileSource_t *src, ParseFixedInfo_t *fixinfo)
{
	return stf_fixed_parse_sheet_partial (src, fixinfo, 0, -1);
}







