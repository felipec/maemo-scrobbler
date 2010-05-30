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

HILDON_CFLAGS := $(shell pkg-config --cflags hildon-1 hildon-control-panel)
HILDON_LIBS := $(shell pkg-config --libs hildon-1 hildon-control-panel)

OSSO_CFLAGS := $(shell pkg-config --cflags libosso)
OSSO_LIBS := $(shell pkg-config --libs libosso)

SCROBBLE_LIBS := $(SOUP_LIBS)

all:

libscrobble.a: scrobble.o
libscrobble.a: override CFLAGS += $(GLIB_CFLAGS) $(SOUP_CFLAGS)

mafw-scrobbler: main.o libscrobble.a
mafw-scrobbler: override CFLAGS += $(GLIB_CFLAGS) $(GTHREAD_CFLAGS) $(MAFW_CFLAGS)
mafw-scrobbler: override LIBS += $(GLIB_LIBS) $(GTHREAD_LIBS) $(MAFW_LIBS) $(SCROBBLE_LIBS)
bins += mafw-scrobbler

libcp-scrobbler.so: control_panel.o
libcp-scrobbler.so: override CFLAGS += $(HILDON_CFLAGS) $(OSSO_CFLAGS)
libcp-scrobbler.so: override LIBS += $(HILDON_LIBS) $(OSSO_LIBS)
libs += libcp-scrobbler.so

all: libscrobble.a $(bins) $(libs)

D = $(DESTDIR)

# pretty print
ifndef V
QUIET_CC    = @echo '   CC         '$@;
QUIET_LINK  = @echo '   LINK       '$@;
QUIET_CLEAN = @echo '   CLEAN      '$@;
endif

install: $(bins) $(libs)
	install -m 755 mafw-scrobbler -D $(D)/usr/bin/mafw-scrobbler
	install -m 644 libcp-scrobbler.so -D \
		$(D)/usr/lib/hildon-control-panel/libcp-scrobbler.so
	install -m 644 mafw-scrobbler.desktop -D \
		$(D)/usr/share/applications/hildon-control-panel/mafw-scrobbler.desktop
	install -m 644 fm.png -D $(D)/usr/share/icons/hicolor/48x48/apps/fm.png

%.a::
	$(QUIET_LINK)$(AR) rcs $@ $^

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

$(bins):
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LIBS) -o $@ $^

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

clean:
	$(QUIET_CLEAN)$(RM) *.o *.d *.a $(bins) $(libs)

-include *.d
