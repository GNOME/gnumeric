#ifndef PRINT_INFO_H
#define PRINT_INFO_H

#include "gnumeric.h"
#include "value.h"

#define METERS_TO_POINTS(x) (x * 2834.6456692) / 100

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
	Value range;
} PrintRepeatRange;

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
	Sheet    *sheet;
	int       page;
	int       pages;
	Value     *date_time;
} HFRenderInfo;

PrintInformation *print_info_new         (void);
void              print_info_save        (PrintInformation *pi);
void              print_info_free        (PrintInformation *pi);

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

	
const char *unit_name_get_short_name (UnitName name);
const char *unit_name_get_name       (UnitName name);
double      print_unit_get_prefered  (PrintUnit *unit);
double      unit_convert             (double value, UnitName source, UnitName target);
UnitName    unit_name_to_unit        (const char *s);

void        print_init               (void);
void        print_shutdown           (void);

/* Formats known */
extern GList *hf_formats;

#endif
