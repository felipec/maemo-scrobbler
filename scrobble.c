/*
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the LGPLv2.1.
 */

#include "scrobble.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <glib.h>
#include <libsoup/soup.h>

struct sr_session_priv {
	char *url;
	char *client_id;
	char *client_ver;
	char *user, *hash_pwd;
	GQueue *queue;
	GMutex *queue_mutex;
	SoupSession *soup;
	int handshake_delay;
	char *session_id;
	char *now_playing_url;
	char *submit_url;
	int hard_failure_count;
	int submit_count;
	sr_track_t *last_track;
};

sr_session_t *
sr_session_new(const char *url,
	       const char *client_id,
	       const char *client_ver)
{
	sr_session_t *s;
	struct sr_session_priv *priv;
	s = calloc(1, sizeof(*s));
	s->priv = priv = calloc(1, sizeof(*priv));
	priv->queue = g_queue_new();
	priv->queue_mutex = g_mutex_new();
	priv->url = strdup(url);
	priv->client_id = strdup(client_id);
	priv->client_ver = strdup(client_ver);
	priv->soup = soup_session_async_new();
	priv->handshake_delay = 1;
	return s;
}

void
sr_session_free(sr_session_t *s)
{
	struct sr_session_priv *priv;

	if (!s)
		return;

	priv = s->priv;

	soup_session_abort(priv->soup);
	g_object_unref(priv->soup);
	while (!g_queue_is_empty(priv->queue)) {
		sr_track_t *t;
		t = g_queue_pop_head(priv->queue);
		sr_track_free(t);
	}
	g_queue_free(priv->queue);
	g_mutex_free(priv->queue_mutex);
	free(priv->url);
	free(priv->client_id);
	free(priv->client_ver);
	free(priv->user);
	g_free(priv->hash_pwd);
	g_free(priv->session_id);
	g_free(priv->now_playing_url);
	g_free(priv->submit_url);
	free(s->priv);
	free(s);
}

void sr_session_set_cred(sr_session_t *s,
			 char *user,
			 char *password)
{
	struct sr_session_priv *priv = s->priv;
	priv->user = strdup(user);
	priv->hash_pwd = g_compute_checksum_for_string(G_CHECKSUM_MD5, password, -1);
}

sr_track_t *
sr_track_new(void)
{
	sr_track_t *t;
	t = calloc(1, sizeof(*t));
	return t;
}

void
sr_track_free(sr_track_t *t)
{
	g_free(t->artist);
	g_free(t->title);
	g_free(t->album);
	g_free(t->mbid);
	free(t);
}

sr_track_t *
sr_track_dup(sr_track_t *in)
{
	sr_track_t *t;
	t = sr_track_new();
	t->artist = g_strdup(in->artist);
	t->title = g_strdup(in->title);
	t->timestamp = in->timestamp;
	t->source = in->source;
	t->rating = in->rating;
	t->length = in->length;
	t->album = g_strdup(in->album);
	t->position = in->position;
	t->mbid = g_strdup(in->mbid);
	return t;
}

static inline void
check_last(sr_session_t *s,
	   int timestamp)
{
	struct sr_session_priv *priv = s->priv;
	sr_track_t *c;
	int playtime;

	c = priv->last_track;
	if (!c)
		return;
	playtime = timestamp - c->timestamp;
	/* did the last track played long enough? */
	if (playtime >= 240 || playtime >= c->length / 2)
		g_queue_push_tail(priv->queue, c);
	else
		sr_track_free(c);
	priv->last_track = NULL;
}

void
sr_session_pause(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	g_mutex_lock(priv->queue_mutex);
	check_last(s, time(NULL));
	g_mutex_unlock(priv->queue_mutex);
}

void
sr_session_add_track(sr_session_t *s,
		     sr_track_t *t)
{
	struct sr_session_priv *priv = s->priv;

	g_mutex_lock(priv->queue_mutex);
	check_last(s, t->timestamp);
	if (t->length > 30)
		priv->last_track = t;
	else
		sr_track_free(t);
	g_mutex_unlock(priv->queue_mutex);
}

static inline void
got_field(sr_track_t *t,
	  char k,
	  const char *value)
{
	switch (k) {
	case 'a':
		t->artist = g_strdup(value);
		break;
	case 't':
		t->title = g_strdup(value);
		break;
	case 'i':
		t->timestamp = atoi(value);
		break;
	case 'o':
		t->source = value[0];
		break;
	case 'r':
		t->rating = value[0];
		break;
	case 'l':
		t->length = atoi(value);
		break;
	case 'b':
		t->album = g_strdup(value);
		break;
	case 'n':
		t->position = atoi(value);
		break;
	case 'm':
		t->mbid = g_strdup(value);
		break;
	default:
		break;
	}
}

