/*
 * ms-formula-read.c: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "ms-formula-read.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"
#include <gutils.h>
#include <func.h>
#include <value.h>
#include <expr-name.h>
#include <str.h>
#include <parse-util.h>
#include <sheet.h>

#include <fcntl.h>
#include <assert.h>
#include <stdio.h>

/* #define NO_DEBUG_EXCEL */

/**
 * Various bits of data for operators
 * see S59E2B.HTM for formula_ptg values
 * formula PTG, prefix, middle, suffix, precedence
 **/

/**
 * Populated from xlcall.h
 * Functions in order, zero based, with number of arguments or
 *		'-1' for vararg or with optional arguments.
 *		'-2' for unknown numbers or arguments.
 * NB. all argument counts are those for Excel.
 * Many of the unknowns are xlm macro commands.
 *     macrofun.hlp has info on them but supporting Excel4 macro sheets is not
 *     top priority.
 **/
FormulaFuncData formula_func_data[FORMULA_FUNC_DATA_LEN] =
{
/* 0 */		{ "COUNT", -1 },
/* 1 */		{ "IF", -1 },
/* 2 */		{ "ISNA", 1 },
/* 3 */		{ "ISERROR", 1 },
/* 4 */		{ "SUM", -1 },
/* 5 */		{ "AVERAGE", -1 },
/* 6 */		{ "MIN", -1 },
/* 7 */		{ "MAX", -1 },
/* 8 */		{ "ROW", 1 },
/* 9 */		{ "COLUMN", 1 },
/* 10 */	{ "NA", 0 },
/* 11 */	{ "NPV", -1 },
/* 12 */	{ "STDEV", -1 },
/* 13 */	{ "DOLLAR", -1 },
/* 14 */	{ "FIXED", -1 },
/* 15 */	{ "SIN", 1 },
/* 16 */	{ "COS", 1 },
/* 17 */	{ "TAN", 1 },
/* 18 */	{ "ATAN", 1 },
/* 19 */	{ "PI", 0 },
/* 20 */	{ "SQRT", 1 },
/* 21 */	{ "EXP", 1 },
/* 22 */	{ "LN", 1 },
/* 23 */	{ "LOG10", 1 },
/* 24 */	{ "ABS", 1 },
/* 25 */	{ "INT", 1 },
/* 26 */	{ "SIGN", 1 },
/* 27 */	{ "ROUND", 2 },
/* 28 */	{ "LOOKUP", -1 },
/* 29 */	{ "INDEX", -1 }, /* array form has only 3 */
/* 30 */	{ "REPT", 2 },
/* 31 */	{ "MID", 3 },
/* 32 */	{ "LEN", 1 },
/* 33 */	{ "VALUE", 1 },
/* 34 */	{ "TRUE", 0 },
/* 35 */	{ "FALSE", 0 },
/* 36 */	{ "AND", -1 },
/* 37 */	{ "OR", -1 },
/* 38 */	{ "NOT", 1 },
/* 39 */	{ "MOD", 2 },
/* 40 */	{ "DCOUNT", 3 },
/* 41 */	{ "DSUM", 3 },
/* 42 */	{ "DAVERAGE", 3 },
/* 43 */	{ "DMIN", 3 },
/* 44 */	{ "DMAX", 3 },
/* 45 */	{ "DSTDEV", 3 },
/* 46 */	{ "VAR", -1 },
/* 47 */	{ "DVAR", 3 },
/* 48 */	{ "TEXT", 2 },
/* 49 */	{ "LINEST", -1 },
/* 50 */	{ "TREND", -1 },
/* 51 */	{ "LOGEST", -1 },
/* 52 */	{ "GROWTH", -1 },
/* 53 */	{ "GOTO", -2 },
/* 55 Unknown*/	{ "UnknownFunction55", -2 },
/* 54 */	{ "HALT", -2 },
/* 56 */	{ "PV", -1 },	/* type is optional */
/* 57 */	{ "FV", -1 },	/* type is optional */
/* 58 */	{ "NPER", -1 },	/* type is optional */
/* 59 */	{ "PMT", -1 },	/* type is optional */
/* 60 */	{ "RATE", -1 },	/* guess is optional */
/* 61 */	{ "MIRR", 3 },
/* 62 */	{ "IRR", -1 },	/* guess is optional */
/* 63 */	{ "RAND", 0 },
/* 64 */	{ "MATCH", 3 },/* match_type is optional */
/* 65 */	{ "DATE", 3 },
/* 66 */	{ "TIME", 3 },
/* 67 */	{ "DAY", 1 },
/* 68 */	{ "MONTH", 1 },
/* 69 */	{ "YEAR", 1 },
/* 70 */	{ "WEEKDAY", -1 },/* Return type is optional */
/* 71 */	{ "HOUR", 1 },
/* 72 */	{ "MINUTE", 1 },
/* 73 */	{ "SECOND", 1 },
/* 74 */	{ "NOW", 0 },
/* 75 */	{ "AREAS", 1 },
/* 76 */	{ "ROWS", 1 },
/* 77 */	{ "COLUMNS", 1 },
/* 78 */	{ "OFFSET", -1 },
/* 79 */	{ "ABSREF", 2 },	/* XLM */
/* 80 */	{ "RELREF", -2 },
/* 81 */	{ "ARGUMENT", -2 },
/* 82 */	{ "SEARCH", -1 },/* Start_num is optional */
/* 83 */	{ "TRANSPOSE", 1 },
/* 84 */	{ "ERROR", -2 },
/* 85 */	{ "STEP", -2 },
/* 86 */	{ "TYPE", 1 },
/* 87 */	{ "ECHO", -2 },
/* 88 */	{ "SETNAME", -2 },
/* 89 */	{ "CALLER", -2 },
/* 90 */	{ "DEREF", -2 },
/* 91 */	{ "WINDOWS", -2 },
/* 92 */	{ "SERIESSUM", 4 }, /* Renamed from SERIES */
/* 93 */	{ "DOCUMENTS", -2 },
/* 94 */	{ "ACTIVECELL", -2 },
/* 95 */	{ "SELECTION", -2 },
/* 96 */	{ "RESULT", -2 },
/* 97 */	{ "ATAN2", 2 },
/* 98 */	{ "ASIN", 1 },
/* 99 */	{ "ACOS", 1 },
/* 100 */	{ "CHOOSE", -1 },
/* 101 */	{ "HLOOKUP", -1 }, /* range_lookup is optional */
/* 102 */	{ "VLOOKUP", -1 }, /* range_lookup is optional */
/* 103 */	{ "LINKS", -2 },
/* 104 */	{ "INPUT", -2 },
/* 105 */	{ "ISREF", 1 },	/* This a guess */
/* 106 */	{ "GETFORMULA", -2 },
/* 107 */	{ "GETNAME", -2 },
/* 108 */	{ "SETVALUE", -2 },
/* 109 */	{ "LOG", -1 }, /* Base is optional */
/* 110 */	{ "EXEC", -2 },
/* 111 */	{ "CHAR", 1 },
/* 112 */	{ "LOWER", 1 },
/* 113 */	{ "UPPER", 1 },
/* 114 */	{ "PROPER", 1 },
/* 115 */	{ "LEFT", -1 }, /* Num_chars is optional */
/* 116 */	{ "RIGHT", -1 }, /* Num_chars is optional */
/* 117 */	{ "EXACT", 2 },
/* 118 */	{ "TRIM", 1 },
/* 119 */	{ "REPLACE", 4 },
/* 120 */	{ "SUBSTITUTE", -1 }, /* Instance num is optional */
/* 121 */	{ "CODE", 1 },
/* 122 */	{ "NAMES", -2 },
/* 123 */	{ "DIRECTORY", -2 },
/* 124 */	{ "FIND", -1 }, /* start_num is optional */
/* 125 */	{ "CELL", 2 },
/* 126 */	{ "ISERR", 1 },
/* 127 */	{ "ISTEXT", 1 },
/* 128 */	{ "ISNUMBER", 1 },
/* 129 */	{ "ISBLANK", 1 },
/* 130 */	{ "T", 1 },
/* 131 */	{ "N", 1 },
/* 132 */	{ "FOPEN", -2 },
/* 133 */	{ "FCLOSE", -2 },
/* 134 */	{ "FSIZE", -2 },
/* 135 */	{ "FREADLN", -2 },
/* 136 */	{ "FREAD", -2 },
/* 137 */	{ "FWRITELN", -2 },
/* 138 */	{ "FWRITE", -2 },
/* 139 */	{ "FPOS", -2 },
/* 140 */	{ "DATEVALUE", 1 },
/* 141 */	{ "TIMEVALUE", 1 },
/* 142 */	{ "SLN", 3 },
/* 143 */	{ "SYD", 4 },
/* 144 */	{ "DDB", -1 }, /* Factor is optional */
/* 145 */	{ "GETDEF", -2 },
/* 146 */	{ "REFTEXT", -2 },
/* 147 */	{ "TEXTREF", -2 },
/* 148 */	{ "INDIRECT", -1 }, /* ai is optional */
/* 149 */	{ "REGISTER", -1 },
/* 150 */	{ "CALL", -1 },
/* 151 */	{ "ADDBAR", -2 },
/* 152 */	{ "ADDMENU", -2 },
/* 153 */	{ "ADDCOMMAND", -2 },
/* 154 */	{ "ENABLECOMMAND", -2 },
/* 155 */	{ "CHECKCOMMAND", -2 },
/* 156 */	{ "RENAMECOMMAND", -2 },
/* 157 */	{ "SHOWBAR", -2 },
/* 158 */	{ "DELETEMENU", -2 },
/* 159 */	{ "DELETECOMMAND", -2 },
/* 160 */	{ "GETCHARTITEM", -2 },
/* 161 */	{ "DIALOGBOX", -2 },
/* 162 */	{ "CLEAN", 1 },
/* 163 */	{ "MDETERM", 1 },
/* 164 */	{ "MINVERSE", 1 },
/* 165 */	{ "MMULT", 2 },
/* 166 */	{ "FILES", -2 },
/* 167 */	{ "IPMT", -1 },	/* Type is optional */
/* 168 */	{ "PPMT", -1 },	/* Type is optional */
/* 169 */	{ "COUNTA", -1 },/* Type is optional */
/* 170 */	{ "CANCELKEY", 1 },	/* XLM */
/* 171 Unknown*/{ "UnknownFunction171", -2 },
/* 172 Unknown*/{ "UnknownFunction172", -2 },
/* 173 Unknown*/{ "UnknownFunction173", -2 },
/* 174 Unknown*/{ "UnknownFunction174", -2 },
/* 175 */	{ "INITIATE", -2 },
/* 176 */	{ "REQUEST", -2 },
/* 177 */	{ "POKE", -2 },
/* 178 */	{ "EXECUTE", -2 },
/* 179 */	{ "TERMINATE", -2 },
/* 180 */	{ "RESTART", -2 },
/* 181 */	{ "HELP", -2 },
/* 182 */	{ "GETBAR", -2 },
/* 183 */	{ "PRODUCT", -1 },
/* 184 */	{ "FACT", 1 },
/* 185 */	{ "GETCELL", -1 },
/* 186 */	{ "GETWORKSPACE", -1 },
/* 187 */	{ "GETWINDOW", -1 },
/* 188 */	{ "GETDOCUMENT", -1 },
/* 189 */	{ "DPRODUCT", 3 },
/* 190 */	{ "ISNONTEXT", 1 },
/* 191 */	{ "GETNOTE", -2 },
/* 192 */	{ "NOTE", -2 },
/* 193 */	{ "STDEVP", -1 },
/* 194 */	{ "VARP", -1 },
/* 195 */	{ "DSTDEVP", 3 },
/* 196 */	{ "DVARP", 3 },
/* 197 */	{ "TRUNC", -1 }, /* num_digits is optional */
/* 198 */	{ "ISLOGICAL", 1 },
/* 199 */	{ "DCOUNTA", 3 },
/* 200 */	{ "DELETEBAR", -2 },
/* 201 */	{ "UNREGISTER", -2 },
/* 202 Unknown*/{ "UnknownFunction202", -2 },
/* 203 Unknown*/{ "UnknownFunction203", -2 },
/* 204 */	{ "USDOLLAR", -2 },
/* 205 */	{ "FINDB", -2 },
/* 206 */	{ "SEARCHB", -2 },
/* 207 */	{ "REPLACEB", -2 },
/* 208 */	{ "LEFTB", -2 },
/* 209 */	{ "RIGHTB", -2 },
/* 210 */	{ "MIDB", -2 },
/* 211 */	{ "LENB", -2 },
/* 212 */	{ "ROUNDUP", 2 },
/* 213 */	{ "ROUNDDOWN", 2 },
/* 214 */	{ "ASC", -2 },
/* 215 */	{ "DBCS", -2 },
/* 216 */	{ "RANK", -1 },	/* order is optional */
/* 217 Unknown*/{ "UnknownFunction217", -2 },
/* 218 Unknown*/{ "UnknownFunction218", -2 },
/* 219 */	{ "ADDRESS", -1 },	/* abs_num is optional */
/* 220 */	{ "DAYS360", -1 },	/* method is optional */
/* 221 */	{ "TODAY", 0 },
/* 222 */       { "VDB", -1 },
/* 223 Unknown*/{ "UnknownFunction223", -2 },
/* 224 Unknown*/{ "UnknownFunction224", -2 },
/* 225 Unknown*/{ "UnknownFunction225", -2 },
/* 226 Unknown*/{ "UnknownFunction226", -2 },
/* 227 */	{ "MEDIAN", -1 },
/* 228 */	{ "SUMPRODUCT", -1 },
/* 229 */	{ "SINH", 1 },
/* 230 */	{ "COSH", 1 },
/* 231 */	{ "TANH", 1 },
/* 232 */	{ "ASINH", 1 },
/* 233 */	{ "ACOSH", 1 },
/* 234 */	{ "ATANH", 1 },
/* 235 */	{ "DGET", 3 },
/* 236 */	{ "CREATEOBJECT", -2 },
/* 237 */	{ "VOLATILE", },
/* 238 */	{ "LASTERROR", },
/* 239 */	{ "CUSTOMUNDO", -2 },
/* 240 */	{ "CUSTOMREPEAT", -2 },
/* 241 */	{ "FORMULACONVERT", -2 },
/* 242 */	{ "GETLINKINFO", -2 },
/* 243 */	{ "TEXTBOX", },
/* 244 */	{ "INFO", 1 },
/* 245 */	{ "GROUP", -2 },
/* 246 */	{ "GETOBJECT", -2 },
/* 247 */	{ "DB", -1 },	/* month is optional */
/* 248 */	{ "PAUSE", -2 },
/* 249 Unknown*/{ "UnknownFunction249", -2 },
/* 250 Unknown*/{ "UnknownFunction250", -2 },
/* 251 */	{ "RESUME", -2 },
/* 252 */	{ "FREQUENCY", 2 },
/* 253 */	{ "ADDTOOLBAR", -2 },
/* 254 */	{ "DELETETOOLBAR", -2 },
/* 255 */	{ "MAGIC", -1 }, /* Dodgy special case */
/* 256 */	{ "RESETTOOLBAR", -2 },
/* 257 */	{ "EVALUATE", },
/* 258 */	{ "GETTOOLBAR", -2 },
/* 259 */	{ "GETTOOL", -2 },
/* 260 */	{ "SPELLINGCHECK", -2 },
/* 261 */	{ "ERROR.TYPE", 1 },
/* 262 */	{ "APPTITLE", -2 },
/* 263 */	{ "WINDOWTITLE", -2 },
/* 264 */	{ "SAVETOOLBAR", -2 },
/* 265 */	{ "ENABLETOOL", -2 },
/* 266 */	{ "PRESSTOOL", -2 },
/* 267 */	{ "REGISTERID", },
/* 268 */	{ "GETWORKBOOK", -2 },
/* 269 */	{ "AVEDEV", -1 },
/* 270 */	{ "BETADIST", -1 },
/* 271 */	{ "GAMMALN", 1 },
/* 272 */	{ "BETAINV", -1 },
/* 273 */	{ "BINOMDIST", 4 },
/* 274 */	{ "CHIDIST", 2 },
/* 275 */	{ "CHIINV", 2 },
/* 276 */	{ "COMBIN", 2 },
/* 277 */	{ "CONFIDENCE", 3 },
/* 278 */	{ "CRITBINOM", 3 },
/* 279 */	{ "EVEN", 1 },
/* 280 */	{ "EXPONDIST", 3 },
/* 281 */	{ "FDIST", 3 },
/* 282 */	{ "FINV", 3 },
/* 283 */	{ "FISHER", 1 },
/* 284 */	{ "FISHERINV", 1 },
/* 285 */	{ "FLOOR", 2 },
/* 286 */	{ "GAMMADIST", 4 },
/* 287 */	{ "GAMMAINV", 3 },
/* 288 */	{ "CEILING", 2 },
/* 289 */	{ "HYPGEOMDIST", 4 },
/* 290 */	{ "LOGNORMDIST", 3 },
/* 291 */	{ "LOGINV", 3 },
/* 292 */	{ "NEGBINOMDIST", 3 },
/* 293 */	{ "NORMDIST", 4 },
/* 294 */	{ "NORMSDIST", 1 },
/* 295 */	{ "NORMINV", 3 },
/* 296 */	{ "NORMSINV", 1 },
/* 297 */	{ "STANDARDIZE", 3 },
/* 298 */	{ "ODD", 1 },
/* 299 */	{ "PERMUT", 2 },
/* 300 */	{ "POISSON", 3 },
/* 301 */	{ "TDIST", 3 },
/* 302 */	{ "WEIBULL", 4 },
/* 303 */	{ "SUMXMY2", 2 },
/* 304 */	{ "SUMX2MY2", 2 },
/* 305 */	{ "SUMX2PY2", 2 },
/* 306 */	{ "CHITEST", 2 },
/* 307 */	{ "CORREL", 2 },
/* 308 */	{ "COVAR", 2 },
/* 309 */	{ "FORECAST", 3 },
/* 310 */	{ "FTEST", 2 },
/* 311 */	{ "INTERCEPT",2 },
/* 312 */	{ "PEARSON", 2 },
/* 313 */	{ "RSQ", 2 },
/* 314 */	{ "STEYX", 2 },
/* 315 */	{ "SLOPE", 2 },
/* 316 */	{ "TTEST", 4 },
/* 317 */	{ "PROB", -1 },	/* upper_limit is optional */
/* 318 */	{ "DEVSQ", -1 },
/* 319 */	{ "GEOMEAN", -1 },
/* 320 */	{ "HARMEAN", -1 },
/* 321 */	{ "SUMSQ", -1 },
/* 322 */	{ "KURT", -1 },
/* 323 */	{ "SKEW", -1 },
/* 324 */	{ "ZTEST", -1 },/* sigma is optional */
/* 325 */	{ "LARGE", 2 },
/* 326 */	{ "SMALL", 2 },
/* 327 */	{ "QUARTILE", 2 },
/* 328 */	{ "PERCENTILE", 2 },
/* 329 */	{ "PERCENTRANK", -1 },/* Significance is optional */
/* 330 */	{ "MODE", -1 },
/* 331 */	{ "TRIMMEAN", 2 },
/* 332 */	{ "TINV", 2 },
/* 333 Unknown*/{ "UnknownFunction333", -2 },
/* 334 */	{ "MOVIECOMMAND", -2 },
/* 335 */	{ "GETMOVIE", -2 },
/* 336 */	{ "CONCATENATE", -1 },
/* 337 */	{ "POWER", 2 },
/* 338 */	{ "PIVOTADDDATA", -2 },
/* 339 */	{ "GETPIVOTTABLE", -2 },
/* 340 */	{ "GETPIVOTFIELD", -2 },
/* 341 */	{ "GETPIVOTITEM", -2 },
/* 342 */	{ "RADIANS", 1 },
/* 343 */	{ "DEGREES", 1 },
/* 344 */	{ "SUBTOTAL", -1 },
/* 345 */	{ "SUMIF", -1 }, /* Actual range is optional */
/* 346 */	{ "COUNTIF", 2 },
/* 347 */	{ "COUNTBLANK", 1 },
/* 348 */	{ "SCENARIOGET", -2 },
/* 349 */	{ "OPTIONSLISTSGET", -2 },
/* 350 */	{ "ISPMT", 4 },
/* 351 */	{ "DATEDIF", -2 },
/* 352 */	{ "DATESTRING", -2 },
/* 353 */	{ "NUMBERSTRING", -2 },
/* 354 */	{ "ROMAN", -1 },
/* 355 */	{ "OPENDIALOG", -2 },
/* 356 */	{ "SAVEDIALOG", -2 },
/* 357 */	{ "VIEWGET", -2 },
/* 358 */	{ "GETPIVOTDATA", 2 },
/* 359 */	{ "HYPERLINK", -1 },	/* cell_contents is optional */
/* 360 */	{ "PHONETIC", -2 },
/* 361 */	{ "AVERAGEA", -1 },
/* 362 */	{ "MAXA", -1 },
/* 363 */	{ "MINA", -1 },
/* 364 */	{ "STDEVPA", -1 },
/* 365 */	{ "VARPA", -1 },
/* 366 */	{ "STDEVA", -1 },
/* 367 */	{ "VARA", -1 }
};

