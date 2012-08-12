#ifndef GOFFICE_DUMMY_DATA_H
#define GOFFICE_DUMMY_DATA_H

#include <glib-object.h>

G_BEGIN_DECLS
#ifndef GOFFICE_NAMESPACE_DISABLE

/* DOES NOT BELONG HERE */
typedef struct _GODataCache		GODataCache;
typedef struct _GODataCacheField	GODataCacheField;
typedef struct _GODataCacheSource	GODataCacheSource;

typedef struct _GODataSlicer		GODataSlicer;
typedef struct _GODataSlicerField	GODataSlicerField;

typedef enum {
	GDS_FIELD_TYPE_UNSET    = -1,
	GDS_FIELD_TYPE_PAGE	=  0,
	GDS_FIELD_TYPE_ROW	=  1,
	GDS_FIELD_TYPE_COL	=  2,
	GDS_FIELD_TYPE_DATA	=  3,
	GDS_FIELD_TYPE_MAX
} GODataSlicerFieldType;

typedef enum {
	GO_AGGREGATE_AUTO,	/* automatically select sum vs count */

	GO_AGGREGATE_BY_MIN,
	GO_AGGREGATE_BY_MAX,
	GO_AGGREGATE_BY_SUM,
	GO_AGGREGATE_BY_PRODUCT,
	GO_AGGREGATE_BY_COUNT,	/* only numeric */
	GO_AGGREGATE_BY_COUNTA,	/* non-null */
	/* GO_AGGREGATE_BY_COUNT_... more fine tuning ? */
	GO_AGGREGATE_BY_AVERAGE,
	GO_AGGREGATE_BY_STDDEV,
	GO_AGGREGATE_BY_STDDEVP,
	GO_AGGREGATE_BY_VAR,
	GO_AGGREGATE_BY_VARP
} GOAggregateBy;
#endif
G_END_DECLS

#endif /* GOFFICE_DUMMY_DATA_H */
