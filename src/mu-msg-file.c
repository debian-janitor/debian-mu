/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
**
** Copyright (C) 2011 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>

/* hopefully, the should get us a sane PATH_MAX */
#include <limits.h>
/* not all systems provide PATH_MAX in limits.h */
#ifndef PATH_MAX
#include <sys/param.h>
#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif 	/*!PATH_MAX */
#endif 	/*PATH_MAX */

#include <gmime/gmime.h>

#include "mu-util.h"
#include "mu-str.h"
#include "mu-maildir.h"
#include "mu-msg-priv.h"


static gboolean init_file_metadata (MuMsgFile *self, const char* path,
				    const char *mdir, GError **err);
static gboolean init_mime_msg (MuMsgFile *msg, const char *path, GError **err);


/* note, we do the gmime initialization here rather than in
 * mu-runtime, because this way we don't need mu-runtime for simple
 * cases -- such as our unit tests */
static gboolean _gmime_initialized = FALSE;

static void
gmime_init (void)
{
	g_return_if_fail (!_gmime_initialized);
	
#ifdef GMIME_ENABLE_RFC2047_WORKAROUNDS
	g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);
#else
	g_mime_init(0);
#endif /* GMIME_ENABLE_RFC2047_WORKAROUNDS */

	_gmime_initialized = TRUE;
}

static void
gmime_uninit (void)
{
		g_return_if_fail (_gmime_initialized);

		g_mime_shutdown();
		_gmime_initialized = FALSE;
}


MuMsgFile*   
mu_msg_file_new (const char* filepath, const char *mdir, GError **err)
{
	MuMsgFile *self;

	g_return_val_if_fail (filepath, NULL);
	
	if (G_UNLIKELY(!_gmime_initialized)) {
		gmime_init ();
		g_atexit (gmime_uninit);
	}
		
	self = g_slice_new0 (MuMsgFile);	
	
	if (!init_file_metadata (self, filepath, mdir, err)) {
		mu_msg_file_destroy (self);
		return NULL;
	}
	
	if (!init_mime_msg (self, filepath, err)) {
		mu_msg_file_destroy (self);
		return NULL;
	}
	
	return self;
}


void
mu_msg_file_destroy (MuMsgFile *self)
{
	if (!self)
		return;

	if (self->_mime_msg)
		g_object_unref (self->_mime_msg);
	
	g_slice_free (MuMsgFile, self);
}


static gboolean
init_file_metadata (MuMsgFile *self, const char* path, const gchar* mdir,
		    GError **err)
{
	struct stat statbuf;

	if (access (path, R_OK) != 0) {
		g_set_error (err, 0, MU_ERROR_FILE,
			     "cannot read file %s: %s",
			     path, strerror(errno));
		return FALSE;
	}

	if (stat (path, &statbuf) < 0) {
		g_set_error (err, 0, MU_ERROR_FILE,
			     "cannot stat %s: %s",
			     path, strerror(errno));
		return FALSE;
	}
	
	if (!S_ISREG(statbuf.st_mode)) {
		g_set_error (err, 0, MU_ERROR_FILE,
			     "not a regular file: %s", path);
		return FALSE;
	}
	
	self->_timestamp = statbuf.st_mtime;
	self->_size	 = (size_t)statbuf.st_size;

	strncpy (self->_path, path, PATH_MAX);
	strncpy (self->_maildir, mdir ? mdir : "", PATH_MAX); 
	
	return TRUE;
}



static GMimeStream*
get_mime_stream (MuMsgFile *self, const char *path, GError **err)
{
	FILE *file;
	GMimeStream *stream;
	
	file = fopen (path, "r");
	if (!file) {
		g_set_error (err, 0, MU_ERROR_FILE,
			     "cannot open %s: %s",
			     path, strerror (errno));
		return NULL;
	}
	
	stream = g_mime_stream_file_new (file);
	if (!stream) {
		g_set_error (err, 0, MU_ERROR_GMIME,
			     "cannot create mime stream for %s",
			     path);
		fclose (file);
		return NULL;
	}

	return stream;
}

