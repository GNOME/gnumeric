/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pln.c: read sheets using a Plan Perfect encoding.
 *
 * Kevin Handy <kth@srv.net>
 *	Based on ff-csv code.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "plugin.h"
#include "numbers.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "error-info.h"
#include "file.h"
#include "sheet.h"
#include "value.h"
#include "cell.h"
#include "workbook.h"
#include "workbook-view.h"
#include "command-context.h"
#include "io-context.h"

#include <gsf/gsf-input.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <math.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

gboolean pln_file_probe (GnumFileOpener const *fo, GsfInput *input,
			 FileProbeLevel pl);
void pln_file_open (GnumFileOpener const *fo, IOContext *io_context,
                    WorkbookView *view, GsfInput *input);

#define FONT_WIDTH 8
	/* Font width should really be calculated, but it's too hard right now */

#define PLN_BYTE(pointer) (((int)((*(pointer)))) & 0xFF)
#define PLN_WORD(pointer) (PLN_BYTE (pointer) | (PLN_BYTE ((pointer) + 1) << 8))

static const char* formula1[] =
{
	"?bad1?",		/* 0 */
	"MINUS",
	"ABS",
	"INT",
	"SIGN",
	"NOT",
	"TRUE",
	"FALSE",
	"AND",
	"OR",
	"AVE",			/* 10 */
	"COUNT",
	"MIN",
	"MAX",
	"NA",
	"ISNA",
	"TIME",
	"TODAY",
	"FACT",
	"ROW",
	"COL"			/* 20 */
};

static const char* formula2[] =
{
	"?bad1?",		/* 0 */
	"POWER",
	"LN",
	"LOG",
	"SQRT",
	"PI",
	"EXP",
	"SIN",
	"COS",
	"TAN",
	"MOD",			/* 10 */
	"ASIN",
	"ACOS",
	"ATAN",
	"TERM",
	"PV",
	"PMT",
	"FV",
	"NPV",
	"LOOKUP",
	"INDEX",		/* 20 */
	"ROUND",
	"STDEV",
	"CONCAT",
	"MID",
	"LENGTH",
	"VALUE",
	"TEXT",
	"MDY",
	"MONTH",
	"DAY",			/* 30 */
	"YEAR",
	"DATETEXT",
	"DATEVALUE",
	"VAR",
	"RANDOM",
	"CURRENCY",
	"ITERATION",
	"ISVALUE",
	"ISTEXT",
	"REPLACE",		/* 40 */
	"RADIANS",
	"CELL",
	"SUBTRACT",
	"IRR",
	"FIND",
	"LEFT",
	"RIGHT",
	"UPPER",
	"LOWER",
	"PROPER",
	"CHAR",			/* 50 */
	"CODE",
	"TRIM",
	"REPEAT",
	"BLOCK",
	"CURSOR",
	"DDB",
	"SLN",
	"SYD",
	"RATE",			/* 60 */
	"STATUS",
	"FOREACH",
	"DEGREES",
	"HOUR",
	"MINUTE",
	"SECOND",
	"HMS",
	"TIMETEXT",
	"TIMEVALUE",
	"PRODUCT",		/* 70 */
	"QUOTIENT",
	"VARP",
	"STDEVP",
	"ATAN2",
	"MATCH",
	"MATCH2",
	"LOOKUP2",
	"LINK",
	"ISERR",
	"ISERR2",		/* 80 */
	"CHOOSE"
};

static gnum_float
pln_get_number (const char* ch)
{
	int exp;
	gnum_float dvalue, scale = 256.0;
	int i;

	dvalue = 0.0;
	exp = PLN_BYTE (ch);
	for (i = 1; i <= 7; i++) {
		dvalue += PLN_BYTE (ch + i) / scale;
		scale *= 256;
	}
	if (exp & 128)
		dvalue = -dvalue;
	dvalue = ldexpgnum (dvalue, ((exp & 127) - 64) * 4);

	return dvalue;
}


