#ifndef __DATA_SHUFFLING_H__
#define __DATA_SHUFFLING_H__

#include <gnumeric.h>
#include <dao.h>


#define SHUFFLE_COLS  0
#define SHUFFLE_ROWS  1
#define SHUFFLE_AREA  2


void data_shuffling (WorkbookControl        *wbc,
		     data_analysis_output_t *dao,
		     Sheet                  *sheet,
		     Value                  *input,
		     int                    shuffling_type);

#endif