static gboolean
init_mime_msg (MuMsgFile *self, const char* path, GError **err)
{
	GMimeStream *stream;
	GMimeParser *parser;
	
	stream = get_mime_stream (self, path, err);
	if (!stream)
		return FALSE;
	
	parser = g_mime_parser_new_with_stream (stream);
	g_object_unref (stream);
	if (!parser) {
		g_set_error (err, 0, MU_ERROR_GMIME,
			     "%s: cannot create mime parser for %s",
			     __FUNCTION__, path);
		return FALSE;
	}
	
	self->_mime_msg = g_mime_parser_construct_message (parser);
	g_object_unref (parser);
	if (!self->_mime_msg) {
		g_set_error (err, 0, MU_ERROR_GMIME,
			     "%s: cannot construct mime message for %s",
			     __FUNCTION__, path);
		return FALSE;
	}

	return TRUE;
}


static char*
get_recipient (MuMsgFile *self, GMimeRecipientType rtype)
{
	char *recip;
	InternetAddressList *recips;
	
	recips = g_mime_message_get_recipients (self->_mime_msg, rtype);

	/* FALSE --> don't encode */
	recip = (char*)internet_address_list_to_string (recips, FALSE);
	
	if (mu_str_is_empty(recip)) {
		g_free (recip);
		return NULL;
	}

	return recip;
}



static gboolean
part_looks_like_attachment (GMimeObject *part)
{
	GMimeContentDisposition *disp;
	const char *str;
	
	disp  = g_mime_object_get_content_disposition (part);
	if (!GMIME_IS_CONTENT_DISPOSITION(disp))
		return FALSE; /* no content disp? prob not
			       * an attachment. */
	
	str = g_mime_content_disposition_get_disposition (disp);

	/* ok, it says it's an attachment, so it probably is... */
	if (!str)
		return TRUE;
	if (strcmp (str, GMIME_DISPOSITION_ATTACHMENT) == 0)
		return TRUE;
	else if (strcmp (str, GMIME_DISPOSITION_INLINE) == 0) {
		/* inline-images are also considered attachments... */
		GMimeContentType *ct;
		ct = g_mime_object_get_content_type (part);
		if (ct)
			return g_mime_content_type_is_type
				(ct, "image", "*");
	}
	
	return FALSE;
}
					  

static void
msg_cflags_cb (GMimeObject *parent, GMimeObject *part, MuMsgFlags *flags)
{
	if (*flags & MU_MSG_FLAG_HAS_ATTACH)
		return;
	
	if (!GMIME_IS_PART(part))
		return;
	
	if (part_looks_like_attachment(part))
		*flags |= MU_MSG_FLAG_HAS_ATTACH;
}



static MuMsgFlags
get_content_flags (MuMsgFile *self)
{
	GMimeContentType *ctype;
	MuMsgFlags flags;
	GMimeObject *part;

	if (!GMIME_IS_MESSAGE(self->_mime_msg))
		return MU_MSG_FLAG_NONE;

	flags = 0;
	g_mime_message_foreach (self->_mime_msg,
				(GMimeObjectForeachFunc)msg_cflags_cb, 
				&flags);
	
	/* note: signed or encrypted status for a message is determined by
	 *  the top-level mime-part
	 */
	if ((part = g_mime_message_get_mime_part(self->_mime_msg))) {
		ctype = g_mime_object_get_content_type
			(GMIME_OBJECT(part));
		if (!ctype) {
			g_warning ("not a content type!");
			return 0;
		}	
		
		if (ctype) {
			if (g_mime_content_type_is_type
			    (ctype,"*", "signed")) 
				flags |= MU_MSG_FLAG_SIGNED;
			if (g_mime_content_type_is_type
			    (ctype,"*", "encrypted")) 
				flags |= MU_MSG_FLAG_ENCRYPTED;
		}
	} else
		g_warning ("no top level mime part found");

	return flags;
}


static MuMsgFlags
get_flags (MuMsgFile *self)
{
	MuMsgFlags flags;
	
	g_return_val_if_fail (self, MU_MSG_FLAG_NONE);

	flags = mu_maildir_get_flags_from_path (self->_path);
	flags |= get_content_flags (self);
	
	return flags;
}


static size_t
get_size (MuMsgFile *self)
{
	g_return_val_if_fail (self, 0);

	return self->_size;
}


static char*
to_lower (char *s)
{
	char *t = s;
	while (t&&*t) {
		t[0] = g_ascii_tolower(t[0]);
		++t;
	}
	return s;
}


static char*
get_prio_header_field (MuMsgFile *self)
{
	const char *str;
	GMimeObject *obj;

	obj = GMIME_OBJECT(self->_mime_msg);

	str = g_mime_object_get_header (obj, "X-Priority");
	if (!str)
		str = g_mime_object_get_header (obj, "X-MSMail-Priority");
	if (!str)
		str = g_mime_object_get_header (obj, "Importance");
	if (!str)
		str = g_mime_object_get_header (obj, "Precedence");
	if (str) 
		return (to_lower(g_strdup(str)));
	else
		return NULL;
}


