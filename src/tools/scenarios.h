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


void        scenario_free_all     (GList *list);
GList      *scenario_copy_all     (GList *list, Sheet *new);

void        scenario_insert_rows  (GList *list, int row, int count);
void        scenario_insert_cols  (GList *list, int row, int count);
void        scenario_delete_rows  (GList *list, int row, int count);
void        scenario_delete_cols  (GList *list, int row, int count);

void        scenario_manager_ok   (Sheet *sheet, scenario_t *old_values);
scenario_t *scenario_show         (WorkbookControl        *wbc,
				   const gchar            *name,
				   scenario_t             *old_values,
				   data_analysis_output_t *dao);
gboolean    scenario_add_new      (gchar *name,
				   Value *changing_cells,
				   gchar *cell_sel_str,
				   gchar *comment,
				   Sheet *sheet);
gboolean    scenario_mark_deleted (GList *scenarios, gchar *name);
void        scenario_summary      (WorkbookControl        *wbc,
				   Sheet                  *sheet,
				   Value                  *results,
				   Sheet                  **new_sheet);

#endif
