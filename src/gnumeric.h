#ifndef GNUMERIC_H
#define GNUMERIC_H

#include <glib.h>

#define SHEET_MAX_ROWS		(64 * 1024)	/* 0 - 65535 inclusive */
#define SHEET_MAX_COLS		256		/* 0 - 255 inclusive */

typedef struct _Workbook	Workbook;
typedef struct _Sheet		Sheet;
typedef struct _Cell		Cell;

typedef struct _ExprTree	ExprTree;
typedef struct _ArrayRef	ArrayRef;
typedef struct _ExprName	ExprName;

typedef struct _CellRegion	CellRegion;
typedef		GList		CellList;

typedef struct _ColRowInfo	ColRowInfo;
typedef struct _ColRowCollection ColRowCollection;

typedef struct _CellPos		CellPos;
typedef struct _CellRef		CellRef;
typedef struct _Range		Range;

typedef struct _MStyle		MStyle;
typedef struct _SheetStyleData	SheetStyleData;
typedef struct _StyleRegion	StyleRegion;
typedef struct _SheetSelection	SheetSelection;

typedef struct _EvalPosition	EvalPosition;
typedef struct _ParsePosition	ParsePosition;
typedef struct _FunctionEvalInfo FunctionEvalInfo;
typedef struct _CmdContext	CmdContext;

typedef struct _PrintInformation PrintInformation;
typedef struct _String	 	 String;

#endif /* GNUMERIC_H */
