#ifndef GNUMERIC_GRAPH_SERIES_H
#define GNUMERIC_GRAPH_SERIES_H

#include "gnumeric.h"
#include <orb/orbit_object.h>

typedef struct _GraphSeries GraphSeries;

GraphSeries *graph_series_new (Sheet *sheet, Range const *r);
CORBA_Object graph_series_servant (GraphSeries const *series);
void graph_series_set_subscriber (GraphSeries *series, CORBA_Object obj);

#endif /* GNUMERIC_GRAPH_SERIES_H */
