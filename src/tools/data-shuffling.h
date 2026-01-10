#ifndef GNM_TOOLS_DATA_SHUFFLING_H_
#define GNM_TOOLS_DATA_SHUFFLING_H_

#include <gnumeric.h>
#include <tools/dao.h>


#define SHUFFLE_COLS  0
#define SHUFFLE_ROWS  1
#define SHUFFLE_AREA  2


typedef struct _data_shuffling_t {
	GSList  *changes;
	int     a_col;
	int     b_col;
	int     a_row;
	int     b_row;
	int     cols;
	int     rows;
        int     type;

        WorkbookControl *wbc;
	data_analysis_output_t *dao;
	Sheet                  *sheet;

        GnmRange tmp_area;
} data_shuffling_t;


void              data_shuffling_redo (data_shuffling_t *st);
void              data_shuffling_free (data_shuffling_t *st);
data_shuffling_t *data_shuffling      (WorkbookControl        *wbc,
				       data_analysis_output_t *dao,
				       Sheet                  *sheet,
				       GnmValue               *input,
				       int                    shuffling_type);

#endif
