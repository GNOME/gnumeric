#ifndef GNUMERIC_H
#define GNUMERIC_H

#include <glib.h>

#ifndef __attribute__
# if !defined(__GNUC__) || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
/* OK, this compiler probably doesn't understand __attribute__ */
#  define __attribute__(Spec) /* empty */
# endif
#endif

#define SHEET_MAX_ROWS		(16*16*16*16)	/* 0, 1, ... */
#define SHEET_MAX_COLS		(4*4*4*4)	/* 0, 1, ... */

/*
 * Note: more than 364238 columns will introduce a column named TRUE.
 */

typedef struct _CommandContext		CommandContext;
typedef struct _CommandContextStderr	CommandContextStderr;

typedef struct _IOContext		IOContext;
typedef struct _XmlParseContext		XmlParseContext;

typedef struct _Workbook		Workbook;
typedef struct _WorkbookView		WorkbookView;
typedef struct _WorkbookControl		WorkbookControl;

typedef struct _Sheet			Sheet;
typedef struct _SheetView		SheetView;
typedef struct _SheetControl		SheetControl;

typedef struct _SolverParameters	SolverParameters;

typedef struct _SheetObject		SheetObject;
typedef struct _SheetObjectAnchor	SheetObjectAnchor;
typedef struct _GnmHLink		GnmHLink;
typedef struct _GnmInputMsg		GnmInputMsg;

typedef struct _GnmDepContainer		GnmDepContainer;
typedef struct _Dependent		Dependent;
typedef struct _Cell			Cell;
typedef struct _CellComment		CellComment;

typedef union _Value			Value;
typedef struct _ValueBool		ValueBool;
typedef struct _ValueInt		ValueInt;
typedef struct _ValueFloat		ValueFloat;
typedef struct _ValueErr		ValueErr;
typedef struct _ValueStr		ValueStr;
typedef struct _ValueRange		ValueRange;
typedef struct _ValueArray		ValueArray;

typedef struct _RenderedValue		RenderedValue;

typedef GSList 				GnmExprList;
typedef union  _GnmExpr	 		GnmExpr;
typedef struct _GnmExprConstant		GnmExprConstant;
typedef struct _GnmExprFunction		GnmExprFunction;
typedef struct _GnmExprUnary		GnmExprUnary;
typedef struct _GnmExprBinary		GnmExprBinary;
typedef struct _GnmExprName		GnmExprName;
typedef struct _GnmExprCellRef		GnmExprCellRef;
typedef struct _GnmExprArray		GnmExprArray;
typedef struct _GnmExprSet		GnmExprSet;

typedef struct _GnmExprRelocateInfo	GnmExprRelocateInfo;
typedef struct _GnmExprRewriteInfo 	GnmExprRewriteInfo;

typedef struct _GnmNamedExpr		GnmNamedExpr;
typedef struct _GnmNamedExprCollection	GnmNamedExprCollection;

typedef struct _PasteTarget		PasteTarget;
typedef struct _CellRegion		CellRegion;

typedef struct _ColRowInfo	 	ColRowInfo;
typedef struct _ColRowCollection	ColRowCollection;
typedef struct _ColRowSegment	 	ColRowSegment;
typedef GSList  ColRowVisList;
typedef GSList  ColRowStateGroup;
typedef GSList  ColRowStateList;
typedef GList   ColRowIndexList;
typedef struct _ColRowIndexSet          ColRowIndexSet;
typedef gboolean (*ColRowHandler)(ColRowInfo *info, void *user_data);

typedef struct _CellPos		        CellPos;
typedef struct _CellRef		        CellRef;
typedef struct _Range		        Range;
typedef struct _GlobalRange	        GlobalRange;
typedef struct _RangeRef	        RangeRef;

typedef struct _GnmExprConventions      GnmExprConventions;

typedef struct _MStyle		        MStyle;

typedef struct _SheetStyleData	        SheetStyleData;
typedef struct _StyleRegion	        StyleRegion;
typedef GSList				StyleList;

typedef struct _EvalPos		        EvalPos;
typedef struct _ParsePos	        ParsePos;
typedef struct _ParseError	        ParseError;
typedef struct _FunctionEvalInfo        FunctionEvalInfo;
typedef struct _GnmFunc			GnmFunc;
typedef struct _ErrorInfo		ErrorInfo;

typedef struct _PrintInformation        PrintInformation;
typedef struct _String	 	        String;

typedef struct _GnmFileOpener		GnmFileOpener;	/* TODO rename to GnmFileFormatReader */

typedef struct _StyleFormat	        StyleFormat;
typedef struct _StyleFont	        StyleFont;
typedef struct _StyleColor	        StyleColor;
typedef struct _StyleBorder	        StyleBorder;
typedef struct _StyleRow	        StyleRow;
typedef struct _StyleCondition          StyleCondition;
typedef struct _FormatTemplate          FormatTemplate;

typedef struct _Validation              Validation;

typedef struct _GnmFilter		GnmFilter;
typedef struct _GnmFilterCondition	GnmFilterCondition;

typedef struct _GnmGraph		GnmGraph;
typedef struct _GnmGraphPlot		GnmGraphPlot;
typedef struct _GnmGraphSeries		GnmGraphSeries;
typedef struct _GnmGraphVector		GnmGraphVector;

/* Used to locate cells in a sheet */
struct _CellPos {
	int col, row;
};

struct _Range {
	CellPos start, end;
};

struct _GlobalRange {
	Sheet *sheet;
	Range  range;
};

typedef enum {
	CELL_ITER_ALL 		  = 0,
	CELL_ITER_IGNORE_BLANK    = 1 << 0,
	CELL_ITER_IGNORE_HIDDEN	  = 1 << 1, /* hidden manually */

	/* contains SUBTOTAL, or hidden row in a filter */
	CELL_ITER_IGNORE_SUBTOTAL = 1 << 2 
} CellIterFlags;
typedef Value *(*CellIterFunc) (Sheet *sheet, int col, int row,
				Cell *cell, gpointer user_data);

typedef enum {
	SPANCALC_SIMPLE 	= 0x0,	/* Just calc spans */
	SPANCALC_RESIZE		= 0x1,	/* Calculate sizes of all cells */
	SPANCALC_RE_RENDER	= 0x2,	/* Render and Size all cells */
	SPANCALC_RENDER		= 0x4	/* Render and Size any unrendered cells */
} SpanCalcFlags;

typedef enum
{
	GNM_EXPR_EVAL_SCALAR_NON_EMPTY	= 0,
	GNM_EXPR_EVAL_PERMIT_NON_SCALAR	= 0x1,
	GNM_EXPR_EVAL_PERMIT_EMPTY	= 0x2
} GnmExprEvalFlags;

typedef struct _SearchReplace           SearchReplace;

typedef struct _gnm_mem_chunk		gnm_mem_chunk;

typedef struct _GnmPlugin            GnmPlugin;
typedef struct _PluginService        PluginService;
typedef struct _GnumericPluginLoader GnumericPluginLoader;


#endif /* GNUMERIC_H */
