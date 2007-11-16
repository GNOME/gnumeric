/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_STR_H_
# define _GNM_STR_H_

#include "gnumeric.h"

G_BEGIN_DECLS

struct _GnmString {
	int        ref_count;
	char       *str;
};

void gnm_string_init     (void);
void gnm_string_shutdown (void);
void gnm_string_dump     (void);

GnmString *gnm_string_get        (char const *s);
GnmString *gnm_string_get_nocopy (char *s);
GnmString *gnm_string_ref        (GnmString *str);
void       gnm_string_unref      (GnmString *str);

GnmString *gnm_string_concat     (GnmString const *a, GnmString const *b);
GnmString *gnm_string_concat_str (GnmString const *a, char const *b);

G_END_DECLS

#endif /* _GNM_STR_H_ */
