/*
   This file is from wp2x and recoded from latin1 -> utf8
   No other modifications where made.
   Jody Goldberg 2002 Aug 13

This is wp2x, a WordPerfect 5.1 to whatever converter
ftp://ftp.penguin.cz/pub/users/mhi/wp2x/
              
                        ------------------

Copyright (C) 1989-1991 Raymond Chen 
Copyright (c) 1993-1994 Michael Richardson
Copyright (c) 2000 Martin Hinner <mhi@penguin.cz>

This program and its supporting files are Copyright 1991 by
Raymond Chen.  You may distribute the program freely, provided
it is distributed in its entirely, including this README file.
Any modifications to the program should be clearly marked as such.

                        ------------------

Wp2x  is  intended to convert simple files stored in Word Perfect 5.1
format into any other document processing language  that  uses plain
text files.  Examples include TeX, LaTeX, troff, GML and HTML.

I (mhi) found this program in May 2000. It didn't compile successfully
on my Linux and BSD boxes, so I wrote some patches. Because Raymond
Chen's e-mail addresses are no longer working, I decided to
maintain this useful software myself. PLEASE, if you know author's
current e-mail address, let me know.

For more information see doc/README.original or ChangeLog
 */




/*
 * Define structures to hold extended character set translations
 * 
 */

char NOC[] = "??";

unsigned char const *wp_isolatin1_chars[9][256] =
{
/* charset 0 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, "¦", NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 1 */
  {
    NOC, "·", NOC, NOC, "­", NOC, "´", "¨", "¯", NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, "¸", NOC, NOC, NOC, NOC, NOC, "ß", NOC, NOC, "Á", "á", "Â", "â", "Ä", "ä",
    "À", "à", "Å", "å", "Æ", "æ", "Ç", "ç", "É", "é", "Ê", "ê", "Ë", "ë", "È", "è",
    "Í", "í", "Î", "î", "Ï", "ï", "Ì", "ì", "Ñ", "ñ", "Ó", "ó", "Ô", "ô", "Ö", "ö",
    "Ò", "ò", "Ú", "ú", "Û", "û", "Ü", "ü", "Ù", "ù", NOC, "ÿ", "Ã", "ã", "Q", "R",
    "Ø", "ø", "Õ", "õ", "ý", "ý", "Ð", "ð", "Þ", "þ", NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 2 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 3 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 4 */
  {
    NOC, NOC, NOC, NOC, NOC, "¶", "§", "¡", "¿", "«", "»", "£", "¥", NOC, NOC, "ª",
    "º", "½", "¼", "¢", "²", NOC, "®", "©", "¤", "¾", "³", NOC, NOC, NOC, NOC, "\"",
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, "*", "*", NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, "\"",
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, "¹", NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 5 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 6 */
  {
    NOC, "±", NOC, NOC, NOC, NOC, NOC, NOC, "÷", NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, "¬", NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, "°", NOC, NOC, "×", NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 7 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  },

/* charset 8 */
  {
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, "µ", NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC,
    NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC, NOC
  }
};
