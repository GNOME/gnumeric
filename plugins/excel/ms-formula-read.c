/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * ms-formula-read.c: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2003 Michael Meeks, Jody Goldberg
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "ms-formula-read.h"
#include "excel.h"
#include "ms-biff.h"
#include "formula-types.h"
#include "boot.h"
#include <gutils.h>
#include <func.h>
#include <value.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <str.h>
#include <parse-util.h>
#include <sheet.h>
#include <workbook.h>
#include <gsf/gsf-utils.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:read_expr"
/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_formula_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

ExcelFuncDesc const excel_func_desc [] = {
/* 0 */	  { "COUNT",		XL_VARARG, 1, 'V', "R" },
/* 1 */	  { "IF",		XL_VARARG, 2, 'V', "VR" },
/* 2 */	  { "ISNA",		XL_FIXED,  1, 'V', "V" },
/* 3 */	  { "ISERROR",		XL_FIXED,  1, 'V', "V" },
/* 4 */	  { "SUM",		XL_VARARG, 1, 'V', "R" },
/* 5 */	  { "AVERAGE",		XL_VARARG, 1, 'V', "R" },
/* 6 */	  { "MIN",		XL_VARARG, 1, 'V', "R" },
/* 7 */	  { "MAX",		XL_VARARG, 1, 'V', "R" },
/* 8 */	  { "ROW",		XL_VARARG, 1, 'V', "R" },
/* 9 */	  { "COLUMN",		XL_VARARG, 1, 'V', "R" },
/* 10 */  { "NA",		XL_FIXED,  0, 'V', 0 },
/* 11 */  { "NPV",		XL_VARARG, 2, 'V', "VR" },
/* 12 */  { "STDEV",		XL_VARARG, 1, 'V', "R" },
/* 13 */  { "DOLLAR",		XL_VARARG, 1, 'V', "V" },
/* 14 */  { "FIXED",		XL_VARARG, 1, 'V', "V" },
/* 15 */  { "SIN",		XL_FIXED,  1, 'V', "V" },
/* 16 */  { "COS",		XL_FIXED,  1, 'V', "V" },
/* 17 */  { "TAN",		XL_FIXED,  1, 'V', "V" },
/* 18 */  { "ATAN",		XL_FIXED,  1, 'V', "V" },
/* 19 */  { "PI",		XL_FIXED,  0, 'V', 0 },
/* 20 */  { "SQRT",		XL_FIXED,  1, 'V', "V" },
/* 21 */  { "EXP",		XL_FIXED,  1, 'V', "V" },
/* 22 */  { "LN",		XL_FIXED,  1, 'V', "V" },
/* 23 */  { "LOG10",		XL_FIXED,  1, 'V', "V" },
/* 24 */  { "ABS",		XL_FIXED,  1, 'V', "V" },
/* 25 */  { "INT",		XL_FIXED,  1, 'V', "V" },
/* 26 */  { "SIGN",		XL_FIXED,  1, 'V', "V" },
/* 27 */  { "ROUND",		XL_FIXED,  2, 'V', "VV" },
/* 28 */  { "LOOKUP",		XL_VARARG, 2, 'V', "VR" },
/* 29 */  { "INDEX", XL_VOLATILE |XL_VARARG, 4, 'R', "RVVV" },	/* array form has only 3 */
/* 30 */  { "REPT",		XL_FIXED,  2, 'V', "VV" },
/* 31 */  { "MID",		XL_FIXED,  3, 'V', "VVV" },
/* 32 */  { "LEN",		XL_FIXED,  1, 'V', "V" },
/* 33 */  { "VALUE",		XL_FIXED,  1, 'V', "V" },
/* 34 */  { "TRUE",		XL_FIXED,  0, 'V', 0 },
/* 35 */  { "FALSE",		XL_FIXED,  0, 'V', 0 },
/* 36 */  { "AND",		XL_VARARG, 1, 'V', "R" },
/* 37 */  { "OR",		XL_VARARG, 1, 'V', "R" },
/* 38 */  { "NOT",		XL_FIXED,  1, 'V', "V" },
/* 39 */  { "MOD",		XL_FIXED,  2, 'V', "VV" },
/* 40 */  { "DCOUNT",		XL_FIXED,  3, 'V', "RRR" },
/* 41 */  { "DSUM",		XL_FIXED,  3, 'V', "RRR" },
/* 42 */  { "DAVERAGE",		XL_FIXED,  3, 'V', "RRR" },
/* 43 */  { "DMIN",		XL_FIXED,  3, 'V', "RRR" },
/* 44 */  { "DMAX",		XL_FIXED,  3, 'V', "RRR" },
/* 45 */  { "DSTDEV",		XL_FIXED,  3, 'V', "RRR" },
/* 46 */  { "VAR",		XL_VARARG, 1, 'V', "R" },
/* 47 */  { "DVAR",		XL_FIXED,  3, 'V', "RRR" },
/* 48 */  { "TEXT",		XL_FIXED,  2, 'V', "VV" },
/* 49 */  { "LINEST",		XL_VARARG, 1, 'V', "R" },
/* 50 */  { "TREND",		XL_VARARG, 1, 'V', "R" },
/* 51 */  { "LOGEST",		XL_VARARG, 1, 'V', "R" },
/* 52 */  { "GROWTH",		XL_VARARG, 1, 'V', "R" },
/* 53 */  { "GOTO",		XL_XLM },
/* 55 */  { "EXCELFUNC55",	XL_UNKNOWN },
/* 54 */  { "HALT",		XL_XLM },
/* 56 */  { "PV",		XL_VARARG, 1, 'V', "V" },	/* type is optional */
/* 57 */  { "FV",		XL_VARARG, 1, 'V', "V" },	/* type is optional */
/* 58 */  { "NPER",		XL_VARARG, 1, 'V', "V" },	/* type is optional */
/* 59 */  { "PMT",		XL_VARARG, 1, 'V', "V" },	/* type is optional */
/* 60 */  { "RATE",		XL_VARARG, 1, 'V', "V" },	/* guess is optional */
/* 61 */  { "MIRR",		XL_FIXED,  3, 'V', "RVV" },
/* 62 */  { "IRR",		XL_VARARG, 2, 'V', "RV" },	/* guess is optional */
/* 63 */  { "RAND", XL_VOLATILE | XL_FIXED,  0, 'V',  0 },
/* 64 */  { "MATCH",		XL_VARARG, 2, 'V', "VR" },	/* match_type is optional */
/* 65 */  { "DATE",		XL_FIXED,  3, 'V', "VVV" },
/* 66 */  { "TIME",		XL_FIXED,  3, 'V', "VVV" },
/* 67 */  { "DAY",		XL_FIXED,  1, 'V', "V" },
/* 68 */  { "MONTH",		XL_FIXED,  1, 'V', "V" },
/* 69 */  { "YEAR",		XL_FIXED,  1, 'V', "V" },
/* 70 */  { "WEEKDAY",		XL_VARARG, 1, 'V', "V" },	/* Return type is optional */
/* 71 */  { "HOUR",		XL_FIXED,  1, 'V', "V" },
/* 72 */  { "MINUTE",		XL_FIXED,  1, 'V', "V" },
/* 73 */  { "SECOND",		XL_FIXED,  1, 'V', "V" },
/* 74 */  { "NOW",  XL_VOLATILE | XL_FIXED,  0, 'V',  0 },
/* 75 */  { "AREAS",		XL_FIXED,  1, 'V', "R" },
/* 76 */  { "ROWS",		XL_FIXED,  1, 'V', "R" },
/* 77 */  { "COLUMNS",		XL_FIXED,  1, 'V', "R" },
/* 78 */  { "OFFSET",		XL_VARARG, 2, 'R', "RV" },
/* 79 */  { "ABSREF",		XL_XLM,	   2 },
/* 80 */  { "RELREF",		XL_XLM },
/* 81 */  { "ARGUMENT",		XL_XLM },
/* 82 */  { "SEARCH",		XL_VARARG, 1, 'V', "V" },	/* Start_num is optional */
/* 83 */  { "TRANSPOSE",	XL_FIXED,  1, 'V', "A" },
/* 84 */  { "ERROR",		XL_XLM },
/* 85 */  { "STEP",		XL_XLM },
/* 86 */  { "TYPE",		XL_FIXED,  1, 'V', "V" },
/* 87 */  { "ECHO",		XL_XLM },
/* 88 */  { "SETNAME",		XL_XLM },
/* 89 */  { "CALLER",		XL_XLM },
/* 90 */  { "DEREF",		XL_XLM },
/* 91 */  { "WINDOWS",		XL_XLM },
/* 92 */  { "SERIESSUM",	XL_FIXED,  4, 'V', "VVVA" },	/* Renamed from SERIES */
/* 93 */  { "DOCUMENTS",	XL_XLM },
/* 94 */  { "ACTIVECELL",	XL_XLM },
/* 95 */  { "SELECTION",	XL_XLM },
/* 96 */  { "RESULT",		XL_XLM },
/* 97 */  { "ATAN2",		XL_FIXED,  2, 'V', "VV" },
/* 98 */  { "ASIN",		XL_FIXED,  1, 'V', "V" },
/* 99 */  { "ACOS",		XL_FIXED,  1, 'V', "V" },
/* 100 */ { "CHOOSE",		XL_VARARG, 2, 'V', "VR" },
/* 101 */ { "HLOOKUP",		XL_VARARG, 4, 'V', "VRRV" },	/* range_lookup is optional */
/* 102 */ { "VLOOKUP",		XL_VARARG, 4, 'V', "VRRV" },	/* range_lookup is optional */
/* 103 */ { "LINKS",		XL_XLM },
/* 104 */ { "INPUT",		XL_XLM },
/* 105 */ { "ISREF",		XL_FIXED,  1, 'V', "R" },	/* This a guess */
/* 106 */ { "GETFORMULA",	XL_XLM },
/* 107 */ { "GETNAME",		XL_XLM },
/* 108 */ { "SETVALUE",		XL_XLM },
/* 109 */ { "LOG",		XL_VARARG, 1, 'V', "V" },	/* Base is optional */
/* 110 */ { "EXEC",		XL_XLM },
/* 111 */ { "CHAR",		XL_FIXED,  1, 'V', "V" },
/* 112 */ { "LOWER",		XL_FIXED,  1, 'V', "V" },
/* 113 */ { "UPPER",		XL_FIXED,  1, 'V', "V" },
/* 114 */ { "PROPER",		XL_FIXED,  1, 'V', "V" },
/* 115 */ { "LEFT",		XL_VARARG, 1, 'V', "V" },    /* Num_chars is optional */
/* 116 */ { "RIGHT",		XL_VARARG, 1, 'V', "V" },    /* Num_chars is optional */
/* 117 */ { "EXACT",		XL_FIXED,  2, 'V', "VV" },
/* 118 */ { "TRIM",		XL_FIXED,  1, 'V', "V" },
/* 119 */ { "REPLACE",		XL_FIXED,  4, 'V', "VVVV" },
/* 120 */ { "SUBSTITUTE",	XL_VARARG, 1, 'V', "V" },    /* Instance num is optional */
/* 121 */ { "CODE",		XL_FIXED,  1, 'V', "V" },
/* 122 */ { "NAMES",		XL_XLM },
/* 123 */ { "DIRECTORY",	XL_XLM },
/* 124 */ { "FIND",		XL_VARARG, 1, 'V', "V" },/* start_num is optional */
/* 125 */ { "CELL",		XL_VARARG, 2, 'V', "VR" },
/* 126 */ { "ISERR",		XL_FIXED,  1, 'V', "V" },
/* 127 */ { "ISTEXT",		XL_FIXED,  1, 'V', "V" },
/* 128 */ { "ISNUMBER",		XL_FIXED,  1, 'V', "V" },
/* 129 */ { "ISBLANK",		XL_FIXED,  1, 'V', "V" },
/* 130 */ { "T",		XL_FIXED,  1, 'V', "R" },
/* 131 */ { "N",		XL_FIXED,  1, 'V', "R" },
/* 132 */ { "FOPEN",		XL_XLM },
/* 133 */ { "FCLOSE",		XL_XLM },
/* 134 */ { "FSIZE",		XL_XLM },
/* 135 */ { "FREADLN",		XL_XLM },
/* 136 */ { "FREAD",		XL_XLM },
/* 137 */ { "FWRITELN",		XL_XLM },
/* 138 */ { "FWRITE",		XL_XLM },
/* 139 */ { "FPOS",		XL_XLM },
/* 140 */ { "DATEVALUE",	XL_FIXED,  1, 'V', "V" },
/* 141 */ { "TIMEVALUE",	XL_FIXED,  1, 'V', "V" },
/* 142 */ { "SLN",		XL_FIXED,  3, 'V', "VVV" },
/* 143 */ { "SYD",		XL_FIXED,  4, 'V', "VVVV" },
/* 144 */ { "DDB",		XL_VARARG, 1, 'V', "V" },	/* Factor is optional */
/* 145 */ { "GETDEF",		XL_XLM },
/* 146 */ { "REFTEXT",		XL_XLM },
/* 147 */ { "TEXTREF",		XL_XLM },
/* 148 */ { "INDIRECT", XL_VOLATILE |XL_VARARG, 1, 'R', "V" },	/* ai is optional */
/* 149 */ { "REGISTER",		XL_XLM },
/* 150 */ { "CALL",		XL_XLM },
/* 151 */ { "ADDBAR",		XL_XLM },
/* 152 */ { "ADDMENU",		XL_XLM },
/* 153 */ { "ADDCOMMAND",	XL_XLM },
/* 154 */ { "ENABLECOMMAND",	XL_XLM },
/* 155 */ { "CHECKCOMMAND",	XL_XLM },
/* 156 */ { "RENAMECOMMAND",	XL_XLM },
/* 157 */ { "SHOWBAR",		XL_XLM },
/* 158 */ { "DELETEMENU",	XL_XLM },
/* 159 */ { "DELETECOMMAND",	XL_XLM },
/* 160 */ { "GETCHARTITEM",	XL_XLM },
/* 161 */ { "DIALOGBOX",	XL_XLM },
/* 162 */ { "CLEAN",		XL_FIXED,  1, 'V', "V" },
/* 163 */ { "MDETERM",		XL_FIXED,  1, 'V', "A" },
/* 164 */ { "MINVERSE",		XL_FIXED,  1, 'V', "A" },
/* 165 */ { "MMULT",		XL_FIXED,  2, 'V', "AA" },
/* 166 */ { "FILES",		XL_XLM },
/* 167 */ { "IPMT",		XL_VARARG, 1, 'V', "V" },	/* Type is optional */
/* 168 */ { "PPMT",		XL_VARARG, 1, 'V', "V" },	/* Type is optional */
/* 169 */ { "COUNTA",		XL_VARARG, 1, 'V', "R" },	/* Type is optional */
/* 170 */ { "CANCELKEY", 	XL_XLM },
/* 171 */ { "EXCELFUNC171",	XL_UNKNOWN },
/* 172 */ { "AppMin",		XL_XLM },
/* 173 */ { "AppMax",		XL_XLM },
/* 174 */ { "BringToFront",	XL_XLM },
/* 175 */ { "INITIATE",		XL_XLM },
/* 176 */ { "REQUEST",		XL_XLM },
/* 177 */ { "POKE",		XL_XLM },
/* 178 */ { "EXECUTE",		XL_XLM },
/* 179 */ { "TERMINATE",	XL_XLM },
/* 180 */ { "RESTART",		XL_XLM },
/* 181 */ { "HELP",		XL_XLM },
/* 182 */ { "GETBAR",		XL_XLM },
/* 183 */ { "PRODUCT",		XL_VARARG, 1, 'V', "R" },
/* 184 */ { "FACT",		XL_FIXED,  1, 'V', "V" },
/* 185 */ { "GETCELL",		XL_XLM },
/* 186 */ { "GETWORKSPACE",	XL_XLM },
/* 187 */ { "GETWINDOW",	XL_XLM },
/* 188 */ { "GETDOCUMENT",	XL_XLM },
/* 189 */ { "DPRODUCT",		XL_FIXED,  3, 'V', "RRR" },
/* 190 */ { "ISNONTEXT",	XL_FIXED,  1, 'V', "V" },
/* 191 */ { "GETNOTE",		XL_XLM },
/* 192 */ { "NOTE",		XL_XLM },
/* 193 */ { "STDEVP",		XL_VARARG, 1, 'V', "R" },
/* 194 */ { "VARP",		XL_VARARG, 1, 'V', "R" },
/* 195 */ { "DSTDEVP",		XL_FIXED,  3, 'V', "RRR" },
/* 196 */ { "DVARP",		XL_FIXED,  3, 'V', "RRR" },
/* 197 */ { "TRUNC",		XL_VARARG, 1, 'V', "V" },      /* num_digits is optional */
/* 198 */ { "ISLOGICAL",	XL_FIXED,  1, 'V', "V" },
/* 199 */ { "DCOUNTA",		XL_FIXED,  3, 'V', "RRR" },
/* 200 */ { "DELETEBAR",	XL_XLM },
/* 201 */ { "UNREGISTER",	XL_XLM },
/* 202 */ { "EXCELFUNC202",	XL_UNKNOWN },
/* 203 */ { "EXCELFUNC203",	XL_UNKNOWN },
/* 204 */ { "USDOLLAR",		XL_XLM },
/* 205 */ { "FINDB",		XL_XLM },
/* 206 */ { "SEARCHB",		XL_XLM },
/* 207 */ { "REPLACEB",		XL_XLM },
/* 208 */ { "LEFTB",		XL_XLM },
/* 209 */ { "RIGHTB",		XL_XLM },
/* 210 */ { "MIDB",		XL_XLM },
/* 211 */ { "LENB",		XL_XLM },
/* 212 */ { "ROUNDUP",		XL_FIXED,  2, 'V', "VV" },
/* 213 */ { "ROUNDDOWN",	XL_FIXED,  2, 'V', "VV" },
/* 214 */ { "ASC",		XL_XLM },
/* 215 */ { "DBCS",		XL_XLM },
/* 216 */ { "RANK",		XL_VARARG, 3, 'V', "VRV" },	/* order is optional */
/* 217 */ { "EXCELFUNC217",	XL_UNKNOWN },
/* 218 */ { "EXCELFUNC218",	XL_UNKNOWN },
/* 219 */ { "ADDRESS",		XL_VARARG, 5, 'V', "VVVVV" },	/* abs_num, a1, sheet_text are optional */
/* 220 */ { "DAYS360",		XL_VARARG, 1, 'V', "V" },	/* method is optional */
/* 221 */ { "TODAY", XL_VOLATILE |XL_FIXED,  0, 'V', 0 },
/* 222 */ { "VDB",		XL_VARARG, 1, 'V', "V" },
/* 223 */ { "EditColor",	XL_XLM },
/* 224 */ { "ShowLevels",	XL_XLM },
/* 225 */ { "FormatMain",	XL_XLM },
/* 226 */ { "EXCELFUNC226",	XL_UNKNOWN },
/* 227 */ { "MEDIAN",		XL_VARARG, 1, 'V', "R" },
/* 228 */ { "SUMPRODUCT",	XL_VARARG, 1, 'V', "A" },
/* 229 */ { "SINH",		XL_FIXED,  1, 'V', "V" },
/* 230 */ { "COSH",		XL_FIXED,  1, 'V', "V" },
/* 231 */ { "TANH",		XL_FIXED,  1, 'V', "V" },
/* 232 */ { "ASINH",		XL_FIXED,  1, 'V', "V" },
/* 233 */ { "ACOSH",		XL_FIXED,  1, 'V', "V" },
/* 234 */ { "ATANH",		XL_FIXED,  1, 'V', "V" },
/* 235 */ { "DGET",		XL_FIXED,  3, 'V', "RRR" },
/* 236 */ { "CREATEOBJECT",	XL_XLM },
/* 237 */ { "VOLATILE", 	XL_XLM },
/* 238 */ { "LASTERROR", 	XL_XLM },
/* 239 */ { "CUSTOMUNDO",	XL_XLM },
/* 240 */ { "CUSTOMREPEAT",	XL_XLM },
/* 241 */ { "FORMULACONVERT",	XL_XLM },
/* 242 */ { "GETLINKINFO",	XL_XLM },
/* 243 */ { "TEXTBOX", 		XL_XLM },
/* 244 */ { "INFO",		XL_FIXED, 1 },
/* 245 */ { "GROUP",		XL_XLM },
/* 246 */ { "GETOBJECT",	XL_XLM },
/* 247 */ { "DB",		XL_VARARG, 1, 'V',"V" },	/* month is optional */
/* 248 */ { "PAUSE",		XL_XLM },
/* 249 */ { "EXCELFUNC249",	XL_UNKNOWN },
/* 250 */ { "EXCELFUNC250",	XL_UNKNOWN },
/* 251 */ { "RESUME",		XL_XLM },
/* 252 */ { "FREQUENCY",	XL_FIXED,  2, 'V',"RR" },
/* 253 */ { "ADDTOOLBAR",	XL_XLM },
/* 254 */ { "DELETETOOLBAR",	XL_XLM },
/* 255 */ { "extension slot",	XL_MAGIC },
/* 256 */ { "RESETTOOLBAR",	XL_XLM },
/* 257 */ { "EVALUATE", 	XL_XLM },
/* 258 */ { "GETTOOLBAR",	XL_XLM },
/* 259 */ { "GETTOOL",		XL_XLM },
/* 260 */ { "SPELLINGCHECK",	XL_XLM },
/* 261 */ { "ERROR.TYPE",	XL_FIXED,  1, 'V', "V" },
/* 262 */ { "APPTITLE",		XL_XLM },
/* 263 */ { "WINDOWTITLE",	XL_XLM },
/* 264 */ { "SAVETOOLBAR",	XL_XLM },
/* 265 */ { "ENABLETOOL",	XL_XLM },
/* 266 */ { "PRESSTOOL",	XL_XLM },
/* 267 */ { "REGISTERID", 	XL_XLM },
/* 268 */ { "GETWORKBOOK",	XL_XLM },
/* 269 */ { "AVEDEV",		XL_VARARG, 1, 'V', "R" },
/* 270 */ { "BETADIST",		XL_VARARG, 1, 'V', "V" },
/* 271 */ { "GAMMALN",		XL_FIXED,  1, 'V', "V" },
/* 272 */ { "BETAINV",		XL_VARARG, 1, 'V', "V" },
/* 273 */ { "BINOMDIST",	XL_FIXED,  4, 'V', "VVVV" },
/* 274 */ { "CHIDIST",		XL_FIXED,  2, 'V', "VV" },
/* 275 */ { "CHIINV",		XL_FIXED,  2, 'V', "VV" },
/* 276 */ { "COMBIN",		XL_FIXED,  2, 'V', "VV" },
/* 277 */ { "CONFIDENCE",	XL_FIXED,  3, 'V', "VVV" },
/* 278 */ { "CRITBINOM",	XL_FIXED,  3, 'V', "VVV" },
/* 279 */ { "EVEN",		XL_FIXED,  1, 'V', "V" },
/* 280 */ { "EXPONDIST",	XL_FIXED,  3, 'V', "VVV" },
/* 281 */ { "FDIST",		XL_FIXED,  3, 'V', "VVV" },
/* 282 */ { "FINV",		XL_FIXED,  3, 'V', "VVV" },
/* 283 */ { "FISHER",		XL_FIXED,  1, 'V', "V" },
/* 284 */ { "FISHERINV",	XL_FIXED,  1, 'V', "V" },
/* 285 */ { "FLOOR",		XL_FIXED,  2, 'V', "VV" },
/* 286 */ { "GAMMADIST",	XL_FIXED,  4, 'V', "VVVV" },
/* 287 */ { "GAMMAINV",		XL_FIXED,  3, 'V', "VVV" },
/* 288 */ { "CEILING",		XL_FIXED,  2, 'V', "VV" },
/* 289 */ { "HYPGEOMDIST",	XL_FIXED,  4, 'V', "VVVV" },
/* 290 */ { "LOGNORMDIST",	XL_FIXED,  3, 'V', "VVV" },
/* 291 */ { "LOGINV",		XL_FIXED,  3, 'V', "VVV" },
/* 292 */ { "NEGBINOMDIST",	XL_FIXED,  3, 'V', "VVV" },
/* 293 */ { "NORMDIST",		XL_FIXED,  4, 'V', "VVVV" },
/* 294 */ { "NORMSDIST",	XL_FIXED,  1, 'V', "V" },
/* 295 */ { "NORMINV",		XL_FIXED,  3, 'V', "VVV" },
/* 296 */ { "NORMSINV",		XL_FIXED,  1, 'V', "V" },
/* 297 */ { "STANDARDIZE",	XL_FIXED,  3, 'V', "VVV" },
/* 298 */ { "ODD",		XL_FIXED,  1, 'V', "V" },
/* 299 */ { "PERMUT",		XL_FIXED,  2, 'V', "VV" },
/* 300 */ { "POISSON",		XL_FIXED,  3, 'V', "VVV" },
/* 301 */ { "TDIST",		XL_FIXED,  3, 'V', "VVV" },
/* 302 */ { "WEIBULL",		XL_FIXED,  4, 'V', "VVVV" },
/* 303 */ { "SUMXMY2",		XL_FIXED,  2, 'V', "AA" },
/* 304 */ { "SUMX2MY2",		XL_FIXED,  2, 'V', "AA" },
/* 305 */ { "SUMX2PY2",		XL_FIXED,  2, 'V', "AA" },
/* 306 */ { "CHITEST",		XL_FIXED,  2, 'V', "AA" },
/* 307 */ { "CORREL",		XL_FIXED,  2, 'V', "AA" },
/* 308 */ { "COVAR",		XL_FIXED,  2, 'V', "AA" },
/* 309 */ { "FORECAST",		XL_FIXED,  3, 'V', "VAA" },
/* 310 */ { "FTEST",		XL_FIXED,  2, 'V', "AA" },
/* 311 */ { "INTERCEPT",	XL_FIXED,  2, 'V', "AA" },
/* 312 */ { "PEARSON",		XL_FIXED,  2, 'V', "AA" },
/* 313 */ { "RSQ",		XL_FIXED,  2, 'V', "AA" },
/* 314 */ { "STEYX",		XL_FIXED,  2, 'V', "AA" },
/* 315 */ { "SLOPE",		XL_FIXED,  2, 'V', "AA" },
/* 316 */ { "TTEST",		XL_FIXED,  4, 'V', "AAVV" },
/* 317 */ { "PROB",		XL_VARARG, 3, 'V', "AAV" },	/* upper_limit is optional */
/* 318 */ { "DEVSQ",		XL_VARARG, 1, 'V', "R" },
/* 319 */ { "GEOMEAN",		XL_VARARG, 1, 'V', "R" },
/* 320 */ { "HARMEAN",		XL_VARARG, 1, 'V', "R" },
/* 321 */ { "SUMSQ",		XL_VARARG, 1, 'V', "R" },
/* 322 */ { "KURT",		XL_VARARG, 1, 'V', "R" },
/* 323 */ { "SKEW",		XL_VARARG, 1, 'V', "R" },
/* 324 */ { "ZTEST",		XL_VARARG, 2, 'V', "RV" },	/* sigma is optional */
/* 325 */ { "LARGE",		XL_FIXED,  2, 'V', "RV" },
/* 326 */ { "SMALL",		XL_FIXED,  2, 'V', "RV" },
/* 327 */ { "QUARTILE",		XL_FIXED,  2, 'V', "RV" },
/* 328 */ { "PERCENTILE",	XL_FIXED,  2, 'V', "RV" },
/* 329 */ { "PERCENTRANK",	XL_VARARG, 2, 'V', "RV" },	/* Significance is optional */
/* 330 */ { "MODE",		XL_VARARG, 1, 'V', "A" },
/* 331 */ { "TRIMMEAN",		XL_FIXED,  2, 'V', "RV" },
/* 332 */ { "TINV",		XL_FIXED,  2, 'V', "VV" },
/* 333 */ { "EXCELFUNC333",	XL_UNKNOWN },
/* 334 */ { "MOVIECOMMAND",	XL_XLM },
/* 335 */ { "GETMOVIE",		XL_XLM },
/* 336 */ { "CONCATENATE",	XL_VARARG, 1, 'V', "V" },
/* 337 */ { "POWER",		XL_FIXED,  2, 'V', "VV" },
/* 338 */ { "PIVOTADDDATA",	XL_XLM },
/* 339 */ { "GETPIVOTTABLE",	XL_XLM },
/* 340 */ { "GETPIVOTFIELD",	XL_XLM },
/* 341 */ { "GETPIVOTITEM",	XL_XLM },
/* 342 */ { "RADIANS",		XL_FIXED,  1, 'V', "V" },
/* 343 */ { "DEGREES",		XL_FIXED,  1, 'V', "V" },
/* 344 */ { "SUBTOTAL",		XL_VARARG, 2, 'V', "VR" },
/* 345 */ { "SUMIF",		XL_VARARG, 3, 'V', "RVR" },	/* Actual range is optional */
/* 346 */ { "COUNTIF",		XL_FIXED,  2, 'V', "RV" },
/* 347 */ { "COUNTBLANK",	XL_FIXED,  1, 'V', "R" },
/* 348 */ { "SCENARIOGET",	XL_XLM },
/* 349 */ { "OPTIONSLISTSGET",	XL_XLM },
/* 350 */ { "ISPMT",		XL_FIXED,  4, 'V', "VVVV" },
/* 351 */ { "DATEDIF",		XL_FIXED,  3, 'V', "VVV" },
/* 352 */ { "DATESTRING",	XL_XLM },
/* 353 */ { "NUMBERSTRING",	XL_XLM },
/* 354 */ { "ROMAN",		XL_VARARG, 2, 'V', "VV" },
/* 355 */ { "OPENDIALOG",	XL_XLM },
/* 356 */ { "SAVEDIALOG",	XL_XLM },
/* 357 */ { "VIEWGET",		XL_XLM },
/* 358 */ { "GETPIVOTDATA",	XL_FIXED,  2, 'V', "RV" },
/* 359 */ { "HYPERLINK",	XL_VARARG, 1, 'V', "V" },	/* cell_contents is optional */
/* 360 */ { "PHONETIC",		XL_FIXED,  1, 'V', "V" },
/* 361 */ { "AVERAGEA",		XL_VARARG, 1, 'V', "R" },
/* 362 */ { "MAXA",		XL_VARARG, 1, 'V', "R" },
/* 363 */ { "MINA",		XL_VARARG, 1, 'V', "R" },
/* 364 */ { "STDEVPA",		XL_VARARG, 1, 'V', "R" },
/* 365 */ { "VARPA",		XL_VARARG, 1, 'V', "R" },
/* 366 */ { "STDEVA",		XL_VARARG, 1, 'V', "R" },
/* 367 */ { "VARA",		XL_VARARG, 1, 'V', "R" },
};

