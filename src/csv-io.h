/*
 * csv-io.h: interfaces to save/read Sheets using a CSV encoding.
 * (most of the code taken from xml-io.h by Daniel Veillard <Daniel.Veillard@w3.org>)
 *
 * Vincent Renardias <vincent@ldsol.com>
 *
 * $Id$
 */

#ifndef GNUMERIC_CSV_IO_H
#define GNUMERIC_CSV_IO_H

Sheet    *gnumericReadCSVSheet     (const char *filename);
Workbook *gnumericReadCSVWorkbook  (const char *filename);
/* not yet 
int       gnumericWriteCSVSheet    (Sheet *sheet, const char *filename);
int       gnumericWriteCSVWorkbook (Workbook *sheet, const char *filename);
*/
void      csv_init (void);

#endif /* GNUMERIC_CSV_IO_H */
