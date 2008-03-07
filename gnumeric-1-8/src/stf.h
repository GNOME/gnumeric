/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STF_H_
# define _GNM_STF_H_

#include <gnumeric.h>

G_BEGIN_DECLS

void stf_init (void);
void stf_shutdown (void);
void stf_text_to_columns (WorkbookControl *wbc, GOCmdContext *cc);

G_END_DECLS

#endif /* _GNM_STF_H_ */
