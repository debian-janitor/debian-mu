/* -*-mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-*/
/*
** Copyright (C) 2008-2011 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  
**  
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif /*HAVE_CONFIG_H*/

#include <glib.h>
#include "src/mu-flags.h"
#include "test-mu-common.h"


static void
test_mu_flag_char (void)
{
	g_assert_cmpuint (mu_flag_char (MU_FLAG_DRAFT),		==, 'D');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_FLAGGED),	==, 'F');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_PASSED),	==, 'P');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_REPLIED),	==, 'R');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_SEEN),		==, 'S');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_TRASHED),	==, 'T');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_NEW),		==, 'N');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_SIGNED),	==, 's');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_ENCRYPTED),	==, 'x');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_HAS_ATTACH),	==, 'a');
	g_assert_cmpuint (mu_flag_char (MU_FLAG_UNREAD),	==, 'u');
	g_assert_cmpuint (mu_flag_char (12345),			==,  0);
}



static void
test_mu_flag_name (void)
{
	g_assert_cmpstr (mu_flag_name (MU_FLAG_DRAFT),		==, "draft");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_FLAGGED),	==, "flagged");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_PASSED),		==, "passed");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_REPLIED),	==, "replied");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_SEEN),		==, "seen");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_TRASHED),	==, "trashed");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_NEW),		==, "new");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_SIGNED),		==, "signed");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_ENCRYPTED),	==, "encrypted");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_HAS_ATTACH),	==, "attach");
	g_assert_cmpstr (mu_flag_name (MU_FLAG_UNREAD),		==, "unread");
	g_assert_cmpstr (mu_flag_name (12345),			==,  NULL);
}

static void
test_mu_flags_to_str_s (void)
{
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_PASSED|MU_FLAG_SIGNED,
					   MU_FLAG_TYPE_ANY),
			 ==, "Ps");
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_NEW, MU_FLAG_TYPE_ANY),
			 ==, "N");
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_HAS_ATTACH|MU_FLAG_TRASHED,
					   MU_FLAG_TYPE_ANY),
			 ==, "Ta");
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_NONE, MU_FLAG_TYPE_ANY),
			 ==, "");
	
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_PASSED|MU_FLAG_SIGNED,
					   MU_FLAG_TYPE_CONTENT),
			 ==, "s");

	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_NEW, MU_FLAG_TYPE_MAILDIR),
			 ==, "N");
	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_HAS_ATTACH|MU_FLAG_TRASHED,
					   MU_FLAG_TYPE_MAILFILE),
			 ==, "T");

	g_assert_cmpstr (mu_flags_to_str_s(MU_FLAG_NONE, MU_FLAG_TYPE_PSEUDO),
			 ==, "");
}


static void
test_mu_flags_from_str (void)
{
	g_assert_cmpuint (mu_flags_from_str ("RP", MU_FLAG_TYPE_ANY), ==,
			  MU_FLAG_REPLIED | MU_FLAG_PASSED);
	g_assert_cmpuint (mu_flags_from_str ("Ns", MU_FLAG_TYPE_ANY), ==,
			  MU_FLAG_NEW | MU_FLAG_SIGNED);
	g_assert_cmpuint (mu_flags_from_str ("axD", MU_FLAG_TYPE_ANY), ==,
			  MU_FLAG_HAS_ATTACH | MU_FLAG_ENCRYPTED | MU_FLAG_DRAFT);

	g_assert_cmpuint (mu_flags_from_str ("RP", MU_FLAG_TYPE_MAILFILE), ==,
			  MU_FLAG_REPLIED | MU_FLAG_PASSED);
	g_assert_cmpuint (mu_flags_from_str ("Ns", MU_FLAG_TYPE_MAILFILE), ==,
			  MU_FLAG_NONE);

	g_assert_cmpuint (mu_flags_from_str ("qwi", MU_FLAG_TYPE_MAILFILE), ==,
			  MU_FLAG_INVALID);	
}

static void
test_mu_flags_from_str_delta (void)
{
	g_assert_cmpuint (mu_flags_from_str_delta ("+S-R",
						   MU_FLAG_REPLIED | MU_FLAG_DRAFT,
						   MU_FLAG_TYPE_ANY),==,
			  MU_FLAG_SEEN | MU_FLAG_DRAFT);

	g_assert_cmpuint (mu_flags_from_str_delta ("",
						   MU_FLAG_REPLIED | MU_FLAG_DRAFT,
						   MU_FLAG_TYPE_ANY),==,
			  MU_FLAG_REPLIED | MU_FLAG_DRAFT);

	g_assert_cmpuint (mu_flags_from_str_delta ("-N+P+S-D",
						   MU_FLAG_SIGNED | MU_FLAG_DRAFT,
						   MU_FLAG_TYPE_ANY),==,
			  MU_FLAG_PASSED | MU_FLAG_SEEN | MU_FLAG_SIGNED);

	/* g_assert_cmpuint (mu_flags_from_str_delta ("foobar", */
	/* 					   MU_FLAG_INVALID, */
	/* 					   MU_FLAG_TYPE_ANY),==, */
	/* 		  MU_FLAG_INVALID); */	
}
	


int
main (int argc, char *argv[])
{
	int rv;
	g_test_init (&argc, &argv, NULL);

	/* mu_msg_str_date */
	g_test_add_func ("/mu-flags/test-mu-flag-char", test_mu_flag_char);
	g_test_add_func ("/mu-flags/test-mu-flag-name",test_mu_flag_name);
	g_test_add_func ("/mu-flags/test-mu-flags-to-str-s",test_mu_flags_to_str_s);
	g_test_add_func ("/mu-flags/test-mu-flags-from-str",test_mu_flags_from_str);
	g_test_add_func ("/mu-flags/test-mu-flags-from-str-delta",test_mu_flags_from_str_delta );

	g_log_set_handler (NULL,
			   G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL| G_LOG_FLAG_RECURSION,
			   (GLogFunc)black_hole, NULL);

	rv = g_test_run ();

	return rv;		
}
