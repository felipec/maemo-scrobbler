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
#include <stdarg.h>

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
	int np_timer;

	/* web-service */
	char *api_url;
	char *api_key;
	char *api_secret;
	char *session_key;
	GQueue *love_queue;
	GMutex *love_queue_mutex;
	bool api_problems;
};

static void now_playing(sr_session_t *s, sr_track_t *t);
static void ws_auth(sr_session_t *s);
static void ws_love(sr_session_t *s);

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
	priv->url = g_strdup(url);
	priv->client_id = g_strdup(client_id);
	priv->client_ver = g_strdup(client_ver);
	priv->soup = soup_session_async_new();
	priv->handshake_delay = 1;
	priv->love_queue = g_queue_new();
	priv->love_queue_mutex = g_mutex_new();
	return s;
}

void
sr_session_free(sr_session_t *s)
{
	struct sr_session_priv *priv;

	if (!s)
		return;

	priv = s->priv;

	while (!g_queue_is_empty(priv->love_queue)) {
		sr_track_t *t;
		t = g_queue_pop_head(priv->love_queue);
		sr_track_free(t);
	}
	g_queue_free(priv->love_queue);
	g_mutex_free(priv->love_queue_mutex);

	g_free(priv->api_url);
	g_free(priv->api_key);
	g_free(priv->api_secret);
	g_free(priv->session_key);

	if (priv->np_timer)
		g_source_remove(priv->np_timer);

	soup_session_abort(priv->soup);
	g_object_unref(priv->soup);
	while (!g_queue_is_empty(priv->queue)) {
		sr_track_t *t;
		t = g_queue_pop_head(priv->queue);
		sr_track_free(t);
	}
	g_queue_free(priv->queue);
	g_mutex_free(priv->queue_mutex);
	g_free(priv->url);
	g_free(priv->client_id);
	g_free(priv->client_ver);
	g_free(priv->user);
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
	g_free(priv->user);
	g_free(priv->hash_pwd);
	priv->user = g_strdup(user);
	priv->hash_pwd = g_compute_checksum_for_string(G_CHECKSUM_MD5, password, -1);
}

void sr_session_set_cred_hash(sr_session_t *s,
		char *user,
		char *hash_pwd)
{
	struct sr_session_priv *priv = s->priv;
	g_free(priv->user);
	g_free(priv->hash_pwd);
	priv->user = g_strdup(user);
	priv->hash_pwd = g_strdup(hash_pwd);
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
	if (!t)
		return;
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

	if (c->rating == 'L' && priv->session_key) {
		g_mutex_lock(priv->love_queue_mutex);
		g_queue_push_tail(priv->love_queue, sr_track_dup(c));
		g_mutex_unlock(priv->love_queue_mutex);
		if (!priv->api_problems)
			ws_love(s);
	}

	playtime = timestamp - c->timestamp;
	/* did the last track played long enough? */
	if ((playtime >= 240 || playtime >= c->length / 2) && c->length > 30)
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

static gboolean
do_now_playing(void *data)
{
	sr_session_t *s = data;
	struct sr_session_priv *priv = s->priv;
	priv->np_timer = 0;
	now_playing(s, priv->last_track);
	return FALSE;
}

void
sr_session_add_track(sr_session_t *s,
		sr_track_t *t)
{
	struct sr_session_priv *priv = s->priv;

	if (priv->np_timer)
		g_source_remove(priv->np_timer);

	priv->np_timer = g_timeout_add_seconds(3, do_now_playing, s);

	g_mutex_lock(priv->queue_mutex);
	check_last(s, t->timestamp);
	priv->last_track = t;
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

	/* just to avoid warnings */
	k = 0; p = NULL;

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
try_handshake(void *data)
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
		s->error_cb(s, true, msg);
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

	if (priv->api_key) {
		if (!priv->session_key)
			ws_auth(s);
		else
			ws_love(s);
	}
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
		if (!t)
			break;
		sr_track_free(t);
	}
	priv->submit_count = 0;
	g_mutex_unlock(priv->queue_mutex);

	if (!g_queue_is_empty(priv->queue))
		/* still need to submit more */
		sr_session_submit(s);
	else if (s->scrobble_cb)
		s->scrobble_cb(s);
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
		void *user_data)
{
	sr_session_t *s = user_data;
	struct sr_session_priv *priv = s->priv;
	const char *data, *end;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code)) {
		hard_failure(s);
		goto nok;
	}

	data = message->response_body->data;
	end = strchr(data, '\n');
	if (!end) /* really bad */
		goto nok;

	if (strncmp(data, "OK", end - data) == 0) {
		drop_submitted(user_data);
		return;
	}
	else if (strncmp(data, "BADSESSION", end - data) == 0)
		invalidate_session(s);
	else
		hard_failure(s);
