/* 
** Copyright (C) 2008-2010 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
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

#include "test-mu-common.h"
#include "src/mu-maildir.h"

static void
test_mu_maildir_mkmdir_01 (void)
{
	int i;
	gchar *tmpdir, *mdir, *tmp;
	const gchar *subs[] = {"tmp", "cur", "new"};
	
	tmpdir = test_mu_common_get_random_tmpdir ();
	mdir   = g_strdup_printf ("%s%c%s", tmpdir, G_DIR_SEPARATOR,
				  "cuux");
	
	g_assert_cmpuint (mu_maildir_mkdir (mdir, 0755, FALSE, NULL),
			  ==, TRUE);

	for (i = 0; i != G_N_ELEMENTS(subs); ++i) {
		gchar* dir;
		
		dir = g_strdup_printf ("%s%c%s", mdir, G_DIR_SEPARATOR,
				       subs[i]);
		g_assert_cmpuint (g_access (dir, R_OK), ==, 0);
		g_assert_cmpuint (g_access (dir, W_OK), ==, 0);
		g_free (dir);
	}

	tmp = g_strdup_printf ("%s%c%s", mdir, G_DIR_SEPARATOR, ".noindex");
	g_assert_cmpuint (g_access (tmp, F_OK), !=, 0);
	
	g_free (tmp);
	g_free (tmpdir);
	g_free (mdir);
	
}


static void
test_mu_maildir_mkmdir_02 (void)
{
	int i;
	gchar *tmpdir, *mdir, *tmp;
	const gchar *subs[] = {"tmp", "cur", "new"};
	
	tmpdir = test_mu_common_get_random_tmpdir ();
	mdir   = g_strdup_printf ("%s%c%s", tmpdir, G_DIR_SEPARATOR,
				  "cuux");
	
	g_assert_cmpuint (mu_maildir_mkdir (mdir, 0755, TRUE, NULL),
			  ==, TRUE);

	for (i = 0; i != G_N_ELEMENTS(subs); ++i) {
		gchar* dir;
		
		dir = g_strdup_printf ("%s%c%s", mdir, G_DIR_SEPARATOR,
				       subs[i]);
		g_assert_cmpuint (g_access (dir, R_OK), ==, 0);

		g_assert_cmpuint (g_access (dir, W_OK), ==, 0);
		g_free (dir);
	}
	
	tmp = g_strdup_printf ("%s%c%s", mdir, G_DIR_SEPARATOR, ".noindex");
	g_assert_cmpuint (g_access (tmp, F_OK), ==, 0);
	
	g_free (tmp);
	g_free (tmpdir);
	g_free (mdir);	
}



static gboolean
ignore_error (const char* log_domain, GLogLevelFlags log_level, const gchar* msg,
	      gpointer user_data)
{
	return FALSE; /* don't abort */
}

static void
test_mu_maildir_mkmdir_03 (void)
{	
	/* this must fail */
	g_test_log_set_fatal_handler ((GTestLogFatalFunc)ignore_error, NULL);

	g_assert_cmpuint (mu_maildir_mkdir (NULL, 0755, TRUE, NULL),
					    ==, FALSE);
}
		

static gchar*
copy_test_data (void)
{
	gchar *dir, *cmd;
	
	dir = test_mu_common_get_random_tmpdir();
	cmd = g_strdup_printf ("mkdir %s", dir);
	g_assert (g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL));
	g_free (cmd);
	
	cmd = g_strdup_printf ("cp -R %s %s", MU_TESTMAILDIR, dir);
	g_assert (g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL));
	g_free (cmd);

	/* unbreak make distcheck */
	cmd = g_strdup_printf ("chmod -R 700 %s", dir);
	g_assert (g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL));
	g_free (cmd);
	
	return dir;
}


typedef struct {
	int _file_count;
	int _dir_entered;
	int _dir_left;
} WalkData;

static MuResult
dir_cb (const char *fullpath, gboolean enter, WalkData *data)
{
	if (enter) 
		++data->_dir_entered;
	else
		++data->_dir_left;

	/* g_print ("%s: %s: %s\n", __FUNCTION__, enter ? "entering" : "leaving", */
	/* 	 fullpath); */

	return MU_OK;
}


static MuResult
msg_cb (const char *fullpath, const char* mdir, struct stat *statinfo,
	WalkData *data)
{
	++data->_file_count;
	return MU_OK;
}


static void
test_mu_maildir_walk_01 (void)
{
	char *tmpdir;
	WalkData data;
	MuResult rv;
	
	tmpdir = copy_test_data ();
	memset (&data, 0, sizeof(WalkData));

	/* g_print ("tmpdir: %s\n", tmpdir); */
	
	rv = mu_maildir_walk (tmpdir,
			      (MuMaildirWalkMsgCallback)msg_cb, 
			      (MuMaildirWalkDirCallback)dir_cb,
			      &data);

	g_assert_cmpuint (MU_OK, ==, rv);
	g_assert_cmpuint (data._file_count, ==, 13); 
	g_assert_cmpuint (data._dir_entered,==, 5);
	g_assert_cmpuint (data._dir_left,==, 5);

	g_free (tmpdir);
}


