#ifndef GNUMERIC_XML_IO_AUTOFT_H
#define GNUMERIC_XML_IO_AUTOFT_H

#include "format-template.h"

int        gnumeric_xml_write_format_template    (CommandContext *context, FormatTemplate *ft,
						  const char *filename);

int        gnumeric_xml_read_format_template     (CommandContext *context, FormatTemplate *ft,
						  const char *filename);

#endif /* GNUMERIC_XML_IO_AUTOFT_H */