static ExprTree *
expr_tree_string (const char *str)
{
	return expr_tree_new_constant (value_new_string (str));
}

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV7 (guint8 col, guint16 gbitrw, int curcol, int currow,
	  gboolean const shared)
{
	CellRef *cr = g_new (CellRef, 1);
	guint16 const row = (guint16)(gbitrw & 0x3fff);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 2) {
		printf ("7In : 0x%x, 0x%x  at %s%s\n", col, gbitrw,
			cell_coord_name (curcol, currow), (shared?" (shared)":""));
	}
#endif

	cr->sheet = NULL;

	cr->row_relative = (gbitrw & 0x8000) != 0;
	if (cr->row_relative) {
		if (shared) {
			/* ICK ! XL is storing signed numbers without storing
			 * the sign bit.  we need to assume that if the 13th
			 * bit is set it is meant to be negative.  then we
			 * reinstate the sign bit and allow compiler to handle
			 * sign extension.
			 */
			if (row & 0x2000)
				cr->row = (gint16)(row | 0xc000);
			else
				cr->row = row;
		} else
			cr->row = row - currow;
	} else
		cr->row = row;

	cr->col_relative = (gbitrw & 0x4000) != 0;
	if (cr->col_relative) {
		if (shared)
			cr->col = (gint8)col;
		else
			cr->col = col - curcol;
	} else
		cr->col = col;

	return cr;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV8 (guint16 row, guint16 gbitcl, int curcol, int currow,
	  gboolean const shared)
{
	CellRef *cr = g_new (CellRef, 1);
	guint8 const col = (guint8)(gbitcl & 0xff);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 2) {
		printf ("8In : 0x%x, 0x%x  at %s%s\n", row, gbitcl,
			cell_coord_name (curcol, currow), (shared?" (shared)":""));
	}
