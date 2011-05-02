#ifndef GNUMERIC_GTK_DEAD_KITTENS_H
#define GNUMERIC_GTK_DEAD_KITTENS_H

#include <gutils.h>
#include <gdk/gdkkeysyms.h>

/* To be included only from C files, not headers.  */
#ifndef HAVE_GDK_WINDOW_GET_SCREEN
#define gdk_window_get_screen gdk_drawable_get_screen
#endif

#ifndef GDK_KEY_VoidSymbol
#define GDK_KEY_VoidSymbol GDK_VoidSymbol
#endif
#ifndef GDK_KEY_BackSpace
#define GDK_KEY_BackSpace GDK_BackSpace
#endif
#ifndef GDK_KEY_Tab
#define GDK_KEY_Tab GDK_Tab
#endif
#ifndef GDK_KEY_ISO_Left_Tab
#define GDK_KEY_ISO_Left_Tab GDK_ISO_Left_Tab
#endif
#ifndef GDK_KEY_Linefeed
#define GDK_KEY_Linefeed GDK_Linefeed
#endif
#ifndef GDK_KEY_Clear
#define GDK_KEY_Clear GDK_Clear
#endif
#ifndef GDK_KEY_Return
#define GDK_KEY_Return GDK_Return
#endif
#ifndef GDK_KEY_Pause
#define GDK_KEY_Pause GDK_Pause
#endif
#ifndef GDK_KEY_Scroll_Lock
#define GDK_KEY_Scroll_Lock GDK_Scroll_Lock
#endif
#ifndef GDK_KEY_Sys_Req
#define GDK_KEY_Sys_Req GDK_Sys_Req
#endif
#ifndef GDK_KEY_Escape
#define GDK_KEY_Escape GDK_Escape
#endif
#ifndef GDK_KEY_Delete
#define GDK_KEY_Delete GDK_Delete
#endif
#ifndef GDK_KEY_Multi_key
#define GDK_KEY_Multi_key GDK_Multi_key
#endif
#ifndef GDK_KEY_Codeinput
#define GDK_KEY_Codeinput GDK_Codeinput
#endif
#ifndef GDK_KEY_Home
#define GDK_KEY_Home GDK_Home
#endif
#ifndef GDK_KEY_Left
#define GDK_KEY_Left GDK_Left
#endif
#ifndef GDK_KEY_Up
#define GDK_KEY_Up GDK_Up
#endif
#ifndef GDK_KEY_Right
#define GDK_KEY_Right GDK_Right
#endif
#ifndef GDK_KEY_Down
#define GDK_KEY_Down GDK_Down
#endif
#ifndef GDK_KEY_Prior
#define GDK_KEY_Prior GDK_Prior
#endif
#ifndef GDK_KEY_Page_Up
#define GDK_KEY_Page_Up GDK_Page_Up
#endif
#ifndef GDK_KEY_Next
#define GDK_KEY_Next GDK_Next
#endif
#ifndef GDK_KEY_Page_Down
#define GDK_KEY_Page_Down GDK_Page_Down
#endif
#ifndef GDK_KEY_End
#define GDK_KEY_End GDK_End
#endif
#ifndef GDK_KEY_Begin
#define GDK_KEY_Begin GDK_Begin
#endif
#ifndef GDK_KEY_Select
#define GDK_KEY_Select GDK_Select
#endif
#ifndef GDK_KEY_Print
#define GDK_KEY_Print GDK_Print
#endif
#ifndef GDK_KEY_Execute
#define GDK_KEY_Execute GDK_Execute
#endif
#ifndef GDK_KEY_Insert
#define GDK_KEY_Insert GDK_Insert
#endif
#ifndef GDK_KEY_Undo
#define GDK_KEY_Undo GDK_Undo
#endif
#ifndef GDK_KEY_Redo
#define GDK_KEY_Redo GDK_Redo
#endif
#ifndef GDK_KEY_Menu
#define GDK_KEY_Menu GDK_Menu
#endif
#ifndef GDK_KEY_Find
#define GDK_KEY_Find GDK_Find
#endif
#ifndef GDK_KEY_Cancel
#define GDK_KEY_Cancel GDK_Cancel
#endif
#ifndef GDK_KEY_Help
#define GDK_KEY_Help GDK_Help
#endif
#ifndef GDK_KEY_Break
#define GDK_KEY_Break GDK_Break
#endif
#ifndef GDK_KEY_Mode_switch
#define GDK_KEY_Mode_switch GDK_Mode_switch
#endif
#ifndef GDK_KEY_script_switch
#define GDK_KEY_script_switch GDK_script_switch
#endif
#ifndef GDK_KEY_Num_Lock
#define GDK_KEY_Num_Lock GDK_Num_Lock
#endif
#ifndef GDK_KEY_KP_Space
#define GDK_KEY_KP_Space GDK_KP_Space
#endif
#ifndef GDK_KEY_KP_Tab
#define GDK_KEY_KP_Tab GDK_KP_Tab
#endif
#ifndef GDK_KEY_KP_Enter
#define GDK_KEY_KP_Enter GDK_KP_Enter
#endif
#ifndef GDK_KEY_KP_F1
#define GDK_KEY_KP_F1 GDK_KP_F1
#endif
#ifndef GDK_KEY_KP_F2
#define GDK_KEY_KP_F2 GDK_KP_F2
#endif
#ifndef GDK_KEY_KP_F3
#define GDK_KEY_KP_F3 GDK_KP_F3
#endif
#ifndef GDK_KEY_KP_F4
#define GDK_KEY_KP_F4 GDK_KP_F4
#endif
#ifndef GDK_KEY_KP_Home
#define GDK_KEY_KP_Home GDK_KP_Home
#endif
#ifndef GDK_KEY_KP_Left
#define GDK_KEY_KP_Left GDK_KP_Left
#endif
#ifndef GDK_KEY_KP_Up
#define GDK_KEY_KP_Up GDK_KP_Up
#endif
#ifndef GDK_KEY_KP_Right
#define GDK_KEY_KP_Right GDK_KP_Right
#endif
#ifndef GDK_KEY_KP_Down
#define GDK_KEY_KP_Down GDK_KP_Down
#endif
#ifndef GDK_KEY_KP_Prior
#define GDK_KEY_KP_Prior GDK_KP_Prior
#endif
#ifndef GDK_KEY_KP_Page_Up
#define GDK_KEY_KP_Page_Up GDK_KP_Page_Up
#endif
#ifndef GDK_KEY_KP_Next
#define GDK_KEY_KP_Next GDK_KP_Next
#endif
#ifndef GDK_KEY_KP_Page_Down
#define GDK_KEY_KP_Page_Down GDK_KP_Page_Down
#endif
#ifndef GDK_KEY_KP_End
#define GDK_KEY_KP_End GDK_KP_End
#endif
#ifndef GDK_KEY_KP_Begin
#define GDK_KEY_KP_Begin GDK_KP_Begin
#endif
#ifndef GDK_KEY_KP_Insert
#define GDK_KEY_KP_Insert GDK_KP_Insert
#endif
#ifndef GDK_KEY_KP_Delete
#define GDK_KEY_KP_Delete GDK_KP_Delete
#endif
#ifndef GDK_KEY_KP_Equal
#define GDK_KEY_KP_Equal GDK_KP_Equal
#endif
#ifndef GDK_KEY_KP_Multiply
#define GDK_KEY_KP_Multiply GDK_KP_Multiply
#endif
#ifndef GDK_KEY_KP_Add
#define GDK_KEY_KP_Add GDK_KP_Add
#endif
#ifndef GDK_KEY_KP_Separator
#define GDK_KEY_KP_Separator GDK_KP_Separator
#endif
#ifndef GDK_KEY_KP_Subtract
#define GDK_KEY_KP_Subtract GDK_KP_Subtract
#endif
#ifndef GDK_KEY_KP_Decimal
#define GDK_KEY_KP_Decimal GDK_KP_Decimal
#endif
#ifndef GDK_KEY_KP_Divide
#define GDK_KEY_KP_Divide GDK_KP_Divide
#endif
#ifndef GDK_KEY_KP_0
#define GDK_KEY_KP_0 GDK_KP_0
#endif
#ifndef GDK_KEY_KP_1
#define GDK_KEY_KP_1 GDK_KP_1
#endif
#ifndef GDK_KEY_KP_2
#define GDK_KEY_KP_2 GDK_KP_2
#endif
#ifndef GDK_KEY_KP_3
#define GDK_KEY_KP_3 GDK_KP_3
#endif
#ifndef GDK_KEY_KP_4
#define GDK_KEY_KP_4 GDK_KP_4
#endif
#ifndef GDK_KEY_KP_5
#define GDK_KEY_KP_5 GDK_KP_5
#endif
#ifndef GDK_KEY_KP_6
#define GDK_KEY_KP_6 GDK_KP_6
#endif
#ifndef GDK_KEY_KP_7
#define GDK_KEY_KP_7 GDK_KP_7
#endif
#ifndef GDK_KEY_KP_8
#define GDK_KEY_KP_8 GDK_KP_8
#endif
#ifndef GDK_KEY_KP_9
#define GDK_KEY_KP_9 GDK_KP_9
#endif
#ifndef GDK_KEY_F1
#define GDK_KEY_F1 GDK_F1
#endif
#ifndef GDK_KEY_F2
#define GDK_KEY_F2 GDK_F2
#endif
#ifndef GDK_KEY_F3
#define GDK_KEY_F3 GDK_F3
#endif
#ifndef GDK_KEY_F4
#define GDK_KEY_F4 GDK_F4
#endif
#ifndef GDK_KEY_F5
#define GDK_KEY_F5 GDK_F5
#endif
#ifndef GDK_KEY_F6
#define GDK_KEY_F6 GDK_F6
#endif
#ifndef GDK_KEY_F7
#define GDK_KEY_F7 GDK_F7
#endif
#ifndef GDK_KEY_F8
#define GDK_KEY_F8 GDK_F8
#endif
#ifndef GDK_KEY_F9
#define GDK_KEY_F9 GDK_F9
#endif
#ifndef GDK_KEY_F10
#define GDK_KEY_F10 GDK_F10
#endif
#ifndef GDK_KEY_F11
#define GDK_KEY_F11 GDK_F11
#endif
#ifndef GDK_KEY_F12
#define GDK_KEY_F12 GDK_F12
#endif
#ifndef GDK_KEY_F13
#define GDK_KEY_F13 GDK_F13
#endif
#ifndef GDK_KEY_F14
#define GDK_KEY_F14 GDK_F14
#endif
#ifndef GDK_KEY_F15
#define GDK_KEY_F15 GDK_F15
#endif
#ifndef GDK_KEY_F16
#define GDK_KEY_F16 GDK_F16
#endif
#ifndef GDK_KEY_F17
#define GDK_KEY_F17 GDK_F17
#endif
#ifndef GDK_KEY_F18
#define GDK_KEY_F18 GDK_F18
#endif
#ifndef GDK_KEY_F19
#define GDK_KEY_F19 GDK_F19
#endif
#ifndef GDK_KEY_F20
#define GDK_KEY_F20 GDK_F20
#endif
#ifndef GDK_KEY_F21
#define GDK_KEY_F21 GDK_F21
#endif
#ifndef GDK_KEY_F22
#define GDK_KEY_F22 GDK_F22
#endif
#ifndef GDK_KEY_F23
#define GDK_KEY_F23 GDK_F23
#endif
#ifndef GDK_KEY_F24
#define GDK_KEY_F24 GDK_F24
#endif
#ifndef GDK_KEY_F25
#define GDK_KEY_F25 GDK_F25
#endif
#ifndef GDK_KEY_F26
#define GDK_KEY_F26 GDK_F26
#endif
#ifndef GDK_KEY_F27
#define GDK_KEY_F27 GDK_F27
#endif
#ifndef GDK_KEY_F28
#define GDK_KEY_F28 GDK_F28
#endif
#ifndef GDK_KEY_F29
#define GDK_KEY_F29 GDK_F29
#endif
#ifndef GDK_KEY_F30
#define GDK_KEY_F30 GDK_F30
#endif
#ifndef GDK_KEY_F31
#define GDK_KEY_F31 GDK_F31
#endif
#ifndef GDK_KEY_F32
#define GDK_KEY_F32 GDK_F32
#endif
#ifndef GDK_KEY_F33
#define GDK_KEY_F33 GDK_F33
#endif
#ifndef GDK_KEY_F34
#define GDK_KEY_F34 GDK_F34
#endif
#ifndef GDK_KEY_F35
#define GDK_KEY_F35 GDK_F35
#endif
#ifndef GDK_KEY_Shift_L
#define GDK_KEY_Shift_L GDK_Shift_L
#endif
#ifndef GDK_KEY_Shift_R
#define GDK_KEY_Shift_R GDK_Shift_R
#endif
#ifndef GDK_KEY_Control_L
#define GDK_KEY_Control_L GDK_Control_L
#endif
#ifndef GDK_KEY_Control_R
#define GDK_KEY_Control_R GDK_Control_R
#endif
#ifndef GDK_KEY_Caps_Lock
#define GDK_KEY_Caps_Lock GDK_Caps_Lock
#endif
#ifndef GDK_KEY_Shift_Lock
#define GDK_KEY_Shift_Lock GDK_Shift_Lock
#endif
#ifndef GDK_KEY_Meta_L
#define GDK_KEY_Meta_L GDK_Meta_L
#endif
#ifndef GDK_KEY_Meta_R
#define GDK_KEY_Meta_R GDK_Meta_R
#endif
#ifndef GDK_KEY_Alt_L
#define GDK_KEY_Alt_L GDK_Alt_L
#endif
#ifndef GDK_KEY_Alt_R
#define GDK_KEY_Alt_R GDK_Alt_R
#endif
#ifndef GDK_KEY_Super_L
#define GDK_KEY_Super_L GDK_Super_L
#endif
#ifndef GDK_KEY_Super_R
#define GDK_KEY_Super_R GDK_Super_R
#endif
#ifndef GDK_KEY_Hyper_L
#define GDK_KEY_Hyper_L GDK_Hyper_L
#endif
#ifndef GDK_KEY_Hyper_R
#define GDK_KEY_Hyper_R GDK_Hyper_R
#endif
#ifndef GDK_KEY_space
#define GDK_KEY_space GDK_space
#endif
#ifndef GDK_KEY_exclam
#define GDK_KEY_exclam GDK_exclam
#endif
#ifndef GDK_KEY_quotedbl
#define GDK_KEY_quotedbl GDK_quotedbl
#endif
#ifndef GDK_KEY_numbersign
#define GDK_KEY_numbersign GDK_numbersign
#endif
#ifndef GDK_KEY_dollar
#define GDK_KEY_dollar GDK_dollar
#endif
#ifndef GDK_KEY_percent
#define GDK_KEY_percent GDK_percent
#endif
#ifndef GDK_KEY_ampersand
#define GDK_KEY_ampersand GDK_ampersand
#endif
#ifndef GDK_KEY_apostrophe
#define GDK_KEY_apostrophe GDK_apostrophe
#endif
#ifndef GDK_KEY_quoteright
#define GDK_KEY_quoteright GDK_quoteright
#endif
#ifndef GDK_KEY_parenleft
#define GDK_KEY_parenleft GDK_parenleft
#endif
#ifndef GDK_KEY_parenright
#define GDK_KEY_parenright GDK_parenright
#endif
#ifndef GDK_KEY_asterisk
#define GDK_KEY_asterisk GDK_asterisk
#endif
#ifndef GDK_KEY_plus
#define GDK_KEY_plus GDK_plus
#endif
#ifndef GDK_KEY_comma
#define GDK_KEY_comma GDK_comma
#endif
#ifndef GDK_KEY_minus
#define GDK_KEY_minus GDK_minus
#endif
#ifndef GDK_KEY_period
#define GDK_KEY_period GDK_period
#endif
#ifndef GDK_KEY_slash
#define GDK_KEY_slash GDK_slash
#endif
#ifndef GDK_KEY_0
#define GDK_KEY_0 GDK_0
#endif
#ifndef GDK_KEY_1
#define GDK_KEY_1 GDK_1
#endif
#ifndef GDK_KEY_2
#define GDK_KEY_2 GDK_2
#endif
#ifndef GDK_KEY_3
#define GDK_KEY_3 GDK_3
#endif
#ifndef GDK_KEY_4
#define GDK_KEY_4 GDK_4
#endif
#ifndef GDK_KEY_5
#define GDK_KEY_5 GDK_5
#endif
#ifndef GDK_KEY_6
#define GDK_KEY_6 GDK_6
#endif
#ifndef GDK_KEY_7
#define GDK_KEY_7 GDK_7
#endif
#ifndef GDK_KEY_8
#define GDK_KEY_8 GDK_8
#endif
#ifndef GDK_KEY_9
#define GDK_KEY_9 GDK_9
#endif
#ifndef GDK_KEY_colon
#define GDK_KEY_colon GDK_colon
#endif
#ifndef GDK_KEY_semicolon
#define GDK_KEY_semicolon GDK_semicolon
#endif
#ifndef GDK_KEY_less
#define GDK_KEY_less GDK_less
#endif
#ifndef GDK_KEY_equal
#define GDK_KEY_equal GDK_equal
#endif
#ifndef GDK_KEY_greater
#define GDK_KEY_greater GDK_greater
#endif
#ifndef GDK_KEY_question
#define GDK_KEY_question GDK_question
#endif
#ifndef GDK_KEY_at
#define GDK_KEY_at GDK_at
#endif
#ifndef GDK_KEY_A
#define GDK_KEY_A GDK_A
#endif
#ifndef GDK_KEY_B
#define GDK_KEY_B GDK_B
#endif
#ifndef GDK_KEY_C
#define GDK_KEY_C GDK_C
#endif
#ifndef GDK_KEY_D
#define GDK_KEY_D GDK_D
#endif
#ifndef GDK_KEY_E
#define GDK_KEY_E GDK_E
#endif
#ifndef GDK_KEY_F
#define GDK_KEY_F GDK_F
#endif
#ifndef GDK_KEY_G
#define GDK_KEY_G GDK_G
#endif
#ifndef GDK_KEY_H
#define GDK_KEY_H GDK_H
#endif
#ifndef GDK_KEY_I
#define GDK_KEY_I GDK_I
#endif
#ifndef GDK_KEY_J
#define GDK_KEY_J GDK_J
#endif
#ifndef GDK_KEY_K
#define GDK_KEY_K GDK_K
#endif
#ifndef GDK_KEY_L
#define GDK_KEY_L GDK_L
#endif
#ifndef GDK_KEY_M
#define GDK_KEY_M GDK_M
#endif
#ifndef GDK_KEY_N
#define GDK_KEY_N GDK_N
#endif
#ifndef GDK_KEY_O
#define GDK_KEY_O GDK_O
#endif
#ifndef GDK_KEY_P
#define GDK_KEY_P GDK_P
#endif
#ifndef GDK_KEY_Q
#define GDK_KEY_Q GDK_Q
#endif
#ifndef GDK_KEY_R
#define GDK_KEY_R GDK_R
#endif
#ifndef GDK_KEY_S
#define GDK_KEY_S GDK_S
#endif
#ifndef GDK_KEY_T
#define GDK_KEY_T GDK_T
#endif
#ifndef GDK_KEY_U
#define GDK_KEY_U GDK_U
#endif
#ifndef GDK_KEY_V
#define GDK_KEY_V GDK_V
#endif
#ifndef GDK_KEY_W
#define GDK_KEY_W GDK_W
#endif
#ifndef GDK_KEY_X
#define GDK_KEY_X GDK_X
#endif
#ifndef GDK_KEY_Y
#define GDK_KEY_Y GDK_Y
#endif
#ifndef GDK_KEY_Z
#define GDK_KEY_Z GDK_Z
#endif
#ifndef GDK_KEY_bracketleft
#define GDK_KEY_bracketleft GDK_bracketleft
#endif
#ifndef GDK_KEY_backslash
#define GDK_KEY_backslash GDK_backslash
#endif
#ifndef GDK_KEY_bracketright
#define GDK_KEY_bracketright GDK_bracketright
#endif
#ifndef GDK_KEY_asciicircum
#define GDK_KEY_asciicircum GDK_asciicircum
#endif
#ifndef GDK_KEY_underscore
#define GDK_KEY_underscore GDK_underscore
#endif
#ifndef GDK_KEY_grave
#define GDK_KEY_grave GDK_grave
#endif
#ifndef GDK_KEY_quoteleft
#define GDK_KEY_quoteleft GDK_quoteleft
#endif
#ifndef GDK_KEY_a
#define GDK_KEY_a GDK_a
#endif
#ifndef GDK_KEY_b
#define GDK_KEY_b GDK_b
#endif
#ifndef GDK_KEY_c
#define GDK_KEY_c GDK_c
#endif
#ifndef GDK_KEY_d
#define GDK_KEY_d GDK_d
#endif
#ifndef GDK_KEY_e
#define GDK_KEY_e GDK_e
#endif
#ifndef GDK_KEY_f
#define GDK_KEY_f GDK_f
#endif
#ifndef GDK_KEY_g
#define GDK_KEY_g GDK_g
#endif
#ifndef GDK_KEY_h
#define GDK_KEY_h GDK_h
#endif
#ifndef GDK_KEY_i
#define GDK_KEY_i GDK_i
#endif
#ifndef GDK_KEY_j
#define GDK_KEY_j GDK_j
#endif
#ifndef GDK_KEY_k
#define GDK_KEY_k GDK_k
#endif
#ifndef GDK_KEY_l
#define GDK_KEY_l GDK_l
#endif
#ifndef GDK_KEY_m
#define GDK_KEY_m GDK_m
#endif
#ifndef GDK_KEY_n
#define GDK_KEY_n GDK_n
#endif
#ifndef GDK_KEY_o
#define GDK_KEY_o GDK_o
#endif
#ifndef GDK_KEY_p
#define GDK_KEY_p GDK_p
#endif
#ifndef GDK_KEY_q
#define GDK_KEY_q GDK_q
#endif
#ifndef GDK_KEY_r
#define GDK_KEY_r GDK_r
#endif
#ifndef GDK_KEY_s
#define GDK_KEY_s GDK_s
#endif
#ifndef GDK_KEY_t
#define GDK_KEY_t GDK_t
#endif
#ifndef GDK_KEY_u
#define GDK_KEY_u GDK_u
#endif
#ifndef GDK_KEY_v
#define GDK_KEY_v GDK_v
#endif
#ifndef GDK_KEY_w
#define GDK_KEY_w GDK_w
#endif
#ifndef GDK_KEY_x
#define GDK_KEY_x GDK_x
#endif
#ifndef GDK_KEY_y
#define GDK_KEY_y GDK_y
#endif
#ifndef GDK_KEY_z
#define GDK_KEY_z GDK_z
#endif
#ifndef GDK_KEY_braceleft
#define GDK_KEY_braceleft GDK_braceleft
#endif
#ifndef GDK_KEY_bar
#define GDK_KEY_bar GDK_bar
#endif
#ifndef GDK_KEY_braceright
#define GDK_KEY_braceright GDK_braceright
#endif
#ifndef GDK_KEY_asciitilde
#define GDK_KEY_asciitilde GDK_asciitilde
#endif
#ifndef GDK_KEY_nobreakspace
#define GDK_KEY_nobreakspace GDK_nobreakspace
#endif
#ifndef GDK_KEY_exclamdown
#define GDK_KEY_exclamdown GDK_exclamdown
#endif
#ifndef GDK_KEY_cent
#define GDK_KEY_cent GDK_cent
#endif
#ifndef GDK_KEY_sterling
#define GDK_KEY_sterling GDK_sterling
#endif
#ifndef GDK_KEY_currency
#define GDK_KEY_currency GDK_currency
#endif
#ifndef GDK_KEY_yen
#define GDK_KEY_yen GDK_yen
#endif
#ifndef GDK_KEY_brokenbar
#define GDK_KEY_brokenbar GDK_brokenbar
#endif
#ifndef GDK_KEY_section
#define GDK_KEY_section GDK_section
#endif
#ifndef GDK_KEY_diaeresis
#define GDK_KEY_diaeresis GDK_diaeresis
#endif
#ifndef GDK_KEY_copyright
#define GDK_KEY_copyright GDK_copyright
#endif
#ifndef GDK_KEY_ordfeminine
#define GDK_KEY_ordfeminine GDK_ordfeminine
#endif
#ifndef GDK_KEY_guillemotleft
#define GDK_KEY_guillemotleft GDK_guillemotleft
#endif
#ifndef GDK_KEY_notsign
#define GDK_KEY_notsign GDK_notsign
#endif
#ifndef GDK_KEY_hyphen
#define GDK_KEY_hyphen GDK_hyphen
#endif
#ifndef GDK_KEY_registered
#define GDK_KEY_registered GDK_registered
#endif
#ifndef GDK_KEY_degree
#define GDK_KEY_degree GDK_degree
#endif
#ifndef GDK_KEY_plusminus
#define GDK_KEY_plusminus GDK_plusminus
#endif
#ifndef GDK_KEY_twosuperior
#define GDK_KEY_twosuperior GDK_twosuperior
#endif
#ifndef GDK_KEY_threesuperior
#define GDK_KEY_threesuperior GDK_threesuperior
#endif
#ifndef GDK_KEY_acute
#define GDK_KEY_acute GDK_acute
#endif
#ifndef GDK_KEY_mu
#define GDK_KEY_mu GDK_mu
#endif
#ifndef GDK_KEY_paragraph
#define GDK_KEY_paragraph GDK_paragraph
#endif
#ifndef GDK_KEY_periodcentered
#define GDK_KEY_periodcentered GDK_periodcentered
#endif
#ifndef GDK_KEY_onesuperior
#define GDK_KEY_onesuperior GDK_onesuperior
#endif
#ifndef GDK_KEY_masculine
#define GDK_KEY_masculine GDK_masculine
#endif
#ifndef GDK_KEY_guillemotright
#define GDK_KEY_guillemotright GDK_guillemotright
#endif
#ifndef GDK_KEY_questiondown
#define GDK_KEY_questiondown GDK_questiondown
#endif
#ifndef GDK_KEY_Aring
#define GDK_KEY_Aring GDK_Aring
#endif
#ifndef GDK_KEY_AE
#define GDK_KEY_AE GDK_AE
#endif
#ifndef GDK_KEY_ETH
#define GDK_KEY_ETH GDK_ETH
#endif
#ifndef GDK_KEY_Eth
#define GDK_KEY_Eth GDK_Eth
#endif
#ifndef GDK_KEY_multiply
#define GDK_KEY_multiply GDK_multiply
#endif
#ifndef GDK_KEY_Oslash
#define GDK_KEY_Oslash GDK_Oslash
#endif
#ifndef GDK_KEY_Ooblique
#define GDK_KEY_Ooblique GDK_Ooblique
#endif
#ifndef GDK_KEY_THORN
#define GDK_KEY_THORN GDK_THORN
#endif
#ifndef GDK_KEY_Thorn
#define GDK_KEY_Thorn GDK_Thorn
#endif
#ifndef GDK_KEY_ssharp
#define GDK_KEY_ssharp GDK_ssharp
#endif
#ifndef GDK_KEY_aring
#define GDK_KEY_aring GDK_aring
#endif
#ifndef GDK_KEY_ae
#define GDK_KEY_ae GDK_ae
#endif
#ifndef GDK_KEY_eth
#define GDK_KEY_eth GDK_eth
#endif
#ifndef GDK_KEY_division
#define GDK_KEY_division GDK_division
#endif
#ifndef GDK_KEY_oslash
#define GDK_KEY_oslash GDK_oslash
#endif
#ifndef GDK_KEY_ooblique
#define GDK_KEY_ooblique GDK_ooblique
#endif
#ifndef GDK_KEY_thorn
#define GDK_KEY_thorn GDK_thorn
#endif
#ifndef GDK_KEY_breve
#define GDK_KEY_breve GDK_breve
#endif
#ifndef GDK_KEY_Lstroke
#define GDK_KEY_Lstroke GDK_Lstroke
#endif
#ifndef GDK_KEY_lstroke
#define GDK_KEY_lstroke GDK_lstroke
#endif
#ifndef GDK_KEY_Abreve
#define GDK_KEY_Abreve GDK_Abreve
#endif
#ifndef GDK_KEY_Dstroke
#define GDK_KEY_Dstroke GDK_Dstroke
#endif
#ifndef GDK_KEY_Uring
#define GDK_KEY_Uring GDK_Uring
#endif
#ifndef GDK_KEY_abreve
#define GDK_KEY_abreve GDK_abreve
#endif
#ifndef GDK_KEY_dstroke
#define GDK_KEY_dstroke GDK_dstroke
#endif
#ifndef GDK_KEY_uring
#define GDK_KEY_uring GDK_uring
#endif
#ifndef GDK_KEY_Hstroke
#define GDK_KEY_Hstroke GDK_Hstroke
#endif
#ifndef GDK_KEY_Gbreve
#define GDK_KEY_Gbreve GDK_Gbreve
#endif
#ifndef GDK_KEY_hstroke
#define GDK_KEY_hstroke GDK_hstroke
#endif
#ifndef GDK_KEY_idotless
#define GDK_KEY_idotless GDK_idotless
#endif
#ifndef GDK_KEY_gbreve
#define GDK_KEY_gbreve GDK_gbreve
#endif
#ifndef GDK_KEY_Ubreve
#define GDK_KEY_Ubreve GDK_Ubreve
#endif
#ifndef GDK_KEY_ubreve
#define GDK_KEY_ubreve GDK_ubreve
#endif
#ifndef GDK_KEY_kra
#define GDK_KEY_kra GDK_kra
#endif
#ifndef GDK_KEY_kappa
#define GDK_KEY_kappa GDK_kappa
#endif
#ifndef GDK_KEY_Tslash
#define GDK_KEY_Tslash GDK_Tslash
#endif
#ifndef GDK_KEY_tslash
#define GDK_KEY_tslash GDK_tslash
#endif
#ifndef GDK_KEY_ENG
#define GDK_KEY_ENG GDK_ENG
#endif
#ifndef GDK_KEY_eng
#define GDK_KEY_eng GDK_eng
#endif
#ifndef GDK_KEY_OE
#define GDK_KEY_OE GDK_OE
#endif
#ifndef GDK_KEY_oe
#define GDK_KEY_oe GDK_oe
#endif
#ifndef GDK_KEY_overline
#define GDK_KEY_overline GDK_overline
#endif
#ifndef GDK_KEY_leftradical
#define GDK_KEY_leftradical GDK_leftradical
#endif
#ifndef GDK_KEY_topleftradical
#define GDK_KEY_topleftradical GDK_topleftradical
#endif
#ifndef GDK_KEY_horizconnector
#define GDK_KEY_horizconnector GDK_horizconnector
#endif
#ifndef GDK_KEY_topintegral
#define GDK_KEY_topintegral GDK_topintegral
#endif
#ifndef GDK_KEY_botintegral
#define GDK_KEY_botintegral GDK_botintegral
#endif
#ifndef GDK_KEY_vertconnector
#define GDK_KEY_vertconnector GDK_vertconnector
#endif
#ifndef GDK_KEY_topleftsqbracket
#define GDK_KEY_topleftsqbracket GDK_topleftsqbracket
#endif
#ifndef GDK_KEY_botleftsqbracket
#define GDK_KEY_botleftsqbracket GDK_botleftsqbracket
#endif
#ifndef GDK_KEY_toprightsqbracket
#define GDK_KEY_toprightsqbracket GDK_toprightsqbracket
#endif
#ifndef GDK_KEY_botrightsqbracket
#define GDK_KEY_botrightsqbracket GDK_botrightsqbracket
#endif
#ifndef GDK_KEY_topleftparens
#define GDK_KEY_topleftparens GDK_topleftparens
#endif
#ifndef GDK_KEY_botleftparens
#define GDK_KEY_botleftparens GDK_botleftparens
#endif
#ifndef GDK_KEY_toprightparens
#define GDK_KEY_toprightparens GDK_toprightparens
#endif
#ifndef GDK_KEY_botrightparens
#define GDK_KEY_botrightparens GDK_botrightparens
#endif
#ifndef GDK_KEY_leftmiddlecurlybrace
#define GDK_KEY_leftmiddlecurlybrace GDK_leftmiddlecurlybrace
#endif
#ifndef GDK_KEY_rightmiddlecurlybrace
#define GDK_KEY_rightmiddlecurlybrace GDK_rightmiddlecurlybrace
#endif
#ifndef GDK_KEY_topleftsummation
#define GDK_KEY_topleftsummation GDK_topleftsummation
#endif
#ifndef GDK_KEY_botleftsummation
#define GDK_KEY_botleftsummation GDK_botleftsummation
#endif
#ifndef GDK_KEY_topvertsummationconnector
#define GDK_KEY_topvertsummationconnector GDK_topvertsummationconnector
#endif
#ifndef GDK_KEY_botvertsummationconnector
#define GDK_KEY_botvertsummationconnector GDK_botvertsummationconnector
#endif
#ifndef GDK_KEY_toprightsummation
#define GDK_KEY_toprightsummation GDK_toprightsummation
#endif
#ifndef GDK_KEY_botrightsummation
#define GDK_KEY_botrightsummation GDK_botrightsummation
#endif
#ifndef GDK_KEY_rightmiddlesummation
#define GDK_KEY_rightmiddlesummation GDK_rightmiddlesummation
#endif
#ifndef GDK_KEY_lessthanequal
#define GDK_KEY_lessthanequal GDK_lessthanequal
#endif
#ifndef GDK_KEY_notequal
#define GDK_KEY_notequal GDK_notequal
#endif
#ifndef GDK_KEY_greaterthanequal
#define GDK_KEY_greaterthanequal GDK_greaterthanequal
#endif
#ifndef GDK_KEY_integral
#define GDK_KEY_integral GDK_integral
#endif
#ifndef GDK_KEY_therefore
#define GDK_KEY_therefore GDK_therefore
#endif
#ifndef GDK_KEY_variation
#define GDK_KEY_variation GDK_variation
#endif
#ifndef GDK_KEY_infinity
#define GDK_KEY_infinity GDK_infinity
#endif
#ifndef GDK_KEY_nabla
#define GDK_KEY_nabla GDK_nabla
#endif
#ifndef GDK_KEY_approximate
#define GDK_KEY_approximate GDK_approximate
#endif
#ifndef GDK_KEY_similarequal
#define GDK_KEY_similarequal GDK_similarequal
#endif
#ifndef GDK_KEY_ifonlyif
#define GDK_KEY_ifonlyif GDK_ifonlyif
#endif
#ifndef GDK_KEY_implies
#define GDK_KEY_implies GDK_implies
#endif
#ifndef GDK_KEY_identical
#define GDK_KEY_identical GDK_identical
#endif
#ifndef GDK_KEY_radical
#define GDK_KEY_radical GDK_radical
#endif
#ifndef GDK_KEY_includedin
#define GDK_KEY_includedin GDK_includedin
#endif
#ifndef GDK_KEY_includes
#define GDK_KEY_includes GDK_includes
#endif
#ifndef GDK_KEY_intersection
#define GDK_KEY_intersection GDK_intersection
#endif
#ifndef GDK_KEY_union
#define GDK_KEY_union GDK_union
#endif
#ifndef GDK_KEY_logicaland
#define GDK_KEY_logicaland GDK_logicaland
#endif
#ifndef GDK_KEY_logicalor
#define GDK_KEY_logicalor GDK_logicalor
#endif
#ifndef GDK_KEY_leftarrow
#define GDK_KEY_leftarrow GDK_leftarrow
#endif
#ifndef GDK_KEY_uparrow
#define GDK_KEY_uparrow GDK_uparrow
#endif
#ifndef GDK_KEY_rightarrow
#define GDK_KEY_rightarrow GDK_rightarrow
#endif
#ifndef GDK_KEY_downarrow
#define GDK_KEY_downarrow GDK_downarrow
#endif
#ifndef GDK_KEY_blank
#define GDK_KEY_blank GDK_blank
#endif
#ifndef GDK_KEY_soliddiamond
#define GDK_KEY_soliddiamond GDK_soliddiamond
#endif
#ifndef GDK_KEY_checkerboard
#define GDK_KEY_checkerboard GDK_checkerboard
#endif
#ifndef GDK_KEY_ht
#define GDK_KEY_ht GDK_ht
#endif
#ifndef GDK_KEY_ff
#define GDK_KEY_ff GDK_ff
#endif
#ifndef GDK_KEY_cr
#define GDK_KEY_cr GDK_cr
#endif
#ifndef GDK_KEY_lf
#define GDK_KEY_lf GDK_lf
#endif
#ifndef GDK_KEY_nl
#define GDK_KEY_nl GDK_nl
#endif
#ifndef GDK_KEY_vt
#define GDK_KEY_vt GDK_vt
#endif
#ifndef GDK_KEY_lowrightcorner
#define GDK_KEY_lowrightcorner GDK_lowrightcorner
#endif
#ifndef GDK_KEY_uprightcorner
#define GDK_KEY_uprightcorner GDK_uprightcorner
#endif
#ifndef GDK_KEY_upleftcorner
#define GDK_KEY_upleftcorner GDK_upleftcorner
#endif
#ifndef GDK_KEY_lowleftcorner
#define GDK_KEY_lowleftcorner GDK_lowleftcorner
#endif
#ifndef GDK_KEY_crossinglines
#define GDK_KEY_crossinglines GDK_crossinglines
#endif
#ifndef GDK_KEY_leftt
#define GDK_KEY_leftt GDK_leftt
#endif
#ifndef GDK_KEY_rightt
#define GDK_KEY_rightt GDK_rightt
#endif
#ifndef GDK_KEY_bott
#define GDK_KEY_bott GDK_bott
#endif
#ifndef GDK_KEY_topt
#define GDK_KEY_topt GDK_topt
#endif
#ifndef GDK_KEY_vertbar
#define GDK_KEY_vertbar GDK_vertbar
#endif
#ifndef GDK_KEY_emspace
#define GDK_KEY_emspace GDK_emspace
#endif
#ifndef GDK_KEY_enspace
#define GDK_KEY_enspace GDK_enspace
#endif
#ifndef GDK_KEY_em3space
#define GDK_KEY_em3space GDK_em3space
#endif
#ifndef GDK_KEY_em4space
#define GDK_KEY_em4space GDK_em4space
#endif
#ifndef GDK_KEY_digitspace
#define GDK_KEY_digitspace GDK_digitspace
#endif
#ifndef GDK_KEY_punctspace
#define GDK_KEY_punctspace GDK_punctspace
#endif
#ifndef GDK_KEY_thinspace
#define GDK_KEY_thinspace GDK_thinspace
#endif
#ifndef GDK_KEY_hairspace
#define GDK_KEY_hairspace GDK_hairspace
#endif
#ifndef GDK_KEY_emdash
#define GDK_KEY_emdash GDK_emdash
#endif
#ifndef GDK_KEY_endash
#define GDK_KEY_endash GDK_endash
#endif
#ifndef GDK_KEY_signifblank
#define GDK_KEY_signifblank GDK_signifblank
#endif
#ifndef GDK_KEY_ellipsis
#define GDK_KEY_ellipsis GDK_ellipsis
#endif
#ifndef GDK_KEY_doubbaselinedot
#define GDK_KEY_doubbaselinedot GDK_doubbaselinedot
#endif
#ifndef GDK_KEY_careof
#define GDK_KEY_careof GDK_careof
#endif
#ifndef GDK_KEY_figdash
#define GDK_KEY_figdash GDK_figdash
#endif
#ifndef GDK_KEY_leftanglebracket
#define GDK_KEY_leftanglebracket GDK_leftanglebracket
#endif
#ifndef GDK_KEY_decimalpoint
#define GDK_KEY_decimalpoint GDK_decimalpoint
#endif
#ifndef GDK_KEY_rightanglebracket
#define GDK_KEY_rightanglebracket GDK_rightanglebracket
#endif
#ifndef GDK_KEY_marker
#define GDK_KEY_marker GDK_marker
#endif
#ifndef GDK_KEY_trademark
#define GDK_KEY_trademark GDK_trademark
#endif
#ifndef GDK_KEY_signaturemark
#define GDK_KEY_signaturemark GDK_signaturemark
#endif
#ifndef GDK_KEY_trademarkincircle
#define GDK_KEY_trademarkincircle GDK_trademarkincircle
#endif
#ifndef GDK_KEY_leftopentriangle
#define GDK_KEY_leftopentriangle GDK_leftopentriangle
#endif
#ifndef GDK_KEY_rightopentriangle
#define GDK_KEY_rightopentriangle GDK_rightopentriangle
#endif
#ifndef GDK_KEY_emopencircle
#define GDK_KEY_emopencircle GDK_emopencircle
#endif
#ifndef GDK_KEY_emopenrectangle
#define GDK_KEY_emopenrectangle GDK_emopenrectangle
#endif
#ifndef GDK_KEY_leftsinglequotemark
#define GDK_KEY_leftsinglequotemark GDK_leftsinglequotemark
#endif
#ifndef GDK_KEY_rightsinglequotemark
#define GDK_KEY_rightsinglequotemark GDK_rightsinglequotemark
#endif
#ifndef GDK_KEY_leftdoublequotemark
#define GDK_KEY_leftdoublequotemark GDK_leftdoublequotemark
#endif
#ifndef GDK_KEY_rightdoublequotemark
#define GDK_KEY_rightdoublequotemark GDK_rightdoublequotemark
#endif
#ifndef GDK_KEY_prescription
#define GDK_KEY_prescription GDK_prescription
#endif
#ifndef GDK_KEY_minutes
#define GDK_KEY_minutes GDK_minutes
#endif
#ifndef GDK_KEY_seconds
#define GDK_KEY_seconds GDK_seconds
#endif
#ifndef GDK_KEY_latincross
#define GDK_KEY_latincross GDK_latincross
#endif
#ifndef GDK_KEY_hexagram
#define GDK_KEY_hexagram GDK_hexagram
#endif
#ifndef GDK_KEY_filledrectbullet
#define GDK_KEY_filledrectbullet GDK_filledrectbullet
#endif
#ifndef GDK_KEY_filledlefttribullet
#define GDK_KEY_filledlefttribullet GDK_filledlefttribullet
#endif
#ifndef GDK_KEY_filledrighttribullet
#define GDK_KEY_filledrighttribullet GDK_filledrighttribullet
#endif
#ifndef GDK_KEY_emfilledcircle
#define GDK_KEY_emfilledcircle GDK_emfilledcircle
#endif
#ifndef GDK_KEY_emfilledrect
#define GDK_KEY_emfilledrect GDK_emfilledrect
#endif
#ifndef GDK_KEY_enopencircbullet
#define GDK_KEY_enopencircbullet GDK_enopencircbullet
#endif
#ifndef GDK_KEY_enopensquarebullet
#define GDK_KEY_enopensquarebullet GDK_enopensquarebullet
#endif
#ifndef GDK_KEY_openrectbullet
#define GDK_KEY_openrectbullet GDK_openrectbullet
#endif
#ifndef GDK_KEY_opentribulletup
#define GDK_KEY_opentribulletup GDK_opentribulletup
#endif
#ifndef GDK_KEY_opentribulletdown
#define GDK_KEY_opentribulletdown GDK_opentribulletdown
#endif
#ifndef GDK_KEY_openstar
#define GDK_KEY_openstar GDK_openstar
#endif
#ifndef GDK_KEY_enfilledcircbullet
#define GDK_KEY_enfilledcircbullet GDK_enfilledcircbullet
#endif
#ifndef GDK_KEY_enfilledsqbullet
#define GDK_KEY_enfilledsqbullet GDK_enfilledsqbullet
#endif
#ifndef GDK_KEY_filledtribulletup
#define GDK_KEY_filledtribulletup GDK_filledtribulletup
#endif
#ifndef GDK_KEY_filledtribulletdown
#define GDK_KEY_filledtribulletdown GDK_filledtribulletdown
#endif
#ifndef GDK_KEY_leftpointer
#define GDK_KEY_leftpointer GDK_leftpointer
#endif
#ifndef GDK_KEY_rightpointer
#define GDK_KEY_rightpointer GDK_rightpointer
#endif
#ifndef GDK_KEY_club
#define GDK_KEY_club GDK_club
#endif
#ifndef GDK_KEY_diamond
#define GDK_KEY_diamond GDK_diamond
#endif
#ifndef GDK_KEY_heart
#define GDK_KEY_heart GDK_heart
#endif
#ifndef GDK_KEY_maltesecross
#define GDK_KEY_maltesecross GDK_maltesecross
#endif
#ifndef GDK_KEY_dagger
#define GDK_KEY_dagger GDK_dagger
#endif
#ifndef GDK_KEY_doubledagger
#define GDK_KEY_doubledagger GDK_doubledagger
#endif
#ifndef GDK_KEY_checkmark
#define GDK_KEY_checkmark GDK_checkmark
#endif
#ifndef GDK_KEY_ballotcross
#define GDK_KEY_ballotcross GDK_ballotcross
#endif
#ifndef GDK_KEY_musicalsharp
#define GDK_KEY_musicalsharp GDK_musicalsharp
#endif
#ifndef GDK_KEY_musicalflat
#define GDK_KEY_musicalflat GDK_musicalflat
#endif
#ifndef GDK_KEY_malesymbol
#define GDK_KEY_malesymbol GDK_malesymbol
#endif
#ifndef GDK_KEY_femalesymbol
#define GDK_KEY_femalesymbol GDK_femalesymbol
#endif
#ifndef GDK_KEY_telephone
#define GDK_KEY_telephone GDK_telephone
#endif
#ifndef GDK_KEY_telephonerecorder
#define GDK_KEY_telephonerecorder GDK_telephonerecorder
#endif
#ifndef GDK_KEY_phonographcopyright
#define GDK_KEY_phonographcopyright GDK_phonographcopyright
#endif
#ifndef GDK_KEY_singlelowquotemark
#define GDK_KEY_singlelowquotemark GDK_singlelowquotemark
#endif
#ifndef GDK_KEY_doublelowquotemark
#define GDK_KEY_doublelowquotemark GDK_doublelowquotemark
#endif
#ifndef GDK_KEY_cursor
#define GDK_KEY_cursor GDK_cursor
#endif
#ifndef GDK_KEY_overbar
#define GDK_KEY_overbar GDK_overbar
#endif
#ifndef GDK_KEY_downtack
#define GDK_KEY_downtack GDK_downtack
#endif
#ifndef GDK_KEY_upshoe
#define GDK_KEY_upshoe GDK_upshoe
#endif
#ifndef GDK_KEY_downstile
#define GDK_KEY_downstile GDK_downstile
#endif
#ifndef GDK_KEY_underbar
#define GDK_KEY_underbar GDK_underbar
#endif
#ifndef GDK_KEY_jot
#define GDK_KEY_jot GDK_jot
#endif
#ifndef GDK_KEY_quad
#define GDK_KEY_quad GDK_quad
#endif
#ifndef GDK_KEY_uptack
#define GDK_KEY_uptack GDK_uptack
#endif
#ifndef GDK_KEY_circle
#define GDK_KEY_circle GDK_circle
#endif
#ifndef GDK_KEY_upstile
#define GDK_KEY_upstile GDK_upstile
#endif
#ifndef GDK_KEY_downshoe
#define GDK_KEY_downshoe GDK_downshoe
#endif
#ifndef GDK_KEY_rightshoe
#define GDK_KEY_rightshoe GDK_rightshoe
#endif
#ifndef GDK_KEY_leftshoe
#define GDK_KEY_leftshoe GDK_leftshoe
#endif
#ifndef GDK_KEY_lefttack
#define GDK_KEY_lefttack GDK_lefttack
#endif
#ifndef GDK_KEY_righttack
#define GDK_KEY_righttack GDK_righttack
#endif
#ifndef GDK_KEY_ModeLock
#define GDK_KEY_ModeLock GDK_ModeLock
#endif
#ifndef GDK_KEY_MonBrightnessUp
#define GDK_KEY_MonBrightnessUp GDK_MonBrightnessUp
#endif
#ifndef GDK_KEY_MonBrightnessDown
#define GDK_KEY_MonBrightnessDown GDK_MonBrightnessDown
#endif
#ifndef GDK_KEY_KbdLightOnOff
#define GDK_KEY_KbdLightOnOff GDK_KbdLightOnOff
#endif
#ifndef GDK_KEY_KbdBrightnessUp
#define GDK_KEY_KbdBrightnessUp GDK_KbdBrightnessUp
#endif
#ifndef GDK_KEY_KbdBrightnessDown
#define GDK_KEY_KbdBrightnessDown GDK_KbdBrightnessDown
#endif
#ifndef GDK_KEY_Standby
#define GDK_KEY_Standby GDK_Standby
#endif
#ifndef GDK_KEY_HomePage
#define GDK_KEY_HomePage GDK_HomePage
#endif
#ifndef GDK_KEY_Mail
#define GDK_KEY_Mail GDK_Mail
#endif
#ifndef GDK_KEY_Start
#define GDK_KEY_Start GDK_Start
#endif
#ifndef GDK_KEY_Search
#define GDK_KEY_Search GDK_Search
#endif
#ifndef GDK_KEY_Back
#define GDK_KEY_Back GDK_Back
#endif
#ifndef GDK_KEY_Forward
#define GDK_KEY_Forward GDK_Forward
#endif
#ifndef GDK_KEY_Stop
#define GDK_KEY_Stop GDK_Stop
#endif
#ifndef GDK_KEY_Refresh
#define GDK_KEY_Refresh GDK_Refresh
#endif
#ifndef GDK_KEY_LightBulb
#define GDK_KEY_LightBulb GDK_LightBulb
#endif
#ifndef GDK_KEY_BrightnessAdjust
#define GDK_KEY_BrightnessAdjust GDK_BrightnessAdjust
#endif
#ifndef GDK_KEY_Finance
#define GDK_KEY_Finance GDK_Finance
#endif
#ifndef GDK_KEY_Community
#define GDK_KEY_Community GDK_Community
#endif
#ifndef GDK_KEY_BackForward
#define GDK_KEY_BackForward GDK_BackForward
#endif
#ifndef GDK_KEY_ApplicationLeft
#define GDK_KEY_ApplicationLeft GDK_ApplicationLeft
#endif
#ifndef GDK_KEY_ApplicationRight
#define GDK_KEY_ApplicationRight GDK_ApplicationRight
#endif
#ifndef GDK_KEY_Book
#define GDK_KEY_Book GDK_Book
#endif
#ifndef GDK_KEY_CD
#define GDK_KEY_CD GDK_CD
#endif
#ifndef GDK_KEY_WindowClear
#define GDK_KEY_WindowClear GDK_WindowClear
#endif
#ifndef GDK_KEY_Close
#define GDK_KEY_Close GDK_Close
#endif
#ifndef GDK_KEY_Copy
#define GDK_KEY_Copy GDK_Copy
#endif
#ifndef GDK_KEY_Cut
#define GDK_KEY_Cut GDK_Cut
#endif
#ifndef GDK_KEY_Display
#define GDK_KEY_Display GDK_Display
#endif
#ifndef GDK_KEY_Go
#define GDK_KEY_Go GDK_Go
#endif
#ifndef GDK_KEY_LogOff
#define GDK_KEY_LogOff GDK_LogOff
#endif
#ifndef GDK_KEY_New
#define GDK_KEY_New GDK_New
#endif
#ifndef GDK_KEY_Open
#define GDK_KEY_Open GDK_Open
#endif
#ifndef GDK_KEY_Option
#define GDK_KEY_Option GDK_Option
#endif
#ifndef GDK_KEY_Paste
#define GDK_KEY_Paste GDK_Paste
#endif
#ifndef GDK_KEY_Reload
#define GDK_KEY_Reload GDK_Reload
#endif
#ifndef GDK_KEY_RotateWindows
#define GDK_KEY_RotateWindows GDK_RotateWindows
#endif
#ifndef GDK_KEY_RotationPB
#define GDK_KEY_RotationPB GDK_RotationPB
#endif
#ifndef GDK_KEY_RotationKB
#define GDK_KEY_RotationKB GDK_RotationKB
#endif
#ifndef GDK_KEY_Save
#define GDK_KEY_Save GDK_Save
#endif
#ifndef GDK_KEY_ScrollUp
#define GDK_KEY_ScrollUp GDK_ScrollUp
#endif
#ifndef GDK_KEY_ScrollDown
#define GDK_KEY_ScrollDown GDK_ScrollDown
#endif
#ifndef GDK_KEY_ScrollClick
#define GDK_KEY_ScrollClick GDK_ScrollClick
#endif
#ifndef GDK_KEY_Send
#define GDK_KEY_Send GDK_Send
#endif
#ifndef GDK_KEY_Spell
#define GDK_KEY_Spell GDK_Spell
#endif
#ifndef GDK_KEY_SplitScreen
#define GDK_KEY_SplitScreen GDK_SplitScreen
#endif
#ifndef GDK_KEY_Time
#define GDK_KEY_Time GDK_Time
#endif
#ifndef GDK_KEY_SelectButton
#define GDK_KEY_SelectButton GDK_SelectButton
#endif
#ifndef GDK_KEY_View
#define GDK_KEY_View GDK_View
#endif
#ifndef GDK_KEY_TopMenu
#define GDK_KEY_TopMenu GDK_TopMenu
#endif

