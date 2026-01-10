#ifndef GNM_STF_H_
# define GNM_STF_H_

#include <gnumeric.h>

G_BEGIN_DECLS

void stf_init (void);
void stf_shutdown (void);
void stf_text_to_columns (WorkbookControl *wbc, GOCmdContext *cc);

G_END_DECLS

#endif /* GNM_STF_H_ */
