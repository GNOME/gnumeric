#ifndef PRINT_INFO_H
#define PRINT_INFO_H

#include "sheet.h"

#define METERS_TO_POINTS(x) (x * 2834.6456692) / 100

typedef enum {
	PRINT_ORIENT_HORIZONTAL,
	PRINT_ORIENT_VERTICAL
} PrintOrientation;

/*
 * Scaling for the sheet: percentage or make it fit a number
 * of columns and rows
 */
typedef struct {
	enum {
		PERCENTAGE,
		SIZE_FIT
	} type;

	double percentage;
	struct {
		int cols;
		int rows;
	} dim;
} PrintScaling;

/*
 * Margins.  In Points
 */
typedef struct {
	double top;
	double bottom;
	double left;
	double right;
	double header;
	double footer;
} PrintMargins;

/* Header/Footer definition */
typedef struct {
	char *left_format;
	char *middle_format;
	char *right_format;
} PrintHF;

struct _PrintInformation {
	PrintOrientation orientation;
	PrintScaling     scaling;
	PrintMargins     margins;
	
	unsigned int     center_vertically:1;
	unsigned int     center_horizontally:1;
	
	unsigned int     print_line_divisions:1;
	unsigned int     print_black_and_white:1;
	unsigned int     print_as_draft:1;
	unsigned int     print_titles:1;
	unsigned int     print_order:1;
	
	PrintHF          *header;
	PrintHF          *footer;

	void             *print_config_dialog_data;
};

PrintInformation *print_info_new  (void);
void              print_info_free (PrintInformation *pi);

PrintHF          *print_hf_new    (char *left_side_format,
				   char *middle_format,
				   char *right_side_format);
void              print_hf_free   (PrintHF *print_hf);

#endif

