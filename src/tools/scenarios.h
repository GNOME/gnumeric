#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <tools/dao.h>

typedef struct _scenario_t {
        Sheet *sheet;
        gchar *name;

        GnmValue **changing_cells;
        GnmRange  range;

        gchar *comment;
        gchar *cell_sel_str;

        gboolean marked_deleted;
} scenario_t;

typedef struct _scenario_cmd_t {
        scenario_t *redo;
        scenario_t *undo;
} scenario_cmd_t;

scenario_t *scenario_by_name      (GList *scenarios, const gchar *name,
				   gboolean *all_deleted);
void        scenario_free         (scenario_t *s);

GList      *scenarios_dup	  (GList *list, Sheet *dst);
void        scenarios_free	  (GList *list);
void        scenarios_insert_rows (GList *list, int row, int count);
void        scenarios_insert_cols (GList *list, int row, int count);
void        scenarios_delete_rows (GList *list, int row, int count);
void        scenarios_delete_cols (GList *list, int row, int count);
void        scenarios_move_range  (GList *list, GnmRange const *origin,
				   int col_offset, int row_offset);

void        scenario_manager_ok   (Sheet *sheet);
scenario_t *scenario_show         (WorkbookControl        *wbc,
				   scenario_t             *scenario,
				   scenario_t             *old_values,
				   data_analysis_output_t *dao);
gboolean    scenario_add_new      (gchar const *name,
				   GnmValue *changing_cells,
				   gchar const *cell_sel_str,
				   gchar const *comment,
				   Sheet *sheet, scenario_t **new_scenario);
void        scenario_add          (Sheet *sheet, scenario_t *scenario);
gboolean    scenario_mark_deleted (GList *scenarios, gchar *name);
GList      *scenario_delete       (GList *scenarios, gchar *name);
scenario_t *scenario_copy         (scenario_t *s, Sheet *new_sheet);
void        scenario_summary      (WorkbookControl        *wbc,
				   Sheet                  *sheet,
				   GSList                 *results,
				   Sheet                  **new_sheet);
void        scenario_recover_all  (GList *scenarios);

#endif
