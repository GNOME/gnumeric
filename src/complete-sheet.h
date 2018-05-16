#ifndef _GNM_COMPLETE_SHEET_H_
# define _GNM_COMPLETE_SHEET_H_

#include <gnumeric.h>
#include <complete.h>

G_BEGIN_DECLS

#define GNM_COMPLETE_SHEET_TYPE        (gnm_complete_sheet_get_type ())
#define GNM_COMPLETE_SHEET(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_COMPLETE_SHEET_TYPE, GnmCompleteSheet))
#define GNM_IS_COMPLETE_SHEET(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_COMPLETE_SHEET_TYPE))

typedef struct {
	GnmComplete parent;

	/* Cell being entered into.  */
	Sheet *sheet;
	GnmCellPos entry;

	/* Where we are searching.  */
	GnmCellPos current;
	GnmCell *cell;

	char  *current_text;
} GnmCompleteSheet;

typedef struct {
	GnmCompleteClass parent_class;
} GnmCompleteSheetClass;

GType     gnm_complete_sheet_get_type (void);
GnmComplete *gnm_complete_sheet_new   (Sheet *sheet, int col, int row,
				       GnmCompleteMatchNotifyFn notify,
				       void *notify_closure);

G_END_DECLS

#endif /* _GNM_COMPLETE_SHEET_H_ */
