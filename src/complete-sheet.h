#ifndef GNUMERIC_COMPLETE_SHEET_H
#define GNUMERIC_COMPLETE_SHEET_H

#include "gnumeric.h"
#include "complete.h"

#define COMPLETE_SHEET_TYPE        (complete_sheet_get_type ())
#define COMPLETE_SHEET(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), COMPLETE_SHEET_TYPE, CompleteSheet))
#define COMPLETE_SHEET_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), COMPLETE_SHEET_TYPE, CompleteSheetClass))
#define IS_COMPLETE_SHEET(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), COMPLETE_SHEET_TYPE))
#define IS_COMPLETE_SHEET_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), COMPLETE_SHEET_TYPE))

typedef struct {
	Complete parent;

	/* Cell being entered into.  */
	Sheet *sheet;
	GnmCellPos entry;

	/* Where we are searching.  */
	GnmCellPos current;
	GnmCell *cell;

	char  *current_text;
} CompleteSheet;

typedef struct {
	CompleteClass parent_class;
} CompleteSheetClass;

GType     complete_sheet_get_type (void);
Complete *complete_sheet_new      (Sheet *sheet, int col, int row,
				   CompleteMatchNotifyFn notify,
				   void *notify_closure);

#endif /* GNUMERIC_COMPLETE_H */
