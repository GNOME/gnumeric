#ifndef GNUMERIC_AUTO_FORMAT_H
#define GNUMERIC_AUTO_FORMAT_H

#include "gnumeric.h"
#include "func.h"

typedef enum {
	AF_UNKNOWN = 0,

	/*
	 * Things like PV(...).
	 */
	AF_MONETARY,

	/*
	 * Things like TODAY(...).
	 */
	AF_DATE,

	/*
	 * Things like TIME(...).
	 */
	AF_TIME,

	/*
	 * Things like IRR(...).
	 */
	AF_PERCENT,

	 /*
	  * Things like SUM(...).  If any of the arguments yield a format
	  * we use that.  The "2" form, useful for IF, starts at the second
	  * argument.
	  */
	AF_FIRST_ARG_FORMAT,
	AF_FIRST_ARG_FORMAT2,

	/* ----------------------------------------------------------------- */
	/* Internal use only from here on.  */

	/*
	 * Things like COUNT(...).  This probably should not be used for
	 * auto_style_format_suggest.
	 */
	AF_UNITLESS,

	/*
	 * Typically references to a formatted cell.  This is not useful for
	 * auto_format_function_result since there is no way to specify the
	 * format.
	 */
	AF_EXPLICIT
} AutoFormatTypes;

void auto_format_init (void);
void auto_format_shutdown (void);

void auto_format_function_result (FunctionDefinition *fd, AutoFormatTypes res);
StyleFormat *auto_style_format_suggest (const GnmExpr *expr, const EvalPos *epos);

#endif
