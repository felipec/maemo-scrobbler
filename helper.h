/*
 * Copyright (C) 2010 Felipe Contreras
 *
 * This code is licenced under the LGPLv2.1.
 */

#ifndef HELPER_H
#define HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "scrobble.h"
#include <stdbool.h>

void hp_init(void);
void hp_deinit(void);
void hp_submit(void);
void hp_love_current(bool on);
void hp_love(const char *artist, const char *title, bool on);
void hp_stop(void);
void hp_next(void);

void hp_set_artist(const char *value);
void hp_set_title(const char *value);
void hp_set_length(int value);
void hp_set_album(const char *value);
void hp_set_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif /* HELPER_H */
