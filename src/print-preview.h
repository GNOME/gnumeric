#ifndef GNUMERIC_PRINT_PREVIEW_H
#define GNUMERIC_PRINT_PREVIEW_H

typedef struct _PrintPreview PrintPreview;

PrintPreview      *print_preview_new               (Workbook *wb);
GnomePrintContext *print_preview_get_print_context (PrintPreview *preview);
void               print_preview_print_done        (PrintPreview *pp);

#endif /* GNUMERIC_PRINT_PREVIEW_H */
