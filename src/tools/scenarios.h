#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <dao.h>


typedef struct {
        Sheet *sheet;
        gchar *name;

        Value **changing_cells;
        Range range;

        gchar *comment;
        gchar *cell_sel_str;

        gboolean marked_deleted;
} scenario_t;

typedef struct {
        scenario_t *redo;
        scenario_t *undo;
} scenario_cmd_t;


scenario_t *scenario_by_name      (GList *scenarios, const gchar *name,
				   gboolean *all_deleted);
void        scenario_free_all     (GList *list);
void        scenario_free         (scenario_t *s);
GList      *scenario_copy_all     (GList *list, Sheet *new);

void        scenario_insert_rows  (GList *list, int row, int count);
void        scenario_insert_cols  (GList *list, int row, int count);
void        scenario_delete_rows  (GList *list, int row, int count);
void        scenario_delete_cols  (GList *list, int row, int count);
void        scenario_move_range   (GList *list, const Range *origin,
				   int col_offset, int row_offset);

void        scenario_manager_ok   (Sheet *sheet);
scenario_t *scenario_show         (WorkbookControl        *wbc,
				   scenario_t             *scenario,
				   scenario_t             *old_values,
				   data_analysis_output_t *dao);
gboolean    scenario_add_new      (gchar *name,
				   Value *changing_cells,
				   gchar *cell_sel_str,
				   gchar *comment,
				   Sheet *sheet, scenario_t **new_scenario);
void        scenario_add          (Sheet *sheet, scenario_t *scenario);
gboolean    scenario_mark_deleted (GList *scenarios, gchar *name);
GList      *scenario_delete       (GList *scenarios, gchar *name);
scenario_t *scenario_copy         (scenario_t *s, Sheet *new_sheet);
void        scenario_summary      (WorkbookControl        *wbc,
				   Sheet                  *sheet,
				   Value                  *results,
				   Sheet                  **new_sheet);

#endif
