#ifndef GNUMERIC_IO_CONTEXT_H
#define GNUMERIC_IO_CONTEXT_H

#include <stdio.h>
#include <gtk/gtkobject.h>
#include <gal/util/e-util.h>
#include "gnumeric.h"
#include "error-info.h"

/* typedef struct _IOContext IOContext; */
typedef struct _IOContextClass IOContextClass;

#define TYPE_IO_CONTEXT    (io_context_get_type ())
#define IO_CONTEXT(obj)    (GTK_CHECK_CAST ((obj), TYPE_IO_CONTEXT, IOContext))
#define IS_IO_CONTEXT(obj) (GTK_CHECK_TYPE ((obj), TYPE_IO_CONTEXT))

typedef enum {
	WB_PROGRESS_CELLS  = 1,
	WB_PROGRESS_ALL    = (WB_PROGRESS_CELLS)
} WbProgressElements;

GType      io_context_get_type (void);
IOContext *gnumeric_io_context_new        (WorkbookControl *wbc);

/*
 * These are the exceptions that can arise.
 * NOTE : The selection is quite limited by IDL's intentional non-support for
 *        inheritance (single or multiple).
 */
void       gnumeric_io_error_system       (IOContext *context, char const *msg);
void       gnumeric_io_error_read         (IOContext *context, char const *msg);
void       gnumeric_io_error_save         (IOContext *context, char const *msg);
void       gnumeric_io_error_unknown      (IOContext *context);

void       gnumeric_io_error_info_set     (IOContext *context, ErrorInfo *error);
void       gnumeric_io_error_string       (IOContext *context, const gchar *str);
void       gnumeric_io_error_push         (IOContext *context, ErrorInfo *error);
ErrorInfo *gnumeric_io_error_pop          (IOContext *context);
void       gnumeric_io_error_clear        (IOContext *context);
void       gnumeric_io_error_display      (IOContext *context);

void       gnumeric_io_clear_error        (IOContext *context);
gboolean   gnumeric_io_error_occurred     (IOContext *context);

void       io_progress_message      (IOContext *io_context, const gchar *msg);
void       io_progress_update       (IOContext *io_context, gdouble f);
void       io_progress_range_push   (IOContext *io_context, gdouble min, gdouble max);
void       io_progress_range_pop    (IOContext *io_context);

void       file_io_progress_set    (IOContext *io_context, const gchar *file_name, FILE *f);
void       file_io_progress_update (IOContext *io_context);

void       memory_io_progress_set    (IOContext *io_context, void *mem_start, gint mem_size);
void       memory_io_progress_update (IOContext *io_context, void *mem_current);

void       count_io_progress_set    (IOContext *io_context, gint total, gint step);
void       count_io_progress_update (IOContext *io_context, gint inc);

void       value_io_progress_set    (IOContext *io_context, gint total, gint step);
void       value_io_progress_update (IOContext *io_context, gint value);

void       workbook_io_progress_set    (IOContext *io_context, Workbook *wb,
                                        WbProgressElements elements, gint step);
void       workbook_io_progress_update (IOContext *io_context, gint inc);

void       io_progress_unset      (IOContext *io_context);

void gnumeric_warning_unknown_font        (IOContext *context, char const *msg);
void gnumeric_warning_unknown_feature     (IOContext *context, char const *msg);
void gnumeric_warning_unknown_function    (IOContext *context, char const *msg);

#endif /* GNUMERIC_IO_CONTEXT_H */
