#ifndef PRINT_INFO_H
#define PRINT_INFO_H

#include "gnumeric.h"
#include <libgnomeprint/gnome-print-paper.h>	/* for typedef of GnomePrintPaper */
#include <libgnomeprint/gnome-print-config.h>	/* for typedef of GnomePrintPaper */

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

typedef struct {
	double    points;
	const GnomePrintUnit *desired_display;
} PrintUnit;

/*
 * Margins.  In Points
 */
typedef struct {
	PrintUnit top;     /* see print.c for the definition (these are header/footer) */
	PrintUnit bottom;
} PrintMargins;

/* Header/Footer definition */
typedef struct {
	char *left_format;
	char *middle_format;
	char *right_format;
} PrintHF;

typedef struct {
	gboolean use;
	Range range;
} PrintRepeatRange;

struct _PrintInformation {
	PrintOrientation orientation;
	PrintScaling     scaling;
	PrintMargins     margins;
	PrintRepeatRange repeat_top, repeat_left;
	int	         n_copies;
	unsigned int     center_vertically:1;
	unsigned int     center_horizontally:1;

	unsigned int     print_grid_lines:1;
	unsigned int     print_even_if_only_styles:1;
	unsigned int     print_black_and_white:1;
	unsigned int     print_as_draft:1;
	unsigned int     print_comments:1;
	unsigned int     print_titles:1;

	enum {
		PRINT_ORDER_DOWN_THEN_RIGHT,
		PRINT_ORDER_RIGHT_THEN_DOWN
	}                print_order;

	PrintHF          *header;
	PrintHF          *footer;

	GnomePrintConfig *print_config;
};

typedef enum {
	HF_RENDER_PRINT,
	HF_RENDER_TO_ENGLISH,
	HF_RENDER_TO_LOCALE
} HFRenderType;

typedef struct {
	Sheet const *sheet;
	int       page;
	int       pages;
	Value     *date_time;
} HFRenderInfo;

PrintInformation *print_info_new         (void);
PrintInformation *print_info_dup	 (PrintInformation const *pi);
void              print_info_free        (PrintInformation *pi);
void              print_info_save        (PrintInformation const *pi);

PrintHF          *print_hf_new           (char const *left,
					  char const *middle,
				          char const *right);
void              print_hf_free          (PrintHF *print_hf);
PrintHF          *print_hf_copy          (PrintHF const *source);
PrintHF          *print_hf_register      (PrintHF *hf);
gboolean          print_hf_same          (PrintHF const *a, PrintHF const *b);

char             *hf_format_render       (char const *format,
					  HFRenderInfo *info,
					  HFRenderType render_type);
HFRenderInfo     *hf_render_info_new     (void);
void              hf_render_info_destroy (HFRenderInfo *hfi);

const GnomePrintUnit *unit_name_to_unit    (const char *name);
double      unit_convert             (double value,
				      const GnomePrintUnit *source,
				      const GnomePrintUnit *target);

void        print_init               (void);
void        print_shutdown           (void);

gboolean    print_info_get_margins   (PrintInformation const *pi, 
				      double *header, double *footer, double *left, double *right);
void        print_info_set_margins   (PrintInformation *pi, 
				      double header, double footer, double left, double right);
void        print_info_set_margin_header   (PrintInformation *pi, double header);
void        print_info_set_margin_footer   (PrintInformation *pi, double footer);
void        print_info_set_margin_left     (PrintInformation *pi, double left);
void        print_info_set_margin_right    (PrintInformation *pi, double right);

/* Formats known */
extern GList *hf_formats;

#endif
