#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

typedef struct {
	GtkObject parent;
} DataSource;

typedef struct {
	GtkObjectClass parent_class;
	
	Value    (*get_value)(DataSource *source, const char *spec, int pos);
	gboolean (*set_value)(DataSource *source, const char *spec, int pos, Value *value);
} DataSourceClass;

GtkType    data_source_get_type  (void);
Value     *data_source_get_value (DataSource *source, const char *spec, int pos);
DataResult data_source_set_value (DataSource *source, const char *spec, int pos, Value *value);

#endif /* DATA_SOURCE_H */
