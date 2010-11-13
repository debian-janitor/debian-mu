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

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "mu-maildir.h"
#include "mu-cmd.h"

gboolean
mu_cmd_equals (MuConfigOptions *config, const gchar *cmd)
{
	g_return_val_if_fail (config, FALSE);
	g_return_val_if_fail (cmd, FALSE);
	
	if (!config->params || !config->params[0])
		return FALSE;

	return (strcmp (config->params[0], cmd) == 0);
}


static MuCmd 
cmd_from_string (const char* cmd)
{
	int i;
	typedef struct {
		const gchar* _name;
		MuCmd        _cmd;
	} Cmd;

	Cmd cmd_map[] = {
		{ "index",   MU_CMD_INDEX },
		{ "find",    MU_CMD_FIND },
		{ "cleanup", MU_CMD_CLEANUP },
		{ "mkdir",   MU_CMD_MKDIR },
		{ "view",    MU_CMD_VIEW },
		{ "extract", MU_CMD_EXTRACT }
	};
	
	for (i = 0; i != G_N_ELEMENTS(cmd_map); ++i) 
		if (strcmp (cmd, cmd_map[i]._name) == 0)
			return cmd_map[i]._cmd;

	return MU_CMD_UNKNOWN;
}

static void
show_usage (gboolean noerror)
{
	const char* usage=
		"usage: mu [options] command [parameters]\n"
		"where command is one of index, find, view, mkdir, cleanup "
		"or extract\n\n"
		"see the mu or mu-easy manpages for more information\n";

	if (noerror)
		g_print ("%s", usage);
	else
		g_printerr ("%s", usage);
}

static void
show_version (void)
{
	g_print ("mu (mail indexer/searcher) " VERSION "\n"
		 "Copyright (C) 2008-2010 Dirk-Jan C. Binnema (GPLv3+)\n");
}

gboolean
mu_cmd_execute (MuConfigOptions *opts)
{
	MuCmd cmd;
	
	if (opts->version) {
		show_version ();
		return TRUE;
	}
	
	if (!opts->params||!opts->params[0]) {/* no command? */
		show_version ();
		g_print ("\n");
		show_usage (TRUE);
		return FALSE;
	}
	
	cmd = cmd_from_string (opts->params[0]);

	switch (cmd) {

	case MU_CMD_CLEANUP:    return mu_cmd_cleanup (opts);
	case MU_CMD_EXTRACT:    return mu_cmd_extract (opts);
	case MU_CMD_FIND:       return mu_cmd_find (opts);
	case MU_CMD_INDEX:      return mu_cmd_index (opts);
	case MU_CMD_MKDIR:      return mu_cmd_mkdir (opts);
	case MU_CMD_VIEW:       return mu_cmd_view (opts);
		
	case MU_CMD_UNKNOWN:
		show_usage (FALSE);
		return FALSE;
	default:
		g_return_val_if_reached (FALSE);
	}	
}
