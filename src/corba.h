#ifndef GNUMERIC_CORBA_H
#define GNUMERIC_CORBA_H

extern      PortableServer_POA    gnumeric_poa;
gboolean    WorkbookFactory_init  (void);

CommandContext *command_context_corba (void);

#endif
