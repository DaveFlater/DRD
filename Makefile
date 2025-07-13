# Dynamic Range Destroyer (DRD)
# © 2025 David Flater
# GPLv3

# Applicable:
# https://lv2plug.in/pages/filesystem-hierarchy-standard.html

# Prefix under which the LV2 include files are found (not necessarily where
# plugin bundles are installed)
LV2PREFIX ?= /usr
# Directory into which the plugin bundle should be installed
# For a user install say DESTDIR=$HOME/.lv2
DESTDIR ?= /usr/local/lib/lv2

.EXTRA_PREREQS = Makefile

DRD.so: DRD.c
	gcc -I$(LV2PREFIX)/include -O2 -fPIC -fvisibility=hidden -shared -lm -o DRD.so DRD.c

install: DRD.so manifest.ttl DRD.ttl
	install -d $(DESTDIR)/DRD.lv2
	install DRD.so $(DESTDIR)/DRD.lv2
	install -m 644 manifest.ttl DRD.ttl $(DESTDIR)/DRD.lv2

README.html: README.md
	python3 -m markdown -x fenced_code README.md > README.html

clean:
	rm -f DRD.so README.html
