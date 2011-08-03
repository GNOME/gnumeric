#ifndef GNUMERIC_GTK_COMPILATION_H
#define GNUMERIC_GTK_COMPILATION_H

#if defined(__GNUC__)
#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__ )
#if (GCC_VERSION >= 402)
#if (GCC_VERSION >= 406)
#define GNM_BEGIN_KILL_SWITCH_WARNING _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wswitch\"")
#define GNM_END_KILL_SWITCH_WARNING _Pragma("GCC diagnostic pop")
#else
#define GNM_BEGIN_KILL_SWITCH_WARNING _Pragma("GCC diagnostic ignored \"-Wswitch\"")
#define GNM_END_KILL_SWITCH_WARNING _Pragma("GCC diagnostic warning \"-Wswitch\"")
#endif
#endif
#endif

#ifndef GNM_BEGIN_KILL_SWITCH_WARNING
#define GNM_BEGIN_KILL_SWITCH_WARNING
#endif
#ifndef GNM_END_KILL_SWITCH_WARNING
#define GNM_END_KILL_SWITCH_WARNING
#endif

#endif