int excel_func_desc_size = G_N_ELEMENTS (excel_func_desc);

static GnmExpr const *
expr_tree_string (char const *str)
{
	return gnm_expr_new_constant (value_new_string (str));
}
static GnmExpr const *
expr_tree_error (ExcelReadSheet const *esheet, int col, int row,
		 char const *msg, char const *str)
{
	if (esheet != NULL && esheet->sheet != NULL) {
		g_warning ("%s!%s : %s",
			   esheet->sheet->name_unquoted,
			   cell_coord_name (col, row), msg);
	} else if (col >= 0 && row >= 0) {
		g_warning ("%s : %s", cell_coord_name (col, row), msg);
	} else {
		g_warning ("%s", msg);
	}

	return gnm_expr_new_constant (value_new_error (NULL, str));
}

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static void
getRefV7 (CellRef *cr,
	  guint8 col, guint16 gbitrw, int curcol, int currow,
	  gboolean const shared)
{
	guint16 const row = (guint16)(gbitrw & 0x3fff);

	d (2, fprintf (stderr, "7In : 0x%x, 0x%x  at %s%s\n", col, gbitrw,
		      cell_coord_name (curcol, currow), (shared?" (shared)":"")););

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
}

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static void
getRefV8 (CellRef *cr,
	  guint16 row, guint16 gbitcl, int curcol, int currow,
	  gboolean const shared)
{
	guint8 const col = (guint8)(gbitcl & 0xff);

	d (2, fprintf (stderr, "8In : 0x%x, 0x%x  at %s%s\n", row, gbitcl,
		      cell_coord_name (curcol, currow), (shared?" (shared)":"")););

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
}

