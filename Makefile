CC := gcc

EXTRA_WARNINGS = -Wextra -Wno-unused-parameter

CFLAGS := -ggdb -Wall -std=c99 $(EXTRA_WARNINGS)

override CFLAGS += -D_XOPEN_SOURCE=500

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

SOUP_CFLAGS := $(shell pkg-config --cflags libsoup-2.4)
SOUP_LIBS := -lsoup-2.4 -lgio-2.0 -lgobject-2.0 -lglib-2.0

GTHREAD_CFLAGS := $(shell pkg-config --cflags gthread-2.0)
GTHREAD_LIBS := -lgthread-2.0 -lglib-2.0

MAFW_CFLAGS := $(shell pkg-config --cflags mafw-shared mafw)
MAFW_LIBS := -lmafw-shared -lmafw -lgobject-2.0 -lglib-2.0

HILDON_CFLAGS := $(shell pkg-config --cflags hildon-1 hildon-control-panel libosso)
HILDON_LIBS := -lhildon-1 -lgtk-x11-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

CONIC_CFLAGS := $(shell pkg-config --cflags conic)
CONIC_LIBS := -lconic

SCROBBLE_LIBS := $(SOUP_LIBS)

all:

libscrobble.a: scrobble.o
libscrobble.a: override CFLAGS += $(GLIB_CFLAGS) $(SOUP_CFLAGS)

scrobbler: main.o libscrobble.a
scrobbler: override CFLAGS += $(GLIB_CFLAGS) $(GTHREAD_CFLAGS) $(MAFW_CFLAGS) $(CONIC_CFLAGS)
scrobbler: override LIBS += $(GLIB_LIBS) $(GTHREAD_LIBS) $(MAFW_LIBS) $(CONIC_LIBS) $(SCROBBLE_LIBS)
bins += scrobbler

libcp-scrobbler.so: control_panel.o
libcp-scrobbler.so: override CFLAGS += $(HILDON_CFLAGS)
libcp-scrobbler.so: override LIBS += $(HILDON_LIBS)
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
	install -m 755 scrobbler -D $(D)/usr/bin/scrobbler
	install -m 644 libcp-scrobbler.so -D \
		$(D)/usr/lib/hildon-control-panel/libcp-scrobbler.so
	install -m 644 cp.desktop -D \
		$(D)/usr/share/applications/hildon-control-panel/scrobbler.desktop
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
