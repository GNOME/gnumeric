/*
 * ms-formula.c: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */

#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "gnumeric.h"

#include "utils.h"

#include "excel.h"
#include "ms-biff.h"
#include "ms-formula-read.h"

#define FORMULA_DEBUG 0

#define NO_PRECEDENCE 256

typedef struct _FORMULA_FUNC_DATA
{
	char *prefix ;
	int num_args ; /* -1 for multi-arg */
		       /* -2 for unknown args */
} FORMULA_FUNC_DATA ;

/**
 * Various bits of data for operators
 * see S59E2B.HTM for formula_ptg values
 * formula PTG, prefix, middle, suffix, precedence
 **/

typedef struct _FORMULA_OP_DATA
{
	Operation op;
} FORMULA_OP_DATA ;

  /* Binary operator tokens */
FORMULA_OP_DATA formula_op_data[] = {
	{ OPER_ADD  }, /* ptgAdd : Addition */
	{ OPER_SUB  }, /* ptgSub : Subtraction */
	{ OPER_MULT }, /* ptgMul : Multiplication */
	{ OPER_DIV }, /* ptgDiv : Division */
	{ OPER_EXP }, /* ptgPower : Exponentiation */
	{ OPER_CONCAT }, /* ptgConcat : Concatenation */
	{ OPER_LT }, /* ptgLT : Less Than */
	{ OPER_LTE }, /* ptgLTE : Less Than or Equal */
	{ OPER_EQUAL }, /* ptgEQ : Equal */
	{ OPER_GTE }, /* ptgGTE : Greater Than or Equal */
	{ OPER_GT }, /* ptgGT : Greater Than */
	{ OPER_NOT_EQUAL }, /* ptgNE : Not Equal */
/* FIXME: These need implementing ... */
	{ OPER_ADD }, /* ptgIsect : Intersection */
	{ OPER_ADD }, /* ptgUnion : Union */
	{ OPER_ADD }, /* ptgRange : Range */
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))
#define FORMULA_OP_START      0x03

/**
 * Populate from xlcall.h
 * Functions in order, zero based, with number of arguments or
 *		'-1' for vararg or with optional arguments.
 *		'-2' for unknown numbers or arguments.
 * NB. all argument counts are those for Excel.
 * Many of the unknowns are xlm macro commands.
 *     macrofun.hlp has info on them but supporting Excel4 macro sheets is not
 *     top priority.
 **/
FORMULA_FUNC_DATA formula_func_data[] =
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
/* 29 */	{ "INDEX", },
/* 30 */	{ "REPT", -1 },
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
/* 64 */	{ "MATCH", -1 },/* match_type is optional */
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
/* 92 */	{ "SERIESNUM", 4 }, /* Renamed from SERIES */
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
/* 105 */	{ "ISREF", -2 },
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
/* 222 Unknown*/{ "UnknownFunction222", -2 },
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
/* 261 */	{ "ERRORTYPE", 1 },
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
/* 347 */	{ "COUNTBLANK", -1 },
/* 348 */	{ "SCENARIOGET", -2 },
/* 349 */	{ "OPTIONSLISTSGET", -2 },
/* 350 */	{ "ISPMT", -2 },
/* 351 */	{ "DATEDIF", -2 },
/* 352 */	{ "DATESTRING", -2 },
/* 353 */	{ "NUMBERSTRING", -2 },
/* 354 */	{ "ROMAN", -1 },
/* 355 */	{ "OPENDIALOG", -2 },
/* 356 */	{ "SAVEDIALOG", -2 },
/* 357 */	{ "VIEWGET", -2 },
/* 358 */	{ "GETPIVOTDATA", },
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

#define FORMULA_FUNC_DATA_LEN (sizeof(formula_func_data)/sizeof(FORMULA_FUNC_DATA))

/**
 * Helper functions.
 **/

static ExprTree *
expr_tree_string (const char *str)
{
	return expr_tree_new_constant (value_new_string (str));
}