static void
parse_list_push (GnmExprList **list, GnmExpr const *pd)
{
	d (5, fprintf (stderr, "Push 0x%p\n", pd););
	if (pd == NULL) {
		g_warning ("FIXME: Pushing nothing onto excel function stack");
		pd = expr_tree_error (NULL, -1, -1,
			"Incorrect number of parsed formula arguments",
			"#WrongArgs");
	}
	*list = gnm_expr_list_prepend (*list, pd);
}

static void
parse_list_push_raw (GnmExprList **list, Value *v)
{
	parse_list_push (list, gnm_expr_new_constant (v));
}

static GnmExpr const *
parse_list_pop (GnmExprList **list)
{
	/* Get the head */
	GnmExprList *tmp = *list;
	if (tmp != NULL) {
		GnmExpr const *ans = tmp->data;
		*list = g_slist_remove (*list, ans);
		d (5, fprintf (stderr, "Pop 0x%x\n", (int)ans););
		return ans;
	}

	return expr_tree_error (NULL, -1, -1,
		"Incorrect number of parsed formula arguments",
		"#WrongArgs");
}

/**
 * Returns a new list composed of the last n items pop'd off the list.
 **/
static GnmExprList *
parse_list_last_n (GnmExprList **list, gint n)
{
	GnmExprList *l = NULL;
	while (n-->0)
		l = gnm_expr_list_prepend (l, parse_list_pop(list));
	return l;
}


