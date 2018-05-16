#ifndef _GNM_SHEET_AUTOFILL_H_
# define _GNM_SHEET_AUTOFILL_H_

#include <gnumeric.h>

G_BEGIN_DECLS

void gnm_autofill_init  (void);
void gnm_autofill_shutdown  (void);

void gnm_autofill_fill (Sheet *sheet, gboolean default_increment,
			int base_col, int base_row,
			int w,        int h,
			int end_col,  int end_row);

GString *gnm_autofill_hint (Sheet *sheet, gboolean default_increment,
			    int base_col, int base_row,
			    int w,        int h,
			    int end_col,  int end_row);

G_END_DECLS

#endif /* _GNM_SHEET_AUTOFILL_H_ */