static inline bool
track_is_valid(sr_track_t *t)
{
	return !!t->artist;
}

int
sr_session_load_list(sr_session_t *s,
		     const char *file)
{
	struct sr_session_priv *priv = s->priv;
	FILE *f;
	char c, *p;
	char k, v[255];
	int stage = 1;
	sr_track_t *t;

	f = fopen(file, "r");
	if (!f)
		return 1;

	g_mutex_lock(priv->queue_mutex);
	t = sr_track_new();
	while (true) {
		c = getc(f);
		if (stage == 1) {
			if (c == '\n') {
				g_queue_push_tail(priv->queue, t);
				t = sr_track_new();
				continue;
			}
			if (c == (char) EOF) {
				if (track_is_valid(t))
					g_queue_push_tail(priv->queue, t);
				break;
			}
			k = c;
			p = v;
			fseek(f, 2, SEEK_CUR);
			stage++;
		}
		else if (stage == 2) {
			*p = c;
			if (c == '\n') {
				*p = '\0';
				got_field(t, k, v);
				stage = 1;
			}
			p++;
		}
	}
	g_mutex_unlock(priv->queue_mutex);

	fclose(f);
	return 0;
}

static void
store_track(void *data,
	    void *user_data)
{
	sr_track_t *t = data;
	FILE *f = user_data;

	fprintf(f, "a: %s\n", t->artist);
	fprintf(f, "t: %s\n", t->title);
	fprintf(f, "i: %u\n", t->timestamp);
	fprintf(f, "o: %c\n", t->source);
	if (t->rating)
		fprintf(f, "r: %c\n", t->rating);
	fprintf(f, "l: %i\n", t->length);
	if (t->album)
		fprintf(f, "b: %s\n", t->album);
	if (t->position)
		fprintf(f, "n: %i\n", t->position);
	if (t->mbid)
		fprintf(f, "m: %s\n", t->mbid);

	fputc('\n', f);
}

int
sr_session_store_list(sr_session_t *s,
		      const char *file)
{
	FILE *f;
	struct sr_session_priv *priv = s->priv;

	f = fopen(file, "w");
	g_mutex_lock(priv->queue_mutex);
	g_queue_foreach(priv->queue, store_track, f);
	g_mutex_unlock(priv->queue_mutex);
	fclose(f);
	return 0;
}

void
sr_session_test(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	g_mutex_lock(priv->queue_mutex);
	g_queue_foreach(priv->queue, store_track, stdout);
	g_mutex_unlock(priv->queue_mutex);
}

static void
parse_handshake(sr_session_t *s,
		const char *data)
{
	struct sr_session_priv *priv = s->priv;
	char **response;

	response = g_strsplit(data, "\n", 5);

	g_free(priv->session_id);
	g_free(priv->now_playing_url);
	g_free(priv->submit_url);

	priv->session_id = g_strdup(response[1]);
	priv->now_playing_url = g_strdup(response[2]);
	priv->submit_url = g_strdup(response[3]);

	g_strfreev(response);
}

static gboolean
try_handshake(gpointer data)
{
	sr_session_handshake(data);
	return false;
}

static inline void
handshake_failure(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;

	g_timeout_add_seconds(priv->handshake_delay * 60,
			      try_handshake, s);

	if (priv->handshake_delay < 120)
		priv->handshake_delay *= 2;
}

static inline void
fatal_error(sr_session_t *s,
	    const char *msg)
{
	if (s->error_cb)
		s->error_cb(true, msg);
}

static void
handshake_cb(SoupSession *session,
	     SoupMessage *message,
	     void *user_data)
{
	sr_session_t *s = user_data;
	struct sr_session_priv *priv = s->priv;
	const char *data, *end;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code)) {
		handshake_failure(s);
		return;
	}

	data = message->response_body->data;
	end = strchr(data, '\n');
	if (!end) /* really bad */
		return;

	if (strncmp(data, "OK", end - data) == 0) {
		priv->handshake_delay = 1;
		parse_handshake(s, data);
		sr_session_submit(s);
	}
	else if (strncmp(data, "BANNED", end - data) == 0)
		fatal_error(s, "Client is banned");
	else if (strncmp(data, "BADAUTH", end - data) == 0)
		fatal_error(s, "Bad authorization");
	else if (strncmp(data, "BADTIME", end - data) == 0)
		fatal_error(s, "Wrong system time");
	else
		handshake_failure(s);
}

