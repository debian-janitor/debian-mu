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

#include <string.h>
#include "mu-msg-fields.h"

enum _FieldFlags { 
	FLAG_GMIME         = 1 << 1, /* field retrieved through gmime */
	FLAG_XAPIAN_INDEX  = 1 << 2, /* field is indexed in xapian */
	FLAG_XAPIAN_TERM   = 1 << 3, /* field stored as term in xapian */
	FLAG_XAPIAN_VALUE  = 1 << 4  /* field stored as value in xapian */
};
typedef enum _FieldFlags FieldFlags;

/*
 * this struct describes the fields of an e-mail
 /*/
struct _MuMsgField {
	MuMsgFieldId    _id;		/* the id of the field */
	MuMsgFieldType  _type;		/* the type of the field */
	const char     *_name;		/* the name of the field */
	const char     *_shortcut;	/* the shortcut for use in --fields and sorting */
	const char     *_xprefix;	/* the Xapian-prefix  */ 
	FieldFlags      _flags;		/* the flags that tells us what to do */
};


static const MuMsgField FIELD_DATA[] = {
	{  
		MU_MSG_FIELD_ID_BODY_TEXT,
		MU_MSG_FIELD_TYPE_STRING,
		"body", "b", "B",
		FLAG_GMIME | FLAG_XAPIAN_INDEX
	},
	
	{ 
		MU_MSG_FIELD_ID_BODY_HTML,
		MU_MSG_FIELD_TYPE_STRING,
		"bodyhtml", "h", NULL,
		FLAG_GMIME
	},
	
	{ 
		MU_MSG_FIELD_ID_CC,
		MU_MSG_FIELD_TYPE_STRING,
		"cc", "c", "C",
		FLAG_GMIME | FLAG_XAPIAN_INDEX | FLAG_XAPIAN_VALUE
	},
	
	{ 
		MU_MSG_FIELD_ID_DATE, 
		MU_MSG_FIELD_TYPE_TIME_T,
		"date", "d", "D",
		FLAG_GMIME | FLAG_XAPIAN_VALUE
	},
	
	{ 
		MU_MSG_FIELD_ID_FLAGS, 
		MU_MSG_FIELD_TYPE_INT,
		"flags", "g", "G",  /* flaGs */
		FLAG_GMIME | FLAG_XAPIAN_VALUE
	},

	{ 
		MU_MSG_FIELD_ID_FROM,
		MU_MSG_FIELD_TYPE_STRING,
		"from", "f", "F",
		FLAG_GMIME | FLAG_XAPIAN_INDEX | FLAG_XAPIAN_VALUE
	},

	{   
		MU_MSG_FIELD_ID_PATH, 
		MU_MSG_FIELD_TYPE_STRING,
		"path", "l", "L",   /* 'l' for location */
 		FLAG_GMIME | FLAG_XAPIAN_VALUE
	},

	{   
		MU_MSG_FIELD_ID_MAILDIR, 
		MU_MSG_FIELD_TYPE_STRING,
		"maildir", "m", "M",
		FLAG_GMIME | FLAG_XAPIAN_TERM | FLAG_XAPIAN_VALUE
	},
	
	{ 
		MU_MSG_FIELD_ID_PRIORITY,
		MU_MSG_FIELD_TYPE_INT,
		"prio", "p", "P",  
		FLAG_GMIME | FLAG_XAPIAN_VALUE
	},

	{ 
		MU_MSG_FIELD_ID_SIZE,
		MU_MSG_FIELD_TYPE_BYTESIZE,
		"size", "z", "Z", /* siZe */
		FLAG_GMIME
	},
	
	{ 
		MU_MSG_FIELD_ID_SUBJECT,
		MU_MSG_FIELD_TYPE_STRING,
		"subject", "s", "S",
		FLAG_GMIME | FLAG_XAPIAN_INDEX | FLAG_XAPIAN_VALUE
	},
	
	{ 
		MU_MSG_FIELD_ID_TO,
		MU_MSG_FIELD_TYPE_STRING,
		"to", "t", "T",
		FLAG_GMIME | FLAG_XAPIAN_INDEX | FLAG_XAPIAN_VALUE
	},
	
	{ 
		MU_MSG_FIELD_ID_MSGID,
		MU_MSG_FIELD_TYPE_STRING,
		"msgid", "i", "I",  /* 'i' for Id */
		FLAG_GMIME | FLAG_XAPIAN_TERM
	},
	
	{ 
		MU_MSG_FIELD_ID_TIMESTAMP,
		MU_MSG_FIELD_TYPE_TIME_T,
		"timestamp", "x", NULL,
		FLAG_GMIME 
	}
};

