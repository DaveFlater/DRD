# Dynamic Range Destroyer (DRD)
# © 2025 David Flater
# GPLv3

# Prefix under which the LV2 include files are found
LV2PREFIX ?= /usr
# Prefix to install the plugin to
DRDPREFIX ?= $(LV2PREFIX)

.EXTRA_PREREQS = Makefile

DRD.so: DRD.c
	gcc -I$(LV2PREFIX)/include -O2 -fPIC -fvisibility=hidden -shared -lm -o DRD.so DRD.c

install: DRD.so manifest.ttl DRD.ttl
	install -d $(DRDPREFIX)/lib/lv2/DRD.lv2
	install DRD.so $(DRDPREFIX)/lib/lv2/DRD.lv2
	install -m 644 manifest.ttl DRD.ttl $(DRDPREFIX)/lib/lv2/DRD.lv2

README.html: README.md
	python3 -m markdown -x fenced_code README.md > README.html

clean:
	rm -f DRD.so README.html