static MuMsgPrio
parse_prio_str (const char* priostr)
{
	int i;
	struct {
		const char*	_str;
		MuMsgPrio	_prio;
	} str_prio[] = {
		{ "high",	MU_MSG_PRIO_HIGH },
		{ "1",		MU_MSG_PRIO_HIGH },
		{ "2",		MU_MSG_PRIO_HIGH },
		
		{ "normal",	MU_MSG_PRIO_NORMAL },
		{ "3",		MU_MSG_PRIO_NORMAL },

		{ "low",	MU_MSG_PRIO_LOW },
		{ "list",	MU_MSG_PRIO_LOW },
		{ "bulk",	MU_MSG_PRIO_LOW },
		{ "4",		MU_MSG_PRIO_LOW },
		{ "5",		MU_MSG_PRIO_LOW }
	};

	for (i = 0; i != G_N_ELEMENTS(str_prio); ++i)
		if (g_strstr_len (priostr, -1, str_prio[i]._str) != NULL)
			return str_prio[i]._prio;
	
	/* e.g., last-fm uses 'fm-user'... as precedence */
	return MU_MSG_PRIO_NORMAL;
}

static MuMsgPrio
get_prio (MuMsgFile *self)
{
	MuMsgPrio prio;
	char* priostr;

	g_return_val_if_fail (self, MU_MSG_PRIO_NONE);

	priostr = get_prio_header_field (self);
	if (!priostr)
		return MU_MSG_PRIO_NORMAL;
	
	prio = parse_prio_str (priostr);
	g_free (priostr);

	return prio;
}


/* static const char*      */
/* get_header (MuMsgFile *self, const char* header) */
/* { */
/* 	g_return_val_if_fail (msg, NULL); */
/* 	g_return_val_if_fail (header, NULL); */

/* 	return g_mime_object_get_header (GMIME_OBJECT(self->_mime_msg),  */
/* 					 header); */
/* } */


struct _GetBodyData {
	GMimeObject *_txt_part, *_html_part;
	gboolean _want_html;
};
typedef struct _GetBodyData GetBodyData;


static gboolean
looks_like_attachment (GMimeObject *part)
{
	const char *str;
	GMimeContentDisposition *disp;
	
	disp = g_mime_object_get_content_disposition (GMIME_OBJECT(part));
	if (!GMIME_IS_CONTENT_DISPOSITION(disp))
		return FALSE;  

	str = g_mime_content_disposition_get_disposition (disp);
	if (!str)
		return FALSE;
	
	if (strcmp(str,GMIME_DISPOSITION_INLINE) == 0)
		return FALSE; /* inline, so it's not an attachment */
	
	return TRUE; /* looks like an attachment */
}

static void
get_body_cb (GMimeObject *parent, GMimeObject *part, GetBodyData *data)
{
	GMimeContentType *ct;		

	/* already found what we're looking for? */
	if ((data->_want_html && data->_html_part != NULL) ||
	    (!data->_want_html && data->_txt_part != NULL))
		return;
	
	ct = g_mime_object_get_content_type (part);
	if (!GMIME_IS_CONTENT_TYPE(ct)) {
		g_warning ("not a content type!");
		return;
	}
	
	if (looks_like_attachment (part))
		return; /* not the body */
	
	/* is it right content type? */
	if (g_mime_content_type_is_type (ct, "text", "plain"))
		data->_txt_part = part;
	else if (g_mime_content_type_is_type (ct, "text", "html"))
		data->_html_part = part;
	else
		return; /* wrong type */
}	


/* turn \0-terminated buf into ascii (which is a utf8 subset); convert
 *   any non-ascii into '.'
 */
static void
asciify (char *buf)
{
	char *c;
	for (c = buf; c && *c; ++c)
		if (!isascii(*c))
			c[0] = '.';
}



static gchar*
text_to_utf8 (const char* buffer, const char *charset)
{
	GError *err;
	gchar * utf8;

	err = NULL;
	utf8 = g_convert_with_fallback (buffer, -1, "UTF-8",
					charset, (gchar*)".", 
					NULL, NULL, &err);
	if (!utf8) {
		MU_WRITE_LOG ("%s: conversion failed from %s: %s",
			      __FUNCTION__, charset,
			      err ? err->message : "");
		if (err)
			g_error_free (err);
	}
	
	return utf8;
}


