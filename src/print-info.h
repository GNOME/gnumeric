#ifndef PRINT_INFO_H
#define PRINT_INFO_H

#include "gnumeric.h"
#include <libgnomeprint/gnome-print-paper.h>	/* for typedef of GnomePrintPaper */
#include <libgnomeprint/gnome-print-config.h>	/* for typedef of GnomePrintPaper */

typedef enum {
	PRINT_ORIENT_HORIZONTAL,
	PRINT_ORIENT_VERTICAL,
	PRINT_ORIENT_HORIZONTAL_UPSIDE_DOWN,
	PRINT_ORIENT_VERTICAL_UPSIDE_DOWN
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

	/* We store separate x and y scales internally, for the
	 * 'fit-to' printing feature. (They are calculated at print-time)
	 * When the user is doing the simple scaling, both these values
	 * will be equal.
	 */
	struct {
		double x;
		double y;
	} percentage;

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
	double left, right, header, footer;
} PrintMargins;

/* Header/Footer definition */
typedef struct {
	char *left_format;
	char *middle_format;
	char *right_format;
} PrintHF;

typedef struct {
	gboolean use;
	GnmRange range;
} PrintRepeatRange;

struct _PrintInformation {
	PrintScaling     scaling;
	PrintMargins     margins;
	PrintRepeatRange repeat_top, repeat_left;
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

	PrintOrientation  orientation;
	int		  n_copies;
	char		 *gp_config_str;
	char		 *paper;
};

typedef enum {
	HF_RENDER_PRINT
} HFRenderType;

typedef struct {
	Sheet const *sheet;
	int       page;
	int       pages;
	GnmValue *date_time;
} HFRenderInfo;

PrintInformation *print_info_new         (void);
PrintInformation *print_info_dup	 (PrintInformation const *pi);
void              print_info_free        (PrintInformation *pi);
void              print_info_save        (PrintInformation const *pi);
GnomePrintConfig *print_info_make_config (PrintInformation const *pi);
void		  print_info_load_config (PrintInformation *pi, GnomePrintConfig *config);

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

void        print_info_set_n_copies  (PrintInformation *pi, int copies);
guint	    print_info_get_n_copies  (PrintInformation const *pi);
void	    print_info_set_paper     (PrintInformation *pi, char const *paper);
char const *print_info_get_paper     (PrintInformation const *pi);
void        print_info_get_margins   (PrintInformation const *pi,
				      double *header, double *footer, double *left, double *right);
void        print_info_set_margins   (PrintInformation *pi,
				      double header, double footer, double left, double right);
void        print_info_set_margin_header (PrintInformation *pi, double header);
void        print_info_set_margin_footer (PrintInformation *pi, double footer);
void        print_info_set_margin_left   (PrintInformation *pi, double left);
void        print_info_set_margin_right  (PrintInformation *pi, double right);
void        print_info_set_orientation   (PrintInformation *pi, 
					  PrintOrientation orient); 
PrintOrientation print_info_get_orientation (PrintInformation const *pi); 

/* Formats known */
extern GList *hf_formats;

#endif