#endif

	cr->sheet = NULL;

	cr->row_relative = (gbitcl & 0x8000) != 0;
	if (cr->row_relative) {
		if (shared)
			cr->row = (gint16)row;
		else
			cr->row = row - currow;
	} else
		cr->row = row;

	cr->col_relative = (gbitcl & 0x4000) != 0;
	if (cr->col_relative) {
		if (shared)
			cr->col = (gint8)col;
		else
			cr->col = col - curcol;
	} else
		cr->col = col;

	return cr;
}

static void
parse_list_push (ExprList **list, ExprTree *pd)
{
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_formula_debug > 5) {
			printf ("Push 0x%x\n", (int)pd);
		}
#endif
	if (!pd)
		printf ("FIXME: Pushing nothing onto excel function stack\n");
	*list = expr_list_prepend (*list, pd);
}
static void
parse_list_push_raw (ExprList **list, Value *v)
{
	parse_list_push (list, expr_tree_new_constant (v));
}

static ExprTree *
parse_list_pop (ExprList **list)
{
	/* Get the head */
	ExprList *tmp = g_slist_nth (*list, 0);
	if (tmp != NULL)
	{
		ExprTree *ans = tmp->data;
		*list = g_slist_remove (*list, ans);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_formula_debug > 5) {
		    printf ("Pop 0x%x\n", (int)ans);
		}
#endif
		return ans;
	}

	puts ("Incorrect number of parsed formula arguments");
	return expr_tree_string ("WrongArgs");
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static ExprList *
parse_list_last_n (ExprList **list, gint n)
{
	ExprList *l = NULL;
	while (n-->0)
		l = g_slist_prepend (l, parse_list_pop(list));
	return l;
}


