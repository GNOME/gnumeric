#ifndef PRINT_INFO_H
#define PRINT_INFO_H

#include "gnumeric.h"
#include <libgnome/gnome-paper.h>	/* for typedef of GnomePaper */

typedef enum {
	PRINT_ORIENT_HORIZONTAL,
	PRINT_ORIENT_VERTICAL
} PrintOrientation;

typedef enum {
	UNIT_POINTS,
	UNIT_MILLIMETER,
	UNIT_CENTIMETER,
	UNIT_INCH
} UnitName;

#define UNIT_LAST (UNIT_INCH+1)

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
	UnitName desired_display;
} PrintUnit;

/*
 * Margins.  In Points
 */
typedef struct {
	PrintUnit top;
	PrintUnit bottom;
	PrintUnit left;
	PrintUnit right;
	PrintUnit header;
	PrintUnit footer;
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

	unsigned int     center_vertically:1;
	unsigned int     center_horizontally:1;

	unsigned int     print_grid_lines:1;
	unsigned int     print_even_if_only_styles:1;
	unsigned int     print_black_and_white:1;
	unsigned int     print_as_draft:1;
	unsigned int     print_titles:1;

	enum {
		PRINT_ORDER_DOWN_THEN_RIGHT,
		PRINT_ORDER_RIGHT_THEN_DOWN
	}                print_order;

	PrintHF          *header;
	PrintHF          *footer;

	const GnomePaper *paper;

	PrintRepeatRange  repeat_top, repeat_left;
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
void              print_info_save        (PrintInformation *pi);
void              print_info_free        (PrintInformation *pi);
PrintInformation *print_info_copy        (PrintInformation *src_pi);

PrintHF          *print_hf_new           (const char *left_side_format,
					  const char *middle_format,
				          const char *right_side_format);
void              print_hf_free          (PrintHF *print_hf);
PrintHF          *print_hf_copy          (const PrintHF *source);
PrintHF          *print_hf_register      (PrintHF *hf);
gboolean          print_hf_same          (const PrintHF *a,
					  const PrintHF *b);

char             *hf_format_render       (const char *format,
					  HFRenderInfo *info,
					  HFRenderType render_type);
HFRenderInfo     *hf_render_info_new     (void);
void              hf_render_info_destroy (HFRenderInfo *hfi);


const char *unit_name_get_short_name (UnitName name, gboolean translated);
const char *unit_name_get_name       (UnitName name, gboolean translated);
UnitName    unit_name_to_unit        (const char *s, gboolean translated);
double      print_unit_get_prefered  (PrintUnit *unit);
double      unit_convert             (double value, UnitName source, UnitName target);

void        print_init               (void);
void        print_shutdown           (void);

/* Formats known */
extern GList *hf_formats;

#endif