static void
parse_list_free (GnmExprList **list)
{
	while (*list)
		gnm_expr_unref (parse_list_pop(list));
}

static gboolean
make_function (GnmExprList **stack, int fn_idx, int numargs)
{
	GnmFunc *name = NULL;

	if (fn_idx == 0xff) {
		/*
		 * This is undocumented.
		 * function 0xff seems to indicate an external function whose
		 * name should be on top of the stack.
		 */
		/* FIXME FIXME FIXME : How to handle missing trailing args ?? */
		GnmExprList *args = parse_list_last_n (stack, numargs-1);
		GnmExpr const *tmp = parse_list_pop (stack);
		char const *f_name = NULL;

		if (tmp != NULL) {
			if (tmp->any.oper == GNM_EXPR_OP_CONSTANT &&
			    tmp->constant.value->type == VALUE_STRING)
				f_name = tmp->constant.value->v_str.val->str;
			else if (tmp->any.oper == GNM_EXPR_OP_NAME)
				f_name = tmp->name.name->name->str;
		}

		if (f_name == NULL) {
			if (tmp)
				gnm_expr_unref (tmp);
			parse_list_free (&args);
			parse_list_push_raw (stack,
				value_new_error (NULL, _("Broken function")));
			g_warning ("So much for that theory.");
			return FALSE;
		}

		/* FIXME : Add support for workbook local functions */
		name = gnm_func_lookup (f_name, NULL);
		if (name == NULL)
			name = gnm_func_add_placeholder (f_name, "", TRUE);

		gnm_expr_unref (tmp);
		parse_list_push (stack, gnm_expr_new_funcall (name, args));
		return TRUE;
	} else if (fn_idx >= 0 && fn_idx < excel_func_desc_size) {
		ExcelFuncDesc const *fd = excel_func_desc + fn_idx;
		GnmExprList *args;

		d (2, fprintf (stderr, "Function '%s', %d, templ: %d %x\n",
			       fd->name, numargs, fd->num_known_args, fd->flags););

		if ((fd->flags & XL_VARARG) && numargs < 0)
			g_warning ("We think '%s' is vararg, and XL doesn't", fd->name);
		if ((fd->flags & XL_FIXED) && numargs >= 0)
			g_warning ("We think '%s' is fixed, and XL doesn't", fd->name);

		/* Right args for multi-arg funcs. */
		if (fd->flags & XL_FIXED) {
			int const available_args =
			    (*stack != NULL) ? g_slist_length(*stack) : 0;
			numargs = fd->num_known_args;
			/* handle missing trailing arguments */
			if (numargs > available_args)
				numargs = available_args;
		} else if (fd->flags & XL_UNKNOWN)
			g_warning("This sheet uses an Excel function "
				  "('%s') for which we do \n"
				  "not have adequate documentation.  "
				  "Please forward a copy (if possible) to\n"
				  "gnumeric-list@gnome.org.  Thanks",
				  fd->name);

		args = parse_list_last_n (stack, numargs);
		if (fd->name) {
			name = gnm_func_lookup (fd->name, NULL);
			if (name == NULL)
				name = gnm_func_add_placeholder (fd->name, "Builtin ", TRUE);
		}
		/* This should not happen */
		if (!name) {
			char *txt;
			txt = g_strdup_printf ("[Function '%s']",
					       fd->name ? fd->name : "?");
			g_warning ("Unknown %s", txt);
			parse_list_push_raw (stack, value_new_error (NULL, txt));
			g_free (txt);

			parse_list_free (&args);
			return FALSE;
		}
		parse_list_push (stack, gnm_expr_new_funcall (name, args));
		return TRUE;
	} else
		g_warning ("FIXME, unimplemented fn 0x%x, with %d args",
			fn_idx, numargs);
	return FALSE;
}