#ifndef HAVE_GTK_CELL_RENDERER_GET_ALIGNMENT
#define gtk_cell_renderer_get_alignment(_cr_,_px_,_py_) do {	\
  gfloat *px = (_px_);						\
  gfloat *py = (_py_);						\
  if (px) *px = (_cr_)->xalign;					\
  if (py) *py = (_cr_)->yalign;					\
} while (0)
#endif

#ifndef HAVE_GTK_CELL_RENDERER_GET_PADDING
#define gtk_cell_renderer_get_padding(_cr_,_px_,_py_) do {	\
  int *px = (_px_);						\
  int *py = (_py_);						\
  if (px) *px = (_cr_)->xpad;					\
  if (py) *py = (_cr_)->ypad;					\
} while (0)
#endif

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_BACKGROUND_SET
#define gtk_cell_renderer_text_get_background_set(_cr_) \
  gnm_object_get_bool ((_cr_), "background-set")
#endif

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_FOREGROUND_SET
#define gtk_cell_renderer_text_get_foreground_set(_cr_) \
  gnm_object_get_bool ((_cr_), "foreground-set")
#endif

/* This function does not exist in gtk+ yet.  634344.  */
#ifndef HAVE_GTK_CELL_RENDERER_TEXT_GET_EDITABLE
#define gtk_cell_renderer_text_get_editable(_cr_) \
  gnm_object_get_bool ((_cr_), "editable")
