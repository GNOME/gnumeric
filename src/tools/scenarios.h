#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <tools/dao.h>

typedef struct GnmScenario_ {
        Sheet *sheet;
        gchar *name;

        GnmValue **changing_cells;
        GnmRange  range;

        gchar *comment;
        gchar *cell_sel_str;

        gboolean marked_deleted;
} GnmScenario;

typedef struct _scenario_cmd_t {
        GnmScenario *redo;
        GnmScenario *undo;
} scenario_cmd_t;

GnmScenario *scenario_by_name      (GList *scenarios, const gchar *name,
				   gboolean *all_deleted);
void        scenario_free         (GnmScenario *s);

GList      *scenarios_dup	  (GList *list, Sheet *dst);
void        scenarios_free	  (GList *list);

void        scenario_manager_ok   (Sheet *sheet);
GnmScenario *scenario_show         (WorkbookControl        *wbc,
				   GnmScenario             *scenario,
				   GnmScenario             *old_values,
				   data_analysis_output_t *dao);
gboolean    scenario_add_new      (gchar const *name,
				   GnmValue *changing_cells,
				   gchar const *cell_sel_str,
				   gchar const *comment,
				   Sheet *sheet, GnmScenario **new_scenario);
void        scenario_add          (Sheet *sheet, GnmScenario *scenario);
gboolean    scenario_mark_deleted (GList *scenarios, gchar *name);
GList      *scenario_delete       (GList *scenarios, gchar *name);
GnmScenario *scenario_copy         (GnmScenario *s, Sheet *new_sheet);
void        scenario_summary      (WorkbookControl        *wbc,
				   Sheet                  *sheet,
				   GSList                 *results,
				   Sheet                  **new_sheet);
void        scenario_recover_all  (GList *scenarios);

#endif
