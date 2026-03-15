#ifndef GNM_TOOLS_DATA_SHUFFLING_H_
#define GNM_TOOLS_DATA_SHUFFLING_H_

#include <gnumeric.h>

#include <glib-object.h>

#define GNM_DATA_SHUFFLE_TYPE (gnm_data_shuffle_get_type ())
G_DECLARE_FINAL_TYPE (GnmDataShuffle, gnm_data_shuffle, GNM, DATA_SHUFFLE, GObject)

typedef enum {
	GNM_DATA_SHUFFLE_COLS,
	GNM_DATA_SHUFFLE_ROWS,
	GNM_DATA_SHUFFLE_AREA
} GnmDataShuffleType;

struct _GnmDataShuffle {
	GObject parent;
	GSList  *changes;
	int     a_col;
	int     b_col;
	int     a_row;
	int     b_row;
	int     cols;
	int     rows;
        GnmDataShuffleType  type;

	data_analysis_output_t *dao;
	Sheet                  *sheet;

        GnmRange tmp_area;
};


void              gnm_data_shuffle_redo (GnmDataShuffle *st, WorkbookControl *wbc);
GnmDataShuffle   *gnm_data_shuffle_new  (data_analysis_output_t *dao,
					 Sheet                  *sheet,
					 GnmValue const         *input_range,
					 GnmDataShuffleType     shuffling_type);

#endif
