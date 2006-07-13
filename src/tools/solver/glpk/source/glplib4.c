/* glplib4.c */

/*----------------------------------------------------------------------
-- This code is part of GNU Linear Programming Kit (GLPK).
--
-- Copyright (C) 2000, 01, 02, 03, 04, 05, 06 Andrew Makhorin,
-- Department for Applied Informatics, Moscow Aviation Institute,
-- Moscow, Russia. All rights reserved. E-mail: <mao@mai2.rcnet.ru>.
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
-- 02110-1301, USA.
----------------------------------------------------------------------*/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "glplib.h"

/*----------------------------------------------------------------------
-- write_bmp16 - write 16-color raster image in Windows Bitmap format.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int write_bmp16(char *fname, int m, int n, char map[]);
--
-- *Description*
--
-- The routine write_bmp16 writes 16-color raster image in uncompressed
-- Windows Bitmap format (BMP) to a binary file whose name is specified
-- by the character string fname.
--
-- The parameters m and n specify, respectively, the number of rows and
-- the numbers of columns (i.e. height and width) of the raster image.
--
-- The character array map has m*n elements. Elements map[0, ..., n-1]
-- correspond to the first (top) scanline, elements map[n, ..., 2*n-1]
-- correspond to the second scanline, etc.
--
-- Each element of the array map specifies a color of the corresponding
-- pixel as 8-bit binary number XXXXIRGB, where four high-order bits (X)
-- are ignored, I is high intensity bit, R is red color bit, G is green
-- color bit, and B is blue color bit. Thus, all 16 possible colors are
-- coded as following decimal numbers:
--
--    0 = black         8 = dark gray
--    1 = blue          9 = bright blue
--    2 = green        10 = bright green
--    3 = cyan         11 = bright cyan
--    4 = red          12 = bright red
--    5 = magenta      13 = bright magenta
--    6 = brown        14 = yellow
--    7 = light gray   15 = white
--
-- *Returns*
--
-- If no error occured, the routine returns zero; otherwise, it prints
-- an appropriate error message and returns non-zero. */

static void put_byte(FILE *fp, int val)
{     unsigned char b = (unsigned char)val;
      fwrite(&b, sizeof(char), 1, fp);
      return;
}

static void put_word(FILE *fp, int val) /* big endian */
{     put_byte(fp, val);
      put_byte(fp, val >> 8);
      return;
}

static void put_dword(FILE *fp, int val) /* big endian */
{     put_word(fp, val);
      put_word(fp, val >> 16);
      return;
}

int write_bmp16(char *fname, int m, int n, char map[])
{     FILE *fp = NULL;
      int offset, bmsize, i, j, b;
      if (m < 1)
      {  print("write_bmp16: m = %d; invalid height", m);
         goto fail;
      }
      if (n < 1)
      {  print("write_bmp16: n = %d; invalid width", n);
         goto fail;
      }
      fp = ufopen(fname, "wb");
      if (fp == NULL)
      {  print("write_bmp16: unable to create `%s' - %s", fname,
            strerror(errno));
         goto fail;
      }
      offset = 14 + 40 + 16 * 4;
      bmsize = (4 * n + 31) / 32;
      /* struct BMPFILEHEADER (14 bytes) */
      /* UINT bfType */          put_byte(fp, 'B'), put_byte(fp, 'M');
      /* DWORD bfSize */         put_dword(fp, offset + bmsize * 4);
      /* UINT bfReserved1 */     put_word(fp, 0);
      /* UNIT bfReserved2 */     put_word(fp, 0);
      /* DWORD bfOffBits */      put_dword(fp, offset);
      /* struct BMPINFOHEADER (40 bytes) */
      /* DWORD biSize */         put_dword(fp, 40);
      /* LONG biWidth */         put_dword(fp, n);
      /* LONG biHeight */        put_dword(fp, m);
      /* WORD biPlanes */        put_word(fp, 1);
      /* WORD biBitCount */      put_word(fp, 4);
      /* DWORD biCompression */  put_dword(fp, 0 /* BI_RGB */);
      /* DWORD biSizeImage */    put_dword(fp, 0);
      /* LONG biXPelsPerMeter */ put_dword(fp, 2953 /* 75 dpi */);
      /* LONG biYPelsPerMeter */ put_dword(fp, 2953 /* 75 dpi */);
      /* DWORD biClrUsed */      put_dword(fp, 0);
      /* DWORD biClrImportant */ put_dword(fp, 0);
      /* struct RGBQUAD (16 * 4 bytes) */
      /* CGA-compatible colors: */
      /*  0 = black */           put_dword(fp, 0x000000);
      /*  1 = blue */            put_dword(fp, 0x000080);
      /*  2 = green */           put_dword(fp, 0x008000);
      /*  3 = cyan */            put_dword(fp, 0x008080);
      /*  4 = red */             put_dword(fp, 0x800000);
      /*  5 = magenta */         put_dword(fp, 0x800080);
      /*  6 = brown */           put_dword(fp, 0x808000);
      /*  7 = light gray */      put_dword(fp, 0xC0C0C0);
      /*  8 = dark gray */       put_dword(fp, 0x808080);
      /*  9 = bright blue */     put_dword(fp, 0x0000FF);
      /* 10 = bright green */    put_dword(fp, 0x00FF00);
      /* 11 = bright cyan */     put_dword(fp, 0x00FFFF);
      /* 12 = bright red */      put_dword(fp, 0xFF0000);
      /* 13 = bright magenta */  put_dword(fp, 0xFF00FF);
      /* 14 = yellow */          put_dword(fp, 0xFFFF00);
      /* 15 = white */           put_dword(fp, 0xFFFFFF);
      /* pixel data bits */
      b = 0;
      for (i = m - 1; i >= 0; i--)
      {  for (j = 0; j < ((n + 7) / 8) * 8; j++)
         {  b <<= 4;
            b |= (j < n ? map[i * n + j] & 15 : 0);
            if (j & 1) put_byte(fp, b);
         }
      }
      fflush(fp);
      if (ferror(fp))
      {  print("write_bmp16: write error on `%s' - %s", fname,
            strerror(errno));
         goto fail;
      }
      ufclose(fp);
      return 0;
fail: if (fp != NULL) ufclose(fp);
      return 1;
}

/* eof */
