#ifndef GNM_TOOLS_DATA_SHUFFLING_H_
#define GNM_TOOLS_DATA_SHUFFLING_H_

#include <gnumeric.h>

typedef enum {
	GNM_DATA_SHUFFLE_COLS,
	GNM_DATA_SHUFFLE_ROWS,
	GNM_DATA_SHUFFLE_AREA
} GnmDataShuffleType;

typedef struct _data_shuffling_t {
	GSList  *changes;
	int     a_col;
	int     b_col;
	int     a_row;
	int     b_row;
	int     cols;
	int     rows;
        GnmDataShuffleType  type;

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
				       GnmDataShuffleType     shuffling_type);

#endif
