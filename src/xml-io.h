/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef __GNUMERIC_XML_IO__
#define __GNUMERIC_XML_IO__

extern Sheet *gnumericReadXmlSheet(const char *filename);
extern int gnumericWriteXmlSheet(Sheet *sheet, const char *filename);
extern Workbook *gnumericReadXmlWorkbook(const char *filename);
extern int gnumericWriteXmlWorkbook(Workbook *sheet, const char *filename);

#endif /* __GNUMERIC_XML_IO__ */