#endif

#ifdef HAVE_GTK_OBJECT_DESTROY
#define gnm_destroy_class(_class_) ((GtkObjectClass *)(_class_))
#define gnm_destroy_class_chain(_class_,_obj_) gnm_destroy_class(_class_)->destroy((GtkObject*)(_obj_))
#define gnm_destroy_class_set(_class_,_func_) gnm_destroy_class(_class_)->destroy = ((void (*)(GtkObject*))(_func_))
#else
#define gnm_destroy_class(_class_) ((GtkWidgetClass *)(_class_))
#define gnm_destroy_class_chain(_class_,_obj_) gnm_destroy_class(_class_)->destroy((GtkWidget*)(_obj_))
#define gnm_destroy_class_set(_class_,_func_) gnm_destroy_class(_class_)->destroy = (_func_)
#endif

#ifndef HAVE_GTK_DIALOG_GET_ACTION_AREA
#define gtk_dialog_get_action_area(x) ((x)->action_area)
#endif

#ifndef HAVE_GTK_DIALOG_GET_CONTENT_AREA
#define gtk_dialog_get_content_area(x) ((x)->vbox)
#endif

#ifndef HAVE_GTK_ENTRY_GET_TEXT_LENGTH
#define gtk_entry_get_text_length(x) g_utf8_strlen(gtk_entry_get_text((x)),-1)
#endif

