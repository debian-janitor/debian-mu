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


MuMsgFile*
mu_msg_file_new (const char* filepath, const char *mdir, GError **err)
{
	MuMsgFile *self;

	g_return_val_if_fail (filepath, NULL);

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

	mu_str_free_list (self->_free_later);

	g_slice_free (MuMsgFile, self);
}

static const gchar*
free_string_later (MuMsgFile *self, gchar *str)
{
	self->_free_later = g_slist_prepend (self->_free_later, str);
	return str;
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

	if (recip && !g_utf8_validate (recip, -1, NULL)) {
		g_debug ("invalid recipient in %s\n", self->_path);
		mu_str_asciify_in_place (recip); /* ugly... */
	}

	if (mu_str_is_empty(recip)) {
		g_free (recip);
		return NULL;
	}

	return recip;
}


static gboolean
looks_like_attachment (GMimeObject *part)
{
	const char *str;
	GMimeContentDisposition *disp;
	GMimeContentType *ct;

	disp = g_mime_object_get_content_disposition (GMIME_OBJECT(part));
	if (!GMIME_IS_CONTENT_DISPOSITION(disp))
		return FALSE;

	str = g_mime_content_disposition_get_disposition (disp);
	if (!str)
		return FALSE;

	ct = g_mime_object_get_content_type (part);
	if (!ct)
		return FALSE; /* ignore this part... */

	/* note, some mailers use ATTACHMENT, INLINE instead of their
	 * more common lower-case counterparts */
	if (g_ascii_strcasecmp(str, GMIME_DISPOSITION_ATTACHMENT) == 0)
		return TRUE;

	if (g_ascii_strcasecmp(str, GMIME_DISPOSITION_INLINE) == 0) {
		/* some inline parts are also considered attachments... */
		int i;
		const char* att_types[][2] = {
			{"image", "*"},
			{"application", "*"},
			{"message", "*"}};

		for (i = 0; i != G_N_ELEMENTS (att_types); ++i)
			if (g_mime_content_type_is_type (ct,
							 att_types[i][0],
							 att_types[i][1]))
				return TRUE; /* looks like an attachment */
	}

	return FALSE; /* does not look like an attachment */
}


static void
msg_cflags_cb (GMimeObject *parent, GMimeObject *part, MuFlags *flags)
{
	if (*flags & MU_FLAG_HAS_ATTACH)
		return;

	if (!GMIME_IS_PART(part))
		return;

	if (!(*flags & MU_FLAG_HAS_ATTACH) && looks_like_attachment(part))
		*flags |= MU_FLAG_HAS_ATTACH;
}



static MuFlags
get_content_flags (MuMsgFile *self)
{
	GMimeContentType *ctype;
	MuFlags flags;
	GMimeObject *part;

	if (!GMIME_IS_MESSAGE(self->_mime_msg))
		return MU_FLAG_NONE;

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
				flags |= MU_FLAG_SIGNED;
			if (g_mime_content_type_is_type
			    (ctype,"*", "encrypted"))
				flags |= MU_FLAG_ENCRYPTED;
		}
	} else
		g_warning ("no top level mime part found");

	return flags;
}


static MuFlags
get_flags (MuMsgFile *self)
{
	MuFlags flags;

	g_return_val_if_fail (self, MU_FLAG_INVALID);

	flags = mu_maildir_get_flags_from_path (self->_path);
	flags |= get_content_flags (self);

	/* pseudo-flag --> unread means either NEW or NOT SEEN, just
	 * for searching convenience */
	if ((flags & MU_FLAG_NEW) || !(flags & MU_FLAG_SEEN))
		flags |= MU_FLAG_UNREAD;

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

	str = g_mime_object_get_header (obj, "Precedence");
	if (!str)
		str = g_mime_object_get_header (obj, "X-Priority");
	if (!str)
		str = g_mime_object_get_header (obj, "Importance");
	/* NOTE: "X-MSMail-Priority" is never seen without "X-Priority" */
	/* if (!str) */
	/* 	str = g_mime_object_get_header (obj, "X-MSMail-Priority"); */
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


struct _GetBodyData {
	GMimeObject *_txt_part, *_html_part;
	gboolean _want_html;
};
typedef struct _GetBodyData GetBodyData;



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
		char *utf8 = mu_str_convert_to_utf8 (buffer, charset);
		if (utf8) {
			g_free (buffer);
			return utf8;
		}
	}

	/* hmmm.... no charset at all, or conversion failed; ugly
	 *  hack: replace all non-ascii chars with '.' */
	mu_str_asciify_in_place (buffer);
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