static char *
pln_get_row_name(int row0, int col0, int row1, int col1)
{
	static char workingname[8];	/* You better make a copy of this */
	int workptr;
	int row2;
	gboolean absolute;

	workptr = 0;

	switch (row1 & 0xc000)
	{
	case 0x0000:
		/*
		 * Positive Relative row number
		 */
		absolute = FALSE;
		row2 = row0 + (row1 & 0x3fff);
		break;

	case 0x8000:
		/*
		 * Absolute row number
		 */
		absolute = TRUE;
		row2 = (row1 & 0x3fff);
		break;

	case 0xc000:
		/*
		 * Negitive Relative row number
		 */
		absolute = FALSE;
		row2 = row0 - (0x4000 - (row1 & 0x3fff));
		break;

	default:
		/*
		 * Undefined
		 */
		absolute = FALSE;
		row2 = (row1 & 0x3fff);
		break;

	}

	/*
	 * Now try to generate a good name
	 */
	if (absolute)
	{
		sprintf(workingname + workptr, "$%d", row2 + 1);
	}
	else
	{
		sprintf(workingname + workptr, "%d", row2 + 1);
	}

	return workingname;
}

static char *
pln_get_col_name(int row0, int col0, int row1, int col1)
{
	static char workingname[8];	/* You better make a copy of this */
	int workptr;
	int col2;
	gboolean absolute;

	workptr = 0;

	switch (col1 & 0xc000)
	{
	case 0x0000:
		/*
		 * Positive Relative row number
		 */
		absolute = FALSE;
		col2 = col0 + (col1 & 0x3fff);
		break;

	case 0x8000:
		/*
		 * Absolute row number
		 */
		absolute = TRUE;
		col2 = (col1 & 0x3fff);
		break;

	case 0xc000:
		/*
		 * Negitive Relative row number
		 */
		absolute = FALSE;
		col2 = col0 - (0x4000 - (col1 & 0x3fff));
		break;

	default:
		/*
		 * Undefined
		 */
		absolute = FALSE;
		col2 = (col1 & 0x3fff);
		break;

	}

	/*
	 * Now try to generate a good name
	 */
	if (absolute)
	{
		workingname[workptr++] = '$';
	}

	if (col2 > 26)
	{
		workingname[workptr++] = 'A' + col2 / 26;
	}

	workingname[workptr++] = 'A' + col2 % 26;
	workingname[workptr] = '\0';

	return workingname;
}

