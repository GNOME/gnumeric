/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_NUMBER_MATCH_H_
# define _GNM_NUMBER_MATCH_H_

#include "gnumeric.h"

G_BEGIN_DECLS

GnmValue   *format_match_simple (char const *text);
GnmValue   *format_match        (char const *text, GOFormat const *cur_fmt,
				 GODateConventions const *date_conv);
GnmValue   *format_match_number (char const *text, GOFormat const *cur_fmt,
				 GODateConventions const *date_conv);
GnmValue   *format_match_decimal_number_with_locale
                                (char const *text, GOFormatFamily *family,
				 GString const *curr, GString const *thousand,
				 GString const *decimal);

G_END_DECLS

#endif /* _GNM_NUMBER_MATCH_H_ */
