#ifndef GNUMERIC_H
#define GNUMERIC_H

#include <glib.h>

#define SHEET_MAX_ROWS		(64 * 1024)	/* 0 - 65535 inclusive */
#define SHEET_MAX_COLS		256		/* 0 - 255 inclusive */

typedef struct _Workbook	Workbook;
typedef struct _Sheet		Sheet;
typedef struct _Cell		Cell;

typedef struct _Value		Value;
typedef struct _ErrorMessage	ErrorMessage;

typedef struct _ExprTree	   ExprTree;
typedef struct _ArrayRef	   ArrayRef;
typedef struct _ExprName	   ExprName;
typedef struct _ExprRelocateInfo   ExprRelocateInfo;

typedef struct _CellRegion	CellRegion;
typedef		GList		CellList;

typedef struct _ColRowInfo	 ColRowInfo;
typedef struct _ColRowCollection ColRowCollection;

typedef struct _CellPos		CellPos;
typedef struct _CellRef		CellRef;
typedef struct _Range		Range;

typedef struct _MStyle		  MStyle;
typedef enum   _MStyleElementType MStyleElementType;
typedef struct _MStyleBorder      MStyleBorder;

typedef struct _SheetStyleData	SheetStyleData;
typedef struct _StyleRegion	StyleRegion;
typedef struct _SheetSelection	SheetSelection;

typedef struct _EvalPosition	   EvalPosition;
typedef struct _ParsePosition	   ParsePosition;
typedef struct _FunctionEvalInfo   FunctionEvalInfo;
typedef struct _FunctionDefinition FunctionDefinition;

typedef struct _CommandContext	   CommandContext;

typedef struct _PrintInformation PrintInformation;
typedef struct _String	 	 String;

typedef struct _DependencyData   DependencyData;

typedef enum _StyleHAlignFlags StyleHAlignFlags;
typedef enum _StyleVAlignFlags StyleVAlignFlags;
typedef enum _StyleOrientation StyleOrientation;
typedef enum _StyleBorderLocation StyleBorderLocation;
typedef enum _StyleUnderlineType StyleUnderlineType;

typedef struct _StyleFormat		StyleFormat;
typedef struct _StyleFont		StyleFont;
typedef struct _StyleColor		StyleColor;

/* Used to locate cells in a sheet */
struct _CellPos {
	int col, row;
};

struct _Range {
	CellPos start, end;
};

struct _CellRef {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
};

#endif /* GNUMERIC_H */