/**
 * ms_excel_dump_cellname : internal utility to dump the current location safely.
 */
static void
ms_excel_dump_cellname (ExcelWorkbook const *ewb, ExcelReadSheet const *esheet,
			int fn_col, int fn_row)
{
	if (esheet && esheet->sheet && esheet->sheet->name_unquoted)
		fprintf (stderr, "%s!", esheet->sheet->name_unquoted);
	else if (ewb && ewb->gnum_wb && workbook_get_filename (ewb->gnum_wb)) {
		fprintf (stderr, "[%s]", workbook_get_filename (ewb->gnum_wb));
		return;
	}
	fprintf (stderr, "%s%d : ", col_name(fn_col), fn_row+1);
}

/* Binary operator tokens */
static GnmExprOp const binary_ops [] = {
	GNM_EXPR_OP_ADD,	/* 0x03, ptgAdd */
	GNM_EXPR_OP_SUB,	/* 0x04, ptgSub */
	GNM_EXPR_OP_MULT,	/* 0x05, ptgMul */
	GNM_EXPR_OP_DIV,	/* 0x06, ptgDiv */
	GNM_EXPR_OP_EXP,	/* 0x07, ptgPower */
	GNM_EXPR_OP_CAT,	/* 0x08, ptgConcat */
	GNM_EXPR_OP_LT,		/* 0x09, ptgLT */
	GNM_EXPR_OP_LTE,	/* 0x0a, ptgLTE */
	GNM_EXPR_OP_EQUAL,	/* 0x0b, ptgEQ */
	GNM_EXPR_OP_GTE,	/* 0x0c, ptgGTE */
	GNM_EXPR_OP_GT,		/* 0x0d, ptgGT */
	GNM_EXPR_OP_NOT_EQUAL,	/* 0x0e, ptgNE */

	GNM_EXPR_OP_INTERSECT,	/* 0x0f, ptgIsect : Intersection */
	0, /* handled elsewhere	   0x10, ptgUnion : Union */
	GNM_EXPR_OP_RANGE_CTOR	/* 0x11, ptgRange : Range */
};

static GnmExprOp const unary_ops [] = {
	GNM_EXPR_OP_UNARY_PLUS,/* 0x12, ptgU_PLUS  */
	GNM_EXPR_OP_UNARY_NEG,	/* 0x13, ptgU_MINUS */
	GNM_EXPR_OP_PERCENTAGE,	/* 0x14, ptgPERCENT */
};

