#ifndef __SCENARIOS_H__
#define __SCENARIOS_H__

#include <gnumeric.h>
#include <dao.h>


typedef struct {
        gchar *name;

        Value **changing_cells;
        Range range;

        gchar *comment;
        gchar *cell_sel_str;
} scenario_t;


void   scenario_free_all (GList *list);
GList *scenario_copy_all (GList *list, Sheet *new);

void scenarios_ok (WorkbookControl        *wbc, data_analysis_output_t *dao);

void scenario_show (WorkbookControl        *wbc,
		    gchar                  *name,
		    data_analysis_output_t *dao);

void scenario_add_new (WorkbookControl        *wbc,
		       gchar                  *name,
		       Value                  *changing_cells,
		       gchar                  *cell_sel_str,
		       gchar                  *comment,
		       data_analysis_output_t *dao);

void scenario_delete (WorkbookControl        *wbc,
		      gchar                  *name,
		      data_analysis_output_t *dao);

void scenario_summary (WorkbookControl        *wbc,
		       data_analysis_output_t *dao);

#endif
