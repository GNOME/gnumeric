/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GNUMERIC_H_
# define _GNM_GNUMERIC_H_

#include <glib.h>
#include <goffice/app/goffice-app.h>
#include <goffice/utils/goffice-utils.h>

G_BEGIN_DECLS

#define SHEET_MAX_ROWS		(16*16*16*16)	/* 0, 1, ... */
#define SHEET_MAX_COLS		(4*4*4*4)	/* 0, 1, ... */

/*
 * Note: more than 364238 columns will introduce a column named TRUE.
 */

typedef struct _GnmApp			GnmApp;
typedef struct _Workbook		Workbook;
typedef struct _WorkbookView		WorkbookView;
typedef struct _WorkbookControl		WorkbookControl;
typedef struct _WorkbookSheetState	WorkbookSheetState;

typedef enum {
	GNM_SHEET_VISIBILITY_VISIBLE,
	GNM_SHEET_VISIBILITY_HIDDEN,
	GNM_SHEET_VISIBILITY_VERY_HIDDEN
} GnmSheetVisibility;
typedef struct _Sheet			Sheet;
typedef struct _SheetView		SheetView;
typedef struct _SheetControl		SheetControl;

typedef struct _SheetObject		SheetObject;
typedef struct _SheetObjectAnchor	SheetObjectAnchor;
typedef struct _SheetObjectView		 SheetObjectView;
typedef struct _SheetObjectViewContainer SheetObjectViewContainer;
typedef struct _SheetObjectImageableIface SheetObjectImageableIface;
typedef struct _SheetObjectExportableIface SheetObjectExportableIface;

typedef struct _GnmDepContainer		GnmDepContainer;
typedef struct _GnmDependent		GnmDependent;
typedef struct _GnmCell			GnmCell;
typedef struct _GnmComment		GnmComment;

typedef union  _GnmValue		GnmValue;
typedef struct _GnmValueBool		GnmValueBool;
typedef struct _GnmValueFloat		GnmValueFloat;
typedef struct _GnmValueErr		GnmValueErr;
typedef struct _GnmValueStr		GnmValueStr;
typedef struct _GnmValueRange		GnmValueRange;
typedef struct _GnmValueArray		GnmValueArray;

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

typedef struct _GnmRenderedValue	GnmRenderedValue;
typedef struct _GnmRenderedRotatedValue	GnmRenderedRotatedValue;

typedef GSList 				GnmExprList;
typedef union  _GnmExpr	 		GnmExpr;
typedef struct _GnmExprConstant		GnmExprConstant;
typedef struct _GnmExprFunction		GnmExprFunction;
typedef struct _GnmExprUnary		GnmExprUnary;
typedef struct _GnmExprBinary		GnmExprBinary;
typedef struct _GnmExprName		GnmExprName;
typedef struct _GnmExprCellRef		GnmExprCellRef;
typedef struct _GnmExprArrayCorner	GnmExprArrayCorner;
typedef struct _GnmExprArrayElem	GnmExprArrayElem;
typedef struct _GnmExprSet		GnmExprSet;
typedef GnmExpr const *			GnmExprConstPtr;

typedef struct _GnmExprTop		GnmExprTop;
typedef struct _GnmExprSharer		GnmExprSharer;

typedef struct _GnmExprRelocateInfo	GnmExprRelocateInfo;

typedef struct _GnmNamedExpr		GnmNamedExpr;
typedef struct _GnmNamedExprCollection	GnmNamedExprCollection;

typedef struct _GnmPasteTarget		GnmPasteTarget;
typedef struct _GnmCellRegion		GnmCellRegion;

typedef struct _ColRowInfo	 	ColRowInfo;
typedef struct _ColRowCollection	ColRowCollection;
typedef struct _ColRowSegment	 	ColRowSegment;
typedef GSList  			ColRowVisList;
typedef GSList  			ColRowStateGroup;
typedef GSList  			ColRowStateList;
typedef GList   			ColRowIndexList;
typedef struct _ColRowIndexSet          ColRowIndexSet;