#ifndef HAVE_GTK_ENTRY_GET_TEXT_AREA
#  ifdef HAVE_GTK_ENTRY_TEXT_AREA
#    define gtk_entry_get_text_area(x) ((x)->text_area)
#  else
#    define gtk_entry_get_text_area(x) ((x)->_g_sealed__text_area)
#  endif
#endif

#ifndef HAVE_GTK_ENTRY_GET_OVERWRITE_MODE
#define gtk_entry_get_overwrite_mode(_e_) ((_e_)->overwrite_mode)
#endif

/* This function does not exist in gtk+ yet.  634342.  */
#ifndef HAVE_GTK_ENTRY_SET_EDITING_CANCELLED
#define gtk_entry_set_editing_cancelled(_e_,_b_) \
  g_object_set ((_e_), "editing-canceled", (gboolean)(_b_), NULL)
#endif

#ifndef HAVE_GTK_LAYOUT_GET_BIN_WINDOW
#define gtk_layout_get_bin_window(x) ((x)->bin_window)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_DATA
#define gtk_selection_data_get_data(_s_) ((_s_)->data)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_LENGTH
#define gtk_selection_data_get_length(_s_) ((_s_)->length)
#endif

#ifndef HAVE_GTK_SELECTION_DATA_GET_TARGET
#define gtk_selection_data_get_target(_s_) ((_s_)->target)
#endif

