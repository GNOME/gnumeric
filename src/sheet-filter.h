/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_FILTER_H
#define GNUMERIC_FILTER_H

#include "gnumeric.h"

GnmFilter *gnm_filter_new	(Sheet *sheet, Range const *r);
void	   gnm_filter_free	(GnmFilter *filter);
void	   gnm_filter_remove	(GnmFilter *filter);

gboolean   gnm_filter_contains_row (GnmFilter const *filter, int row);

#endif /* GNUMERIC_FILTER_H */
