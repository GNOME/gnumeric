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

Sheet    *gnumeric_xml_read_sheet     (const char *filename);
int       gnumeric_xml_write_sheet    (Sheet *sheet, const char *filename);
Workbook *gnumeric_xml_read_workbook  (const char *filename);
int       gnumeric_xml_write_workbook (Workbook *sheet, const char *filename);

void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
