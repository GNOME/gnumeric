/*
 * text-io.h: interfaces to save/read Sheets using a simple text encoding.
 * (most of the code taken from csv-io.h by Vincent Renardias
 *                                                      <vincent@ldsol.com>)
 *
 * Takashi Matsuda <matsu@arch.comp.kyutech.ac.jp>
 *
 * $Id$
 */

#ifndef GNUMERIC_TEXT_IO_H
#define GNUMERIC_TEXT_IO_H

#include "../../src/gnumeric.h"
#include "../../src/gnumeric-util.h"
#include "../../src/plugin.h"

PluginInitResult       init_plugin (CommandContext *context, PluginData *pd);

#endif /* GNUMERIC_TEXT_IO_H */
