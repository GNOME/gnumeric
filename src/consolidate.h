#ifndef GNM_CONSOLIDATE_H_
#define GNM_CONSOLIDATE_H_

#include <gnumeric.h>
#include <tools/dao.h>
#include <tools/analysis-tools.h>

G_BEGIN_DECLS

typedef enum {
	/*
	 * These can be both set, both unset or
	 * one of them can be set. Indicates
	 * what sort of consolidation we will
	 * execute
	 */
	CONSOLIDATE_ROW_LABELS   = 1 << 0,
	CONSOLIDATE_COL_LABELS   = 1 << 1,

	/*
	 * If set the row and/or column labels
	 * will be copied to the destination area
	 */
	CONSOLIDATE_COPY_LABELS  = 1 << 2,

	/* If this is set we put the outcome
	 * of our formulas into the destination
	 * otherwise we put formulas
	 */
	CONSOLIDATE_PUT_VALUES   = 1 << 3
} GnmConsolidateMode;

struct GnmConsolidate_ {
	GnmFunc *fd;

	GSList      *src;

	GnmConsolidateMode mode;

	/* <private> */
	unsigned ref_count;     /* boxed type */
};

GType        gnm_consolidate_get_type (void);
GnmConsolidate *gnm_consolidate_new  (void);
void         gnm_consolidate_free (GnmConsolidate *cs, gboolean content_only);

void         gnm_consolidate_set_function    (GnmConsolidate *cs, GnmFunc *fd);
void         gnm_consolidate_set_mode        (GnmConsolidate *cs,
					  GnmConsolidateMode mode);

gboolean     gnm_consolidate_add_source      (GnmConsolidate *cs, GnmValue *range);
gboolean     gnm_consolidate_check_destination (GnmConsolidate *cs,
					    data_analysis_output_t *dao);

#define GNM_TYPE_CONSOLIDATE_TOOL (gnm_consolidate_tool_get_type ())
GType gnm_consolidate_tool_get_type (void);
typedef struct _GnmConsolidateTool GnmConsolidateTool;
typedef struct _GnmConsolidateToolClass GnmConsolidateToolClass;

struct _GnmConsolidateTool {
	GnmAnalysisTool parent;
	GnmConsolidate *cs;
};

struct _GnmConsolidateToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_CONSOLIDATE_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_CONSOLIDATE_TOOL, GnmConsolidateTool))
#define GNM_IS_CONSOLIDATE_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_CONSOLIDATE_TOOL))

GnmAnalysisTool *gnm_consolidate_tool_new (GnmConsolidate *cs);

G_END_DECLS

#endif /* GNM_CONSOLIDATE_H_ */
