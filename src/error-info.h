#ifndef GNUMERIC_ERROR_INFO_H
#define GNUMERIC_ERROR_INFO_H

#include <glib.h>

typedef struct _ErrorInfo ErrorInfo;

ErrorInfo   *error_info_new_str (const gchar *msg);
ErrorInfo   *error_info_new_printf (const gchar *msg_format, ...) G_GNUC_PRINTF (1, 2);
ErrorInfo   *error_info_new_vprintf (const gchar *msg_format, va_list args);
ErrorInfo   *error_info_new_str_with_details (const gchar *msg, ErrorInfo *details);
ErrorInfo   *error_info_new_str_with_details_list (const gchar *msg, GList *details);
ErrorInfo   *error_info_new_from_error_list (GList *errors);
ErrorInfo   *error_info_new_from_errno (void);
void         error_info_add_details (ErrorInfo *error, ErrorInfo *details);
void         error_info_add_details_list (ErrorInfo *error, GList *details);
void         error_info_free (ErrorInfo *error);
void         error_info_print (ErrorInfo *error);
const gchar *error_info_peek_message (ErrorInfo *error);
GList       *error_info_peek_details (ErrorInfo *error);

#endif /* GNUMERIC_ERROR_INFO_H */