/* NOTE: buffer will be *freed* or returned unchanged */
static char*
convert_to_utf8 (GMimePart *part, char *buffer)
{
	GMimeContentType *ctype;
	const char* charset;
	unsigned char *cur;
	
	/* optimization: if the buffer is plain ascii, no conversion
	 * is done... */
	for (cur = (unsigned char*)buffer; *cur && *cur < 0x80; ++cur);
	if (*cur == '\0')
		return buffer;
	
	ctype = g_mime_object_get_content_type (GMIME_OBJECT(part));
	g_return_val_if_fail (GMIME_IS_CONTENT_TYPE(ctype), NULL);
	
	charset = g_mime_content_type_get_parameter (ctype, "charset");
	if (charset) 
		charset = g_mime_charset_iconv_name (charset);
	
	/* of course, the charset specified may be incorrect... */
	if (charset) {
		char *utf8 = text_to_utf8 (buffer, charset);
		if (utf8) {
			g_free (buffer);
			return utf8;
		}
	}

	/* hmmm.... no charset at all, or conversion failed; ugly
	 *  hack: replace all non-ascii chars with '.'
	 *  instead... TODO: come up with something better */
	asciify (buffer);
	return buffer;
}


static gchar*
stream_to_string (GMimeStream *stream, size_t buflen)
{
	char *buffer;
	ssize_t bytes;
	
	buffer = g_new(char, buflen + 1);
	g_mime_stream_reset (stream);
	
	/* we read everything in one go */
	bytes = g_mime_stream_read (stream, buffer, buflen);
	if (bytes < 0) {
		g_warning ("%s: failed to read from stream", __FUNCTION__);
		g_free (buffer);
		return NULL;
	}
	
	buffer[bytes]='\0'; 

	return buffer;
}


static gchar*
part_to_string (GMimePart *part, gboolean *err)
{
	GMimeDataWrapper *wrapper;
	GMimeStream *stream = NULL;
	ssize_t buflen;
	char *buffer = NULL;

	*err = TRUE;
	g_return_val_if_fail (GMIME_IS_PART(part), NULL);
	
	wrapper = g_mime_part_get_content_object (part);
	if (!wrapper) {
		/* this happens with invalid mails */
		g_debug ("failed to create data wrapper");
		goto cleanup;
	}

	stream = g_mime_stream_mem_new ();
	if (!stream) {
		g_warning ("failed to create mem stream");
		goto cleanup;
	}

	buflen = g_mime_data_wrapper_write_to_stream (wrapper, stream);
	if (buflen <= 0)  {/* empty buffer, not an error */
		*err = FALSE;
		goto cleanup;
	}
	
	buffer = stream_to_string (stream, (size_t)buflen);
	
	/* convert_to_utf8 will free the old 'buffer' if needed */
	buffer = convert_to_utf8 (part, buffer);
	
	*err = FALSE;
	
cleanup:				
	if (stream)
		g_object_unref (G_OBJECT(stream));
	
	return buffer;
}


static char*
get_body (MuMsgFile *self, gboolean want_html)
{
	GetBodyData data;
	char *str;
	gboolean err;
	
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (GMIME_IS_MESSAGE(self->_mime_msg), NULL);
	
	memset (&data, 0, sizeof(GetBodyData));
	data._want_html = want_html;

	err = FALSE;
	g_mime_message_foreach (self->_mime_msg,
				(GMimeObjectForeachFunc)get_body_cb,
				&data);
	if (want_html)
		str = data._html_part ?
			part_to_string (GMIME_PART(data._html_part), &err) :
			NULL; 
	else
		str = data._txt_part ?
			part_to_string (GMIME_PART(data._txt_part), &err) :
			NULL;

	/* note, str may be NULL (no body), but that's not necessarily
	 * an error; we only warn when an actual error occured */
	if (err) 
		g_warning ("error occured while retrieving %s body" 
			   "for message %s",
			   want_html ? "html" : "text", self->_path);
	return str;
}


static GSList*
get_msgids_from_header (MuMsgFile *self, const char* header)
{
	GSList *msgids;
	const char *str;

	msgids = NULL;
	str = g_mime_object_get_header (GMIME_OBJECT(self->_mime_msg),
					header);
	
	/* get stuff from the 'references' header */
	if (str) {
		const GMimeReferences *cur;
		GMimeReferences *mime_refs;
		mime_refs = g_mime_references_decode (str);
		for (cur = mime_refs; cur; cur = g_mime_references_get_next(cur)) {
			const char* msgid;
			msgid = g_mime_references_get_message_id (cur);
			if (msgid)
				msgids = g_slist_prepend (msgids, g_strdup (msgid));
		}
		g_mime_references_free (mime_refs);
	}

	return g_slist_reverse (msgids);
}