static void
parse_list_free (ExprList **list)
{
	while (*list)
		expr_tree_unref (parse_list_pop(list));
}

static void
make_inter_sheet_ref (ExcelWorkbook *wb, guint16 extn_idx, CellRef *a, CellRef *b)
{
	g_return_if_fail (wb);
	g_return_if_fail (a);

	a->sheet = biff_get_externsheet_name (wb, extn_idx, 1);
	if (b)
		b->sheet = biff_get_externsheet_name (wb, extn_idx, 0);
}

static void
make_inter_sheet_ref_v7 (ExcelWorkbook *wb, guint16 extn_idx,
			 guint16 first, guint16 second, CellRef *a, CellRef *b)
{
	ExcelSheet *sheet;

	g_return_if_fail (wb);
	g_return_if_fail (a);

	if ((gint16)extn_idx>0) {
		printf ("FIXME: BIFF 7 ExternSheet 3D ref\n");
		return;
	}

	g_return_if_fail (wb->excel_sheets);
	g_return_if_fail (first<wb->excel_sheets->len);

	sheet = g_ptr_array_index (wb->excel_sheets, first);
	g_return_if_fail (sheet);
	a->sheet = sheet->gnum_sheet;

	if (b) {
		g_return_if_fail (second < wb->excel_sheets->len);
		sheet = g_ptr_array_index (wb->excel_sheets, second);
		g_return_if_fail (sheet);
		b->sheet = sheet->gnum_sheet;
	}
}

static gboolean
make_function (ExprList **stack, int fn_idx, int numargs)
{
	FunctionDefinition *name = NULL;

	if (fn_idx == 0xff) {
		/*
		 * This is undocumented.
		 * function 0xff seems to indicate an external function whose
		 * name should be on top of the stack.
		 */
		/* FIXME FIXME FIXME : How to handle missing trailing args ?? */
		ExprList *args = parse_list_last_n (stack, numargs-1);
		ExprTree *tmp = parse_list_pop (stack);
		char const *f_name = NULL;

		if (tmp != NULL) {
		    if (tmp->any.oper == OPER_CONSTANT &&
			tmp->constant.value->type == VALUE_STRING)
			    f_name = tmp->constant.value->v_str.val->str;
		    else if (tmp->any.oper == OPER_NAME)
			    f_name = tmp->name.name->name->str;
		}

		if (f_name == NULL) {
			if (tmp)
				expr_tree_unref (tmp);
			parse_list_free (&args);
			parse_list_push_raw (stack,
				value_new_error (NULL, _("Broken function")));
			printf ("So much for that theory.\n");
			return FALSE;
		}

		/* FIXME : Add support for workbook local functions */
		name = func_lookup_by_name (f_name, NULL);
		if (name == NULL)
			name = function_add_placeholder (f_name, "");

		expr_tree_unref (tmp);
		parse_list_push (stack, expr_tree_new_funcall (name, args));
		return TRUE;
	} else if (fn_idx >= 0 && fn_idx < FORMULA_FUNC_DATA_LEN) {
		const FormulaFuncData *fd = &formula_func_data[fn_idx];
		ExprList *args;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_formula_debug > 0) {
			printf ("Function '%s', args %d, templ: %d\n",
				fd->prefix, numargs, fd->num_args);
		}
#endif
		/* Right args for multi-arg funcs. */
		if (fd->num_args >= 0) {
			int const available_args =
			    (*stack != NULL) ? g_slist_length(*stack) : 0;
			numargs = fd->num_args;
			/* handle missing trailing arguments */
			if (numargs > available_args)
				numargs = available_args;
		} else if (fd->num_args == -2)
			g_warning("This sheet uses an Excel function "
				  "('%s') for which we do \n"
				  "not have adequate documentation.  "
				  "Please forward a copy (if possible) to\n"
				  "gnumeric-list@gnome.org.  Thanks",
				  fd->prefix);

		args = parse_list_last_n (stack, numargs);
		if (fd->prefix) {
			name = func_lookup_by_name (fd->prefix, NULL);
			if (name == NULL)
				name = function_add_placeholder (fd->prefix, "Builtin ");
		}
		/* This should not happen */
		if (!name) {
			char *txt;
			txt = g_strdup_printf ("[Function '%s']",
					       fd->prefix?fd->prefix:"?");
			printf ("Unknown %s\n", txt);
			parse_list_push_raw (stack, value_new_error (NULL, txt));
			g_free (txt);

			parse_list_free (&args);
			return FALSE;
		}
		parse_list_push (stack, expr_tree_new_funcall (name, args));
		return TRUE;
	} else
		printf ("FIXME, unimplemented fn 0x%x, with %d args\n",
			fn_idx, numargs);
	return FALSE;
}

