/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_NUMBER_MATCH_H_
# define _GNM_NUMBER_MATCH_H_

#include "gnumeric.h"

G_BEGIN_DECLS

GnmValue   *format_match_simple (char const *s);
GnmValue   *format_match        (char const *s, GOFormat *cur_fmt,
				 GODateConventions const *date_conv);
GnmValue   *format_match_number (char const *s, GOFormat *cur_fmt,
				 GODateConventions const *date_conv);

G_END_DECLS

#endif /* _GNM_NUMBER_MATCH_H_ */