nok:
	g_mutex_lock(priv->queue_mutex);
	priv->submit_count = 0;
	g_mutex_unlock(priv->queue_mutex);

}

#define ADD_FIELD(id, fmt, field) \
	do { \
		if ((field)) \
		g_string_append_printf(data, "&" id "[%i]=%" fmt, i, (field)); \
		else \
		g_string_append_printf(data, "&" id "[%i]=", i); \
	} while (0);

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
	if (g_queue_is_empty(priv->queue) || priv->submit_count) {
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

static void
now_playing_cb(SoupSession *session,
		SoupMessage *message,
		void *user_data)
{
	sr_session_t *s = user_data;
	const char *data, *end;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code))
		/* now need to do anything drastic, right? */
		return;

	data = message->response_body->data;
	end = strchr(data, '\n');
	if (!end) /* really bad */
		return;

	if (strncmp(data, "BADSESSION", end - data) == 0)
		invalidate_session(s);
}

#undef ADD_FIELD
#define ADD_FIELD(id, fmt, field) \
	do { \
		if ((field)) \
		g_string_append_printf(data, "&" id "=%" fmt, (field)); \
		else \
		g_string_append_printf(data, "&" id "="); \
	} while (0);

static void
now_playing(sr_session_t *s,
		sr_track_t *t)
{
	struct sr_session_priv *priv = s->priv;
	SoupMessage *message;
	GString *data;
	char *artist, *title;
	char *album = NULL, *mbid = NULL;

	/* haven't got the session yet? */
	if (!priv->session_id)
		return;

	data = g_string_new(NULL);
	g_string_append_printf(data, "s=%s", priv->session_id);

	artist = soup_uri_encode(t->artist, EXTRA_URI_ENCODE_CHARS);
	title = soup_uri_encode(t->title, EXTRA_URI_ENCODE_CHARS);
	if (t->album)
		album = soup_uri_encode(t->album, EXTRA_URI_ENCODE_CHARS);
	if (t->mbid)
		mbid = soup_uri_encode(t->mbid, EXTRA_URI_ENCODE_CHARS);

	/* required fields */
	g_string_append_printf(data, "&a=%s&t=%s", artist, title);

	/* optional fields */
	ADD_FIELD("b", "s", album);
	ADD_FIELD("l", "i", t->length);
	ADD_FIELD("n", "i", t->position);
	ADD_FIELD("m", "s", mbid);

	g_free(artist);
	g_free(title);
	g_free(album);
	g_free(mbid);

	message = soup_message_new("POST", priv->now_playing_url);
	soup_message_set_request(message,
			"application/x-www-form-urlencoded",
			SOUP_MEMORY_TAKE,
			data->str,
			data->len);
	soup_session_queue_message(priv->soup,
			message,
			now_playing_cb,
			s);
	g_string_free(data, false); /* soup gets ownership */
}

void
sr_session_set_proxy(sr_session_t *s, const char *url)
{
	struct sr_session_priv *priv = s->priv;
	SoupURI *soup_uri;
	if (url)
		soup_uri = soup_uri_new(url);
	else
		soup_uri = NULL;
	g_object_set(priv->soup, "proxy-uri", soup_uri, NULL);
}

/* web-service */

void
sr_session_set_session_key(sr_session_t *s,
		const char *session_key)
{
	struct sr_session_priv *priv = s->priv;
	priv->session_key = g_strdup(session_key);
}

void
sr_session_set_api(sr_session_t *s,
		const char *api_url,
		const char *api_key,
		const char *api_secret)
{
	struct sr_session_priv *priv = s->priv;
	priv->api_url = g_strdup(api_url);
	priv->api_key = g_strdup(api_key);
	priv->api_secret = g_strdup(api_secret);
}

struct ws_param {
	const char *key;
	const char *value;
};

static struct ws_param *
param_new(const char *key, const char *value)
{
	struct ws_param *p = malloc(sizeof(*p));
	p->key = key;
	p->value = value;
	return p;
}

static int
param_compare(struct ws_param *a, struct ws_param *b)
{
	return strcmp(a->key, b->key);
}

static void ws_params(sr_session_t *s, char **params, ...) __attribute__((sentinel));