/**
 * ms_excel_dump_cellname : internal utility to dump the current location safely.
 */
static void
ms_excel_dump_cellname (ExcelSheet const *sheet, int fn_col, int fn_row)
{
	if (sheet && sheet->gnum_sheet && sheet->gnum_sheet->name_unquoted)
		printf ("%s!", sheet->gnum_sheet->name_unquoted);
	printf ("%s%d : ", col_name(fn_col), fn_row+1);
}

/* Binary operator tokens */
static Operation const binary_ops [] = {
	OPER_ADD,	/* 0x03, ptgAdd */
	OPER_SUB,	/* 0x04, ptgSub */
	OPER_MULT,	/* 0x05, ptgMul */
	OPER_DIV,	/* 0x06, ptgDiv */
	OPER_EXP,	/* 0x07, ptgPower */
	OPER_CONCAT,	/* 0x08, ptgConcat */
	OPER_LT,	/* 0x09, ptgLT */
	OPER_LTE,	/* 0x0a, ptgLTE */
	OPER_EQUAL,	/* 0x0b, ptgEQ */
	OPER_GTE,	/* 0x0c, ptgGTE */
	OPER_GT,	/* 0x0d, ptgGT */
	OPER_NOT_EQUAL,	/* 0x0e, ptgNE */

/* FIXME: These need implementing ... */
	OPER_ADD,	/* 0x0f, ptgIsect : Intersection */
	OPER_ADD,	/* 0x10, ptgUnion : Union */
	OPER_ADD,	/* 0x11, ptgRange : Range */
};

static Operation const unary_ops [] = {
	OPER_UNARY_PLUS,/* 0x12, ptgU_PLUS  */
	OPER_UNARY_NEG,	/* 0x13, ptgU_MINUS */
	OPER_PERCENT,	/* 0x14, ptgPERCENT */
};

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicly allocated ExprTree containing the formula, or NULL
 **/
ExprTree *
ms_excel_parse_formula (ExcelSheet const *sheet, guint8 const *mem,
			int fn_col, int fn_row, gboolean shared,
			guint16 length,
			gboolean *array_element)
{
	MsBiffVersion const ver = sheet->wb->container.ver;

	/* so that the offsets and lengths match the documentation */
	guint8 const *cur = mem + 1;

	/* Array sizes and values are stored at the end of the stream */
	guint8 const *array_data = mem + length;

	int len_left = length;
	ExprList *stack = NULL;
	gboolean error = FALSE;

	if (array_element != NULL)
		*array_element = FALSE;

	g_return_val_if_fail (sheet != NULL, NULL);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 1) {
		printf ("\n\n%s:%s%s\n", (sheet->gnum_sheet
					  ? sheet->gnum_sheet->name_unquoted
					  : ""),
			cell_coord_name (fn_col,fn_row), (shared?" (shared)":""));
	}
#endif

	while (len_left > 0 && !error)
	{
		int ptg_length = 0;
		int ptg = MS_OLE_GET_GUINT8(cur-1);
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F;
		if (ptg > FORMULA_PTG_MAX)
			break;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_formula_debug > 5) {
			printf ("Ptg : 0x%02x", ptg);
			if (ptg != ptgbase)
				printf ("(0x%02x)", ptgbase);
			printf ("\n");
		}
#endif

		switch (ptgbase)
		{
		case FORMULA_PTG_EXPR: {
			ExprTree *expr;
			BiffSharedFormula *sf;
			CellPos top_left;

			top_left.row = MS_OLE_GET_GUINT16 (cur+0);
			top_left.col = MS_OLE_GET_GUINT16 (cur+2);
			sf = ms_excel_sheet_shared_formula (sheet, &top_left);

			if (sf == NULL) {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 3) {
					printf("Unknown shared formula "
					       "@ %s:%s\n",
					       (sheet->gnum_sheet
						? sheet->gnum_sheet->name_unquoted
						: ""),
					       cell_coord_name (fn_col, fn_row));
				}
#endif
				parse_list_free (&stack);
				return NULL;
			}

			if (sf->is_array) {
				if (array_element != NULL)
					*array_element = TRUE;
				else
					printf ("EXCEL : unexpected array\n");

				parse_list_free (&stack);
				return NULL;
			}

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 0) {
				printf ("Parse shared formula\n");
			}
#endif
			expr = ms_excel_parse_formula (sheet, sf->data,
						       fn_col, fn_row, TRUE,
						       sf->data_len,
						       array_element);

			parse_list_push (&stack, expr);
			ptg_length = length; /* Force it to be the only token */
			break;
		}

		case FORMULA_PTG_ADD :  case FORMULA_PTG_SUB :
		case FORMULA_PTG_MULT : case FORMULA_PTG_DIV :
		case FORMULA_PTG_EXP :
		case FORMULA_PTG_CONCAT :
		case FORMULA_PTG_LT : case FORMULA_PTG_LTE :
		case FORMULA_PTG_EQUAL :
		case FORMULA_PTG_GTE : case FORMULA_PTG_GT :
		case FORMULA_PTG_NOT_EQUAL :
		case FORMULA_PTG_INTERSECT : case FORMULA_PTG_UNION :
		case FORMULA_PTG_RANGE : {
			ExprTree *r = parse_list_pop (&stack);
			ExprTree *l = parse_list_pop (&stack);
			parse_list_push (&stack, expr_tree_new_binary (
				l,
				binary_ops [ptgbase - FORMULA_PTG_ADD],
				r));
			break;
		}

		case FORMULA_PTG_U_PLUS :
		case FORMULA_PTG_U_MINUS :
		case FORMULA_PTG_PERCENT :
			parse_list_push (&stack, expr_tree_new_unary (
				unary_ops [ptgbase - FORMULA_PTG_U_PLUS],
				parse_list_pop (&stack)));
			break;

		case FORMULA_PTG_PAREN:
