#ifndef GNUMERIC_RENDER_H
#define GNUMERIC_RENDER_H

/*typedef struct {
        char     *format;
	int      want_am_pm;
        char     restriction_type;
        int      restriction_value;
} StyleFormatEntry;

typedef struct {
	int      ref_count;
        GList    *format_list;  *//* Of type RenderFormatEntry. *//*
	char     *format;
} RenderFormat;

typedef struct {
	int                ref_count;
	char              *font_name;
	double             size;
	double             scale;
	GnomeDisplayFont  *dfont;
	GnomeFont         *font;

	unsigned int is_bold:1;
	unsigned int is_italic:1;
} RenderFont;
typedef struct {
	int      ref_count;

	*//*
	 * if the value is BORDER_NONE, then the respective
	 * color is not allocated, otherwise, it has a
	 * valid color.
	 * NB. Use StyleSide to get orientation
	 **//*
 	RenderBorderType type[4] ;
 	RenderColor  *color[4] ;
} RenderBorder;
*/

typedef struct {
/*	RenderFormat   *format;
	RenderBorder   *border;*/
	StyleFont     *font;
	StyleColor    *fore_color;
	StyleColor    *back_color;

	unsigned int pattern:4;
	unsigned int valign:4;
	unsigned int halign:6;
	unsigned int orientation:4;
	unsigned int fit_in_cell:1;
	
/*	unsigned char valid_flags;*/
} RenderInfo;

void                render_init  	      (void);
void	            render_shutdown           (void);

RenderInfo         *render_info_new   	      (StyleElement *e, guint len);
/*void                render_info_merge_to         (Renderinfo *target, Renderinfo *source);
  RenderInfo         *render_info_duplicate        (const Renderinfo *renderinfo);*/
void                render_info_destroy       (RenderInfo *ri);
RenderInfo         *render_info_new_empty     (void);

/*
StyleFormat   *style_format_new       (const char *name);
void                style_format_ref       (RenderinfoFormat *sf);
void                style_format_unref     (RenderinfoFormat *sf);
*/
StyleFont     *style_font_new         (const char *font_name,
				       double size, double scale,
				       int bold, int italic);
StyleFont     *style_font_new_from    (StyleFont *sf, double scale);
StyleFont     *style_font_new_simple  (const char *font_name,
				       double size, double scale,
				       int bold, int italic);
GdkFont            *style_font_gdk_font    (StyleFont *sf);
GnomeFont          *style_font_gnome_font  (StyleFont *sf);
int                 style_font_get_height  (StyleFont *sf);
void           style_font_ref         (StyleFont *sf);
void           style_font_unref       (StyleFont *sf);

StyleColor    *style_color_new        (gushort red, gushort green, gushort blue);
void           style_color_ref        (StyleColor *sc);
void           style_color_unref      (StyleColor *sc);
/*
StyleBorder   *style_border_new_plain (void);
void           style_border_ref       (RenderinfoBorder *sb);
void           style_border_unref     (RenderinfoBorder *sb);
StyleBorder   *style_border_new       (RenderinfoBorderType const border_type[4],
StyleColor *border_color[4]);*/

/*
 * For hashing Renderinfos
 */
guint          render_info_hash    (gconstpointer a);
gint           render_info_compare (gconstpointer a, gconstpointer b);

extern StyleFont *gnumeric_default_font;
extern StyleFont *gnumeric_default_bold_font;
extern StyleFont *gnumeric_default_italic_font;

#endif
