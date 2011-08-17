/*
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the LGPLv2.1.
 */

#ifndef SCROBBLE_H
#define SCROBBLE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SR_LASTFM_URL "http://post.audioscrobbler.com/?hs=true"
#define SR_LIBREFM_URL "http://turtle.libre.fm/?hs=true"
#define SR_LASTFM_API_URL "http://ws.audioscrobbler.com/2.0/"
#define SR_LIBREFM_API_URL "http://alpha.libre.fm/2.0/"

typedef struct sr_track sr_track_t;

struct sr_track {
	char *artist;
	char *title;
	unsigned timestamp;
	char source;
	char rating;
	int length;
	char *album;
	int position;
	char *mbid;
};

typedef struct sr_session sr_session_t;

struct sr_session {
	void *priv;
	void *user_data;
	void (*error_cb) (sr_session_t *s, int fatal, const char *msg);
	void (*scrobble_cb) (sr_session_t *s);
	void (*session_key_cb) (sr_session_t *s, const char *session_key);
};

sr_session_t *sr_session_new(const char *url,
		const char *client_id,
		const char *client_ver);
void sr_session_free(sr_session_t *s);
void sr_session_set_cred(sr_session_t *s, char *user, char *password);
void sr_session_set_cred_hash(sr_session_t *s, char *user, char *hash_pwd);

void sr_session_add_track(sr_session_t *s, sr_track_t *t);
int sr_session_load_list(sr_session_t *s, const char *file);
int sr_session_store_list(sr_session_t *s, const char *file);
void sr_session_pause(sr_session_t *s);
void sr_session_test(sr_session_t *s);

sr_track_t *sr_track_new(void);
void sr_track_free(sr_track_t *t);
sr_track_t *sr_track_dup(sr_track_t *in);

void sr_session_handshake(sr_session_t *s);
void sr_session_submit(sr_session_t *s);
void sr_session_set_proxy(sr_session_t *s, const char *url);

void sr_session_set_api(sr_session_t *s,
		const char *api_url,
		const char *api_key,
		const char *api_secret);
void sr_session_set_session_key(sr_session_t *s, const char *session_key);
void sr_session_set_love(sr_session_t *s, int on);
void sr_session_love(sr_session_t *s, const char *artist, const char *title, int on);

#ifdef __cplusplus
}
#endif

#endif /* SCROBBLE_H */