static void
test_mu_maildir_walk_02 (void)
{
	char *tmpdir, *cmd;
	WalkData data;
	MuResult rv;
	
	tmpdir = copy_test_data ();
	memset (&data, 0, sizeof(WalkData));
	
	/* mark the 'new' dir with '.noindex', to ignore it */ 
	cmd = g_strdup_printf ("touch %s%ctestdir%cnew%c.noindex", tmpdir,
			       G_DIR_SEPARATOR, G_DIR_SEPARATOR,
			       G_DIR_SEPARATOR);
	g_assert (g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL));
	g_free (cmd);
		
	rv = mu_maildir_walk (tmpdir,
			      (MuMaildirWalkMsgCallback)msg_cb, 
			      (MuMaildirWalkDirCallback)dir_cb,
			      &data);

	g_assert_cmpuint (MU_OK, ==, rv);
	g_assert_cmpuint (data._file_count, ==, 9);
	g_assert_cmpuint (data._dir_entered,==, 4);
	g_assert_cmpuint (data._dir_left,==, 4);

	g_free (tmpdir);
}



static void
test_mu_maildir_get_flags_from_path (void)
{
	int i;
	struct {
		const char *path;
		MuMsgFlags flags;
	} paths[] = {
		{
		"/home/foo/Maildir/test/cur/123456:2,FSR",
		MU_MSG_FLAG_REPLIED | MU_MSG_FLAG_SEEN | MU_MSG_FLAG_FLAGGED}, {
		"/home/foo/Maildir/test/new/123456",
		            MU_MSG_FLAG_NEW | MU_MSG_FLAG_UNREAD}, {
		"/home/foo/Maildir/test/new/123456:2,FR",
			    MU_MSG_FLAG_NEW | MU_MSG_FLAG_UNREAD}, {
		"/home/foo/Maildir/test/cur/123456:2,DTP",
			    MU_MSG_FLAG_DRAFT | MU_MSG_FLAG_TRASHED |
			    MU_MSG_FLAG_PASSED | MU_MSG_FLAG_UNREAD }, {
		"/home/foo/Maildir/test/cur/123456:2,S",
			    MU_MSG_FLAG_SEEN}
	};

	for (i = 0; i != G_N_ELEMENTS(paths); ++i) {
		MuMsgFlags flags;
		flags = mu_maildir_get_flags_from_path(paths[i].path);
		g_assert_cmpuint(flags, ==, paths[i].flags);
	}
}

static void
test_mu_maildir_get_path_from_flags (void)
{
	int i;

	struct {
		const char *oldpath;
		MuMsgFlags flags;
		const char *newpath;
	} paths[] = {
		{
		"/home/foo/Maildir/test/cur/123456:2,FR",
			    MU_MSG_FLAG_REPLIED,
			    "/home/foo/Maildir/test/cur/123456:2,R"}, {
		"/home/foo/Maildir/test/cur/123456:2,FR",
			    MU_MSG_FLAG_NEW,
			    "/home/foo/Maildir/test/new/123456"}, {
		"/home/foo/Maildir/test/new/123456:2,FR",
			    MU_MSG_FLAG_SEEN | MU_MSG_FLAG_REPLIED,
			    "/home/foo/Maildir/test/cur/123456:2,RS"}
	};

	for (i = 0; i != G_N_ELEMENTS(paths); ++i) {
		gchar *str;
		str = mu_maildir_get_path_from_flags(paths[i].oldpath,
						      paths[i].flags);
		g_assert_cmpstr(str, ==, paths[i].newpath);
		g_free(str);
	}
}


int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	/* mu_util_maildir_mkmdir */
 	g_test_add_func ("/mu-maildir/mu-maildir-mkmdir-01",
			 test_mu_maildir_mkmdir_01);
	g_test_add_func ("/mu-maildir/mu-maildir-mkmdir-02",
			 test_mu_maildir_mkmdir_02);
	g_test_add_func ("/mu-maildir/mu-maildir-mkmdir-03",
			 test_mu_maildir_mkmdir_03);

	/* mu_util_maildir_walk */
	g_test_add_func ("/mu-maildir/mu-maildir-walk-01",
			 test_mu_maildir_walk_01);
	g_test_add_func ("/mu-maildir/mu-maildir-walk-02",
			 test_mu_maildir_walk_02);

	/* get/set flags */
	g_test_add_func("/mu-maildir/mu-maildir-get-path-from-flags",
			test_mu_maildir_get_path_from_flags);
	g_test_add_func("/mu-maildir/mu-maildir-get-flags-from-path",
			test_mu_maildir_get_flags_from_path);

	
	g_log_set_handler (NULL,
			   G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL| G_LOG_FLAG_RECURSION,
			   (GLogFunc)black_hole, NULL);
	
	return g_test_run ();
}
