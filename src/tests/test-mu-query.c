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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /*HAVE_CONFIG_H*/

#include <glib.h>
#include <glib/gstdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>

#include "test-mu-common.h"
#include "src/mu-query.h"
#include "src/mu-str.h"
#include "src/mu-store.h"

static gchar*
fill_database (const char *testdir)
{
	gchar *cmdline, *tmpdir, *xpath;

	tmpdir = test_mu_common_get_random_tmpdir();
	cmdline = g_strdup_printf ("%s index --muhome=%s --maildir=%s"
				   " --quiet",
				   MU_PROGRAM, tmpdir, testdir);

	if (g_test_verbose())
		g_printerr ("\n%s\n", cmdline);

	g_assert (g_spawn_command_line_sync (cmdline, NULL, NULL,
					     NULL, NULL));
	g_free (cmdline);

	xpath= g_strdup_printf ("%s%c%s", tmpdir,
				G_DIR_SEPARATOR, "xapian");
	g_free (tmpdir);

	return xpath;
}

static void
assert_no_dups (MuMsgIter *iter)
{
	GHashTable *hash;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      (GDestroyNotify)g_free, NULL);

	mu_msg_iter_reset (iter);
	while (!mu_msg_iter_is_done(iter)) {
		MuMsg *msg;
		msg = mu_msg_iter_get_msg_floating (iter);
		/* make sure there are no duplicates */
		g_assert (!g_hash_table_lookup (hash, mu_msg_get_path (msg)));
		g_hash_table_insert (hash, g_strdup (mu_msg_get_path(msg)),
				     GUINT_TO_POINTER(TRUE));
		mu_msg_iter_next (iter);
	}
	mu_msg_iter_reset (iter);
	g_hash_table_destroy (hash);
}


/* note: this also *moves the iter* */
static guint
run_and_count_matches (const char *xpath, const char *query)
{
	MuQuery  *mquery;
	MuMsgIter *iter;
	MuStore *store;
	guint count1, count2;

	store = mu_store_new_read_only (xpath, NULL);
	g_assert (store);

	mquery = mu_query_new (store, NULL);
	g_assert (query);

	mu_store_unref (store);

	if (g_test_verbose()) {
		char *xs;
		g_print ("\n==> query: %s\n", query);
		xs = mu_query_preprocess (query, NULL);
		g_print ("==> preproc: '%s'\n", xs);
		g_free (xs);
		xs = mu_query_as_string (mquery, query, NULL);
		g_print ("==> xquery: '%s'\n", xs);
		g_free (xs);
	}


	iter = mu_query_run (mquery, query, FALSE, MU_MSG_FIELD_ID_NONE,
			     FALSE, -1, NULL);
	mu_query_destroy (mquery);
	g_assert (iter);

	assert_no_dups (iter);

	/* run query twice, to test mu_msg_iter_reset */
	for (count1 = 0; !mu_msg_iter_is_done(iter);
	     mu_msg_iter_next(iter), ++count1);

	mu_msg_iter_reset (iter);

	assert_no_dups (iter);

	for (count2 = 0; !mu_msg_iter_is_done(iter);
	     mu_msg_iter_next(iter), ++count2);

	mu_msg_iter_destroy (iter);

	g_assert_cmpuint (count1, ==, count2);

	return count1;
}

typedef struct  {
	const char *query;
	size_t count; /* expected number of matches */
} QResults;

static void
test_mu_query_01 (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "basic",              3 },
		{ "question",           5 },
		{ "thanks",             2 },
		{ "html",               4 },
		{ "subject:elisp",      1 },
		{ "html AND contains",  1 },
		{ "html and contains",  1 },
		{ "from:pepernoot",     0 },
		{ "foo:pepernoot",      0 },
		{ "funky",              1 },
		{ "fünkÿ",              1 },
		{ "",                   13 }
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
}

static void
test_mu_query_02 (void)
{
	const char* q;
	gchar *xpath;

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath);

	q = "i:f7ccd24b0808061357t453f5962w8b61f9a453b684d0@mail.gmail.com";

	g_assert_cmpuint (run_and_count_matches(xpath, q), ==, 1);
	g_free (xpath);
}


static void
test_mu_query_03 (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "ploughed", 1},
		{ "i:3BE9E6535E3029448670913581E7A1A20D852173@"
		  "emss35m06.us.lmco.com", 1},

		/* subsets of the words in the subject should match */
		{ "s:gcc include search order" , 1},
		{ "s:gcc include search" , 1},
		{ "s:search order" , 1},
		{ "s:include" , 1},

		{ "s:lisp", 1},
		{ "s:LISP", 1},

		{ "s:\"Re: Learning LISP; Scheme vs elisp.\"", 1},
		{ "subject:Re: Learning LISP; Scheme vs elisp.", 0},
		{ "subject:\"Re: Learning LISP; Scheme vs elisp.\"", 1},
		{ "to:help-gnu-emacs@gnu.org", 4},
		{ "t:help-gnu-emacs", 0},
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	g_free (xpath);
}