/*	  printf ("Ignoring redundant parenthesis ptg\n"); */
			ptg_length = 0;
			break;

		case FORMULA_PTG_MISSARG:
			parse_list_push_raw (&stack, value_new_empty ());
			ptg_length = 0;
			break;

		case FORMULA_PTG_ATTR : { /* FIXME: not fully implemented */
			guint8  grbit = MS_OLE_GET_GUINT8(cur);
			guint16 w     = MS_OLE_GET_GUINT16(cur+1);
			ptg_length = 3;
			if (grbit == 0x00) {
				static gboolean warned_a = FALSE;
				static gboolean warned_3 = FALSE;
				if (w == 0xa) {
					if (warned_a)
						break;
					warned_a = TRUE;
				} else if (w == 3) {
					if (warned_3)
						break;
					warned_3 = TRUE;
				} /* else always warn */

				ms_excel_dump_cellname (sheet, fn_col, fn_row);
				printf ("Hmm, ptgAttr of type 0 ??\n"
					"I've seen a case where an instance of this with flag A and another with flag 3\n"
					"bracket a 1x1 array formula.  please send us this file.\n"
					"Flags = 0x%X\n", w);
			} else if (grbit & 0x01) {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 0) {
					printf ("A volatile function: so what\n");
				}
#endif
			} else if (grbit & 0x02) { /* AttrIf: 'optimised' IF function */
				/* Who cares if the TRUE expr has a goto at the end */
				ExprTree *tr;
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 2) {
					printf ("Optimised IF 0x%x 0x%x\n", grbit, w);
					ms_ole_dump (mem, length);
				}
#endif
				tr = (w) ? ms_excel_parse_formula (sheet,
						   cur+ptg_length,
						   fn_col, fn_row, shared, w,
						   NULL)
					 : expr_tree_string ("");
				parse_list_push (&stack, tr);
				ptg_length += w;
			} else if (grbit & 0x04) { /* AttrChoose 'optimised' my foot. */
				guint16 len, lp;
				guint32 offset=0;
				guint8 const *data=cur+3;
				ExprTree *tr;

#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 1) {
					printf ("'Optimised' choose\n");
					ms_ole_dump (mem,length);
				}
#endif
				for (lp=0;lp<w;lp++) { /* w = wCases */
					offset= MS_OLE_GET_GUINT16(data);
					len = MS_OLE_GET_GUINT16(data+2) - offset;
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 1) {
						printf ("Get from %d len %d [ = 0x%x ]\n",
							ptg_length+offset, len,
							*(cur+ptg_length+offset));
					}
#endif
					tr = ms_excel_parse_formula (sheet, cur+ptg_length+offset,
								     fn_col, fn_row, shared,
								     len, NULL);
					data+=2;
					parse_list_push (&stack, tr);
				}
				ptg_length+=MS_OLE_GET_GUINT16(data);
			} else if (grbit & 0x08) { /* AttrGoto */
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 2) {
					printf ("Goto %d: cur = 0x%x\n", w,
						(int)(cur-mem));
					ms_ole_dump (mem, length);
				}
#endif
				ptg_length = w;
			} else if (grbit & 0x10) { /* AttrSum: 'optimised' SUM function */
				if (!make_function (&stack, 0x04, 1))
				{
					error = TRUE;
					puts ("Error in optimised SUM");
				}
			} else if (grbit & 0x40) { /* AttrSpace */
				guint8 num_space = MS_OLE_GET_GUINT8(cur+2);
				guint8 attrs     = MS_OLE_GET_GUINT8(cur+1);
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */;
				else
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 1) {
						printf ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space);
					}
#else
				;
#endif
			} else {
				ms_excel_dump_cellname (sheet, fn_col, fn_row);
				printf ("Unknown PTG Attr gr = 0x%x, w = 0x%x ptg = 0x%x\n", grbit, w, ptg);
				error = TRUE;
			}
		}
		break;

		case FORMULA_PTG_ERR: {
			guint8 err_num;
			const char *errtxt;

			err_num = MS_OLE_GET_GUINT8 (cur);
			if (err_num == 0x17) { /* Magic 'Addin Name' error number */
				parse_list_free (&stack);
				return NULL;   /* To tell name stuff */
			}
			errtxt  = biff_get_error_text (err_num);
			parse_list_push_raw (&stack, value_new_error (NULL, errtxt));
			ptg_length = 1;
			break;
		}
		case FORMULA_PTG_INT: {
			guint16 num = MS_OLE_GET_GUINT16(cur);
			parse_list_push_raw (&stack, value_new_int (num));
			ptg_length = 2;
			break;
		}
		case FORMULA_PTG_BOOL: {
			parse_list_push_raw (&stack, value_new_bool (MS_OLE_GET_GUINT8(cur)));
			ptg_length = 1;
			break;
		}
		case FORMULA_PTG_NUM: {
			double tmp = gnumeric_get_le_double (cur);
			parse_list_push_raw (&stack, value_new_float (tmp));
			ptg_length = 8;
			break;
		}
		case FORMULA_PTG_STR: {
			char *str;
			guint32 len;
/*			ms_ole_dump (mem, length);*/
			if (ver >= MS_BIFF_V8) {
				str = biff_get_text (cur+2, MS_OLE_GET_GUINT16(cur), &len);
				ptg_length = 2 + len;
#if 0
				printf ("v8+ PTG_STR '%s'\n", str);
#endif
			} else {
				str = biff_get_text (cur+1, MS_OLE_GET_GUINT8(cur), &len);
				ptg_length = 1 + len;
#if 0
				printf ("<v7 PTG_STR '%s' len %d ptglen %d\n", str, len, ptg_length);
#endif
			}
			if (!str) str = g_strdup("");
			parse_list_push_raw (&stack, value_new_string (str));
			if (str)  g_free (str);
			break;
		}

		case FORMULA_PTG_EXTENDED : { /* Extended Ptgs for Biff8 */
			/*
			 * The beginings of 'extended' ptg support.
			 * These are mostly undocumented.
			 */
			/* Use 0 for unknown sizes, and ignore the trailing
			 * extended info completely for now.
			 */
			static int const extended_ptg_size[] =
			{
				/* 0x00 */ 0,  /* Reserved */
				/* 0x01 */ 4,  /* eptgElfLel,	No,  Err */
				/* 0x02 */ 4,  /* eptgElfRw,	No,  Ref */
				/* 0x03 */ 4,  /* eptgElfCol,	No,  Ref */
				/* 0x04 */ 0,  /* Reserved */
				/* 0x05 */ 0,  /* Reserved */
				/* 0x06 */ 4,  /* eptgElfRwV,	No,  Value */
				/* 0x07 */ 4,  /* eptgElfColV,	No,  Value */
				/* 0x08 */ 0,  /* Reserved */
				/* 0x09 */ 0,  /* Reserved */
				/* 0x0a */ 13, /* eptgRadical,	No,  Ref */
				/* 0x0b */ 13, /* eptgRadicalS,	Yes, Ref */
				/* 0x0c */ 4,  /* eptgElfRwS,	Yes, Ref */
				/* 0x0d */ 4,  /* eptgElfColS,	Yes, Ref */
				/* 0x0e */ 4,  /* eptgElfRwSV,	Yes, Value */
				/* 0x0f */ 4,  /* eptgElfColSV,	Yes, Value */
				/* 0x10 */ 4,  /* eptgElfRadicalLel, No, Err */
				/* 0x11 */ 0,  /* Reserved */
				/* 0x12 */ 0,  /* Reserved */
				/* 0x13 */ 0,  /* Reserved */
				/* 0x14 */ 0,  /* Reserved */
				/* 0x15 */ 0,  /* Reserved */
				/* 0x16 */ 0,  /* Reserved */
				/* 0x17 */ 0,  /* Reserved */
				/* 0x18 */ 0,  /* Reserved */
				/* 0x19 */ 0,  /* Invalid */
				/* 0x1a */ 0,  /* Invalid */
				/* 0x1b */ 0,  /* Reserved */
				/* 0x1c */ 0,  /* Reserved */
				/* 0x1d */ 4,  /* eptgSxName, No, Value */
				/* 0x1e */ 0   /* Reserved */
			};
			guint8 const eptg_type = MS_OLE_GET_GUINT8(cur);
			if (eptg_type >= sizeof(extended_ptg_size)/sizeof(int))
			{
				g_warning ("EXCEL : unknown ePtg type %02x",
					   eptg_type);
			} else
				ptg_length = 1 + extended_ptg_size[eptg_type];

			/* WARNING : No documentation for this.  However this seems
			 * to make sense.
			 *
			 * NOTE :
			 * I cheat here.
			 * This reference is really to the entire row/col
			 * left/below the specified cell.
			 *
			 * However we don't support that feature in gnumeric
			 * nor do we support taking the intersection of the
			 * vector and the calling cell.
			 *
			 * So
			 *
			 * Cheat.  and perform the intersection here.
			 *
			 * ie
			 * A1 : x
			 * A2 : 2  B2 : =x^2
			 *
			 * x is an eptgElfColV.  I replace that with a2
			 */
			if (eptg_type == 0x06 || /* eptgElfRwV,	 No,  Value */
			    eptg_type == 0x07) { /* eptgElfColV, No,  Value */
				CellRef *ref = getRefV8 (MS_OLE_GET_GUINT16(cur + 1),
							 MS_OLE_GET_GUINT16(cur + 3),
							 fn_col, fn_row, shared);

				if (ref) {
					if (eptg_type == 0x07) { /* Column */
						if (ref->row_relative)
							ref->row = 0;
						else
							ref->row = fn_row;
					} else { 		 /* Row */
						if (ref->col_relative)
							ref->col = 0;
						else
							ref->col = fn_col;
					}

					parse_list_push (&stack, expr_tree_new_var (ref));
					g_free (ref);
				}
			} else {
				printf ("-------------------\n");
				printf ("XL : Extended ptg %x\n", eptg_type);
				ms_ole_dump (mem+2, length-2);
				printf ("-------------------\n");
			}
		}
		break;

		case FORMULA_PTG_ARRAY : {
			/* NB. the spec. is wrong here, these are zero offset */
			guint32 const cols=MS_OLE_GET_GUINT8(array_data)+1;
			guint32 const rows=MS_OLE_GET_GUINT16(array_data+1)+1;
			guint16 lpx,lpy;
			Value *v = value_new_array (cols, rows);
			ptg_length = 7;

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 1) {
				printf ("An Array how interesting: (%d,%d)\n",
					cols, rows);
				ms_ole_dump (mem, length);
			}