#ifndef HAVE_GTK_WIDGET_RENDER_ICON_PIXBUF
#define gtk_widget_render_icon_pixbuf(_w_,_sid_,_size_) gtk_widget_render_icon((_w_),(_sid_),(_size_),NULL)
#endif

#ifndef HAVE_GTK_WIDGET_SET_VISIBLE
#define gtk_widget_set_visible(_w_,_v_) do { if (_v_) gtk_widget_show (_w_); else gtk_widget_hide (_w_); } while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_VISIBLE
#define gtk_widget_get_visible(_w_) GTK_WIDGET_VISIBLE((_w_))
#endif

#ifndef HAVE_GTK_WIDGET_IS_SENSITIVE
#define gtk_widget_is_sensitive(w) GTK_WIDGET_IS_SENSITIVE ((w))
#endif

#ifndef HAVE_GTK_WIDGET_IS_TOPLEVEL
#define gtk_widget_is_toplevel(w_) (GTK_WIDGET_FLAGS ((w_)) & GTK_TOPLEVEL)
#endif

#ifndef HAVE_GTK_WIDGET_GET_STATE
#define gtk_widget_get_state(_w) GTK_WIDGET_STATE((_w))
#endif

#ifndef HAVE_GTK_WIDGET_GET_WINDOW
#define gtk_widget_get_window(w) ((w)->window)
#endif