static void
test_mu_query_04 (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "frodo@example.com", 1},
		{ "f:frodo@example.com", 1},
		{ "f:Frodo Baggins", 1},
		{ "bilbo@anotherexample.com", 1},
		{ "t:bilbo@anotherexample.com", 1},
		{ "t:bilbo", 1},
		{ "f:bilbo", 0},
 		{ "baggins", 1},
		{ "prio:h", 1},
		{ "prio:high", 1},
		{ "prio:normal", 5},
		{ "prio:l", 7},
		{ "not prio:l", 6},
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	g_free (xpath);
}


static void
test_mu_query_logic (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "subject:gcc" , 1},
		{ "subject:lisp" , 1},
		{ "subject:gcc OR subject:lisp" , 2},
		{ "subject:gcc or subject:lisp" , 2},
		{ "subject:gcc AND subject:lisp" , 0},

		{ "subject:gcc OR (subject:scheme AND subject:elisp)" , 2},
		{ "(subject:gcc OR subject:scheme) AND subject:elisp" , 1}
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	g_free (xpath);
}




static void
test_mu_query_accented_chars_01 (void)
{
	MuQuery *query;
	MuMsgIter *iter;
	MuMsg *msg;
	MuStore *store;
	gchar *xpath;
	GError *err;
	gchar *summ;

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

	store = mu_store_new_read_only (xpath, NULL);
	g_assert (store);

	query = mu_query_new (store, NULL);
	mu_store_unref (store);

	iter = mu_query_run (query, "fünkÿ", FALSE, MU_MSG_FIELD_ID_NONE,
			     FALSE, -1, NULL);
	err = NULL;
	msg = mu_msg_iter_get_msg_floating (iter); /* don't unref */
	if (!msg) {
		g_warning ("error getting message: %s", err->message);
		g_error_free (err);
		g_assert_not_reached ();
	}

	g_assert_cmpstr (mu_msg_get_subject(msg),==,
			 "Greetings from Lothlórien");
	/* TODO: fix this again */

	summ = mu_str_summarize (mu_msg_get_body_text(msg), 5);
	g_assert_cmpstr (summ,==, "Let's write some fünkÿ text using umlauts. Foo.");
	g_free (summ);

	mu_msg_iter_destroy (iter);
	mu_query_destroy (query);
	g_free (xpath);
}

static void
test_mu_query_accented_chars_02 (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "f:mü", 1},
		{ "s:motörhead", 1},
		{ "t:Helmut", 1},
		{ "t:Kröger", 1},
		{ "s:MotorHeäD", 1},
		{ "queensryche", 1},
		{ "Queensrÿche", 1},
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	g_free (xpath);

}


static void
test_mu_query_wildcards (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "f:mü", 1},
		{ "s:mo*", 1},
		{ "t:Helm*", 1},
		{ "queensryche", 1},
		{ "Queen*", 1},
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	g_free (xpath);
}



static void
test_mu_query_dates_helsinki (void)
{
	gchar *xpath;
	int i;
	const char *old_tz;

	QResults queries[] = {
		{ "date:20080731..20080804", 5},
		/* { "date:20080804..20080731", 5}, */
		{ "date:20080731..20080804", 5},
		{ "date:20080731..20080804 s:gcc", 1},
		{ "date:200808110803..now", 1},
		{ "date:200808110803..today", 1},
		/* { "date:now..2008-08-11-08-03", 1}, */
		/* { "date:today..2008-08-11-08-03", 1}, */
		{ "date:200808110801..now", 1},
	};

	old_tz = set_tz ("Europe/Helsinki");

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
	set_tz (old_tz);

}

static void
test_mu_query_dates_sydney (void)
{
	gchar *xpath;
	int i;
	const char *old_tz;

	QResults queries[] = {
		{ "date:20080731..20080804", 5},
		/* { "date:20080804..20080731", 5}, */
		{ "date:20080731..20080804", 5},
		{ "date:20080731..20080804 s:gcc", 1},
		{ "date:200808110803..now", 1},
		{ "date:200808110803..today", 1},
		/* { "date:now..2008-08-11-08-03", 1}, */
		/* { "date:today..2008-08-11-08-03", 1}, */
		{ "date:200808110801..now", 1},
	};

	old_tz = set_tz ("Australia/Sydney");

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
	set_tz (old_tz);

}

