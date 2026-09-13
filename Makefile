# The console build.
#
# Everything the build writes goes under build/, including the four files
# build.mak would otherwise drop in the checkout. PSP_EBOOT and PSP_EBOOT_SFO
# are ifndef-guarded there, so naming them here wins.
OUT = build/psp
GEN = build/gen
# At parse time: build.mak's link rule does not create it, and build/ is not
# in the checkout.
$(shell mkdir -p $(OUT) $(GEN))

# The glyph tables and the atlas, baked at parse time from assets/font.bin.
# Generated and gitignored: a font table committed as C is data wearing a
# .c extension.
#
# font_atlas.c rather than bin2o on the atlas: the same bytes then compile
# on both machines under the same symbol, and there is no build tool that
# only one of them has.
$(shell python3 tools/font_bake.py assets/font.bin $(GEN) >/dev/null)
TARGET = $(OUT)/pocketfin
PSP_EBOOT     = $(OUT)/EBOOT.PBP
PSP_EBOOT_SFO = $(OUT)/PARAM.SFO

# src/sources.txt is the whole of what gets compiled; the build links nothing
# else. See README.md.
SOURCES := src/sources.txt

section = $(shell awk -v s=$(1) '/^\[/ { i = ($$0 == "["s"]") } \
                  !/^\[/ && i && NF && $$0 !~ /^#/ { print }' $(SOURCES))

# Both extensions: the console has one .S.
objs = $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(call section,$(1))))

$(shell python3 tools/register_gen.py $(SOURCES) $(GEN) >/dev/null)

# The layers, bottom up. A layer may include only from ones before it.
LAYERS = base io jelly model view page app

CORE_OBJS   = $(foreach L,$(LAYERS),$(call objs,$(L))) $(call objs,generated)
DEV_OBJS    = $(call objs,tools)
TEST_OBJS   = $(GEN)/register.o $(call objs,tests)
PSP_OBJS    = $(call objs,psp)

OBJS = $(CORE_OBJS) $(DEV_OBJS) $(TEST_OBJS) $(PSP_OBJS)

ifeq ($(strip $(OBJS)),)
$(error $(SOURCES) named nothing -- either it is missing or every section is \
empty. Its header says what goes in it)
endif

# The dependency direction, at parse time so it fails before anything
# compiles.
# Straight to stderr: captured through $(info) the newlines collapse and the
# list becomes one unreadable line.
LAYERS_RC := $(shell sh tools/layers.sh >&2; echo $$?)
ifneq ($(LAYERS_RC),0)
$(error the layers refuse this tree -- see above)
endif

# An element may not reach up into the page: one that could see the chrome
# would grow an opinion about the margin, which is the fault the rebuild is
# for. Not something the layer order can say -- both are in view/.
REACH := $(shell grep -lE 'include[ 	]*"view/page_chrome\.h"' \
                 $(wildcard src/view/element*.c) \
                 $(wildcard src/view/element*.h) 2>/dev/null)
ifneq ($(REACH),)
$(error an element may not include page_chrome.h -- see element.h: $(REACH))
endif

# ONE root. Every include names its layer, so what a file reaches into is
# readable at the include site.
INCDIR = src tests

# -MMD writes the header dependencies the -include at the bottom reads.
# Never delete the .d files: without them a changed struct leaves stale
# objects indexing the old layout.
#
# -MP GOES WITH IT. A .d names every header its object included, and make
# treats each as a file it must be able to build -- so RENAMING OR DELETING a
# header breaks the build with "No rule to make target", naming a path that no
# longer exists, until somebody knows to delete the .d files by hand. -MP adds
# a phony target for each header, which makes a vanished one a no-op instead.
# POCKETFIN_CHECKS compiles in the test seams: the card fault in
# src/base/log.h and the fakes in tools/seams.allow.
CFLAGS  = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -MMD -MP -DPOCKETFIN_CHECKS
ASFLAGS = $(CFLAGS)

# pspdisplay and pspge are already linked by build.mak; naming them again gives
# duplicate import stubs. psp-fixup-imports warns "stubs out of order" in every
# ordering tried and the module loads and runs, so psp.sh excepts that one
# sentence rather than turning the warning gate off.
LIBS = -lpsppower -lpspjpeg -lpspaudio -lpspaudiocodec -lpsputility \
       -lpspnet -lpspnet_apctl -lpspnet_inet -lpspnet_resolver \
       -lpspwlan -lpspgu

# PSPLINK loads relocatable modules only; a static ELF is refused with
# 0x80020148.
BUILD_PRX = 1

EXTRA_TARGETS   = $(PSP_EBOOT)
PSP_EBOOT_TITLE = Pocketfin
# The fin, 144x80. tools/icon.py draws it; it is not run by the build.
PSP_EBOOT_ICON  = assets/ICON0.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# The icon is packed into the EBOOT, but build.mak repacks only when the
# module changes: a new icon on its own left the old one in the EBOOT.
$(PSP_EBOOT): $(PSP_EBOOT_ICON)

-include $(OBJS:.o=.d)

# ---- what gets downloaded ----
#
# PSP/GAME/pocketfin as it sits on the Memory Stick, with one file in it to
# edit, and the same folder zipped as the release -- named for the version in
# src/base/version.h, so the build tested on the console is the one published.
# The connection file is written here rather than kept in the tree: a
# checked-in example is a file somebody copies WITHOUT reading, and the
# comments are the whole point of it.
DIST = build/dist/PSP/GAME/pocketfin
VERSION := $(shell awk -F'"' '/define POCKETFIN_VERSION/ { print $$2 }' src/base/version.h)
RELEASE = pocketfin-$(VERSION).zip

dist: $(PSP_EBOOT)
	@rm -rf build/dist
	@mkdir -p $(DIST)
	@cp $(PSP_EBOOT) $(DIST)/EBOOT.PBP
	@cp LICENSE $(DIST)/LICENSE
	@cp assets/LICENSE-Roboto $(DIST)/LICENSE-Roboto
	@printf '%s\n' \
	  '# Pocketfin -- your Jellyfin server. One setting per line.' \
	  '' \
	  '# Required. An IP address; names like jellyfin.local are not supported.' \
	  'host: 192.168.0.2' \
	  '' \
	  '# 8096 unless you changed it.' \
	  'port: 8096' \
	  '' \
	  '# Leave both out if your server has no sign-in.' \
	  'user: YOUR_USERNAME' \
	  'password: YOUR_PASSWORD' \
	  > $(DIST)/jellyfin.txt
	@{ echo report_playback 1; echo logging 0; echo bitrate_kbps 2800; echo cpu_mhz 222; } > $(DIST)/settings.txt
	@cd build/dist && python3 -m zipfile -c $(RELEASE) PSP
	@echo "build/dist/PSP is ready: copy it to the root of the Memory Stick"
	@echo "build/dist/$(RELEASE) is the release: the same folder, zipped"

.PHONY: dist
