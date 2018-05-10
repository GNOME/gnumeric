#ifndef GNUMERIC_GTK_COMPILATION_H
#define GNUMERIC_GTK_COMPILATION_H

#if defined(__GNUC__)
#define GNM_BEGIN_KILL_SWITCH_WARNING _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wswitch\"")
#define GNM_END_KILL_SWITCH_WARNING _Pragma("GCC diagnostic pop")
#endif

#ifndef GNM_BEGIN_KILL_SWITCH_WARNING
#define GNM_BEGIN_KILL_SWITCH_WARNING
#endif
#ifndef GNM_END_KILL_SWITCH_WARNING
#define GNM_END_KILL_SWITCH_WARNING
#endif

#endif