static void
test_mu_query_dates_la (void)
{
	gchar *xpath;
	int i;
	const char *old_tz;

	QResults queries[] = {
		{ "date:20080731..20080804", 5},
		/* { "date:20080804..20080731", 5}, */
		{ "date:20080731..20080804", 5},
		{ "date:20080731..20080804 s:gcc", 1},
		{ "date:200808110803..now", 0},
		{ "date:200808110803..today", 0},
		/* { "date:now..2008-08-11-08-03", 1}, */
		/* { "date:today..2008-08-11-08-03", 1}, */
		{ "date:200808110801..now", 0}, /* does not match in LA */
 	};

	old_tz = set_tz ("America/Los_Angeles");

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
	set_tz (old_tz);
}




static void
test_mu_query_sizes (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "size:0b..2m", 13},
		{ "size:2k..4k", 2},
		{ "size:2m..0b", 13}
	};

	xpath = fill_database (MU_TESTMAILDIR);
	g_assert (xpath != NULL);

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);

}


static void
test_mu_query_attach (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "j:sittingbull.jpg", 1},
		{ "file:custer", 0},
		{ "file:custer.jpg", 1}
	};

	xpath = fill_database (MU_TESTMAILDIR2);
	g_assert (xpath != NULL);

	/* g_print ("(%s)\n", xpath); */

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
}


static void
test_mu_query_tags (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "x:paradise", 1},
		{ "tag:lost", 1},
		{ "tag:lost tag:paradise", 1},
		{ "tag:lost tag:horizon", 0},
		{ "tag:lost OR tag:horizon", 1},
		{ "x:paradise,lost", 0},
	};

	xpath = fill_database (MU_TESTMAILDIR2);
	g_assert (xpath != NULL);

	/* g_print ("(%s)\n", xpath); */

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i)
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);

	g_free (xpath);
}


static void
test_mu_query_tags_02 (void)
{
	gchar *xpath;
	int i;

	QResults queries[] = {
		{ "x:paradise", 1},
		{ "tag:@NextActions", 1},
		{ "x:queensrÿche", 1},
		{ "tag:lost OR tag:operation*", 2},
	};

	xpath = fill_database (MU_TESTMAILDIR2);
	g_assert (xpath != NULL);

	/* g_print ("(%s)\n", xpath); */

 	for (i = 0; i != G_N_ELEMENTS(queries); ++i) {
		/* g_print ("%s\n", queries[i].query); */
		g_assert_cmpuint (run_and_count_matches (xpath, queries[i].query),
				  ==, queries[i].count);
	}

	g_free (xpath);
}


static void
test_mu_query_preprocess (void)
{
	unsigned u;
	struct {
		const gchar *expr, *expected;
	} testcases [] = {
		{ "hello", "hello" },
		{ "/[Gmail].Sent Mail", "__gmail__sent mail" }
		/* add more */
	};


 	for (u = 0; u != G_N_ELEMENTS(testcases); ++u) {
		gchar *prep;
		prep = mu_query_preprocess (testcases[u].expr, NULL);
		g_assert_cmpstr (prep, ==, testcases[u].expected);
		g_free (prep);
	}
}




int
main (int argc, char *argv[])
{
	int rv;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/mu-query/test-mu-query-preprocess",
			 test_mu_query_preprocess);

	g_test_add_func ("/mu-query/test-mu-query-01", test_mu_query_01);
	g_test_add_func ("/mu-query/test-mu-query-02", test_mu_query_02);
	g_test_add_func ("/mu-query/test-mu-query-03", test_mu_query_03);
	g_test_add_func ("/mu-query/test-mu-query-04", test_mu_query_04);

	g_test_add_func ("/mu-query/test-mu-query-logic", test_mu_query_logic);

	g_test_add_func ("/mu-query/test-mu-query-accented-chars-1",
			 test_mu_query_accented_chars_01);
	g_test_add_func ("/mu-query/test-mu-query-accented-chars-2",
			 test_mu_query_accented_chars_02);
	g_test_add_func ("/mu-query/test-mu-query-wildcards",
			 test_mu_query_wildcards);
	g_test_add_func ("/mu-query/test-mu-query-sizes",
			 test_mu_query_sizes);

	g_test_add_func ("/mu-query/test-mu-query-dates-helsinki",
			 test_mu_query_dates_helsinki); /* finland */
	g_test_add_func ("/mu-query/test-mu-query-dates-sydney",
			 test_mu_query_dates_sydney);
	g_test_add_func ("/mu-query/test-mu-query-dates-la",
			 test_mu_query_dates_la);

	g_test_add_func ("/mu-query/test-mu-query-attach",
			 test_mu_query_attach);
	g_test_add_func ("/mu-query/test-mu-query-tags",
			 test_mu_query_tags);
	g_test_add_func ("/mu-query/test-mu-query-tags_02",
			 test_mu_query_tags_02);

	if (!g_test_verbose())
	    g_log_set_handler (NULL,
			       G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL| G_LOG_FLAG_RECURSION,
			       (GLogFunc)black_hole, NULL);

	rv = g_test_run ();

	return rv;
}
