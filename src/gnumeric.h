#ifndef _GNM_GNUMERIC_H_
# define _GNM_GNUMERIC_H_

#include <glib.h>
#include <goffice/goffice.h>
#include <gnumeric-fwd.h>

G_BEGIN_DECLS

/* Individual maxima for the dimensions.  See also gnm_sheet_valid_size.  */
#define GNM_MAX_ROWS 0x1000000
#define GNM_MAX_COLS 0x4000

/* Standard size */
#define GNM_DEFAULT_COLS 0x100
#define GNM_DEFAULT_ROWS 0x10000

/* Minimum size.  dependent.c sets row constraint.  */
#define GNM_MIN_ROWS 0x80
#define GNM_MIN_COLS 0x80

// Note: more than 364238 columns will introduce a column named TRUE.

struct _GnmSheetSize {
	int max_cols, max_rows;
};

typedef enum {
	GNM_SHEET_VISIBILITY_VISIBLE,
	GNM_SHEET_VISIBILITY_HIDDEN,
	GNM_SHEET_VISIBILITY_VERY_HIDDEN
} GnmSheetVisibility;
typedef enum {
	GNM_SHEET_DATA,
	GNM_SHEET_OBJECT,
	GNM_SHEET_XLM
} GnmSheetType;

typedef enum {
	GNM_ERROR_NULL,
	GNM_ERROR_DIV0,
	GNM_ERROR_VALUE,
	GNM_ERROR_REF,
	GNM_ERROR_NAME,
	GNM_ERROR_NUM,
	GNM_ERROR_NA,
	GNM_ERROR_UNKNOWN
} GnmStdError;

typedef struct {
	int col, row;	/* these must be int not unsigned in some places (eg SUMIF ) */
} GnmCellPos;
typedef struct {
	GnmCellPos start, end;
} GnmRange;
typedef struct {
	Sheet *sheet;
	GnmRange  range;
} GnmSheetRange;

typedef enum {
	CELL_ITER_ALL			= 0,
	CELL_ITER_IGNORE_NONEXISTENT	= 1 << 0,
	CELL_ITER_IGNORE_EMPTY		= 1 << 1,
	CELL_ITER_IGNORE_BLANK		= (CELL_ITER_IGNORE_NONEXISTENT | CELL_ITER_IGNORE_EMPTY),
	CELL_ITER_IGNORE_HIDDEN		= 1 << 2, /* hidden manually */

	/* contains SUBTOTAL */
	CELL_ITER_IGNORE_SUBTOTAL	= 1 << 3,
	/* hidden row in a filter */
	CELL_ITER_IGNORE_FILTERED	= 1 << 4
} CellIterFlags;
typedef struct _GnmCellIter GnmCellIter;
typedef GnmValue *(*CellIterFunc) (GnmCellIter const *iter, gpointer user);

typedef enum {
	GNM_SPANCALC_SIMPLE	= 0x0,	/* Just calc spans */
	GNM_SPANCALC_RESIZE	= 0x1,	/* Calculate sizes of all cells */
	GNM_SPANCALC_RE_RENDER	= 0x2,	/* Render and Size all cells */
	GNM_SPANCALC_RENDER	= 0x4,	/* Render and Size any unrendered cells */
	GNM_SPANCALC_ROW_HEIGHT	= 0x8	/* Resize the row height */
} GnmSpanCalcFlags;

typedef enum {
	GNM_EXPR_EVAL_SCALAR_NON_EMPTY	= 0,
	GNM_EXPR_EVAL_PERMIT_NON_SCALAR	= 0x1,
	GNM_EXPR_EVAL_PERMIT_EMPTY	= 0x2,
	GNM_EXPR_EVAL_WANT_REF		= 0x4
} GnmExprEvalFlags;

G_END_DECLS

#endif /* _GNM_GNUMERIC_H_ */
