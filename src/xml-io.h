/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

Sheet*    gnumericReadXmlSheet     (const char *filename);
int       gnumericWriteXmlSheet    (Sheet *sheet, const char *filename);
Workbook* gnumericReadXmlWorkbook  (const char *filename);
int       gnumericWriteXmlWorkbook (Workbook *sheet, const char *filename);

#endif /* GNUMERIC_XML_IO_H */