static void
ws_params(sr_session_t *s, char **params, ...)
{
	struct sr_session_priv *priv = s->priv;
	GList *l = NULL;
	va_list args;
	GString *params_str, *tmp;
	gchar *api_sig;

	if (!params)
		return;

	params_str = g_string_sized_new(0x100);

	va_start(args, params);
	do {
		const char *key, *value;
		key = va_arg(args, char *);
		if (!key)
			break;
		value = va_arg(args, char *);
		if (!value)
			break;
		l = g_list_prepend(l, param_new(key, value));
		g_string_append_printf(params_str, "&%s=%s", key, value);
	} while (true);
	va_end(args);

	l = g_list_sort(l, (GCompareFunc) param_compare);

	tmp = g_string_sized_new(0x100);

	GList *c;
	for (c = l; c; c = c->next) {
		struct ws_param *p = c->data;
		g_string_append_printf(tmp, "%s%s", p->key, p->value);
	}

	g_string_append(tmp, priv->api_secret);

	api_sig = g_compute_checksum_for_string(G_CHECKSUM_MD5, tmp->str, -1);

	g_string_free(tmp, TRUE);
	g_list_foreach(l, (GFunc) free, NULL);

	g_string_append_printf(params_str, "&api_sig=%s", api_sig);
	g_free(api_sig);

	*params = params_str->str;
	g_string_free(params_str, FALSE);
}

static void
ws_auth_cb(SoupSession *session,
		SoupMessage *message,
		void *user_data)
{
	sr_session_t *s = user_data;
	struct sr_session_priv *priv = s->priv;
	const char *data, *begin, *end;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code))
		return;

	data = message->response_body->data;

	begin = strstr(data, "<key>");
	if (!begin) /* really bad */
		return;
	begin += 5;
	end = strstr(begin, "</key>");
	if (!end) /* really bad */
		return;
	priv->session_key = g_strndup(begin, end - begin);
	if (s->session_key_cb)
		s->session_key_cb(s, priv->session_key);
}

static void
ws_auth(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	gchar *auth, *tmp;
	gchar *auth_url;
	SoupMessage *message;

	gchar *params;

	tmp = g_strdup_printf("%s%s", priv->user, priv->hash_pwd);
	auth = g_compute_checksum_for_string(G_CHECKSUM_MD5, tmp, -1);
	g_free(tmp);

	ws_params(s, &params,
			"api_key", priv->api_key,
			"authToken", auth,
			"method", "auth.getMobileSession",
			"username", priv->user,
			NULL);

	auth_url = g_strdup_printf("%s?%s",
			priv->api_url,
			params);
	g_free(params);

	message = soup_message_new("GET", auth_url);
	soup_session_queue_message(priv->soup,
			message,
			ws_auth_cb,
			s);

	g_free(auth_url);
	g_free(auth);
}

static void
ws_love_cb(SoupSession *session,
		SoupMessage *message,
		void *user_data)
{
	sr_session_t *s = user_data;
	struct sr_session_priv *priv = s->priv;
	sr_track_t *t;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code)) {
		priv->api_problems = true;
		return;
	}

	g_mutex_lock(priv->love_queue_mutex);
	t = g_queue_pop_head(priv->love_queue);
	g_mutex_unlock(priv->love_queue_mutex);
	sr_track_free(t);

	priv->api_problems = false;

	if (!g_queue_is_empty(priv->love_queue))
		/* still need to submit more */
		ws_love(s);
}

static void
ws_love(sr_session_t *s)
{
	struct sr_session_priv *priv = s->priv;
	SoupMessage *message;
	gchar *params;
	sr_track_t *t;

	g_mutex_lock(priv->love_queue_mutex);
	t = g_queue_peek_head(priv->love_queue);
	g_mutex_unlock(priv->love_queue_mutex);

	if (!t)
		return;

	ws_params(s, &params,
			"method", "track.love",
			"api_key", priv->api_key,
			"sk", priv->session_key,
			"track", t->title,
			"artist", t->artist,
			NULL);

	message = soup_message_new("POST", priv->api_url);
	soup_message_set_request(message,
			"application/x-www-form-urlencoded",
			SOUP_MEMORY_TAKE,
			params,
			strlen(params));
	soup_session_queue_message(priv->soup,
			message,
			ws_love_cb,
			s);
}

void
sr_session_set_love(sr_session_t *s, int on)
{
	struct sr_session_priv *priv = s->priv;
	sr_track_t *c;

	c = priv->last_track;
	if (!c)
		return;
	c->rating = on ? 'L' : '\0';
}