static gboolean
excel_formula_parses_ref_sheets (MSContainer const *container, guint8 const *data,
				 Sheet **first, Sheet **last)
{
	if (container->ver >= MS_BIFF_V8) {
		ExcelExternSheetV8 const *es =
			excel_externsheet_v8 (container->ewb, GSF_LE_GET_GUINT16 (data));

		if (es != NULL) {
			if (es->first == (Sheet *)2 || es->last == (Sheet *)2) /* deleted sheets */
				return TRUE;
			*first = es->first;
			*last = es->last;
		}
	} else {
		gint16 ixals = GSF_LE_GET_GINT16 (data);
		gint16 a = GSF_LE_GET_GINT16 (data + 10);
		gint16 b = GSF_LE_GET_GINT16 (data + 12);

		if (a < 0 || b < 0) /* deleted sheets */
			return TRUE;

		d (1, fprintf (stderr, " : %hx : %hx : %hx\n", ixals, a, b););

		if (ixals < 0) {
			*first = workbook_sheet_by_index (container->ewb->gnum_wb, a);
			*last = workbook_sheet_by_index (container->ewb->gnum_wb, b);
		} else {
			*first = excel_externsheet_v7 (container, ixals);
			*last  = excel_externsheet_v7 (container, b);
		}
	}

	if (*first == (Sheet *)1) {
		*first = *last = NULL;
		g_warning ("So much for that theory.  Please send us a copy of this workbook");
	} else if (*last == (Sheet *)1) {
		*last = *first;
		g_warning ("so much for that theory.  Please send us a copy of this workbook");
	} else if (*first != NULL && *last == NULL)
		*last = *first;

	return FALSE;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Return a dynamicly allocated GnmExpr containing the formula, or NULL
 **/
GnmExpr const *
excel_parse_formula (MSContainer const *container,
		     ExcelReadSheet const *esheet,
		     int fn_col, int fn_row,
		     guint8 const *mem, guint16 length,
		     gboolean shared,
		     gboolean *array_element)
{
	MsBiffVersion const ver = container->ver;

	/* so that the offsets and lengths match the documentation */
	guint8 const *cur = mem + 1;

	/* Array sizes and values are stored at the end of the stream */
	guint8 const *array_data = mem + length;

	int len_left = length;
	GnmExprList *stack = NULL;
	gboolean error = FALSE;

	if (array_element != NULL)
		*array_element = FALSE;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 1) {
		ms_excel_dump_cellname (container->ewb, esheet, fn_col, fn_row);
		fprintf (stderr, "\n");
		if (ms_excel_formula_debug > 2) {
			fprintf (stderr, "--> len = %d\n", length);
			gsf_mem_dump (mem, length);
		}
	}
#endif

	while (len_left > 0 && !error) {
		int ptg_length = 0;
		int ptg = GSF_LE_GET_GUINT8 (cur-1);
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F;
		if (ptg > FORMULA_PTG_MAX)
			break;
		d (2, {
			fprintf (stderr, "Ptg : 0x%02x", ptg);
			if (ptg != ptgbase)
				fprintf (stderr, "(0x%02x)", ptgbase);
			fprintf (stderr, "\n");
		});

		switch (ptgbase) {
		case FORMULA_PTG_EXPR: {
			GnmExpr const *expr;
			XLSharedFormula *sf;
			CellPos top_left;

			top_left.row = GSF_LE_GET_GUINT16 (cur+0);
			top_left.col = GSF_LE_GET_GUINT16 (cur+2);
			sf = excel_sheet_shared_formula (esheet, &top_left);

			if (sf == NULL) {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 3) {
					fprintf (stderr, "Unknown shared formula @");
					ms_excel_dump_cellname (container->ewb, esheet, fn_col, fn_row);
				}
#endif
				parse_list_free (&stack);
				return NULL;
			}

			if (sf->is_array) {
				if (array_element != NULL)
					*array_element = TRUE;
				else
					fprintf (stderr, "EXCEL : unexpected array\n");

				parse_list_free (&stack);
				return NULL;
			}

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 0) {
				fprintf (stderr, "Parse shared formula\n");
			}
#endif
			expr = excel_parse_formula (container, esheet, fn_col, fn_row,
				sf->data, sf->data_len, TRUE, array_element);

			parse_list_push (&stack, expr);
			ptg_length = length; /* Force it to be the only token */
			break;
		}

		case FORMULA_PTG_TBL :
			ptg_length = 4;
			break;

		case FORMULA_PTG_ADD :  case FORMULA_PTG_SUB :
		case FORMULA_PTG_MULT : case FORMULA_PTG_DIV :
		case FORMULA_PTG_EXP :
		case FORMULA_PTG_CONCAT :
		case FORMULA_PTG_LT : case FORMULA_PTG_LTE :
		case FORMULA_PTG_EQUAL :
		case FORMULA_PTG_GTE : case FORMULA_PTG_GT :
		case FORMULA_PTG_NOT_EQUAL :
		case FORMULA_PTG_INTERSECT :
		case FORMULA_PTG_RANGE : {
			GnmExpr const *r = parse_list_pop (&stack);
			GnmExpr const *l = parse_list_pop (&stack);
			parse_list_push (&stack, gnm_expr_new_binary (
				l,
				binary_ops [ptgbase - FORMULA_PTG_ADD],
				r));
			break;
		}

		case FORMULA_PTG_UNION : {
			GnmExpr const *r = parse_list_pop (&stack);
			GnmExpr const *l = parse_list_pop (&stack);

			/* not exactly legal, but should be reasonable
			 * XL has union operator we have sets.
			 */
			if (l->any.oper != GNM_EXPR_OP_SET) {
				GnmExprList *args = gnm_expr_list_prepend (NULL, r);
				args = gnm_expr_list_prepend (args, l);
				parse_list_push (&stack, gnm_expr_new_set (args));
			} else {
				gnm_expr_list_append (l->set.set, r);
				parse_list_push (&stack, l);
			}
			break;
		}

		case FORMULA_PTG_U_PLUS :
		case FORMULA_PTG_U_MINUS :
		case FORMULA_PTG_PERCENT :
			parse_list_push (&stack, gnm_expr_new_unary (
				unary_ops [ptgbase - FORMULA_PTG_U_PLUS],
				parse_list_pop (&stack)));
			break;

		case FORMULA_PTG_PAREN:
/*	  fprintf (stderr, "Ignoring redundant parenthesis ptg\n"); */
			ptg_length = 0;
			break;

		case FORMULA_PTG_MISSARG:
			parse_list_push_raw (&stack, value_new_empty ());
			ptg_length = 0;
			break;

		case FORMULA_PTG_ATTR : { /* FIXME: not fully implemented */
			guint8  grbit = GSF_LE_GET_GUINT8(cur);
			guint16 w     = GSF_LE_GET_GUINT16(cur+1);
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

				ms_excel_dump_cellname (container->ewb, esheet, fn_col, fn_row);
				fprintf (stderr, "Hmm, ptgAttr of type 0 ??\n"
					"I've seen a case where an instance of this with flag A and another with flag 3\n"
					"bracket a 1x1 array formula.  please send us this file.\n"
					"Flags = 0x%X\n", w);
			} else if (grbit & 0x01) {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 0) {
					fprintf (stderr, "A volatile function: so what\n");
				}
#endif
			} else if (grbit & 0x02) { /* AttrIf: 'optimised' IF function */
				/* Who cares if the TRUE expr has a goto at the end */
				GnmExpr const *tr;
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 2) {
					fprintf (stderr, "Optimised IF 0x%x 0x%x\n", grbit, w);
					gsf_mem_dump (mem, length);
				}
#endif
				tr = w ? excel_parse_formula (container, esheet, fn_col, fn_row,
					   cur+ptg_length, w, shared, NULL)
					: expr_tree_string ("");
				parse_list_push (&stack, tr);
				ptg_length += w;
			} else if (grbit & 0x04) { /* AttrChoose 'optimised' my foot. */
				guint16 len, lp;
				guint32 offset=0;
				guint8 const *data=cur+3;
				GnmExpr const *tr;

#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 1) {
					fprintf (stderr, "'Optimised' choose\n");
					gsf_mem_dump (mem,length);
				}
#endif
				for (lp=0;lp<w;lp++) { /* w = wCases */
					offset= GSF_LE_GET_GUINT16(data);
					len = GSF_LE_GET_GUINT16(data+2) - offset;
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 1) {
						fprintf (stderr, "Get from %d len %d [ = 0x%x ]\n",
							ptg_length+offset, len,
							*(cur+ptg_length+offset));
					}
#endif
					tr = excel_parse_formula (container, esheet, fn_col, fn_row,
						cur+ptg_length+offset, len, shared, NULL);
					data += 2;
					parse_list_push (&stack, tr);
				}
				ptg_length+=GSF_LE_GET_GUINT16(data);
			} else if (grbit & 0x08) { /* AttrGoto */
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_formula_debug > 2) {
					fprintf (stderr, "Goto %d: cur = 0x%x\n", w,
						(int)(cur-mem));
					gsf_mem_dump (mem, length);
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
				guint8 num_space = GSF_LE_GET_GUINT8(cur+2);
				guint8 attrs     = GSF_LE_GET_GUINT8(cur+1);
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */;
				else
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 1) {
						fprintf (stderr, "Redundant whitespace in formula 0x%x count %d\n", attrs, num_space);
					}
