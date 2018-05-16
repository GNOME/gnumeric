#ifndef PLUGIN_PSICONV_PLUGIN_H
#define PLUGIN_PSICONV_PLUGIN_H

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-input.h>

gboolean psiconv_read_header (GsfInput *input);
void	 psiconv_read (GOIOContext *io_context, Workbook *wb, GsfInput *input);

#endif /* PLUGIN_PSICONV_PLUGIN_H */
