#ifndef GNM_GNUMERIC_FWD_H_
#define GNM_GNUMERIC_FWD_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct GnmComplete_             GnmComplete;
typedef struct GnmScenario_             GnmScenario;
typedef struct GnmSolver_     		GnmSolver;
typedef struct GnmSolverConstraint_     GnmSolverConstraint;
typedef struct GnmSolverFactory_        GnmSolverFactory;
typedef struct GnmSolverParameters_	GnmSolverParameters;
typedef struct ColRowCollection_	ColRowCollection;
typedef struct ColRowInfo_		ColRowInfo;
typedef struct ColRowSegment_		ColRowSegment;
typedef struct GnmAction_		GnmAction;
typedef struct GnmApp_			GnmApp;
typedef struct GnmBorder_	        GnmBorder;
typedef struct GnmCell_			GnmCell;
typedef struct GnmCellRef_	        GnmCellRef;	/* abs/rel point with sheet */
typedef struct GnmCellRegion_		GnmCellRegion;
typedef struct GnmColor_	        GnmColor;
typedef struct GnmComment_		GnmComment;
typedef struct GnmConsolidate_		GnmConsolidate;
typedef struct _GnmConventions		GnmConventions;
typedef struct GnmConventionsOut_	GnmConventionsOut;
typedef struct GnmDepContainer_		GnmDepContainer;
typedef struct GnmDependent_		GnmDependent;
typedef struct GnmEvalPos_		GnmEvalPos;
typedef struct GnmExprArrayCorner_	GnmExprArrayCorner;
typedef struct GnmExprArrayElem_	GnmExprArrayElem;
typedef struct GnmExprBinary_		GnmExprBinary;
typedef struct GnmExprCellRef_		GnmExprCellRef;
typedef struct GnmExprConstant_		GnmExprConstant;
typedef struct GnmExprFunction_		GnmExprFunction;
typedef struct GnmExprName_		GnmExprName;
typedef struct GnmExprRelocateInfo_	GnmExprRelocateInfo;
typedef struct GnmExprSet_		GnmExprSet;
typedef struct GnmExprSharer_		GnmExprSharer;
typedef struct GnmExprTop_		GnmExprTop;
typedef struct GnmExprUnary_		GnmExprUnary;
typedef struct GnmFilter_		GnmFilter;
typedef struct GnmFilterCondition_	GnmFilterCondition;
typedef struct GnmFont_			GnmFont;
typedef struct GnmFT_                   GnmFT;
typedef struct GnmFunc_			GnmFunc;
typedef struct GnmFuncDescriptor_	GnmFuncDescriptor;
typedef struct GnmFuncEvalInfo_         GnmFuncEvalInfo;
typedef struct GnmFuncGroup_		GnmFuncGroup;
typedef struct GnmHLink_		GnmHLink;
typedef struct GnmInputMsg_		GnmInputMsg;
typedef struct GnmItemBar_		GnmItemBar;
typedef struct GnmItemCursor_		GnmItemCursor;
typedef struct GnmItemEdit_		GnmItemEdit;
typedef struct GnmItemGrid_		GnmItemGrid;
typedef struct GnmMatrix_               GnmMatrix;
typedef struct GnmNamedExpr_		GnmNamedExpr;
typedef struct GnmNamedExprCollection_	GnmNamedExprCollection;
typedef struct GnmPane_			GnmPane;
typedef struct GnmParseError_	        GnmParseError;
typedef struct GnmParsePos_	        GnmParsePos;
typedef struct GnmPasteTarget_		GnmPasteTarget;
typedef struct GnmRangeRef_	        GnmRangeRef;	/* abs/rel range with sheet */
typedef struct GnmRenderedRotatedValue_	GnmRenderedRotatedValue;
typedef struct GnmRenderedValue_	GnmRenderedValue;
typedef struct GnmRenderedValueCollection_ GnmRenderedValueCollection;
typedef struct GnmSearchReplace_	GnmSearchReplace;
typedef struct GnmSheetSize_		GnmSheetSize;
typedef struct GnmSheetSlicer_		GnmSheetSlicer;
typedef struct GnmSheetStyleData_       GnmSheetStyleData;
typedef struct GnmSheetConditionsData_  GnmSheetConditionsData;
typedef struct GnmSortData_		GnmSortData;
typedef struct GnmStfParseOptions_      GnmStfParseOptions;
typedef struct GnmStfParsedLines_       GnmStfParsedLines;
typedef struct GnmStfExport_            GnmStfExport;
typedef struct GnmStyle_		GnmStyle;
typedef struct GnmStyleConditions_	GnmStyleConditions;
typedef struct GnmStyleRegion_	        GnmStyleRegion;
typedef struct GnmStyleRow_		GnmStyleRow;
typedef struct GnmTabulate_             GnmTabulate;
typedef struct GnmValidation_		GnmValidation;
typedef struct GnmValueArray_		GnmValueArray;
typedef struct GnmValueBool_		GnmValueBool;
typedef struct GnmValueErr_		GnmValueErr;
typedef struct GnmValueFloat_		GnmValueFloat;
typedef struct GnmValueRange_		GnmValueRange;
typedef struct GnmValueStr_		GnmValueStr;
typedef struct GnmPrintInformation_     GnmPrintInformation;
typedef struct Sheet_			Sheet;
typedef struct SheetControl_		SheetControl;
typedef struct SheetControlGUI_		SheetControlGUI;
typedef struct SheetObject_		SheetObject;
typedef struct SheetObjectAnchor_	SheetObjectAnchor;
typedef struct SheetObjectExportable_   SheetObjectExportable;
typedef struct SheetObjectImageable_    SheetObjectImageable;
typedef struct SheetObjectView_		SheetObjectView;
typedef struct SheetObjectViewContainer_ SheetObjectViewContainer;
typedef struct SheetView_		SheetView;
typedef struct WBCGtk_			WBCGtk;
typedef struct Workbook_		Workbook;
typedef struct WorkbookControl_		WorkbookControl;
typedef struct WorkbookSheetState_	WorkbookSheetState;
typedef struct WorkbookView_		WorkbookView;
typedef union  GnmExpr_			GnmExpr;
typedef union  GnmValue_		GnmValue;
typedef struct GnmGenericToolState_	GnmGenericToolState;
typedef struct GnmAnalysisTool_         GnmAnalysisTool;
typedef struct GnmExprDeriv_            GnmExprDeriv;
typedef struct data_analysis_output_t_  data_analysis_output_t;

typedef GList				ColRowIndexList;
typedef GSList				ColRowStateGroup;
typedef GSList				ColRowStateList;
typedef GSList				ColRowVisList;
typedef GSList				GnmExprList;
typedef GSList				GnmStyleList;
typedef GnmExpr const *			GnmExprConstPtr;

G_END_DECLS

#endif