/**
 * End of helper functions
 **/






/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV7(MS_EXCEL_SHEET *sheet, BYTE col, WORD gbitrw, int curcol, int currow, int shrfmla)
{
	gint8 row_offset= 0, col_offset= 0;
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->col          = col ;
	cr->row          = (gbitrw & 0x3fff) ;
	cr->row_relative = (gbitrw & 0x8000)==0x8000 ;
	cr->col_relative = (gbitrw & 0x4000)==0x4000 ;
	cr->sheet = sheet->gnum_sheet ;
	if (FORMULA_DEBUG>2)
		printf ("7In : 0x%x, 0x%x  at %d, %d shared %d\n", col, gbitrw,
			curcol, currow, shrfmla) ; 
	if (shrfmla && cr->row_relative) {
		gint8 t = (cr->row&0x00ff);
		cr->row = currow+t ;
	}
	if (shrfmla && cr->col_relative) {
		gint8 t = (cr->col&0x00ff);
		cr->col = curcol+t ;
	}
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	return cr ;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *
getRefV8(MS_EXCEL_SHEET *sheet, WORD row, WORD gbitcl, int curcol, int currow, int shrfmla)
{
	CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
	cr->sheet = sheet->gnum_sheet ;
	if (FORMULA_DEBUG>2)
		printf ("8In : 0x%x, 0x%x  at %d, %d shared %d\n", row, gbitcl,
			curcol, currow, shrfmla) ;
	cr->row          = row ;
	cr->col          = (gbitcl & 0x3fff) ;
	cr->row_relative = (gbitcl & 0x8000)==0x8000 ;
	cr->col_relative = (gbitcl & 0x4000)==0x4000 ;
	if (shrfmla && cr->row_relative) {  /* Should be correct now -- NJL */
		gint8 t = (cr->row&0x00ff);
		cr->row = currow+t ;
	}
	if (shrfmla && cr->col_relative) {  /* Should be correct now -- NJL */
		gint8 t = (cr->col&0x00ff);
		cr->col = curcol+t ;
	}
	if (cr->row_relative)
		cr->row-= currow ;
	if (cr->col_relative)
		cr->col-= curcol ;
	if (FORMULA_DEBUG>2)
		printf ("Returns : %d,%d Rel:(%d %d)\n", cr->col, cr->row,
			cr->col_relative, cr->row_relative);
	return cr ;
}

typedef ExprTree PARSE_DATA;
typedef GList    PARSE_LIST;

static void
parse_list_push (PARSE_LIST **list, ExprTree *pd)
{
	if (FORMULA_DEBUG > 0 && !pd)
		printf ("FIXME: Pushing nothing onto stack\n");
	*list = g_list_append (*list, pd) ;
}
static void
parse_list_push_raw (PARSE_LIST **list, Value *v)
{
	parse_list_push (list, expr_tree_new_constant (v));
}

static ExprTree *
parse_list_pop (PARSE_LIST **list)
{
	GList *tmp ;
	PARSE_DATA *ans ;
	tmp = g_list_last (*list) ;
	if (tmp == 0)
		return expr_tree_string ("WrongArgs");
	*list = g_list_remove_link (*list, tmp) ;
	ans  = tmp->data ;
	g_list_free (tmp) ;
	return ans ;
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GList *
parse_list_last_n (PARSE_LIST **list, gint n)
{
	GList *l=0;
	while (n-->0)
		l=g_list_prepend (l, parse_list_pop(list));
	return l;
}


static void 
parse_list_free (PARSE_LIST **list)
{
	while (*list)
		expr_tree_unref (parse_list_pop(list));
}

static void
make_inter_sheet_ref (MS_EXCEL_WORKBOOK *wb, guint16 extn_idx, CellRef *a, CellRef *b)
{
	g_return_if_fail (wb);
	g_return_if_fail (a);

	a->sheet = biff_get_externsheet_name (wb, extn_idx, 1) ;
	if (b)
		b->sheet = biff_get_externsheet_name (wb, extn_idx, 0) ;
}

static void
make_inter_sheet_ref_v7 (MS_EXCEL_WORKBOOK *wb, guint16 extn_idx,
			 guint16 first, guint16 second, CellRef *a, CellRef *b)
{
	MS_EXCEL_SHEET *sheet;

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
make_function (PARSE_LIST **stack, int fn_idx, int numargs)
{
	int lp ;
	Symbol *name=NULL;

	if (fn_idx == 0xff && numargs>1) /* Dodgy Special Case */
	{
		ExprTree *tmp;
		GList *args = parse_list_last_n (stack, numargs-1);
		tmp = parse_list_pop (stack) ;
		if (!tmp || tmp->oper != OPER_CONSTANT ||
		    tmp->u.constant->type != VALUE_STRING) {
			if (tmp) expr_tree_unref (tmp);
			parse_list_free (&args);
			parse_list_push (stack, expr_tree_new_error (_("Broken function")));
			printf ("Killroy was here.  Did not know what he was doing.\n");
			return 0;
		}
		else {
			name = symbol_lookup (global_symbol_table, tmp->u.constant->v.str->str);
			if (!name) {
				char *errtxt = g_strdup_printf ("Duff fn '%s'", 
								tmp->u.constant->v.str->str);
				printf ("Fn : '%s'\n", tmp->u.constant->v.str->str);
				parse_list_free (&args);
				parse_list_push (stack, expr_tree_new_error (errtxt));
				g_free (errtxt);
				if (tmp) expr_tree_unref (tmp);
				return 0;
			}
			symbol_ref (name);
			parse_list_push (stack, expr_tree_new_funcall (name, args));
			return 1 ;
		}
	}
	else
		if (fn_idx >= 0 && fn_idx < FORMULA_FUNC_DATA_LEN)
		{
			const FORMULA_FUNC_DATA *fd = &formula_func_data[fn_idx] ;
			GList *args;

#if FORMULA_DEBUG > 0
			printf ("Function '%s', args %d, templ: %d\n", fd->prefix,
				numargs, fd->num_args);
#endif
			/* Right args for multi-arg funcs. */
			if (fd->num_args >= 0)
				numargs = fd->num_args ;
			else if (fd->num_args == -2)
				g_warning("This sheet uses an Excel function "
					  "('%s') for which we don not have "
					  "adequate documentation.\n"
					  "Please forward a copy (if possible) to "
					  "gnumeric-list@gnome.org.  Thanks\n",
					  fd->prefix);

			args = parse_list_last_n (stack, numargs);
			if (fd->prefix)
				name = symbol_lookup (global_symbol_table, fd->prefix);
			if (!name) {
				char *txt;
				txt = g_strdup_printf ("[Function '%s']", 
						       fd->prefix?fd->prefix:"?");
				printf ("Unknown %s\n", txt);
				parse_list_push (stack, expr_tree_new_error (txt));
				g_free (txt);

				parse_list_free (&args);
				return 0;
			}
			symbol_ref (name);
			parse_list_push (stack, expr_tree_new_funcall (name, args));
			return 1 ;
		}
		else
			printf ("FIXME, unimplemented fn 0x%x, with %d args\n", fn_idx, numargs) ;
	return 0 ;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicaly allocated string containing the formula, never NULL
 **/
ExprTree *
ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, guint8 *mem,
			int fn_col, int fn_row, int shared,
			guint16 length)
{
	Cell *cell ;
	int len_left = length ;
	guint8 *cur = mem + 1 ; /* this is so that the offsets and lengths
				   are identical to those in the documentation */
	guint8 *array_data = mem + 3 + length; /* Sad but true */
	PARSE_LIST *stack = NULL;
	int error = 0 ;
	char *ans ;
	
	while (len_left>0 && !error)
	{
		int ptg_length = 0 ;
		int ptg = BIFF_GETBYTE(cur-1) ;
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F ;
		if (ptg > FORMULA_PTG_MAX)
			break ;
		if (FORMULA_DEBUG>0)
			printf ("Ptg : 0x%x -> 0x%x\n", ptg, ptgbase) ;
		switch (ptgbase)
		{
		case FORMULA_PTG_REFN:
		case FORMULA_PTG_REF:
		{
			CellRef *ref=0;
			if (sheet->ver == eBiffV8)
			{
				ref = getRefV8 (sheet, BIFF_GETWORD(cur), BIFF_GETWORD(cur + 2),
						fn_col, fn_row, shared) ;
				ptg_length = 4 ;
			}
			else
			{
				ref = getRefV7 (sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur),
						fn_col, fn_row, shared) ;
				ptg_length = 3 ;
			}
			parse_list_push (&stack, expr_tree_new_var (ref));
			if (ref) g_free (ref) ;
			break ;
		}
		case FORMULA_PTG_NAME_X: /* FIXME: Not using sheet_idx at all ... */
		{
			char *txt ;
			guint16 extn_sheet_idx, extn_name_idx ;
			
			if (sheet->ver == eBiffV8)
			{
				extn_sheet_idx = BIFF_GETWORD(cur) ;
				extn_name_idx  = BIFF_GETWORD(cur+2) ;
/*				printf ("FIXME: v8 NameX : %d %d\n", extn_sheet_idx, extn_name_idx) ; */
				ptg_length = 6 ;
			}
			else
			{
				extn_sheet_idx = BIFF_GETWORD(cur) ;
				extn_name_idx  = BIFF_GETWORD(cur+10) ;
/*				printf ("FIXME: v7 NameX : %d %d\n", extn_sheet_idx, extn_name_idx) ; */
				ptg_length = 24 ;
			}
			if ((txt = biff_name_data_get_name (sheet, extn_name_idx)))
				parse_list_push_raw (&stack, value_new_string (txt));
			else
				parse_list_push_raw (&stack, value_new_string ("DuffName"));
			break ;
		}
		case FORMULA_PTG_REF_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0;
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				ref = getRefV8 (sheet, BIFF_GETWORD(cur+2), BIFF_GETWORD(cur + 4),
						fn_col, fn_row, 0) ;
				make_inter_sheet_ref (sheet->wb, extn_idx, ref, 0) ;
				parse_list_push (&stack, expr_tree_new_var (ref));
				ptg_length = 6 ;
			} else {
				guint16 extn_idx, first_idx, second_idx;

				ref = getRefV7 (sheet, BIFF_GETBYTE(cur+16), BIFF_GETWORD(cur+14),
						fn_col, fn_row, 0) ;
				extn_idx   = BIFF_GETWORD(cur);
				first_idx  = BIFF_GETWORD(cur + 10);
				second_idx = BIFF_GETWORD(cur + 12);
				make_inter_sheet_ref_v7 (sheet->wb, extn_idx, first_idx, second_idx, ref, 0) ;
				parse_list_push (&stack, expr_tree_new_var (ref));
				ptg_length = 17 ;
			}
			if (ref) g_free (ref) ;
			break ;
		}
		case FORMULA_PTG_AREA_3D: /* see S59E2B.HTM */
		{
			CellRef *first=0, *last=0 ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;

				first = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6),
						 fn_col, fn_row, 0) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+8),
						 fn_col, fn_row, 0) ;

				make_inter_sheet_ref (sheet->wb, extn_idx, first, last) ;
				parse_list_push_raw (&stack, value_new_cellrange (first, last));
				ptg_length = 10 ;
			} else {
				guint16 extn_idx, first_idx, second_idx;

				first = getRefV7 (sheet, BIFF_GETBYTE(cur+18), BIFF_GETWORD(cur+14),
						  fn_col, fn_row, 0) ;
				last  = getRefV7 (sheet, BIFF_GETBYTE(cur+19), BIFF_GETWORD(cur+16),
						  fn_col, fn_row, 0) ;
				extn_idx   = BIFF_GETWORD(cur);
				first_idx  = BIFF_GETWORD(cur + 10);
				second_idx = BIFF_GETWORD(cur + 12);
				make_inter_sheet_ref_v7 (sheet->wb, extn_idx, first_idx,
							 second_idx, first, last) ;
				parse_list_push_raw (&stack, value_new_cellrange (first, last));
				ptg_length = 20 ;
			}
			if (first) g_free (first) ;
			if (last)  g_free (last) ;
		}
		break ;
		case FORMULA_PTG_AREAN:
		case FORMULA_PTG_AREA:
		{
			CellRef *first=0, *last=0 ;
			if (sheet->ver == eBiffV8)
			{
				first = getRefV8(sheet, BIFF_GETBYTE(cur+0), BIFF_GETWORD(cur+4),
						 fn_col, fn_row, shared) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6),
						 fn_col, fn_row, shared) ;
				ptg_length = 8 ;
			}
			else
			{
				first = getRefV7(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+0), fn_col, fn_row, shared) ;
				last  = getRefV7(sheet, BIFF_GETBYTE(cur+5), BIFF_GETWORD(cur+2), fn_col, fn_row, shared) ;
				ptg_length = 6 ;
			}

			parse_list_push_raw (&stack, value_new_cellrange (first, last));

			if (first) g_free (first) ;
			if (last)  g_free (last) ;
			break ;
		}
		case FORMULA_PTG_ARRAY:
		{
			Value *v;
			guint32 cols=BIFF_GETBYTE(cur+0)+1; /* NB. the spec. is wrong here, these */
			guint32 rows=BIFF_GETWORD(cur+1)+1; /*     are zero offset numbers */ 
			guint16 lpx,lpy;
			
			v = value_array_new (cols, rows);
			ptg_length = 7;
#if FORMULA_DEBUG > 1
			printf ("An Array how interesting: (%d,%d)\n", cols, rows);
			dump (mem, length);
#endif
			for (lpy=0;lpy<rows;lpy++) {
				for (lpx=0;lpx<cols;lpx++) {
					Value *set_val=0;
					guint8 opts=BIFF_GETBYTE(array_data);
#if FORMULA_DEBUG > 0
					printf ("Opts 0x%x\n", opts);
#endif
					if (opts == 1) {
						set_val = value_new_float (BIFF_GETDOUBLE(array_data+1));
						array_data+=9;
					} else if (opts == 2) {
						guint32 len;
						char *str;

						if (sheet->ver >= eBiffV8) { /* Cunningly not mentioned in spec. ! */
							str = biff_get_text (array_data+3,
									     BIFF_GETWORD(array_data+1),
									     &len);
							array_data+=len+3;
						} else {
							str = biff_get_text (array_data+2,
									     BIFF_GETBYTE(array_data+1),
									     &len);
							array_data+=len+2;
						}
						if (str) {
							set_val = value_new_string (str);
#if FORMULA_DEBUG > 0
							printf ("String '%s'\n", str);
#endif
							g_free (str);
						} else
							set_val = value_new_string ("");
					} else {
						printf ("FIXME: Duff array item type\n");
						break;
					}
					value_array_set (v, lpx, lpy, set_val);
				}
			}
			parse_list_push_raw (&stack, v);
			break;
		}
		case FORMULA_PTG_FUNC:
		{
			if (!make_function (&stack, BIFF_GETWORD(cur), -1)) error = 1 ;
			ptg_length = 2 ;
			break ;
		}
		case FORMULA_PTG_FUNC_VAR:
		{
			int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
			int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
			int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
			int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/

			if (!make_function (&stack, iftab, numargs)) error = 1 ;
			ptg_length = 3 ;
			break ;
		}
