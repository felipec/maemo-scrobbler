CC := gcc

EXTRA_WARNINGS = -Wextra -Wno-unused-parameter

CFLAGS := -ggdb -Wall -std=c99 $(EXTRA_WARNINGS)

override CFLAGS += -D_XOPEN_SOURCE=500

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

SOUP_CFLAGS := $(shell pkg-config --cflags libsoup-2.4)
SOUP_LIBS := $(shell pkg-config --libs libsoup-2.4)

GTHREAD_CFLAGS := $(shell pkg-config --cflags gthread-2.0)
GTHREAD_LIBS := $(shell pkg-config --libs gthread-2.0)

MAFW_CFLAGS := $(shell pkg-config --cflags mafw-shared mafw)
MAFW_LIBS := $(shell pkg-config --libs mafw-shared mafw)

SCROBBLE_LIBS := $(SOUP_LIBS)

all:

libscrobble.a: scrobble.o
libscrobble.a: CFLAGS := $(CFLAGS) $(GLIB_CFLAGS) $(SOUP_CFLAGS)

mafw-scrobbler: main.o libscrobble.a
mafw-scrobbler: CFLAGS := $(CFLAGS) $(GLIB_CFLAGS) $(GTHREAD_CFLAGS) $(MAFW_CFLAGS)
mafw-scrobbler: LIBS := $(LIBS) $(GLIB_LIBS) $(GTHREAD_LIBS) $(MAFW_LIBS) $(SCROBBLE_LIBS)
binaries += mafw-scrobbler

all: libscrobble.a $(binaries)

# pretty print
V = @
Q = $(V:y=)
QUIET_CC    = $(Q:@=@echo '   CC         '$@;)
QUIET_LINK  = $(Q:@=@echo '   LINK       '$@;)
QUIET_CLEAN = $(Q:@=@echo '   CLEAN      '$@;)

%.a::
	$(QUIET_LINK)$(AR) rcs $@ $^

$(binaries):
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

clean:
	$(QUIET_CLEAN)$(RM) *.o *.d *.a $(binaries)

-include *.d