void
mu_msg_field_foreach (MuMsgFieldForEachFunc func, gconstpointer data)
{
	int i;
	for (i = 0; i != sizeof(FIELD_DATA)/sizeof(FIELD_DATA[0]); ++i)
		func (&FIELD_DATA[i], data);
}

typedef gboolean (*FieldMatchFunc) (const MuMsgField *field, 
				    gconstpointer data);

static const MuMsgField*
find_field (FieldMatchFunc matcher, gconstpointer data)
{
	int i;
	for (i = 0; i != sizeof(FIELD_DATA)/sizeof(FIELD_DATA[0]); ++i)
		if (matcher(&FIELD_DATA[i], data))
			return &FIELD_DATA[i];

	return NULL;
}

static gboolean
match_name (const MuMsgField *field, const gchar* name)
{
	return strcmp (field->_name, name) == 0;
}

const MuMsgField*
mu_msg_field_from_name (const char* str)
{
	g_return_val_if_fail (str, NULL);
	return find_field ((FieldMatchFunc)match_name, str);
}

static gboolean
match_shortcut (const MuMsgField *field, char kar)
{
	return field->_shortcut[0] == kar;
}

const MuMsgField*
mu_msg_field_from_shortcut (char kar)
{
	return find_field ((FieldMatchFunc)match_shortcut,
			   GUINT_TO_POINTER((guint)kar));
}

static gboolean
match_id (const MuMsgField *field, MuMsgFieldId id)
{
	return field->_id == id;
}

const MuMsgField*  
mu_msg_field_from_id (MuMsgFieldId id)    
{
	return find_field ((FieldMatchFunc)match_id,
			   GUINT_TO_POINTER(id));
}


gboolean
mu_msg_field_gmime (const MuMsgField *field)
{
	g_return_val_if_fail (field, FALSE);
	return field->_flags & FLAG_GMIME;
}


gboolean
mu_msg_field_xapian_index  (const MuMsgField *field)
{
	g_return_val_if_fail (field, FALSE);
	return field->_flags & FLAG_XAPIAN_INDEX;
}

gboolean
mu_msg_field_xapian_value (const MuMsgField *field)
{
	g_return_val_if_fail (field, FALSE);
	return field->_flags & FLAG_XAPIAN_VALUE;
}

gboolean
mu_msg_field_xapian_term (const MuMsgField *field)
{
	g_return_val_if_fail (field, FALSE);
	return field->_flags & FLAG_XAPIAN_TERM;
}




gboolean
mu_msg_field_is_numeric (const MuMsgField *field)
{
	MuMsgFieldType type;
	
	g_return_val_if_fail (field, FALSE);
	
	type = mu_msg_field_type (field);
	
	return  type == MU_MSG_FIELD_TYPE_BYTESIZE ||
		type == MU_MSG_FIELD_TYPE_TIME_T ||
		type == MU_MSG_FIELD_TYPE_INT;
}

const char*    
mu_msg_field_name (const MuMsgField *field)
{
	g_return_val_if_fail (field, NULL);
	return field->_name;
}

const char*
mu_msg_field_shortcut (const MuMsgField *field)
{
	g_return_val_if_fail (field, NULL);
	return field->_shortcut;
}

MuMsgFieldId
mu_msg_field_id (const MuMsgField *field)
{
	g_return_val_if_fail (field, MU_MSG_FIELD_ID_NONE);
	return field->_id;
}

const char*
mu_msg_field_xapian_prefix (const MuMsgField *field)
{
	g_return_val_if_fail (field, NULL);
	return field->_xprefix;
}


MuMsgFieldType 
mu_msg_field_type (const MuMsgField *field)
{
	g_return_val_if_fail (field, MU_MSG_FIELD_TYPE_NONE);
	return field->_type;
}
