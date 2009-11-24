#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <tools/dao.h>

/* ------------------------------------------------------------------------- */

#define GNM_SCENARIO_TYPE        (gnm_scenario_get_type ())
#define GNM_SCENARIO(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SCENARIO_TYPE, GnmScenario))
#define GNM_SCENARIO_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SCENARIO_TYPE, GnmScenarioClass))
#define GNM_IS_SCENARIO(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SCENARIO_TYPE))

struct GnmScenario_ {
	GObject parent;

        Sheet *sheet;
        char *name;
        char *comment;

        GnmValue **changing_cells;
        GnmRange  range;

        gchar *cell_sel_str;
};

typedef struct {
	GObjectClass parent_class;
} GnmScenarioClass;

GType gnm_scenario_get_type (void);

GnmScenario *gnm_scenario_new (char const *name, char const *comment,
			       Sheet *sheet);

GnmScenario *gnm_scenario_dup (GnmScenario *s, Sheet *new_sheet);

/* ------------------------------------------------------------------------- */



typedef struct _scenario_cmd_t {
        GnmScenario *redo;
        GnmScenario *undo;
} scenario_cmd_t;

GnmScenario *scenario_by_name      (GList *scenarios, const gchar *name,
				   gboolean *all_deleted);

GnmScenario *scenario_show         (GnmScenario             *scenario,
				    GnmScenario             *old_values,
				    data_analysis_output_t *dao);
gboolean    scenario_add_new      (gchar const *name,
				   GnmValue *changing_cells,
				   gchar const *cell_sel_str,
				   gchar const *comment,
				   Sheet *sheet, GnmScenario **new_scenario);

#endif
