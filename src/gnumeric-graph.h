#ifndef GNUMERIC_GRAPH_VECTOR_H
#define GNUMERIC_GRAPH_VECTOR_H

#include "gnumeric.h"
#include <orb/orbit_object.h>

GraphVector *graph_vector_new    (Sheet *sheet, Range const *r, char *name);
void graph_vector_set_subscriber (GraphVector *vector, CORBA_Object manager);
void graph_vector_unsubscribe    (GraphVector *vector);

#endif /* GNUMERIC_GRAPH_VECTOR_H */