#else
				;
#endif
			} else {
				ms_excel_dump_cellname (container->ewb, esheet, fn_col, fn_row);
				fprintf (stderr, "Unknown PTG Attr gr = 0x%x, w = 0x%x ptg = 0x%x\n", grbit, w, ptg);
				error = TRUE;
			}
		}
		break;

		case FORMULA_PTG_ERR: {
			parse_list_push_raw (&stack, biff_get_error (NULL, GSF_LE_GET_GUINT8 (cur)));
			ptg_length = 1;
			break;
		}
		case FORMULA_PTG_INT: {
			guint16 num = GSF_LE_GET_GUINT16(cur);
			parse_list_push_raw (&stack, value_new_int (num));
			ptg_length = 2;
			break;
		}
		case FORMULA_PTG_BOOL:
			parse_list_push_raw (&stack, value_new_bool (GSF_LE_GET_GUINT8(cur)));
			ptg_length = 1;
			break;

		case FORMULA_PTG_NUM: {
			double tmp = gsf_le_get_double (cur);
			parse_list_push_raw (&stack, value_new_float (tmp));
			ptg_length = 8;
			break;
		}
		case FORMULA_PTG_STR: {
			char *str = NULL;
			int len;
			if (ver >= MS_BIFF_V8) {
				len = GSF_LE_GET_GUINT16 (cur);
				if (len <= len_left) {
					str = biff_get_text (cur+2, len, &len);
					ptg_length = 2 + len;
				}
#if 0
				fprintf (stderr, "v8+ PTG_STR '%s'\n", str);
#endif
			} else {
				len = GSF_LE_GET_GUINT8 (cur);
				if (len <= len_left) {
					str = biff_get_text (cur+1, len, &len);
					ptg_length = 1 + len;
				}
			}
			if (str != NULL) {
				d (2, fprintf (stderr, "   -> '%s'\n", str););
				parse_list_push_raw (&stack, value_new_string (str));
				g_free (str);
			} else {
				d (2, fprintf (stderr, "   -> \'\'\n"););
				parse_list_push_raw (&stack, value_new_string (""));
			}
			break;
		}

		case FORMULA_PTG_EXTENDED : { /* Extended Ptgs for Biff8 */
			/*
			 * The beginnings of 'extended' ptg support.
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
			guint8 const eptg_type = GSF_LE_GET_GUINT8(cur);
			if (eptg_type >= G_N_ELEMENTS (extended_ptg_size))
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
				CellRef ref;

				getRefV8 (&ref,
					  GSF_LE_GET_GUINT16(cur + 1),
					  GSF_LE_GET_GUINT16(cur + 3),
					  fn_col, fn_row, shared);

				if (eptg_type == 0x07) { /* Column */
					if (ref.row_relative)
						ref.row = 0;
					else
						ref.row = fn_row;
				} else { 		 /* Row */
					if (ref.col_relative)
						ref.col = 0;
					else
						ref.col = fn_col;
				}

				parse_list_push (&stack, gnm_expr_new_cellref (&ref));
			} else {
				fprintf (stderr, "-------------------\n");
				fprintf (stderr, "XL : Extended ptg %x\n", eptg_type);
				gsf_mem_dump (mem+2, length-2);
				fprintf (stderr, "-------------------\n");
			}
		}
		break;

		case FORMULA_PTG_ARRAY : {
			unsigned cols = GSF_LE_GET_GUINT8  (array_data + 0);
			unsigned rows = GSF_LE_GET_GUINT16 (array_data + 1);
			unsigned lpx, lpy, elem_len = 0;
			Value *v, *elem;
			guint8 val_type;

			if (ver >= MS_BIFF_V8) {
				cols++;
				rows++;
			} else if (cols == 0)
				cols = 256;

			v = value_new_array (cols, rows);
			ptg_length = 7;

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_formula_debug > 4) {
				/* no way to dump the content because we have
				 * no idea how long it is
				 */
				fprintf (stderr, "An Array how interesting: (%d,%d)\n",
					cols, rows);
			}