static char *
pln_parse_formula(const char* ch, int row, int col)
{
	char *result1, *result2;
	int flength;
	int fptr;
	int fcode;
	char *svalue, *svalue1;
	int xlength;
	int row1, col1;
	int i;

	/* FIXME : Why no generate an expression directly ?? */
	result1 = g_strdup("=");

	flength = PLN_WORD(ch);
	fptr = 0;

#if 0
	g_warning("PLN: Formula length %d", flength);
	printf("FBlock: ");
	for (i = 0; i < flength; i++)
	{
		printf("%2x ", PLN_BYTE(ch + 2 + fptr + i));
	}
	printf("\n");
#endif

	while (fptr < flength)
	{
		fcode = PLN_BYTE(ch + 2 + fptr);
		switch(fcode)
		{
		case 1:		/* Add/concat */
			result2 = g_strconcat(result1, "+", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 2:		/* Subtraction */
			result2 = g_strconcat(result1, "-", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 3:		/* Multiplication */
			result2 = g_strconcat(result1, "*", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 4:		/* Division */
			result2 = g_strconcat(result1, "/", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 5:		/* Unary minus */
			result2 = g_strconcat(result1, "-", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 6:		/* Percent */
			result2 = g_strconcat(result1, "%", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 7:		/* SUM */
			result2 = g_strconcat(result1, "SUM(", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 9:		/* Text constant */
			xlength = PLN_BYTE(ch + 2 + fptr + 1);
			svalue = g_strndup(ch + 2 + fptr + 3, xlength);
			result2 = g_strconcat(result1, svalue, 0);
			g_free(result1); result1 = result2;
			g_free(svalue);
			fptr += xlength + 1;
			break;

		case 10:	/* Named block */
			xlength = PLN_BYTE(ch + 2 + fptr + 1);
			svalue = g_strndup(ch + 2 + fptr + 3, xlength);
			result2 = g_strconcat(result1, svalue, 0);
			g_free(result1); result1 = result2;
			g_free(svalue);
			fptr += xlength + 1;
			break;

		case 11:	/* Exponent */
			result2 = g_strconcat(result1, "^", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 12:	/* Formula function (1st block) */
			if (PLN_BYTE(ch + 2 + fptr + 1) < 21)
			{
				result2 = g_strconcat(result1,
					formula1[PLN_BYTE(ch + 2 + fptr + 1)],
					"(", 0);
				g_free(result1); result1 = result2;
			}
			else
			{
				result2 = g_strconcat(result1, "??FB1(", 0);
				g_free(result1); result1 = result2;
			}
			fptr += 2;
			break;

		case 13:	/* Formula function (2nd block) */
			if (PLN_BYTE(ch + 2 + fptr + 1) < 82)
			{
				result2 = g_strconcat(result1,
					formula2[PLN_BYTE(ch + 2 + fptr + 1)],
					"(", 0);
				g_free(result1); result1 = result2;
			}
			else
			{
				result2 = g_strconcat(result1, "??FB2(", 0);
				g_free(result1); result1 = result2;
			}
			fptr += 2;
			break;

		case 14:	/* Special '+' which sums contiguous cells */
			result2 = g_strconcat(result1, "?+?", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 15:	/* Modulo */
			result2 = g_strconcat(result1, "%", 0);
			g_free(result1); result1 = result2;
			printf("%s", " % ");
			fptr++;
			break;

		case 16:	/* Not */
			result2 = g_strconcat(result1, "!", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 17:	/* And */
			result2 = g_strconcat(result1, "&", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 18:	/* Or */
			result2 = g_strconcat(result1, "|", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 19:	/* xor */
			result2 = g_strconcat(result1, "^^", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 20:	/* IF function */
			result2 = g_strconcat(result1, "IF (", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 21:	/* Compare function */
			switch(PLN_BYTE(ch + 2 + fptr + 1))
			{
			case 1:
				result2 = g_strconcat(result1, "==", 0);
				g_free(result1); result1 = result2;
				break;
			case 2:
				result2 = g_strconcat(result1, "<>", 0);
				g_free(result1); result1 = result2;
				break;
			case 3:
				result2 = g_strconcat(result1, ">", 0);
				g_free(result1); result1 = result2;
				break;
			case 4:
				result2 = g_strconcat(result1, ">=", 0);
				g_free(result1); result1 = result2;
				break;
			case 5:
				result2 = g_strconcat(result1, "<", 0);
				g_free(result1); result1 = result2;
				break;
			case 6:
				result2 = g_strconcat(result1, "<=", 0);
				g_free(result1); result1 = result2;
				break;
			default:
				printf(" ?ErRoR? ");
				break;
			}
			fptr += 2;
			break;

		case 22:	/* Comma */
			result2 = g_strconcat(result1, ",", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 23:	/* Left Paren */
			result2 = g_strconcat(result1, "(", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 24:	/* Right paren */
			result2 = g_strconcat(result1, ")", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 25:	/* Spaces */
			for (i = 0; i < PLN_BYTE(ch + 2 + fptr + 1); i++)
			{
				result2 = g_strconcat(result1, " ", 0);
				g_free(result1); result1 = result2;
			}
			fptr += 2;
			break;

		case 26:	/* Special formula error code */
			result2 = g_strconcat(result1, "??ERROR??", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 27:	/* Cell reference */
			row1 = PLN_WORD(ch + 2 + fptr + 1);
			col1 = PLN_WORD(ch + 2 + fptr + 3);
			svalue = pln_get_row_name(row, col, row1, col1);
			svalue1 = pln_get_col_name(row, col, row1, col1);
			result2 = g_strconcat(result1, svalue1, svalue, 0);
			g_free(result1); result1 = result2;
			fptr += 5;
			break;

		case 28:	/* Block reference */
			row1 = PLN_WORD(ch + 2 + fptr + 1);
			col1 = PLN_WORD(ch + 2 + fptr + 3);
			svalue = pln_get_row_name(row, col, row1, col1);
			svalue1 = pln_get_col_name(row, col, row1, col1);
			result2 = g_strconcat(result1, svalue1, svalue, ":", 0);
			g_free(result1); result1 = result2;

			row1 = PLN_WORD(ch + 2 + fptr + 5);
			col1 = PLN_WORD(ch + 2 + fptr + 7);
			svalue = pln_get_row_name(row, col, row1, col1);
			svalue1 = pln_get_col_name(row, col, row1, col1);
			result2 = g_strconcat(result1, svalue1, svalue, 0);
			g_free(result1); result1 = result2;

			fptr += 9;
			break;

		case 29:
			result2 = g_strconcat(result1, "!=", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 30:	/* Floating point constant */
			xlength = PLN_BYTE(ch + 2 + fptr + 9);
#if 1
			svalue = g_strndup(ch + 2 + fptr + 10, xlength);
#else
			dvalue = pln_get_number(ch + 2 + fptr + 1);
			svalue = g_strdup_printf("%f", dvalue);
#endif
			result2 = g_strconcat(result1, svalue, 0);
			g_free(result1); result1 = result2;

			fptr += 10 + xlength;
			g_free(svalue);
			break;

		case 31:	/* Reference to passed arguement in user defined function */
			result2 = g_strconcat(result1, "?31?", 0);
			g_free(result1); result1 = result2;
			fptr += 2;
			break;

		case 32:	/* User function */
			result2 = g_strconcat(result1, "?32?", 0);
			g_free(result1); result1 = result2;
			xlength = PLN_BYTE(ch + 2 + fptr + 1);
			fptr += 2 + xlength;
			break;

		case 33:	/* Temporary variable (#:=) */
			result2 = g_strconcat(result1, "?33?", 0);
			g_free(result1); result1 = result2;
			xlength = PLN_BYTE(ch + 2 + fptr + 2);
			fptr += 3 + xlength;
			break;

		case 34:	/* Temporary variable (#) */
			result2 = g_strconcat(result1, "?34?", 0);
			g_free(result1); result1 = result2;
			xlength = PLN_BYTE(ch + 2 + fptr + 2);
			fptr += 3 + xlength;
			break;

		case 35:	/* 0.0 */
			result2 = g_strconcat(result1, "0.0", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 36:
			result2 = g_strconcat(result1, "{", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 37:
			result2 = g_strconcat(result1, "}", 0);
			g_free(result1); result1 = result2;
			printf(" ?%d? ", fcode);
			fptr++;
			break;

		case 38:		/* Factorial */
			result2 = g_strconcat(result1, "!", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 39:		/* Lookup operator */
			result2 = g_strconcat(result1, "<", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 40:		/* Lookup operator */
			result2 = g_strconcat(result1, ">", 0);
			g_free(result1); result1 = result2;
			printf(" ?%d? ", fcode);
			fptr++;
			break;

		case 41:		/* Attribute on */
			result2 = g_strconcat(result1, "?41?", 0);
			g_free(result1); result1 = result2;
			fptr += 2;
			break;

		case 42:		/* Attribute off */
			result2 = g_strconcat(result1, "?42?", 0);
			g_free(result1); result1 = result2;
			fptr += 2;
			break;

		case 43:	/* Total attributes for formula */
			result2 = g_strconcat(result1, "?43?", 0);
			g_free(result1); result1 = result2;
			fptr += 3;
			break;

		case 44:	/* Conditional attribute */
			result2 = g_strconcat(result1, "?44?", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 45:	/* Assumed multiply - nop display */
			result2 = g_strconcat(result1, "*", 0);
			g_free(result1); result1 = result2;
			fptr++;
			break;

		case 46:	/* Date format */
			result2 = g_strconcat(result1, "?46?", 0);
			g_free(result1); result1 = result2;
			fptr += 2;
			break;

		default:
g_warning("PLN: Undefined formula code %d", fcode);
			fptr++;
			break;
		}
	}

	return result1;
}

static ErrorInfo *
pln_parse_sheet (GsfInput *input, Sheet *sheet)
{
	int rcode, rlength;
	int crow, ccol, ctype, cformat, chelp, clength, cattr, cwidth;
	Cell *cell = NULL;
	gnum_float dvalue;
	Value	*val;
	int lastrow = SHEET_MAX_ROWS;
	int lastcol = SHEET_MAX_COLS;
	guint8 const *data;

	data = gsf_input_read (input, 6, NULL);
	if (data == NULL || PLN_WORD (data + 2) != 0)
		return error_info_new_str (_("PLN : Spreadsheet is password encrypted"));

	/*
	 * Now process the record based sections
	 *
	 * Each record consists of a two-byte record type code,
	 * followed by a two byte length code
	 */
	rcode = 0;

	do {
		data = gsf_input_read (input, 4, NULL);
		if (data == NULL)
			break;

		rcode = PLN_WORD (data);
		rlength = PLN_WORD (data);

		data = gsf_input_read (input, rlength, NULL);
		if (data == NULL)
			break;

		switch (rcode) {
		case 1:		/* Last row/column */
			lastrow = PLN_WORD (data);
			lastcol = PLN_WORD (data + 2);
			break;

		case 3:		/* Column format information */
			for (ccol = 0; ccol < rlength / 6; ccol++) {
				cattr	= PLN_WORD (data + ccol * 6 + 0);
				cformat = PLN_WORD (data + ccol * 6 + 2);
				cwidth	= PLN_WORD (data + ccol * 6 + 4);
				if ((cwidth != 0) && (ccol <= lastcol)) {
					sheet_col_set_size_pts
						(sheet, ccol,
						 (cwidth & 255) * FONT_WIDTH,
						 FALSE);
				}
			}
			break;

		default:
			g_warning("PLN : Record handling code for code %d not yet written", rcode);
		}
	} while (rcode != 25);

	/* process the CELL information */
	while (1) {
		data = gsf_input_read (input, 20, NULL);
		if (data == NULL)
			break;

		crow = PLN_WORD (data);
		/* Special value indicating end of sheet */
		if (crow == 65535)
			return NULL;
		if (crow >= SHEET_MAX_ROWS)
			return error_info_new_printf (
			             _("Invalid PLN file has more than the maximum\n"
			             "number of %s %d"),
			             _("rows"),
			             SHEET_MAX_ROWS);

		ccol	= PLN_WORD (data + 2);
		ctype	= PLN_WORD (data + 12);
		cformat	= PLN_WORD (data + 14);
		chelp	= PLN_WORD (data + 16);
		clength	= PLN_WORD (data + 18);

		switch (ctype & 7) {
		case 0:		/* Empty Cell */
		case 6:		/* format only, no data in cell */
			break;

		case 1:		/* Floating Point */
			dvalue = pln_get_number (data + 4);
			cell = sheet_cell_fetch (sheet, ccol, crow);
			cell_set_value (cell, value_new_float (dvalue));
			break;

		case 2:		/* Short Text */
			cell = sheet_cell_fetch (sheet, ccol, crow);
			if (cell != NULL) {
				val = value_new_string_nocopy (
					g_strndup (data + 5, PLN_BYTE (data + 4)));
				cell_set_value (cell, val);
			}
			break;

		case 3:		/* Long Text */
			data = gsf_input_read (input, PLN_WORD (data+4), NULL);
			if (data != NULL) {
				cell = sheet_cell_fetch (sheet, ccol, crow);
				if (cell != NULL) {
					val = value_new_string_nocopy (
						g_strndup (data + 2, PLN_WORD (data)));
					cell_set_value (cell, val);
				}
			}
			break;

		case 4:		/* Error Cell */
			cell_set_value (sheet_cell_fetch (sheet, ccol, crow),
					value_new_error (NULL, gnumeric_err_VALUE));
			break;

		case 5:		/* na Cell */
			cell_set_value (sheet_cell_fetch (sheet, ccol, crow),
					value_new_error (NULL, gnumeric_err_NA));
			break;
		}

		if (clength != 0 && cell != NULL) {
			data = gsf_input_read (input, clength, NULL);
			if (data != NULL) {
				char *expr = pln_parse_formula (data, crow, ccol);
				cell_set_text (cell, expr);
				g_free (expr);
			}
		}
	}

	return NULL;
}

void
pln_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wb_view, GsfInput *input)
{
	Workbook *wb;
	char  *name;
	Sheet *sheet;
	ErrorInfo *error;

	if (!pln_file_probe (NULL, input, FILE_PROBE_CONTENT_FULL)) {
		gnumeric_io_error_info_set (io_context,
			error_info_new_str (_("PLN : Not a PlanPerfect File")));
		return;
	}

	wb    = wb_view_workbook (wb_view);
	name  = workbook_sheet_get_free_name (wb, "PlanPerfect", FALSE, TRUE);
	sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, sheet, NULL);

	error = pln_parse_sheet (input, sheet);
	if (error != NULL) {
		workbook_sheet_detach (wb, sheet);
		gnumeric_io_error_info_set (io_context, error);
	}
}

static guint8 const signature[] =
    { 0xff, 'W','P','C', 0x10, 0, 0, 0, 0x9, 0x10 };

gboolean
pln_file_probe (GnumFileOpener const *fo, GsfInput *input,
		FileProbeLevel pl)
{
	/*
	 * a plan-perfect header
	 *	0	= -1
	 *	1-3	= "WPC"
	 *	4-7	= 16 (double word)
	 *	8	= 9 (plan perfect file)
	 *	9	= 10 (worksheet file)
	 *	10	= major version number
	 *	11	= minor version number
	 *	12-13	= encryption key
	 *	14-15	= unused
	 */
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, GSF_SEEK_SET))
		header = gsf_input_read (input, sizeof (signature), NULL);
	return header != NULL &&
	    memcmp (header, signature, sizeof (signature)) == 0;
}