typedef struct _GnmFont	        	GnmFont;
typedef struct _GnmFontMetrics        	GnmFontMetrics;
typedef struct _GnmColor	        GnmColor;
typedef struct _GnmBorder	        GnmBorder;
typedef struct _GnmStyle		GnmStyle;
typedef struct _GnmStyleRow        	GnmStyleRow;
typedef GSList				GnmStyleList;
typedef struct _GnmStyleRegion	        GnmStyleRegion;
typedef struct _GnmStyleConditions	GnmStyleConditions;
typedef struct _GnmSheetStyleData       GnmSheetStyleData;
typedef struct _GnmFormatTemplate       GnmFormatTemplate; /* does not really belong here */

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
typedef struct _GnmCellRef	        GnmCellRef;	/* abs/rel point with sheet */
typedef struct _GnmRangeRef	        GnmRangeRef;	/* abs/rel range with sheet */
typedef struct _GnmEvalPos		GnmEvalPos;
typedef struct _GnmParsePos	        GnmParsePos;
typedef struct _GnmParseError	        GnmParseError;
typedef struct _GnmConventions		GnmConventions;
typedef struct _GnmConventionsOut	GnmConventionsOut;
typedef struct _GnmFuncEvalInfo         GnmFuncEvalInfo;
typedef struct _GnmFunc			GnmFunc;
typedef struct _GnmFuncGroup		GnmFuncGroup;
typedef struct _GnmFuncDescriptor	GnmFuncDescriptor;
typedef struct _GnmAction		GnmAction;

typedef enum {
	CELL_ITER_ALL			= 0,
	CELL_ITER_IGNORE_NONEXISTENT	= 1 << 0,
	CELL_ITER_IGNORE_EMPTY		= 1 << 1,
	CELL_ITER_IGNORE_BLANK		= (CELL_ITER_IGNORE_NONEXISTENT | CELL_ITER_IGNORE_EMPTY),
	CELL_ITER_IGNORE_HIDDEN		= 1 << 2, /* hidden manually */

	/* contains SUBTOTAL, or hidden row in a filter */
	CELL_ITER_IGNORE_SUBTOTAL	= 1 << 3
} CellIterFlags;
typedef struct _GnmCellIter GnmCellIter;
typedef GnmValue *(*CellIterFunc) (GnmCellIter const *iter, gpointer user);

typedef enum {
	GNM_SPANCALC_SIMPLE 	= 0x0,	/* Just calc spans */
	GNM_SPANCALC_RESIZE	= 0x1,	/* Calculate sizes of all cells */
	GNM_SPANCALC_RE_RENDER	= 0x2,	/* Render and Size all cells */
	GNM_SPANCALC_RENDER	= 0x4,	/* Render and Size any unrendered cells */
	GNM_SPANCALC_ROW_HEIGHT	= 0x8	/* Resize the row height */
} GnmSpanCalcFlags;

typedef enum {
	GNM_EXPR_EVAL_SCALAR_NON_EMPTY	= 0,
	GNM_EXPR_EVAL_PERMIT_NON_SCALAR	= 0x1,
	GNM_EXPR_EVAL_PERMIT_EMPTY	= 0x2
} GnmExprEvalFlags;

typedef struct _GnmString 	        GnmString;

typedef struct _XmlParseContext		XmlParseContext;

typedef struct _GnmSortData		GnmSortData;
typedef struct _GnmSearchReplace	GnmSearchReplace;
typedef struct _GnmConsolidate		GnmConsolidate;
typedef struct _GnmValidation		GnmValidation;
typedef struct _GnmFilter		GnmFilter;
typedef struct _GnmFilterCondition	GnmFilterCondition;
typedef struct _GnmHLink		GnmHLink;
typedef struct _GnmInputMsg		GnmInputMsg;

typedef struct _PrintInformation        PrintInformation;
typedef struct _SolverParameters	SolverParameters;

G_END_DECLS

#endif /* _GNM_GNUMERIC_H_ */
