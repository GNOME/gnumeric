#ifndef GNUMERIC_WIZARD_H
#define GNUMERIC_WIZARD_H

typedef gboolean (*WizardLaunchFn) (Workbook *wb);

void graphics_wizard (Workbook *wb);

#endif /* GNUMERIC_WIZARD_H */