#ifndef HAVE_GTK_WIDGET_GET_ALLOCATION
#define gtk_widget_get_allocation(w,a) (*(a) = (w)->allocation)
#endif

#ifndef HAVE_GTK_WIDGET_GET_STYLE
#define gtk_widget_get_style(w) ((w)->style)
#endif

#ifndef HAVE_GTK_WIDGET_HAS_FOCUS
#define gtk_widget_has_focus(w) GTK_WIDGET_HAS_FOCUS (w)
#endif

#ifndef HAVE_GTK_WIDGET_SET_CAN_DEFAULT
#define gtk_widget_set_can_default(w,t)					\
	do {								\
	if (t) GTK_WIDGET_SET_FLAGS ((w), GTK_CAN_DEFAULT);	\
		else GTK_WIDGET_UNSET_FLAGS ((w), GTK_CAN_DEFAULT);	\
	} while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_CAN_FOCUS
#define gtk_widget_get_can_focus(_w_) GTK_WIDGET_CAN_FOCUS((_w_))
#endif

#ifndef HAVE_GTK_WIDGET_SET_CAN_FOCUS
#define gtk_widget_set_can_focus(w,t)					\
	do {								\
		if ((t)) GTK_WIDGET_SET_FLAGS ((w), GTK_CAN_FOCUS);	\
		else GTK_WIDGET_UNSET_FLAGS ((w), GTK_CAN_FOCUS);	\
	} while (0)
