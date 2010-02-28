/*
** Copyright (C) 2010 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
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

#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

#include "mu-msg-gmime.h"
#include "mu-maildir.h"
#include "mu-index.h"
#include "mu-query-xapian.h"
#include "mu-msg-iter-xapian.h"
#include "mu-msg-str.h"

#include "mu-util.h"
#include "mu-util-xapian.h"
#include "mu-cmd-find.h"


static void
update_warning (void)
{
	g_printerr ("the database needs to be updated to version %s\n",
		   MU_XAPIAN_DB_VERSION);
	g_message ("please run 'mu index --rebuild' (see the man page)");
}



static gboolean
print_xapian_query (MuQueryXapian *xapian, const gchar *query)
{
	char *querystr;

	MU_WRITE_LOG ("query: '%s' (xquery)", query); 
	
	querystr = mu_query_xapian_as_string (xapian, query);
	g_print ("%s\n", querystr);
	g_free (querystr);

	return TRUE;
}


static const gchar*
display_field (MuMsgIterXapian *iter, const MuMsgField* field)
{
	gint64 val;

	switch (mu_msg_field_type(field)) {
	case MU_MSG_FIELD_TYPE_STRING:
		return mu_msg_iter_xapian_get_field (iter, field);

	case MU_MSG_FIELD_TYPE_INT:
	
		if (mu_msg_field_id(field) == MU_MSG_FIELD_ID_PRIORITY) {
			val = mu_msg_iter_xapian_get_field_numeric (iter, field);
			return mu_msg_str_prio ((MuMsgPriority)val);
		}
		
		if (mu_msg_field_id(field) == MU_MSG_FIELD_ID_FLAGS) {
			val = mu_msg_iter_xapian_get_field_numeric (iter, field);
			return mu_msg_str_flags_s ((MuMsgPriority)val);
		}

		return mu_msg_iter_xapian_get_field (iter, field); /* as string */

	case MU_MSG_FIELD_TYPE_TIME_T: 
		val = mu_msg_iter_xapian_get_field_numeric (iter, field);
		return mu_msg_str_date_s ((time_t)val);

	case MU_MSG_FIELD_TYPE_BYTESIZE: 
		val = mu_msg_iter_xapian_get_field_numeric (iter, field);
		return mu_msg_str_size_s ((time_t)val);
	default:
		g_return_val_if_reached (NULL);
	}
}


/* returns NULL if there is an error */
const MuMsgField*
sort_field_from_string (const char* fieldstr)
{
	const MuMsgField *field;
		
	field = mu_msg_field_from_name (fieldstr);

	if (!field && strlen(fieldstr) == 1)
		field = mu_msg_field_from_shortcut(fieldstr[0]);

	if (!field)
		g_printerr ("not a valid sort field: '%s'\n",
			    fieldstr);
	return field;
}

static size_t
print_rows (MuMsgIterXapian *iter, const char *fields)
{
	size_t count = 0;
	const char* myfields;

	if (mu_msg_iter_xapian_is_null (iter))
		return 0;
	
	do {
		int len = 0;

		myfields = fields;
		while (*myfields) {
			const MuMsgField* field;
			field =	mu_msg_field_from_shortcut (*myfields);
			if (!field || !mu_msg_field_xapian_value (field)) 
				len += printf ("%c", *myfields);
			else
				len += printf ("%s",
					       display_field(iter, field));
			++myfields;
		}
		
		if (len > 0)
			g_print ("\n");

		++count;
		
	} while (mu_msg_iter_xapian_next (iter));
	
	return count;
}

/* create a linksdir if it not exist yet; if it already existed,
 * remove old links if opts->clearlinks was specified */
static gboolean
create_or_clear_linksdir_maybe (const char *linksdir, gboolean clearlinks)
{
	if (access (linksdir, F_OK) != 0) {
		if (!mu_maildir_mkmdir (linksdir, 0700, TRUE)) 
			return FALSE;

	} else if (clearlinks)
		mu_maildir_clear_links (linksdir);
	
	return TRUE;
}


static size_t
make_links (MuMsgIterXapian *iter, const char* linksdir, gboolean clearlinks)
{
	size_t count = 0;
	const MuMsgField *pathfield;
	
	if (!create_or_clear_linksdir_maybe (linksdir, clearlinks))
		return 0;

	if (mu_msg_iter_xapian_is_null (iter))
		return 0;
	
	pathfield = mu_msg_field_from_id (MU_MSG_FIELD_ID_PATH);
	
	/* iterate over the found iters */
	do {
		const char *path;
		
		/* there's no data in the iter */
		if (mu_msg_iter_xapian_is_null (iter))
			return count;		
		
		path = mu_msg_iter_xapian_get_field (iter, pathfield);
		if (!path)
			continue;
			
		/* this might happen  if the database is not up-to-date */
		if (access (path, R_OK) != 0) {
			g_warning ("Cannot read source message %s: %s",
				   path, strerror (errno));
			continue;
		} 
		
		if (!mu_maildir_link (path, linksdir))
			break;
		++count;

	} while (mu_msg_iter_xapian_next (iter));
		 
	return count;
}