gchar*
mu_msg_mime_part_to_string (GMimePart *part, gboolean *err)
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


GMimePart*
mu_msg_mime_get_body_part (GMimeMessage *msg, gboolean want_html)
{
	GetBodyData data;

	g_return_val_if_fail (GMIME_IS_MESSAGE(msg), NULL);

	memset (&data, 0, sizeof(GetBodyData));
	data._want_html = want_html;

	g_mime_message_foreach (msg,
				(GMimeObjectForeachFunc)get_body_cb,
				&data);
	if (want_html)
		return (GMimePart*)data._html_part;
	else
		return (GMimePart*)data._txt_part;
}



static char*
get_body (MuMsgFile *self, gboolean want_html)
{
	GMimePart *part;

	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (GMIME_IS_MESSAGE(self->_mime_msg), NULL);

	part = mu_msg_mime_get_body_part (self->_mime_msg, want_html);
	if (GMIME_IS_PART(part)) {
		gboolean err;
		gchar *str;

		err = FALSE;
		str = mu_msg_mime_part_to_string (part, &err);

		/* note, str may be NULL (no body), but that's not necessarily
		 * an error; we only warn when an actual error occured */
		if (err)
			g_warning ("error occured while retrieving %s body "
				   "for message %s",
				   want_html ? "html" : "text", self->_path);
		return str;
	}

	return NULL;
}


static void
append_text (GMimeObject *parent, GMimeObject *part, gchar **txt)
{
	GMimeContentType *ct;
	gchar *parttxt, *tmp;
	gboolean err;

	if (!GMIME_IS_PART(part))
		return;

	ct = g_mime_object_get_content_type (part);
	if (!GMIME_IS_CONTENT_TYPE(ct)) {
		g_warning ("%s: no content-type for part", __FUNCTION__);
		return;
	}

	/* is it right content type? */
	if (!g_mime_content_type_is_type (ct, "text", "plain"))
		return; /* nope */

	parttxt = mu_msg_mime_part_to_string (GMIME_PART(part), &err);
	if (err) {
		g_warning ("%s: could not get text for part", __FUNCTION__);
		return;
	}

	/* it's a text part -- append it! note, we ignore the
	 * disposition here. */
	tmp = *txt;
	if (*txt) {
		*txt = g_strconcat (*txt, parttxt, NULL);
		g_free (parttxt);
	} else
		*txt = parttxt;

	g_free (tmp);
}

/* instead of just the body, this function returns a concatenation of
 * all text/plain parts with inline disposition
 */
static char*
get_concatenated_text (MuMsgFile *self)
{
	char *txt;

	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (GMIME_IS_MESSAGE(self->_mime_msg), NULL);

	txt = NULL;
	g_mime_message_foreach (self->_mime_msg,
				(GMimeObjectForeachFunc)append_text,
				&txt);

	return txt;
}




static gboolean
contains (GSList *lst, const char *str)
{
	for (; lst; lst = g_slist_next(lst))
		if (g_strcmp0 ((char*)lst->data, str) == 0)
			return TRUE;
	return FALSE;
}


static GSList*
get_references  (MuMsgFile *self)
{
	GSList *msgids;
	const char *str;
	unsigned u;
	const char *headers[] = { "References", "In-reply-to", NULL };

	for (msgids = NULL, u = 0; headers[u]; ++u) {

		const GMimeReferences *cur;
		GMimeReferences *mime_refs;

		str = mu_msg_file_get_header (self, headers[u]);
		if (!str)
			continue;

		mime_refs = g_mime_references_decode (str);
		for (cur = mime_refs; cur; cur = g_mime_references_get_next(cur)) {
			const char* msgid;
			msgid = g_mime_references_get_message_id (cur);
			/* don't include duplicates */
			if (msgid && !contains (msgids, msgid))
				/* explicitly ensure it's utf8-safe, as GMime
				 * does not ensure that */
				msgids = g_slist_prepend (msgids, g_strdup((msgid)));
		}
		g_mime_references_free (mime_refs);
	}

	return g_slist_reverse (msgids);
}


