/**
 * stf_separated.c : handles CSV importing
 *
 * Almer. S. Tigelaar. <almer1@dds.nl>
 **/

#include "stf-util.h"
#include "stf-separated.h"

/**
 * stf_separated_is_separator
 * @character : pointer to the character to check
 * @sepinfo : a struct containing what is considered to be a separator
 *
 * returns : true if character is a separator, false otherwise
 **/
static gboolean
stf_separated_is_separator (const char *character, SeparatedInfo_t *sepinfo)
{
	g_return_val_if_fail (character != NULL, FALSE);
	g_return_val_if_fail (sepinfo != NULL, FALSE);

	switch (*character) {
	case '\t'             : if (sepinfo->separator & TST_TAB)    return TRUE; break;
	case ';'              : if (sepinfo->separator & TST_COLON)  return TRUE; break;
	case ','              : if (sepinfo->separator & TST_COMMA)  return TRUE; break;
	case ' '              : if (sepinfo->separator & TST_SPACE)  return TRUE; break;
	default               : {
		if ( (sepinfo->separator & TST_CUSTOM) && (sepinfo->custom == *character))
			return TRUE;
	} break;
        }

	return FALSE;
}

/**
 * stf_separated_parse_column
 * @src : struct containing information/memory location of the input data
 * @sepinfo : struct containing separation customizations
 *
 * returns : a pointer to the parsed cell contents. (has to be freed by the calling routine)
 **/
static char*
stf_separated_parse_column (FileSource_t *src, SeparatedInfo_t *sepinfo)
{
	GString *res;
	const char *cur;
	gboolean isstring, sawstringterm;
	int len = 0;

	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (sepinfo != NULL, NULL);

	cur = src->cur;
	
	if (*cur == sepinfo->string) {
		cur++;
		isstring = TRUE;
		sawstringterm = FALSE;
	} else {
		isstring = FALSE;
		sawstringterm = TRUE;
        }


	res = g_string_new ("");
	
	while (*cur && *cur != '\n') {

		if (!sawstringterm) {
			if (*cur == sepinfo->string) {
				sawstringterm = TRUE;
				cur++;
				continue;
			}
		} else {
			if (stf_separated_is_separator (cur, sepinfo)) {
				if (sepinfo->duplicates) {
					const char *nextcur = cur;
					nextcur++;
					if (stf_separated_is_separator (nextcur, sepinfo)) {
						cur = nextcur;
						continue;
					}
				}
				break;
			}
		}

		g_string_append_c (res, *cur);

		len++;
		cur++;
	}

	/* Only skip over cell terminators, not line terminators or terminating nulls*/
	if (*cur != '\n' && *cur)
		cur++;

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
 * stf_separated_parse_sheet_partial
 * @src : struct containing information/memory locations of the input data
 * @sepinfo : struct containing separations customizations
 * @startrow : row at which to start parsing
 * @stoprow : row at which to stop parsing or -1 if the whole file need to be parsed
 *
 * This routine will parse the input data into a gnumeric sheet.
 * Note that it will not parse lines that have already been parsed before
 * be sure to call sheet_destroy_contents before calling if you want to parse
 * the *whole* sheet
 *
 * returns : true on successful parse, false otherwise.
 **/
gboolean
stf_separated_parse_sheet_partial (FileSource_t *src, SeparatedInfo_t *sepinfo, int fromline, int toline)
{
	int col, row = 0, colhigh = 0; 
	char *field;
	const char *cur;

	g_return_val_if_fail (src != NULL, FALSE);
	g_return_val_if_fail (sepinfo != NULL, FALSE);
	
	cur = src->cur;
	
	if (toline == -1)
		toline = src->lines + 1;
	
	while (*src->cur) {
		col = 0;

		if (row >= fromline && !sheet_cell_get (src->sheet, 0, row)) {
			while (*src->cur && *src->cur != '\n') {
				Cell *newcell;
				
				field = stf_separated_parse_column (src, sepinfo);
				
				if (field) {
					newcell = sheet_cell_fetch (src->sheet, col, row);
					cell_set_text (newcell, field);
					g_free(field);
				}
					
				if (++col >= SHEET_MAX_COLS) {
					g_warning (WARN_COLS_MSG, SHEET_MAX_COLS);
					src->cur = cur;
					return FALSE;
				}
			}
		} else {
			Cell *cell;
			
			while (*src->cur && *src->cur != '\n')
				src->cur++;

			/* This is an ugly hack, is there a better way to do this??? */
			col = 0;
			do {
				cell = sheet_cell_get (src->sheet, col++, row);
			} while (cell);
			col--;
		}

		if (*src->cur)
			src->cur++;

		if (++row >= SHEET_MAX_ROWS) {
			g_warning (WARN_ROWS_MSG, SHEET_MAX_ROWS);
			src->cur = cur;
			return FALSE;
		}
		
		if (col > colhigh)
			colhigh = col;

		if (row > toline) break;
	}
	
	src->cur = cur;
	src->rowcount = src->lines;
	src->colcount = colhigh - 1;
	return TRUE;
}

/**
 * stf_separated_parse_sheet
 * @src : struct containing information/memory locations of the input data
 * @sepinfo : struct containing separations customizations
 *
 * This routine will parse the input data into a gnumeric sheet.
 * it calls upon stf_separated_parse to do so and it will parse the whole sheet
 *
 * returns : true on successful parse, false otherwise.
 **/
gboolean
stf_separated_parse_sheet (FileSource_t *src, SeparatedInfo_t *sepinfo)
{
	return stf_separated_parse_sheet_partial (src, sepinfo, 0, -1);
}