static gboolean
run_query (MuQueryXapian *xapian, const gchar *query, MuConfigOptions *opts)
{
	MuMsgIterXapian *iter;
	const MuMsgField *sortfield;
	size_t matches;
	
	MU_WRITE_LOG ("query: '%s'", query); 
	
	sortfield = NULL;
	if (opts->sortfield) {
		sortfield = sort_field_from_string (opts->sortfield);
		if (!sortfield) /* error occured? */
			return FALSE;
	}
	
	iter = mu_query_xapian_run (xapian, query, sortfield,
				    !opts->descending, 0);
	if (!iter) {
		g_printerr ("error: running query failed\n");
		return FALSE;
	}

	if (opts->linksdir)
		matches = make_links (iter, opts->linksdir, opts->clearlinks);
	else
		matches = print_rows (iter, opts->fields);
	
	if (matches == 0) 
		g_printerr ("No matches found\n");

	mu_msg_iter_xapian_destroy (iter);

	return matches > 0;
}


static gboolean
do_output (MuQueryXapian *xapian, MuConfigOptions* opts,
	   const gchar **params)
{
	gchar *query;
	gboolean retval = TRUE;
	
	query = mu_util_str_from_strv (params);
		
	/* if xquery is set, we print the xapian query instead of the
	 * output; this is for debugging purposes */
	if (opts->xquery) 
		retval = print_xapian_query (xapian, query);
	else
		retval = run_query (xapian, query, opts);
	
	g_free (query);
	
	return retval;
}


static gboolean
query_params_valid (MuConfigOptions *opts)
{
	if (opts->linksdir) 
		if (opts->xquery) {
			g_printerr ("Invalid option for '--linksdir'\n");
			return FALSE;
		}
		
	if (!opts->params[0] || !opts->params[1]) {
		g_printerr ("Missing search expression\n");
		return FALSE;
	}

	if (mu_util_check_dir (opts->xpath, TRUE, FALSE))
		return TRUE;

	g_printerr ("%s is not a readable Xapian directory\n", opts->xpath);
	g_message ("Did you run 'mu index'?");
	
	return FALSE;
}

gboolean
mu_cmd_find (MuConfigOptions *opts)
{
	MuQueryXapian *xapian;
	gboolean rv;
	const gchar **params;

	g_return_val_if_fail (opts, FALSE);
	
	if (!query_params_valid (opts))
		return FALSE;

	if (mu_util_xapian_db_is_empty (opts->xpath)) {
		g_printerr ("The database is empty; "
			    "use 'mu index' to add some messages\n");
		return FALSE;
	}
		
	if (!mu_util_xapian_db_version_up_to_date (opts->xpath)) {
		update_warning ();
		return FALSE;
	}
	
	/* first param is 'query', search params are after that */
	params = (const gchar**)&opts->params[1];

	mu_msg_gmime_init();
	
	xapian = mu_query_xapian_new (opts->xpath);
	if (!xapian) {
		g_printerr ("Failed to create a Xapian query\n");
		mu_msg_gmime_uninit ();
		return FALSE;
	}

	rv = do_output (xapian, opts, params);
	
	mu_query_xapian_destroy (xapian);
	mu_msg_gmime_uninit();
	
	return rv;
}

/* we ignore fields for now */
static gboolean
view_file (const gchar *path, const gchar *fields)
{
	MuMsgGMime* msg;
	const char *field;
	time_t date;
	
	msg = mu_msg_gmime_new (path, NULL);
	if (!msg)
		return FALSE;

	field = mu_msg_gmime_get_from (msg);
	if (field)
		g_print ("From: %s\n", field);
	
	field = mu_msg_gmime_get_to (msg);
	if (field)
		g_print ("To: %s\n", field);

	field = mu_msg_gmime_get_cc (msg);
	if (field)
		g_print ("Cc: %s\n", field);

	field = mu_msg_gmime_get_subject (msg);
	if (field)
		g_print ("Subject: %s\n", field);
	
	date = mu_msg_gmime_get_date (msg);
	if (date)
		g_print ("Date: %s\n",
			 mu_msg_str_date_s (date));
			 
	field = mu_msg_gmime_get_body_text (msg);
	if (field) 
		g_print ("\n%s\n", field);
	else
		/* not really an error */
		g_warning ("No text body found for %s", path);

	mu_msg_gmime_destroy (msg);
	return TRUE;
}

gboolean
mu_cmd_view (MuConfigOptions *opts)
{
	gboolean rv;
	int i;
	
	g_return_val_if_fail (opts, FALSE);

	/* note: params[0] will be 'view' */
	if (!opts->params[0] || !opts->params[1]) {
		g_printerr ("Missing files to view\n");
		return FALSE;
	}
	
	mu_msg_gmime_init();

	rv = TRUE;
	for (i = 1; opts->params[i] && rv; ++i) 	
		rv = view_file (opts->params[i], NULL);

	mu_msg_gmime_uninit();
	
	return rv;
}
