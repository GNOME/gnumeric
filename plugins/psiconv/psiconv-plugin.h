#ifndef PLUGIN_PSICONV_PLUGIN_H
#define PLUGIN_PSICONV_PLUGIN_H

#include "gnumeric.h"
#include "io-context.h"
#include <stdio.h>

gboolean psiconv_read_header (GsfInput *input);
void	 psiconv_read (IOContext *io_context, Workbook *wb, GsfInput *input);

#endif /* PLUGIN_PSICONV_PLUGIN_H */