/*FIXME: REIMPLEMENT		case FORMULA_PTG_NAME:
		{
		guint16 name_idx ; *//* 1 based */
/*			char *txt;
			if (sheet->ver == eBiffV8)
				name_idx = BIFF_GETWORD(cur+2) ;
			else
				name_idx = BIFF_GETWORD(cur) ;
			txt = biff_name_data_get_name (sheet, name_idx);
			if (!txt) {
				if (FORMULA_DEBUG>0) {
					printf ("FIXME: Ptg Name not found: %d\n", name_idx) ;
					dump(mem, length) ;
				}
				parse_list_push_raw (&stack, g_strdup("Unknown name"), NO_PRECEDENCE) ;
			} else
				parse_list_push_raw (&stack, g_strdup(txt), NO_PRECEDENCE) ;
		}*/
		case FORMULA_PTG_EXP:
		{
			int top_left_col = BIFF_GETWORD(cur+2) ;
			int top_left_row = BIFF_GETWORD(cur+0) ;
			ExprTree *tr;
			tr = ms_excel_sheet_shared_formula (sheet, top_left_col,
							    top_left_row,
							    fn_col, fn_row) ;
			if (!tr) tr = expr_tree_string ("");
			parse_list_push (&stack, tr);
			ptg_length = length; /* Force it to be the only token 4 ; */
			break ;
		}
		case FORMULA_PTG_U_PLUS: /* Discard */
			break;
		case FORMULA_PTG_U_MINUS:
			parse_list_push (&stack,
					 expr_tree_new_unary (OPER_NEG,
							      parse_list_pop (&stack)));
			break;
		case FORMULA_PTG_PAREN:
/*	  printf ("Ignoring redundant parenthesis ptg\n") ; */
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_MISSARG: /* FIXME: Need Null Arg. type. */
			parse_list_push_raw (&stack, value_new_string (""));
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_ATTR: /* FIXME: not fully implemented */
		{
			guint8  grbit = BIFF_GETBYTE(cur) ;
			guint16 w     = BIFF_GETWORD(cur+1) ;
			ptg_length = 3 ;
			if (grbit & 0x01) {
				if (FORMULA_DEBUG>0)
					printf ("A volatile function: so what\n") ;
			} else if (grbit & 0x02) { /* AttrIf: 'optimised' IF function */
				/* Who cares if the TRUE expr has a goto at the end */
				ExprTree *tr;
				if (FORMULA_DEBUG>2) {
					printf ("Optimised IF 0x%x 0x%x\n", grbit, w) ;
					dump (mem, length) ;
				}
				if (w)
					tr = ms_excel_parse_formula (sheet, cur+ptg_length,
								      fn_col, fn_row, shared,
								      w) ;
				else
					tr = expr_tree_string ("");
				parse_list_push (&stack, tr);
				ptg_length += w ;
			} else if (grbit & 0x04) { /* AttrChoose 'optimised' my foot. */
				guint16 len, lp;
				guint32 offset=0;
				guint8 *data=cur+3;
				ExprTree *tr;

				if (FORMULA_DEBUG>1) {
					printf ("'Optimised' choose\n");
					dump (mem,length);
				}
				for (lp=0;lp<w;lp++) { /* w = wCases */
					offset= BIFF_GETWORD(data);
					len = BIFF_GETWORD(data+2) - offset;
					if (FORMULA_DEBUG>1)
						printf ("Get from %d len %d [ = 0x%x ]\n",
							ptg_length+offset, len, *(cur+ptg_length+offset));
					tr = ms_excel_parse_formula (sheet, cur+ptg_length+offset,
								     fn_col, fn_row, shared,
								     len);
					data+=2;
					parse_list_push (&stack, tr);
				}
				ptg_length+=BIFF_GETWORD(data);
			} else if (grbit & 0x08) { /* AttrGoto */
				if (FORMULA_DEBUG>2) {
					printf ("Goto %d: cur = 0x%x\n", w, (int)(cur-mem)) ;
					dump (mem, length) ;
				}
				ptg_length = w ;
			} else if (grbit & 0x10) { /* AttrSum: 'optimised' SUM function */
				if (!make_function (&stack, 0x04, 1))
				{
					error = 1 ;
					printf ("Error in optimised SUM\n") ;
				}
			} else if (grbit & 0x40) { /* AttrSpace */
				guint8 num_space = BIFF_GETBYTE(cur+2) ;
				guint8 attrs     = BIFF_GETBYTE(cur+1) ;
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */ ;
				else
#if FORMULA_DEBUG > 1
					printf ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space) ;
#else
				;
#endif
			} else {
#if FORMULA_DEBUG > 0
				printf ("Unknown PTG Attr 0x%x 0x%x\n", grbit, w) ;
#endif
				error = 1 ;
			}
		break ;
		}
		case FORMULA_PTG_ERR:
		{
			parse_list_push_raw (&stack,
					     value_new_string (biff_get_error_text(BIFF_GETBYTE(cur))));
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_INT:
		{
			guint16 num = BIFF_GETWORD(cur) ;
			parse_list_push_raw (&stack, value_new_int (num));
			ptg_length = 2 ;
			break;
		}
		case FORMULA_PTG_BOOL:
		{
			parse_list_push_raw (&stack, value_new_bool (BIFF_GETBYTE(cur)));
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_NUM:
		{
			double tmp = BIFF_GETDOUBLE(cur) ;
			parse_list_push_raw (&stack, value_new_float (tmp));
			ptg_length = 8 ;
			break ;
		}
		case FORMULA_PTG_STR:
		{
			char *str;
			guint32 len;
/*			dump (mem, length) ;*/
			if (sheet->ver == eBiffV8)
			{
				str = biff_get_text (cur+2, BIFF_GETWORD(cur), &len) ;
				ptg_length = 2 + len ;
/*				printf ("v8+ PTG_STR '%s'\n", str) ; */
			}
			else
			{
				str = biff_get_text (cur+1, BIFF_GETBYTE(cur), &len) ;
				ptg_length = 1 + len ;
/*				printf ("<v7 PTG_STR '%s' len %d ptglen %d\n", str, len, ptg_length) ; */
			}
			if (!str) str = g_strdup("");
			parse_list_push_raw (&stack, value_new_string (str));
			if (str)  g_free (str);
			break ;
		}
		default:
		{
/*	    printf ("Search %d records\n", (int)FORMULA_OP_DATA_LEN) ; */
			if (ptgbase >= FORMULA_OP_START && ptgbase < FORMULA_OP_START+FORMULA_OP_DATA_LEN) {
				FORMULA_OP_DATA *fd =
					&formula_op_data[ptgbase - FORMULA_OP_START];
				ExprTree *l, *r;
				r = parse_list_pop (&stack);
				l = parse_list_pop (&stack);
				parse_list_push (&stack, expr_tree_new_binary (l, fd->op, r));
			} else {
#if FORMULA_DEBUG > 0
				printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase);
#endif
				error=1 ;
			}
		}
		break ;
		}
/*		printf ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length) ; */
		cur+=    (ptg_length+1) ;
		len_left-= (ptg_length+1) ;
	}
	if (error) {
		if (FORMULA_DEBUG>0) {
			printf ("Unknown Formula/Array at [%d, %d]\n", fn_col, fn_row) ;
			printf ("formula data : \n") ;
			dump (mem, length) ;
		}
		
		parse_list_free (&stack) ;
		return expr_tree_string (_(" Unknown formula")) ;
	}
	
	if (!stack)
		return expr_tree_string ("Stack too short - unusual");
	if (g_list_length(stack) > 1) {
		parse_list_free (&stack);
		return expr_tree_string ("Too much data on stack - probable cause: "
					 "fixed args function is var-arg, put '-1' in the table above");
	}
	return parse_list_pop (&stack);
}
