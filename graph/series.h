#ifndef GRAPH_SERIES_H
#define GRAPH_SERIES_H

#include "reference.h"

typedef struct _SeriesName SeriesName;

SeriesName *series_name_new             (void);
void        series_name_set_from_ref    (SeriesName *sn, Reference *ref);
void        series_name_set_from_string (SeriesName *sn, const char *str);
char       *series_get_string           (SeriesName *sn);
void        series_name_destory         (SeriesName *sn);

typedef struct _Series Series;

Series     *series_new                  (DataSource *source, const char *name_spec);
void        series_set_name             (Series *series, SeriesName *series_name);
void        series_set_source           (Series *series, DataSource *source, const char *name_spec);
void        series_destroy              (Series *series);

SeriesName *series_get_series_name      (Series *series);
char       *series_get_name             (Series *series);

#endif