static GSList*
get_references (MuMsgFile *self)
{
	GSList *refs, *inreply;
	
	g_return_val_if_fail (self, NULL);

	refs = get_msgids_from_header (self, "References");
	
	/* now, add in-reply-to:, we only take the first one if there
	 * are more */
	inreply = get_msgids_from_header (self, "In-reply-to");
	if (inreply) {
		refs = g_slist_prepend (refs, g_strdup ((gchar*)inreply->data));
		g_slist_foreach (inreply, (GFunc)g_free, NULL);
		g_slist_free (inreply);
	}
				 
	/* put in proper order */
	return g_slist_reverse (refs);
}

static char*
get_references_str (MuMsgFile *self)
{
	GSList *refs;
	gchar *refsstr;

	g_return_val_if_fail (self, NULL);

	refsstr = NULL;
	refs = get_references (self);
	if (refs) {
		const GSList *cur;
		for (cur = refs; cur; cur = g_slist_next(cur)) {
			char *tmp;
			tmp = g_strdup_printf ("%s%s%s",
					       refsstr ? refsstr : "",
					       refsstr ? "," : "",
					       (gchar*)cur->data);
			g_free (refsstr);
			refsstr = tmp;
		}
	}			

	g_slist_foreach (refs, (GFunc)g_free, NULL);
	g_slist_free (refs);
	
	return refsstr;
}


char*
mu_msg_file_get_str_field (MuMsgFile *self, MuMsgFieldId mfid, gboolean *do_free)
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (mu_msg_field_is_string(mfid), NULL);

	*do_free = FALSE; /* default */
	
	switch (mfid) {

	case MU_MSG_FIELD_ID_BCC: *do_free = TRUE;
		return get_recipient (self, GMIME_RECIPIENT_TYPE_BCC);

	case MU_MSG_FIELD_ID_BODY_TEXT: *do_free = TRUE;
		return get_body (self, FALSE);

	case MU_MSG_FIELD_ID_BODY_HTML: *do_free = TRUE;
		return get_body (self, TRUE);

	case MU_MSG_FIELD_ID_CC: *do_free = TRUE;
		return get_recipient (self, GMIME_RECIPIENT_TYPE_CC);

	case MU_MSG_FIELD_ID_FROM:
		return (char*)g_mime_message_get_sender (self->_mime_msg);
		
	case MU_MSG_FIELD_ID_PATH:
		return self->_path;
		
	case MU_MSG_FIELD_ID_SUBJECT:
		return (char*)g_mime_message_get_subject (self->_mime_msg);

	case MU_MSG_FIELD_ID_TO: *do_free = TRUE;
		return get_recipient (self, GMIME_RECIPIENT_TYPE_TO);

	case MU_MSG_FIELD_ID_MSGID:
		return (char*)g_mime_message_get_message_id (self->_mime_msg);

	case MU_MSG_FIELD_ID_MAILDIR:
		return self->_maildir;
		
	case MU_MSG_FIELD_ID_REFS: *do_free = TRUE;
		return get_references_str (self);
	
	default:
		g_return_val_if_reached (NULL);
	}
}

gint64
mu_msg_file_get_num_field (MuMsgFile *self, const MuMsgFieldId mfid)
{
	g_return_val_if_fail (self, -1);
	g_return_val_if_fail (mu_msg_field_is_numeric(mfid), -1);
	
	switch (mfid) {
		
	case MU_MSG_FIELD_ID_DATE: {
		time_t t;
		g_mime_message_get_date (self->_mime_msg, &t, NULL);
		return (time_t)t;
	}
		
	case MU_MSG_FIELD_ID_FLAGS:
		return (gint64)get_flags(self);

	case MU_MSG_FIELD_ID_PRIO:
		return (gint64)get_prio(self);

	case MU_MSG_FIELD_ID_SIZE:
		return (gint64)get_size(self);

	default: g_return_val_if_reached (-1);
	}
}



const char*
mu_msg_file_get_header (MuMsgFile *self, const char *header)
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (header, NULL);
	
	return g_mime_object_get_header
		(GMIME_OBJECT(self->_mime_msg), header);
}