#endif
			array_data += 3;
			for (lpy = 0; lpy < rows; lpy++) {
				for (lpx = 0; lpx < cols; lpx++) {
					Value *set_val = 0;
					guint8 opts = MS_OLE_GET_GUINT8(array_data);
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 0) {
						printf ("Opts 0x%x\n", opts);
					}
#endif
					if (opts == 1) {
						double const v = gnumeric_get_le_double(array_data+1);
						set_val = value_new_float (v);
						array_data+=9;
					} else if (opts == 2) {
						guint32 len;
						char *str;

						if (ver >= MS_BIFF_V8) { /* Cunningly not mentioned in spec. ! */
							str = biff_get_text (array_data+3,
									     MS_OLE_GET_GUINT16(array_data+1),
									     &len);
							array_data+=len+3;
						} else {
							str = biff_get_text (array_data+2,
									     MS_OLE_GET_GUINT8(array_data+1),
									     &len);
							array_data+=len+2;
						}
						if (str) {
							set_val = value_new_string (str);
#ifndef NO_DEBUG_EXCEL
							if (ms_excel_formula_debug > 0) {
								printf ("String '%s'\n", str);
							}
#endif
							g_free (str);
						} else
							set_val = value_new_string ("");
					} else {
						printf ("FIXME: Duff array item type %d @ %s%d\n",
							opts, col_name(fn_col), fn_row+1);
						ms_ole_dump (array_data+1, 8);
						error = TRUE;
						goto really_duff;
						break;
					}
					value_array_set (v, lpx, lpy, set_val);
				}
			}
		really_duff:
			parse_list_push_raw (&stack, v);
			break;
		}

		case FORMULA_PTG_FUNC:
			if (!make_function (&stack, MS_OLE_GET_GUINT16(cur), -1)) {
				error = TRUE;
				puts ("error making func");
			}
			ptg_length = 2;
			break;

		case FORMULA_PTG_FUNC_VAR: {
			int const numargs = (MS_OLE_GET_GUINT8( cur ) & 0x7f);
			/* index into fn table */
			int const iftab   = (MS_OLE_GET_GUINT16(cur+1) & 0x7fff);
#if 0
			/* Prompts the user ?  */
			int const prompt  = (MS_OLE_GET_GUINT8( cur ) & 0x80);
			/* is a command equiv.?*/
			int const cmdquiv = (MS_OLE_GET_GUINT16(cur+1) & 0x8000);
#endif

			if (!make_function (&stack, iftab, numargs)) {
				error = TRUE;
				puts ("error making func var");
			}
			ptg_length = 3;
			break;
		}

		case FORMULA_PTG_NAME: {
			gint32 name_idx; /* 1 based */

			if (ver >= MS_BIFF_V8) {
				name_idx = MS_OLE_GET_GUINT16 (cur) - 1;
				ptg_length = 4;  /* Docs are wrong, no ixti */
			} else {
				name_idx = MS_OLE_GET_GUINT16 (cur) - 1;
				ptg_length = 14;
			}
			if (name_idx < 0)
				printf ("FIXME: how odd; negative name calling is bad!\n");
			parse_list_push (&stack,
					 biff_name_data_get_name (sheet,
								  name_idx));
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 2)
				printf ("Name idx %d\n", name_idx);
#endif
		}
		break;

#if 0
		case FORMULA_PTG_MEM_FUNC:
			/* Can I just ignore this ? */
			ptg_length = 2 + MS_OLE_GET_GUINT16 (cur);
			break;
