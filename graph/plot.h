#ifndef GRAPH_PLOT_H
#define GRAPH_PLOT_H

#include "reference.h"
#include "series.h"

typedef struct {
	DataSeries   *series;
} PlotState;

PlotState    *plot_state_new (void);

#endif /* GRAPH_PLOT_H */
