/* format.c - attempts to emulate excel's number formatting ability.
 * Copyright (C) 1998 Chris Lahey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
gchar *format_text( gchar *format, gdouble number );

void
test()
{
  printf( "%s|\n", format_text( "??0000.00?", 12.3456789 ) );
  printf( "%s|\n", format_text( "??0000.00?", 12.3 ) );
  printf( "%s|\n", format_text( "??0000.00?", 12345.6789 ) );
  printf( "%s|\n", format_text( "??????.00?", 0.123456789 ) );
  printf( "%s|\n", format_text( "0.000,,", 12200000 ) );
}

int
main( int argc, gchar *argv )
{
  test();
  return 0;
}

void roundup( GString *string )
{
  int i;
  
  for ( i = string->len - 1; string->str[i] == '9'; i--)
    {
      string->str[i] = '0';
    }
  if ( string->str[i] == '.' )
    {
      /* FIXME */
    }
  else
    {
      string->str[i] ++;
    }
}

gchar *
format_text( gchar *format, gdouble number )
{
  gint left_req = 0, right_req = 0;
  gint left_spaces = 0, right_spaces = 0;
  gint right_allowed = 0;
  gint i = 0;
  gdouble temp;
  gboolean negative = FALSE;
  GString *string = g_string_new( "" );
#if 0
  gint length;
#endif
  gchar *returnvalue;
  gint zero_count;

  for ( i = 0; format[i] == '#'; i++ )
    /* Empty statement */;
  for ( ; format[i] == '?'; i++ )
    left_spaces ++;
  for ( ; format[i] == '0'; i++ )
    {
      left_req ++;
      left_spaces ++;
    }
  if ( format[i] == '.' )
    {
      i++;
      for ( ; format[i] == '0'; i++ )
	{
	  right_req ++;
	  right_allowed ++;
	  right_spaces ++;
	}
      for ( ; format[i] == '?'; i++ )
	{
	  right_spaces ++;
	  right_allowed ++;
	}
      for ( ; format[i] == '#'; i++ )
	right_allowed ++;
    }
  for ( ; format[i] == ','; i++ )
    number /= 1000.0;
  if ( format[i] )
    return NULL;
  if ( number < 0.0 )
    {
      number = - number;
      negative = TRUE;
    }
#if 0
  length = ceil( log10( number ) );
  if ( log10( number ) == ceil( log10( number ) ) )
    length ++;
  length = length > left_req ? length : left_req;
  length += 1;
  length += right_allowed;

  g_string_maybe_expand( string, length );
#endif
  
  for ( temp = number; temp >= 1.0; temp /= 10.0 )
    {
      gint digit = floor( temp );
      digit %= 10;
      g_string_prepend_c( string, digit + '0' );
      if ( left_req > 0 )
	left_req --;
      if ( left_spaces > 0 )
	left_spaces --;

    }

  for ( ; left_req > 0; left_req --, left_spaces -- )
    {
      g_string_prepend_c( string, '0' );
    }

  for ( ; left_spaces > 0; left_spaces -- )
    {
      g_string_prepend_c( string, ' ' );
    }

  if( negative )
    g_string_prepend_c( string, '-' );

  g_string_append_c( string, '.' );

  temp = number - floor( number );

  for ( ; right_req > 0; right_req --, right_allowed --, right_spaces -- )
    {
      gint digit;
      temp *= 10.0;
      digit = floor( temp );
      temp -= floor( temp );
      if ( right_allowed == 1 && floor( temp * 10.0 ) >= 5 )
	{
	  if ( digit < 9 )
	    digit ++;
	  else
	    {
	      digit = 0;
	      roundup( string );
	    }
	}
      g_string_append_c( string, digit + '0' );
    }

  zero_count = 0;
  
  for ( ; right_allowed > 0; right_allowed -- )
    {
      gint digit;
      temp *= 10.0;
      digit = floor( temp );
      temp -= floor( temp );
      if ( right_allowed == 1 && floor( temp * 10.0 ) >= 5 )
	{
	  if ( digit < 9 )
	    digit ++;
	  else
	    {
	      digit = 0;
	      roundup( string );
	    }
	}
      if ( digit == 0 )
	zero_count ++;
      else
	{
	  right_spaces -= zero_count + 1;
	  zero_count = 0;
	}
      g_string_append_c( string, digit + '0' );
    }


  g_string_truncate( string, string->len - zero_count );

  for ( ; right_spaces > 0; right_spaces -- )
    {
      g_string_append_c( string, ' ' );
    }
  
  returnvalue = g_malloc0( string->len + 1);
  strncpy( returnvalue, string->str, string->len );
  returnvalue[string->len] = 0;

  g_string_free( string, TRUE );
  
  return returnvalue;
}
