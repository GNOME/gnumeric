#ifndef GNUMERIC_COMPLETE_SHEET_H
#define GNUMERIC_COMPLETE_SHEET_H

#include "complete.h"

#define COMPLETE_SHEET_TYPE        (complete_sheet_get_type ())
#define COMPLETE_SHEET(o)          (GTK_CHECK_CAST ((o), COMPLETE_SHEET_TYPE, CompleteSheet))
#define COMPLETE_SHEET_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), COMPLETE_SHEET_TYPE, CompleteSheetClass))
#define IS_COMPLETE_SHEET(o)       (GTK_CHECK_TYPE ((o), COMPLETE_SHEET_TYPE))
#define IS_COMPLETE_SHEET_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), COMPLETE_SHEET_TYPE))

typedef struct {
	Complete parent;

	char  *current;
	Sheet *sheet;
	int    col, row;
	int    inf, sup;
} CompleteSheet;

typedef struct {
	CompleteClass parent_class;
} CompleteSheetClass;

GtkType   complete_sheet_get_type (void);
Complete *complete_sheet_new      (Sheet *sheet, int col, int row,
				   CompleteMatchNotifyFn notify,
				   void *notify_closure);
#endif /* GNUMERIC_COMPLETE_H */
