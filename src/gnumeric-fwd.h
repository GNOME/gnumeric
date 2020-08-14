#ifndef _GNM_GNUMERIC_FWD_H_
#define _GNM_GNUMERIC_FWD_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct GnmComplete_             GnmComplete;
typedef struct GnmScenario_             GnmScenario;
typedef struct GnmSolver_     		GnmSolver;
typedef struct GnmSolverConstraint_     GnmSolverConstraint;
typedef struct GnmSolverFactory_        GnmSolverFactory;
typedef struct GnmSolverParameters_	GnmSolverParameters;
typedef struct _ColRowCollection	ColRowCollection;
typedef struct _ColRowIndexSet          ColRowIndexSet;
typedef struct _ColRowInfo		ColRowInfo;
typedef struct _ColRowSegment		ColRowSegment;
typedef struct _GnmAction		GnmAction;
typedef struct _GnmApp			GnmApp;
typedef struct _GnmBorder	        GnmBorder;
typedef struct _GnmCell			GnmCell;
typedef struct _GnmCellRef	        GnmCellRef;	/* abs/rel point with sheet */
typedef struct _GnmCellRegion		GnmCellRegion;
typedef struct _GnmColor	        GnmColor;
typedef struct _GnmComment		GnmComment;
typedef struct _GnmConsolidate		GnmConsolidate;
typedef struct _GnmConventions		GnmConventions;
typedef struct _GnmConventionsOut	GnmConventionsOut;
typedef struct _GnmDepContainer		GnmDepContainer;
typedef struct _GnmDependent		GnmDependent;
typedef struct _GnmEvalPos		GnmEvalPos;
typedef struct _GnmExprArrayCorner	GnmExprArrayCorner;
typedef struct _GnmExprArrayElem	GnmExprArrayElem;
typedef struct _GnmExprBinary		GnmExprBinary;
typedef struct _GnmExprCellRef		GnmExprCellRef;
typedef struct _GnmExprConstant		GnmExprConstant;
typedef struct _GnmExprFunction		GnmExprFunction;
typedef struct _GnmExprName		GnmExprName;
typedef struct _GnmExprRelocateInfo	GnmExprRelocateInfo;
typedef struct _GnmExprSet		GnmExprSet;
typedef struct _GnmExprSharer		GnmExprSharer;
typedef struct _GnmExprTop		GnmExprTop;
typedef struct _GnmExprUnary		GnmExprUnary;
typedef struct _GnmFilter		GnmFilter;
typedef struct _GnmFilterCondition	GnmFilterCondition;
typedef struct _GnmFont			GnmFont;
typedef struct _GnmFontMetrics		GnmFontMetrics;
typedef struct GnmFT_       GnmFT; /* does not really belong here */
typedef struct GnmFunc_			GnmFunc;
typedef struct _GnmFuncDescriptor	GnmFuncDescriptor;
typedef struct _GnmFuncEvalInfo         GnmFuncEvalInfo;
typedef struct _GnmFuncGroup		GnmFuncGroup;
typedef struct _GnmHLink		GnmHLink;
typedef struct _GnmInputMsg		GnmInputMsg;
typedef struct _GnmItemBar		GnmItemBar;
typedef struct _GnmItemCursor		GnmItemCursor;
typedef struct _GnmItemEdit		GnmItemEdit;
typedef struct _GnmItemGrid		GnmItemGrid;
typedef struct GnmMatrix_               GnmMatrix;
typedef struct _GnmNamedExpr		GnmNamedExpr;
typedef struct _GnmNamedExprCollection	GnmNamedExprCollection;
typedef struct _GnmPane			GnmPane;
typedef struct _GnmParseError	        GnmParseError;
typedef struct _GnmParsePos	        GnmParsePos;
typedef struct _GnmPasteTarget		GnmPasteTarget;
typedef struct _GnmRangeRef	        GnmRangeRef;	/* abs/rel range with sheet */
typedef struct _GnmRenderedRotatedValue	GnmRenderedRotatedValue;
typedef struct _GnmRenderedValue	GnmRenderedValue;
typedef struct _GnmRenderedValueCollection GnmRenderedValueCollection;
typedef struct _GnmSearchReplace	GnmSearchReplace;
typedef struct _GnmSheetSize		GnmSheetSize;
typedef struct _GnmSheetSlicer		GnmSheetSlicer;
typedef struct _GnmSheetStyleData       GnmSheetStyleData;
typedef struct GnmSheetConditionsData_  GnmSheetConditionsData;
typedef struct _GnmSortData		GnmSortData;
typedef struct _GnmStfExport GnmStfExport;
typedef struct _GnmStyle		GnmStyle;
typedef struct _GnmStyleConditions	GnmStyleConditions;
typedef struct _GnmStyleRegion	        GnmStyleRegion;
typedef struct _GnmStyleRow		GnmStyleRow;
typedef struct _GnmValidation		GnmValidation;
typedef struct _GnmValueArray		GnmValueArray;
typedef struct _GnmValueBool		GnmValueBool;
typedef struct _GnmValueErr		GnmValueErr;
typedef struct _GnmValueFloat		GnmValueFloat;
typedef struct _GnmValueRange		GnmValueRange;
typedef struct _GnmValueStr		GnmValueStr;
typedef struct GnmPrintInformation_        GnmPrintInformation;
typedef struct _Sheet			Sheet;
typedef struct _SheetControl		SheetControl;
typedef struct _SheetControlGUI		SheetControlGUI;
typedef struct _SheetObject		SheetObject;
typedef struct _SheetObjectAnchor	SheetObjectAnchor;
typedef struct _SheetObjectExportable   SheetObjectExportable;
typedef struct _SheetObjectImageable    SheetObjectImageable;
typedef struct _SheetObjectView		SheetObjectView;
typedef struct _SheetObjectViewContainer SheetObjectViewContainer;
typedef struct _SheetView		SheetView;
typedef struct _WBCGtk			WBCGtk;
typedef struct _Workbook		Workbook;
typedef struct _WorkbookControl		WorkbookControl;
typedef struct _WorkbookControlComponent WorkbookControlComponent;
typedef struct _WorkbookSheetState	WorkbookSheetState;
typedef struct _WorkbookView		WorkbookView;
typedef union  _GnmExpr			GnmExpr;
typedef union  _GnmValue		GnmValue;
typedef struct _GenericToolState	GnmGenericToolState;
typedef struct GnmExprDeriv_            GnmExprDeriv;

typedef GList				ColRowIndexList;
typedef GSList				ColRowStateGroup;
typedef GSList				ColRowStateList;
typedef GSList				ColRowVisList;
typedef GSList				GnmExprList;
typedef GSList				GnmStyleList;
typedef GnmExpr const *			GnmExprConstPtr;

G_END_DECLS

#endif

