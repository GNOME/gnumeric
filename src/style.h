
enum style_type_enum {
	STYLE_FONT,
	STYLE_TEXT_SIZE,
	STYLE_BACKGROUND_COLOR,
	STYLE_FOREGROUND_COLOR,
};

typedef struct {
	enum style_type_enum style_type;
	int  serial_number;
	int  ref_count;
} Style;

typedef struct {
	Style style;

	char  *font_name;
} StyleFont;

typedef struct {
	Style style;
	int   size;
} StyleSize;

tyepdef struct {
	Style style;
	char  *format_string;
} StyleFormat;

typedef struct {
	Style    style;
	char     *color_name;	/* external representation */
	GdkColor color;
} StyleColor;

typedef StyleColor StyleBackground;
typedef StyleColor StyleForeground;


