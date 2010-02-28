/* 
** Copyright (C) 2010 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
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

#include <stdlib.h>
#include <xapian.h>
#include <glib/gstdio.h>
#include <string.h>
#include <string>

#include "mu-query-xapian.h"

#include "mu-msg-iter-xapian.h"
#include "mu-msg-iter-xapian-priv.hh"

#include "mu-util.h"
#include "mu-util-xapian.h"


static void add_prefix (const MuMsgField* field,
			Xapian::QueryParser* qparser);

struct _MuQueryXapian {
	Xapian::Database*         _db;
	Xapian::QueryParser*	  _qparser;
	Xapian::Sorter*           _sorters[MU_MSG_FIELD_TYPE_NUM];
};

gboolean
init_mu_query_xapian (MuQueryXapian *mqx, const char* dbpath)
{
	mqx->_db = 0;
	mqx->_qparser = 0;
	
	try {
		mqx->_db = new Xapian::Database(dbpath);
		mqx->_qparser = new Xapian::QueryParser;
		
		mqx->_qparser->set_database(*mqx->_db);
		mqx->_qparser->set_default_op(Xapian::Query::OP_AND);
		mqx->_qparser->set_stemming_strategy
			(Xapian::QueryParser::STEM_SOME);

		memset (mqx->_sorters, 0, sizeof(mqx->_sorters));
		mu_msg_field_foreach ((MuMsgFieldForEachFunc)add_prefix,
				      (gpointer)mqx->_qparser);
		return TRUE;

	} MU_XAPIAN_CATCH_BLOCK;

	try {
		delete mqx->_db;
		delete mqx->_qparser;

	} MU_XAPIAN_CATCH_BLOCK;
	
	return FALSE;
}

	
static void
uninit_mu_query_xapian (MuQueryXapian *mqx)
{
	try {
		delete mqx->_db;
		delete mqx->_qparser;

		for (int i = 0; i != MU_MSG_FIELD_TYPE_NUM; ++i) 
			delete mqx->_sorters[i];
		
	} MU_XAPIAN_CATCH_BLOCK;
}

static Xapian::Query
get_query  (MuQueryXapian * mqx, const char* searchexpr, int *err = 0)  {
	
	try {
		return mqx->_qparser->parse_query
			(searchexpr,
			 Xapian::QueryParser::FLAG_BOOLEAN          | 
			 Xapian::QueryParser::FLAG_PHRASE           |
			 Xapian::QueryParser::FLAG_BOOLEAN_ANY_CASE |
			 Xapian::QueryParser::FLAG_WILDCARD         |
			 Xapian::QueryParser::FLAG_PURE_NOT         |
			 Xapian::QueryParser::FLAG_PARTIAL);

	} MU_XAPIAN_CATCH_BLOCK;
	
	if (err)
		*err  = 1;
	
	return Xapian::Query();
}

static void
add_prefix (const MuMsgField* field, Xapian::QueryParser* qparser)
{
	if (!mu_msg_field_xapian_index(field) &&
	    !mu_msg_field_xapian_term(field))
		return;

	const std::string prefix (mu_msg_field_xapian_prefix(field));
	
	qparser->add_boolean_prefix
		(std::string(mu_msg_field_name(field)), prefix);
	qparser->add_boolean_prefix
		(std::string(mu_msg_field_shortcut(field)), prefix);
	
	/* make the empty string match this field too*/
	qparser->add_prefix ("", prefix);
}

MuQueryXapian*
mu_query_xapian_new (const char* xpath)
{
	MuQueryXapian *mqx;
	
	g_return_val_if_fail (xpath, NULL);

	if (!mu_util_check_dir (xpath, TRUE, FALSE)) {
		g_warning ("'%s' is not a readable xapian dir", xpath);
		return NULL;
	}

	if (mu_util_xapian_db_is_empty (xpath)) {
		g_warning ("database %s is empty; nothing to do", xpath);
		return NULL;
	}
	
	if (!mu_util_xapian_db_version_up_to_date (xpath)) {
		g_warning ("%s is not up-to-date, needs a full update",
			   xpath);
		return NULL;
	}
	
	mqx = g_new (MuQueryXapian, 1);

	if (!init_mu_query_xapian (mqx, xpath)) {
		g_warning ("failed to initialize the Xapian query");
		g_free (mqx);
		return NULL;
	}
	
	return mqx;
}


void
mu_query_xapian_destroy (MuQueryXapian *self)
{
	if (!self)
		return;

	uninit_mu_query_xapian (self);

	g_free (self);
}


MuMsgIterXapian*
mu_query_xapian_run (MuQueryXapian *self, const char* searchexpr,
		     const MuMsgField* sortfield, gboolean ascending,
		     size_t batchsize)  
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (searchexpr, NULL);	
	
	try {
		int err (0);

		Xapian::Query q(get_query(self, searchexpr, &err));
		if (err) {
			g_warning ("Error in query '%s'", searchexpr);
			return NULL;
		}
				
		Xapian::Enquire enq (*self->_db);

		if (batchsize == 0)
			batchsize = self->_db->get_doccount();
		
		if (sortfield) 
			enq.set_sort_by_value (
				(Xapian::valueno)mu_msg_field_id(sortfield),
				ascending);

		enq.set_query(q);
		enq.set_cutoff(0,0);

		return mu_msg_iter_xapian_new (enq, batchsize);
		
	} MU_XAPIAN_CATCH_BLOCK_RETURN(NULL);
}

char*
mu_query_xapian_as_string  (MuQueryXapian *self, const char* searchexpr) 
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (searchexpr, NULL);
		
	try {
		int err (0);
		
		Xapian::Query q(get_query(self, searchexpr, &err));
		if (err)  {
			g_warning ("Error in query '%s'", searchexpr);
			return NULL;
		}

		return g_strdup(q.get_description().c_str());
		
	} MU_XAPIAN_CATCH_BLOCK_RETURN(NULL);
}