#endif

		case FORMULA_PTG_REF_ERR:
			ptg_length = (ver >= MS_BIFF_V8) ? 4 : 3;
			parse_list_push_raw (&stack, value_new_error (NULL, gnumeric_err_REF));
			break;

		case FORMULA_PTG_AREA_ERR:
			ptg_length = (ver >= MS_BIFF_V8) ? 8 : 6;
			parse_list_push_raw (&stack, value_new_error (NULL, gnumeric_err_REF));
			break;

		case FORMULA_PTG_REF: case FORMULA_PTG_REFN: {
			CellRef *ref=0;
			if (ver >= MS_BIFF_V8) {
				ref = getRefV8 (MS_OLE_GET_GUINT16(cur),
						MS_OLE_GET_GUINT16(cur + 2),
						fn_col, fn_row, shared);
				ptg_length = 4;
			} else {
				ref = getRefV7 (MS_OLE_GET_GUINT8(cur+2), MS_OLE_GET_GUINT16(cur),
						fn_col, fn_row, shared);
				ptg_length = 3;
			}
			parse_list_push (&stack, expr_tree_new_var (ref));
			if (ref) g_free (ref);
			break;
		}

		case FORMULA_PTG_AREA: case FORMULA_PTG_AREAN: {
			CellRef *first=0, *last=0;
			if (ver >= MS_BIFF_V8) {
				first = getRefV8 (MS_OLE_GET_GUINT16(cur+0),
						  MS_OLE_GET_GUINT16(cur+4),
						  fn_col, fn_row, shared);
				last  = getRefV8 (MS_OLE_GET_GUINT16(cur+2),
						  MS_OLE_GET_GUINT16(cur+6),
						  fn_col, fn_row, shared);
				ptg_length = 8;
			} else {
				first = getRefV7(MS_OLE_GET_GUINT8(cur+4), MS_OLE_GET_GUINT16(cur+0), fn_col, fn_row, shared);
				last  = getRefV7(MS_OLE_GET_GUINT8(cur+5), MS_OLE_GET_GUINT16(cur+2), fn_col, fn_row, shared);
				ptg_length = 6;
			}

			parse_list_push_raw (&stack, value_new_cellrange (first, last, fn_col, fn_row));

			if (first) g_free (first);
			if (last)  g_free (last);
			break;
		}

		case FORMULA_PTG_NAME_X : { /* FIXME: Not using sheet_idx at all ... */
			ExprTree *tree;
			guint16 extn_name_idx; /* 1 based */
			guint16 extn_sheet_idx;

			if (ver == MS_BIFF_V8) {
				extn_sheet_idx = MS_OLE_GET_GUINT16(cur);
				extn_name_idx  = MS_OLE_GET_GUINT16(cur+2);
/*				printf ("FIXME: v8 NameX : %d %d\n", extn_sheet_idx, extn_name_idx); */
				ptg_length = 6;
			} else {
				extn_sheet_idx = MS_OLE_GET_GUINT16(cur);
				extn_name_idx  = MS_OLE_GET_GUINT16(cur+10);
/*				printf ("FIXME: v7 NameX : %d %d\n", extn_sheet_idx, extn_name_idx); */
				ptg_length = 24;
			}
			tree = biff_name_data_get_name (sheet, extn_name_idx-1);
			parse_list_push (&stack, tree);
		}
		break;

		case FORMULA_PTG_REF_3D : { /* see S59E2B.HTM */
			CellRef *ref=0;
			if (ver >= MS_BIFF_V8) {
				guint16 extn_idx = MS_OLE_GET_GUINT16 (cur);
				ref = getRefV8 (MS_OLE_GET_GUINT16 (cur + 2),
						MS_OLE_GET_GUINT16 (cur + 4),
						fn_col, fn_row, 0);
				make_inter_sheet_ref (sheet->wb, extn_idx, ref, 0);
				parse_list_push (&stack, expr_tree_new_var (ref));
				ptg_length = 6;
			} else {
				guint16 extn_idx, first_idx, second_idx;

				ref = getRefV7 (MS_OLE_GET_GUINT8  (cur + 16),
						MS_OLE_GET_GUINT16 (cur + 14),
						fn_col, fn_row, 0);
				extn_idx   = MS_OLE_GET_GUINT16 (cur);
				first_idx  = MS_OLE_GET_GUINT16 (cur + 10);
				second_idx = MS_OLE_GET_GUINT16 (cur + 12);
				make_inter_sheet_ref_v7 (sheet->wb, extn_idx, first_idx, second_idx, ref, 0);
				parse_list_push (&stack, expr_tree_new_var (ref));
				ptg_length = 17;
			}
			if (ref) g_free (ref);
			break;
		}
		case FORMULA_PTG_AREA_3D : { /* see S59E2B.HTM */
			CellRef *first=0, *last=0;

			if (ver >= MS_BIFF_V8) {
				guint16 extn_idx = MS_OLE_GET_GUINT16(cur);

				first = getRefV8 (MS_OLE_GET_GUINT16(cur+2),
						  MS_OLE_GET_GUINT16(cur+6),
						  fn_col, fn_row, 0);
				last  = getRefV8 (MS_OLE_GET_GUINT16(cur+4),
						  MS_OLE_GET_GUINT16(cur+8),
						  fn_col, fn_row, 0);

				make_inter_sheet_ref (sheet->wb, extn_idx, first, last);
				parse_list_push_raw (&stack, value_new_cellrange (first, last, fn_col, fn_row));
				ptg_length = 10;
			} else {
				guint16 extn_idx, first_idx, second_idx;

				first = getRefV7 (MS_OLE_GET_GUINT8(cur+18), MS_OLE_GET_GUINT16(cur+14),
						  fn_col, fn_row, 0);
				last  = getRefV7 (MS_OLE_GET_GUINT8(cur+19), MS_OLE_GET_GUINT16(cur+16),
						  fn_col, fn_row, 0);
				extn_idx   = MS_OLE_GET_GUINT16(cur);
				first_idx  = MS_OLE_GET_GUINT16(cur + 10);
				second_idx = MS_OLE_GET_GUINT16(cur + 12);
				make_inter_sheet_ref_v7 (sheet->wb, extn_idx, first_idx,
							 second_idx, first, last);
				parse_list_push_raw (&stack, value_new_cellrange (first, last, fn_col, fn_row));
				ptg_length = 20;
			}
			if (first) g_free (first);
			if (last)  g_free (last);
		}

		case FORMULA_PTG_REF_ERR_3D :
			ptg_length = (ver >= MS_BIFF_V8) ? 6 : 17;
			parse_list_push_raw (&stack, value_new_error (NULL, gnumeric_err_REF));
			break;

		case FORMULA_PTG_AREA_ERR_3D :
			ptg_length = (ver >= MS_BIFF_V8) ? 10 : 20;
			parse_list_push_raw (&stack, value_new_error (NULL, gnumeric_err_REF));
			break;

		default:
			g_warning ("EXCEL : Unhandled PTG 0x%x\n", ptg);
			error = TRUE;
		}
/*		printf ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length); */
		cur      += ptg_length + 1;
		len_left -= ptg_length + 1;
	}

	if (error) {
		g_warning ("Unknown Formula/Array at %s!%s%s\n",
			(sheet->gnum_sheet)?
			sheet->gnum_sheet->name_unquoted : "",
			cell_coord_name (fn_col,fn_row),
			(shared?" (shared)":""));
		printf ("formula data : \n");
		ms_ole_dump (mem, length);

		parse_list_free (&stack);
		return expr_tree_string (_(" Unknown formula"));
	}

	if (!stack)
		return expr_tree_string ("Stack too short - unusual");
	if (expr_list_length (stack) > 1) {
		parse_list_free (&stack);
		return expr_tree_string ("Too much data on stack - probable cause: "
					 "fixed args function is var-arg, put '-1' in the table above");
	}
	return parse_list_pop (&stack);
}
