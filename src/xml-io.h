/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include "sheet.h"
#include "gnome-xml/tree.h"

int       gnumeric_xml_read_workbook  (CommandContext *context, Workbook *wb,
				       const char *filename);
int       gnumeric_xml_write_workbook (CommandContext *context, Workbook *wb,
				       const char *filename);

int       gnumeric_xml_write_selection_clipboard (CommandContext *context, Sheet *sheet,
						  xmlChar **buffer, int *size);
int       gnumeric_xml_read_selection_clipboard (CommandContext *context, CellRegion **cr,
						 xmlChar *buffer);
				       
void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