#endif

#ifndef HAVE_GTK_WIDGET_GET_REALIZED
#  ifdef HAVE_WORKING_GTK_WIDGET_REALIZED
#    define gtk_widget_get_realized(w) GTK_WIDGET_REALIZED ((w))
#  else
#    define gtk_widget_get_realized(wid) (((GTK_OBJECT (wid)->_g_sealed__flags) & GTK_REALIZED) != 0)
#  endif
#endif

#ifndef HAVE_GTK_ADJUSTMENT_CONFIGURE
#define gtk_adjustment_configure(_a,_v,_l,_u,_si,_pi,_ps)	\
  g_object_set ((_a),						\
                "lower", (double)(_l),				\
                "upper", (double)(_u),				\
                "step-increment", (double)(_si),		\
                "page-increment", (double)(_pi),		\
                "page-size", (double)(_ps),			\
		"value", (double)(_v),				\
                NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_LOWER
#define gtk_adjustment_get_lower(_a) ((_a)->lower)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_UPPER
#define gtk_adjustment_get_upper(_a) ((_a)->upper)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_PAGE_SIZE
#define gtk_adjustment_get_page_size(_a) ((_a)->page_size)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_PAGE_INCREMENT
#define gtk_adjustment_get_page_increment(_a) ((_a)->page_increment)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_GET_STEP_INCREMENT
#define gtk_adjustment_get_step_increment(_a) ((_a)->step_increment)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_LOWER
#define gtk_adjustment_set_lower(_a,_l) \
  g_object_set ((_a), "lower", (double)(_l), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_SET_UPPER
#define gtk_adjustment_set_upper(_a,_u) \
  g_object_set ((_a), "upper", (double)(_u), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_PAGE_INCREMENT
#define gtk_adjustment_set_page_increment(_a,_pi) \
  g_object_set ((_a), "page-increment", (double)(_pi), NULL)
#endif

#ifndef HAVE_GTK_ADJUSTMENT_STEP_INCREMENT
#define gtk_adjustment_set_step_increment(_a,_si) \
  g_object_set ((_a), "step-increment", (double)(_si), NULL)
#endif

#ifndef HAVE_GTK_TABLE_GET_SIZE
#  ifdef HAVE_GTK_TABLE_NROWS
#     define gtk_table_get_size(_t,_r,_c) do {	\
       int *_pr = (_r);				\
       int *_pc = (_c);				\
       GtkTable *_pt = (_t);			\
       if (_pr) *_pr = _pt->nrows;		\
       if (_pc) *_pc = _pt->ncols;		\
     } while (0)
#  else
     /* At first sealed with no accessors.  */
#     define gtk_table_get_size(_t,_r,_c) do {			\
       int *_pr = (_r);						\
       int *_pc = (_c);						\
       GtkTable *_pt = (_t);					\
       if (_pr) g_object_get (_pt, "n-rows", _pr, NULL);	\
       if (_pc) g_object_get (_pt, "n-columns", _pc, NULL);	\
     } while (0)
#  endif
#endif

/* This function does not exist in gtk+ yet.  634100.  */
#ifndef HAVE_GTK_TREE_VIEW_COLUMN_GET_BUTTON
#  ifdef HAVE_GTK_TREE_VIEW_COLUMN_BUTTON
#    define gtk_tree_view_column_get_button(_c) ((_c)->button)
#  else
#    define gtk_tree_view_column_get_button(_c) ((_c)->_g_sealed__button)
#  endif
#endif

#ifndef HAVE_GTK_WINDOW_GET_DEFAULT_WIDGET
#define gtk_window_get_default_widget(_w_) ((_w_)->default_widget)
#endif

#endif