void
sr_session_handshake(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	gchar *auth, *tmp;
	glong timestamp;
	gchar *handshake_url;
	SoupMessage *message;
	GTimeVal time_val;

	g_get_current_time(&time_val);
	timestamp = time_val.tv_sec;

	tmp = g_strdup_printf("%s%li", priv->hash_pwd, timestamp);
	auth = g_compute_checksum_for_string(G_CHECKSUM_MD5, tmp, -1);
	g_free(tmp);

	handshake_url = g_strdup_printf("%s&p=1.2.1&c=%s&v=%s&u=%s&t=%li&a=%s",
					priv->url,
					priv->client_id,
					priv->client_ver,
					priv->user,
					timestamp,
					auth);

	message = soup_message_new("GET", handshake_url);
	soup_session_queue_message(priv->soup,
				   message,
				   handshake_cb,
				   s);

	g_free(handshake_url);
	g_free(auth);
}

static void
drop_submitted(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	int c;

	g_mutex_lock(priv->queue_mutex);
	for (c = 0; c < priv->submit_count; c++) {
		sr_track_t *t;
		t = g_queue_pop_head(priv->queue);
		sr_track_free(t);
		if (g_queue_is_empty(priv->queue))
			break;
	}
	g_mutex_unlock(priv->queue_mutex);
}

static inline void
invalidate_session(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	g_free(priv->session_id);
	priv->session_id = NULL;
	sr_session_handshake(s);
}

static inline void
hard_failure(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	priv->hard_failure_count++;
	if (priv->hard_failure_count >= 3)
		invalidate_session(s);
}

static void
scrobble_cb(SoupSession *session,
	    SoupMessage *message,
	    gpointer user_data)
{
	sr_session_t *s = user_data;
	const char *data, *end;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code)) {
		hard_failure(s);
		return;
	}

	data = message->response_body->data;
	end = strchr(data, '\n');
	if (!end) /* really bad */
		return;

	if (strncmp(data, "OK", end - data) == 0)
		drop_submitted(user_data);
	else if (strncmp(data, "BADSESSION", end - data) == 0)
		invalidate_session(s);
	else
		hard_failure(s);
}

#define ADD_FIELD(id, fmt, field) \
	do { \
		if ((field)) \
		g_string_append_printf(data, "&" id "[%i]=%" fmt, i, (field)); \
		else \
		g_string_append_printf(data, "&" id "[%i]=", i); \
	} while(0);

#define EXTRA_URI_ENCODE_CHARS "&+"

void
sr_session_submit(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	SoupMessage *message;
	int i = 0;
	GString *data;
	GList *c;

	/* haven't got the session yet? */
	if (!priv->session_id)
		return;

	g_mutex_lock(priv->queue_mutex);
	if (g_queue_is_empty(priv->queue)) {
		g_mutex_unlock(priv->queue_mutex);
		return;
	}

	data = g_string_new(NULL);
	g_string_append_printf(data, "s=%s", priv->session_id);

	for (c = priv->queue->head; c; c = c->next) {
		sr_track_t *t = c->data;
		char *artist, *title;
		char *album = NULL, *mbid = NULL;

		artist = soup_uri_encode(t->artist, EXTRA_URI_ENCODE_CHARS);
		title = soup_uri_encode(t->title, EXTRA_URI_ENCODE_CHARS);
		if (t->album)
			album = soup_uri_encode(t->album, EXTRA_URI_ENCODE_CHARS);
		if (t->mbid)
			mbid = soup_uri_encode(t->mbid, EXTRA_URI_ENCODE_CHARS);

		/* required fields */
		g_string_append_printf(data, "&a[%i]=%s&t[%i]=%s&i[%i]=%i&o[%i]=%c",
				       i, artist,
				       i, title,
				       i, t->timestamp,
				       i, t->source);

		/* optional fields */
		ADD_FIELD("r", "c", t->rating);
		ADD_FIELD("l", "i", t->length);
		ADD_FIELD("b", "s", album);
		ADD_FIELD("n", "i", t->position);
		ADD_FIELD("m", "s", mbid);

		g_free(artist);
		g_free(title);
		g_free(album);
		g_free(mbid);

		if (++i >= 50)
			break;
	}
	priv->submit_count = i;

	g_mutex_unlock(priv->queue_mutex);

	message = soup_message_new("POST", priv->submit_url);
	soup_message_set_request(message,
				 "application/x-www-form-urlencoded",
				 SOUP_MEMORY_TAKE,
				 data->str,
				 data->len);
	soup_session_queue_message(priv->soup,
				   message,
				   scrobble_cb,
				   s);
	g_string_free(data, false); /* soup gets ownership */
}
