#ifndef GNM_RANDOM_GENERATOR_COR_H_
#define GNM_RANDOM_GENERATOR_COR_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

typedef enum {
	random_gen_cor_type_cov = 0,
	random_gen_cor_type_cholesky,
} random_gen_cor_type_t;

typedef struct {
	WorkbookControl *wbc;
	GnmValue        *matrix;
	random_gen_cor_type_t matrix_type;
	gint count;
	gint variables;
} tools_data_random_cor_t;

#define GNM_TYPE_RANDOM_COR_TOOL (gnm_random_cor_tool_get_type ())
GType gnm_random_cor_tool_get_type (void);
typedef struct _GnmRandomCorTool GnmRandomCorTool;
typedef struct _GnmRandomCorToolClass GnmRandomCorToolClass;

struct _GnmRandomCorTool {
	GnmAnalysisTool parent;
	tools_data_random_cor_t data;
};

struct _GnmRandomCorToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_RANDOM_COR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_RANDOM_COR_TOOL, GnmRandomCorTool))
#define GNM_IS_RANDOM_COR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_RANDOM_COR_TOOL))

GnmAnalysisTool *gnm_random_cor_tool_new (void);

#endif
