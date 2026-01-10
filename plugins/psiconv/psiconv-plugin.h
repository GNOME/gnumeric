#ifndef GNM_PLUGIN_PSICONV_PLUGIN_H_
#define GNM_PLUGIN_PSICONV_PLUGIN_H_

#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gsf/gsf-input.h>

gboolean psiconv_read_header (GsfInput *input);
void	 psiconv_read (GOIOContext *io_context, Workbook *wb, GsfInput *input);

#endif /* GNM_PLUGIN_PSICONV_PLUGIN_H_ */
