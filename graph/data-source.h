#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <gtk/gtkobject.h>
#include "src/value.h"

typedef struct {
	GtkObject parent;
} DataSource;

typedef struct {
	GtkObjectClass parent_class;
	
	Value    *(*get_value)(DataSource *source, const char *spec, int pos);
	gboolean  (*set_value)(DataSource *source, const char *spec, int pos, Value *value);
	int       (*len)      (DataSource *source, const char *spec);
} DataSourceClass;

typedef enum {
	DATA_SET_OK,
	DATA_SET_ERROR
} DataResult;

GtkType    data_source_get_type  (void);
Value     *data_source_get_value (DataSource *source, const char *spec, int pos);
gboolean   data_source_set_value (DataSource *source, const char *spec, int pos, Value *value);

#endif /* DATA_SOURCE_H */