#endif
			array_data += 3;
			for (lpy = 0; lpy < rows; lpy++) {
				for (lpx = 0; lpx < cols; lpx++) {
					val_type = GSF_LE_GET_GUINT8 (array_data);
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_formula_debug > 5) {
						fprintf (stderr, "\tArray elem type 0x%x (%d,%d)\n", val_type, lpx, lpy);
					}
#endif
					switch (val_type) {
					case 1:
						elem = value_new_float (gsf_le_get_double (array_data+1));
						elem_len = 9;
						break;

					case 2: {
						guint32 len;
						char *str;

						if (ver >= MS_BIFF_V8) {
							str = biff_get_text (array_data + 3,
									     GSF_LE_GET_GUINT16 (array_data+1),
									     &len);
							elem_len = len + 3;
							/* biff_get_text misses the 1 byte header for empty strings */
							if (len == 0)
								elem_len++;
						} else {
							str = biff_get_text (array_data + 2,
									     GSF_LE_GET_GUINT8 (array_data+1),
									     &len);
							elem_len = len + 2;
						}

						if (str) {
#ifndef NO_DEBUG_EXCEL
							if (ms_excel_formula_debug > 5) {
								fprintf (stderr, "\tString '%s'\n", str);
							}
#endif
							elem = value_new_string_nocopy (str);
						} else
							elem = value_new_string ("");
						break;
					}

					case 4:
						elem = value_new_bool (array_data [1] ? TRUE : FALSE);
						elem_len = 9;
						break;
					case 16:
						elem = biff_get_error (NULL, array_data [1]);
						elem_len = 9;
						break;

					default :
						fprintf (stderr, "FIXME: Duff array item type %d @ %s%d:%d,%d with %d\n",
							val_type, col_name(fn_col), fn_row+1, lpx, lpy, elem_len);
						gsf_mem_dump (array_data-elem_len-9, 9+elem_len+9);
						elem = value_new_empty ();
					}
					value_array_set (v, lpx, lpy, elem);
					array_data += elem_len;
				}
			}
			parse_list_push_raw (&stack, v);
			break;
		}

		case FORMULA_PTG_FUNC:
			if (!make_function (&stack, GSF_LE_GET_GUINT16(cur), -1)) {
				error = TRUE;
				puts ("error making func");
			}
			ptg_length = 2;
			break;

		case FORMULA_PTG_FUNC_VAR: {
			int const numargs = (GSF_LE_GET_GUINT8( cur ) & 0x7f);
			/* index into fn table */
			int const iftab   = (GSF_LE_GET_GUINT16(cur+1) & 0x7fff);
#if 0
			/* Prompts the user ?  */
			int const prompt  = (GSF_LE_GET_GUINT8( cur ) & 0x80);
			/* is a command equiv.?*/
			int const cmdquiv = (GSF_LE_GET_GUINT16(cur+1) & 0x8000);
#endif

			if (!make_function (&stack, iftab, numargs)) {
				error = TRUE;
				puts ("error making func var");
			}
			ptg_length = 3;
			break;
		}

		case FORMULA_PTG_NAME: {
			guint16 name_idx = GSF_LE_GET_GUINT16 (cur);
			GPtrArray    *a;
			GnmExpr const*name;
			GnmNamedExpr *nexpr = NULL;

			if (ver >= MS_BIFF_V8)
				ptg_length = 4;  /* Docs are wrong, no ixti */
			else
				ptg_length = 14;

			a = container->ewb->container.names;
			if (a == NULL || name_idx < 1 || a->len < name_idx ||
			    (nexpr = g_ptr_array_index (a, name_idx-1)) == NULL) {
				g_warning ("EXCEL: %x (of %x) UNKNOWN name %p.",
					   name_idx, a ? a->len : 0xffffffff, container);
				name = gnm_expr_new_constant (
					value_new_error_REF (NULL));
			} else
				name = gnm_expr_new_name (nexpr, NULL, NULL);

			parse_list_push (&stack, name);
			d (2, fprintf (stderr, "Name idx %hu\n", name_idx););
		}
		break;

		case FORMULA_PTG_REF_ERR:
			ptg_length = (ver >= MS_BIFF_V8) ? 4 : 3;
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_AREA_ERR:
			ptg_length = (ver >= MS_BIFF_V8) ? 8 : 6;
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_REF: case FORMULA_PTG_REFN: {
			CellRef ref;
			if (ver >= MS_BIFF_V8) {
				getRefV8 (&ref,
					  GSF_LE_GET_GUINT16(cur),
					  GSF_LE_GET_GUINT16(cur + 2),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_REFN);
				ptg_length = 4;
			} else {
				getRefV7 (&ref,
					  GSF_LE_GET_GUINT8(cur+2),
					  GSF_LE_GET_GUINT16(cur),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_REFN);
				ptg_length = 3;
			}
			parse_list_push (&stack, gnm_expr_new_cellref (&ref));
			break;
		}

		case FORMULA_PTG_AREA: case FORMULA_PTG_AREAN: {
			CellRef first, last;
			if (ver >= MS_BIFF_V8) {
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16(cur+0),
					  GSF_LE_GET_GUINT16(cur+4),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
				getRefV8 (&last,
					  GSF_LE_GET_GUINT16(cur+2),
					  GSF_LE_GET_GUINT16(cur+6),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
				ptg_length = 8;
			} else {
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8(cur+4),
					  GSF_LE_GET_GUINT16(cur+0),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
				getRefV7 (&last,
					  GSF_LE_GET_GUINT8(cur+5),
					  GSF_LE_GET_GUINT16(cur+2),
					  fn_col, fn_row, ptgbase == FORMULA_PTG_AREAN);
				ptg_length = 6;
			}

			parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			break;
		}

		case FORMULA_PTG_MEM_AREA :
		case FORMULA_PTG_MEM_ERR :
			/* ignore this, we handle at run time */
			ptg_length = 6;
			break;

		case FORMULA_PTG_MEM_FUNC:
			/* ignore this, we handle at run time */
			ptg_length = 2;
			break;

		case FORMULA_PTG_NAME_X : {
			guint16 name_idx; /* 1 based */
			GPtrArray    *a = NULL;
			GnmExpr const*name;
			GnmNamedExpr *nexpr = NULL;
			Sheet *sheet = NULL;

			if (ver >= MS_BIFF_V8) {
				guint16 sheet_idx = GSF_LE_GET_GINT16 (cur);
				ExcelExternSheetV8 const *es = excel_externsheet_v8 (
					container->ewb, sheet_idx);
				if (es != NULL && es->supbook < container->ewb->v8.supbook->len) {
					ExcelSupBook const *sup = &g_array_index (
						container->ewb->v8.supbook,
						ExcelSupBook, es->supbook);
					if (sup->type == EXCEL_SUP_BOOK_SELFREF)
						a = container->ewb->container.names;
					else
						a = sup->externname;

					sheet = es->first;
				}

				name_idx  = GSF_LE_GET_GUINT16 (cur+2);

				d (2, fprintf (stderr, "name %hu : externsheet %hu\n",
					       name_idx, sheet_idx););

				ptg_length = 6;
			} else {
				gint16 sheet_idx = GSF_LE_GET_GINT16 (cur);
				name_idx  = GSF_LE_GET_GUINT16 (cur+10);
#if 0
				gsf_mem_dump (cur, 24);
				d (-2, fprintf (stderr, "name = %hu, externsheet = %hd\n",
					       name_idx, sheet_idx););
#endif
				if (sheet_idx < 0) {
					a = container->ewb->container.names;
					sheet_idx = -sheet_idx;
				} else
					a = container->names;
				sheet = excel_externsheet_v7 (container, sheet_idx);
				ptg_length = 24;
			}

			if (a == NULL || name_idx < 1 || a->len < name_idx ||
			    (nexpr = g_ptr_array_index (a, name_idx-1)) == NULL) {
				g_warning ("EXCEL: %x (of %x) UNKNOWN name %p.",
					   name_idx, a ? a->len : 0xffffffff, container);
				name = gnm_expr_new_constant (
					value_new_error_REF (NULL));
			} else {
				/* See supbook_get_sheet for details */
				if (sheet == (Sheet *)1) {
					sheet = nexpr->pos.sheet;
					if (sheet == NULL)
						sheet = ms_container_sheet (container);
				}
				name = gnm_expr_new_name (nexpr, sheet, NULL);
			}

			parse_list_push (&stack, name);
			d (2, fprintf (stderr, "Name idx %hu\n", name_idx););
		}
		break;

		case FORMULA_PTG_REF_3D : { /* see S59E2B.HTM */
			CellRef first, last;
			if (ver >= MS_BIFF_V8) {
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16 (cur + 2),
					  GSF_LE_GET_GUINT16 (cur + 4),
					  fn_col, fn_row, 0);
				last = first;
				ptg_length = 6;
			} else {
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8  (cur + 16),
					  GSF_LE_GET_GUINT16 (cur + 14),
					  fn_col, fn_row, 0);
				last = first;
				ptg_length = 17;
			}

			if (excel_formula_parses_ref_sheets (container, cur, &first.sheet, &last.sheet))
				parse_list_push_raw (&stack, value_new_error_REF (NULL));
			else if (first.sheet != last.sheet)
				parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			else
				parse_list_push (&stack, gnm_expr_new_cellref (&first));
			break;
		}

		case FORMULA_PTG_AREA_3D : { /* see S59E2B.HTM */
			/* See comments in FORMULA_PTG_REF_3D for correct handling of external references */
			CellRef first, last;

			if (ver >= MS_BIFF_V8) {
				getRefV8 (&first,
					  GSF_LE_GET_GUINT16(cur+2),
					  GSF_LE_GET_GUINT16(cur+6),
					  fn_col, fn_row, 0);
				getRefV8 (&last,
					  GSF_LE_GET_GUINT16(cur+4),
					  GSF_LE_GET_GUINT16(cur+8),
					  fn_col, fn_row, 0);
				ptg_length = 10;
			} else {
				getRefV7 (&first,
					  GSF_LE_GET_GUINT8(cur+18),
					  GSF_LE_GET_GUINT16(cur+14),
					  fn_col, fn_row, 0);
				getRefV7 (&last,
					  GSF_LE_GET_GUINT8(cur+19),
					  GSF_LE_GET_GUINT16(cur+16),
					  fn_col, fn_row, 0);
				ptg_length = 20;
			}
			if (excel_formula_parses_ref_sheets (container, cur, &first.sheet, &last.sheet))
				parse_list_push_raw (&stack, value_new_error_REF (NULL));
			else
				parse_list_push_raw (&stack, value_new_cellrange (&first, &last, fn_col, fn_row));
			break;
		}

		case FORMULA_PTG_REF_ERR_3D :
			ptg_length = (ver >= MS_BIFF_V8) ? 6 : 17;
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		case FORMULA_PTG_AREA_ERR_3D :
			ptg_length = (ver >= MS_BIFF_V8) ? 10 : 20;
			parse_list_push_raw (&stack, value_new_error_REF (NULL));
			break;

		default:
			g_warning ("EXCEL : Unhandled PTG 0x%x.", ptg);
			error = TRUE;
		}
/*		fprintf (stderr, "Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length); */
		cur      += ptg_length + 1;
		len_left -= ptg_length + 1;
	}

	if (error) {
		fprintf (stderr, "formula data : %s\n", (shared?" (shared)":"(NOT shared)"));
		gsf_mem_dump (mem, length);

		parse_list_free (&stack);
		return expr_tree_error (esheet, fn_col, fn_row,
			"Unknown Formula/Array", "#Unknown formula");
	}

	if (stack == NULL)
		return expr_tree_error (esheet, fn_col, fn_row,
			"Stack too short - unusual", "#ShortStack");
	if (gnm_expr_list_length (stack) > 1) {
		parse_list_free (&stack);
		return expr_tree_error (esheet, fn_col, fn_row,
			"Too much data on stack - probable cause: fixed args function is var-arg, put '-1' in the table above",
			"#LongStack");
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_formula_debug > 0 && esheet != NULL) {
		ParsePos pp;
		GnmExpr const *expr = parse_list_pop (&stack);
		parse_pos_init (&pp, NULL, esheet->sheet, fn_col, fn_row);
		puts (gnm_expr_as_string (expr, &pp, gnm_expr_conventions_default));
		return expr;
	}
#endif
	return expr_tree_sharer_share (container->ewb->expr_sharer, parse_list_pop (&stack));
}