static GSList*
get_tags (MuMsgFile *self)
{
	const char *hdr;

	hdr = mu_msg_file_get_header (self, "X-Label");
	if (!hdr)
		return NULL;

	return mu_str_to_list (hdr, ',', TRUE);
}


/* wrongly encoded messages my cause GMime to return invalid
 * UTF8... we double check, and ensure our output is always correct
 * utf8 */
gchar *
maybe_cleanup (const char* str, const char *path, gboolean *do_free)
{
	if (!str || G_LIKELY(g_utf8_validate(str, -1, NULL)))
		return (char*)str;

	g_debug ("invalid utf8 in %s", path);

	if (*do_free)
		return mu_str_asciify_in_place ((char*)str);
	else {
		gchar *ascii;
		ascii = mu_str_asciify_in_place(g_strdup (str));
		*do_free = TRUE;
		return ascii;
	}
}


G_GNUC_CONST static GMimeRecipientType
recipient_type (MuMsgFieldId mfid)
{
	switch (mfid) {
	case MU_MSG_FIELD_ID_BCC: return GMIME_RECIPIENT_TYPE_BCC;
	case MU_MSG_FIELD_ID_CC : return GMIME_RECIPIENT_TYPE_CC;
	case MU_MSG_FIELD_ID_TO : return GMIME_RECIPIENT_TYPE_TO;
	default: g_return_val_if_reached (-1);
	}
}


char*
mu_msg_file_get_str_field (MuMsgFile *self, MuMsgFieldId mfid,
			   gboolean *do_free)
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (mu_msg_field_is_string(mfid), NULL);

	*do_free = FALSE; /* default */

	switch (mfid) {

	case MU_MSG_FIELD_ID_EMBEDDED_TEXT: *do_free = TRUE;
		return NULL; /* FIXME */

	case MU_MSG_FIELD_ID_BCC:
	case MU_MSG_FIELD_ID_CC:
	case MU_MSG_FIELD_ID_TO: *do_free = TRUE;
		return get_recipient (self, recipient_type(mfid));

	case MU_MSG_FIELD_ID_BODY_TEXT: *do_free = TRUE;
		return get_concatenated_text (self);
	case MU_MSG_FIELD_ID_BODY_HTML: *do_free = TRUE;
		return get_body (self, TRUE);

	case MU_MSG_FIELD_ID_FROM:
		return (char*)maybe_cleanup
			(g_mime_message_get_sender (self->_mime_msg),
			 self->_path, do_free);

	case MU_MSG_FIELD_ID_PATH: return self->_path;

	case MU_MSG_FIELD_ID_SUBJECT:
		return (char*)maybe_cleanup
			(g_mime_message_get_subject (self->_mime_msg),
			 self->_path, do_free);

	case MU_MSG_FIELD_ID_MSGID:
		return (char*)g_mime_message_get_message_id (self->_mime_msg);

	case MU_MSG_FIELD_ID_MAILDIR: return self->_maildir;

	default: g_return_val_if_reached (NULL);
	}
}


GSList*
mu_msg_file_get_str_list_field (MuMsgFile *self, MuMsgFieldId mfid,
				gboolean *do_free)
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (mu_msg_field_is_string_list(mfid), NULL);

	switch (mfid) {

	case MU_MSG_FIELD_ID_REFS:
		*do_free = TRUE;
		return get_references (self);
	case MU_MSG_FIELD_ID_TAGS:
		*do_free = TRUE;
		return get_tags (self);
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
	const gchar *hdr;

	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (header, NULL);

	/* sadly, g_mime_object_get_header may return non-ascii;
	 * so, we need to ensure that
	 */
	hdr = g_mime_object_get_header (GMIME_OBJECT(self->_mime_msg),
					header);

	return hdr ? free_string_later (self, mu_str_utf8ify(hdr)) : NULL;
}
