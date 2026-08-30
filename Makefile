#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Version string, derived from git (override with `make VERSION=...`):
#   * on an exact tag             -> the tag                 (e.g. v0.4.0)
#   * on a release/vX.Y.Z branch  -> X.Y.Z-dev.<rel>[.<dev>]+<hash>
#       <rel> = commits on the release branch since origin/main
#       <dev> = extra commits on top of the release branch tip (omitted if 0)
#   * anywhere else               -> `git describe --tags --always`
# A side branch based on a release branch inherits that release's version.
#---------------------------------------------------------------------------------
GIT_BRANCH  := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
EXACT_TAG   := $(shell git describe --tags --exact-match 2>/dev/null)
REL_MERGED  := $(shell git for-each-ref --format='%(refname:short)' --merged HEAD \
                 refs/heads/'release/*' refs/remotes/'origin/release/*' 2>/dev/null | sort -V | tail -1)
REL_BASE    := $(shell git rev-parse --verify -q refs/remotes/origin/main >/dev/null && echo refs/remotes/origin/main || echo main)

ifneq ($(strip $(EXACT_TAG)),)
  GIT_VERSION := $(EXACT_TAG)
else
ifeq ($(filter release/%,$(GIT_BRANCH)),)
  REL_NAME := $(subst origin/,,$(REL_MERGED))
else
  REL_NAME := $(GIT_BRANCH)
endif
ifneq ($(filter release/%,$(REL_NAME)),)
  REL_TARGET := $(shell git rev-parse --verify -q refs/heads/$(REL_NAME) >/dev/null && echo refs/heads/$(REL_NAME) || echo refs/remotes/origin/$(REL_NAME))
  REL_CNT    := $(shell git rev-list --count $(REL_BASE)..$(REL_TARGET) 2>/dev/null || echo 1)
  DEV_CNT    := $(shell git rev-list --count $(REL_TARGET)..HEAD 2>/dev/null || echo 0)
  REL_HASH   := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
  ifeq ($(DEV_CNT),0)
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT)+$(REL_HASH)
  else
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT).$(DEV_CNT)+$(REL_HASH)
  endif
else
  GIT_VERSION := $(shell git describe --tags --always 2>/dev/null || echo "0.0-dev")
endif
endif
VERSION ?= $(GIT_VERSION)

# makerom needs a numeric major.minor.micro. Extract it only from a proper
# leading vX.Y.Z (e.g. v0.1.0 or v0.1.0-dev.3+abc); anything else -> 0 0 0.
VER_TRIPLE  := $(shell sh $(CURDIR)/tools/mk/version-triple.sh '$(VERSION)')
VER_MAJOR   := $(word 1,$(VER_TRIPLE))
VER_MINOR   := $(word 2,$(VER_TRIPLE))
VER_MICRO   := $(word 3,$(VER_TRIPLE))

#---------------------------------------------------------------------------------
# Fork of LiquidFenrir's WifiManager, maintained by tinaut1986.
APP_TITLE       :=	WiFiManager+
APP_DESCRIPTION  =	$(shell printf '%s %s' "Backup, restore, edit & QR share for Wi-Fi slots" "$(VERSION)" | cut -c1-64)
APP_AUTHOR      :=	tinaut1986 (fork of LiquidFenrir)

TARGET          :=	WiFiManagerPlus
BUILD           :=	build
SOURCES         :=	source source/quirc source/qrcodegen
DATA            :=	data
INCLUDES        :=	source
OUTDIR          :=	out

ICON            :=	meta/icon.png
# Drop in meta/banner.cgfx (an authored layered/animated 3D banner) and it will
# be used automatically; otherwise the flat meta/banner.png is used.
BANNER_IMAGE    :=	$(if $(wildcard meta/banner.cgfx),meta/banner.cgfx,meta/banner.png)
BANNER_AUDIO    :=	meta/audio.wav
RSF_PATH        :=	meta/app.rsf
UNIQUE_ID       :=	0x05DCA
PRODUCT_CODE    :=	CTR-P-WFMP
ICON_FLAGS      :=	nosavebackups,visible

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DTITLE=\"$(APP_TITLE)\" -DAPP_VERSION=\"$(VERSION)\"

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++14

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else ifneq (,$(findstring icon.png,$(icons)))
		export APP_ICON := $(TOPDIR)/icon.png
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

export _3DSXDEPS	:=	$(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh

.PHONY: all clean cia ftp print-version

# Prefer the binaries vendored in tools/bin (used by CI); fall back to PATH.
MAKEROM     ?= $(if $(wildcard $(CURDIR)/tools/bin/makerom),$(CURDIR)/tools/bin/makerom,makerom)
BANNERTOOL  ?= $(if $(wildcard $(CURDIR)/tools/bin/bannertool),$(CURDIR)/tools/bin/bannertool,bannertool)
HAVE_CIA    := $(shell command -v $(MAKEROM) >/dev/null 2>&1 && command -v $(BANNERTOOL) >/dev/null 2>&1 && echo yes)

FTP_HOST    ?= 192.168.1.137
FTP_PORT    ?= 5000

all:
	@mkdir -p $(BUILD) $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
ifeq ($(HAVE_CIA),yes)
	@$(MAKE) --no-print-directory cia
else
	@echo "(skipping .cia: makerom/bannertool not found - .3dsx built in $(OUTDIR)/)"
endif

cia: $(OUTDIR)/$(TARGET).cia

$(OUTDIR)/$(TARGET).cia: $(OUTPUT).elf
	@$(BANNERTOOL) makebanner $(if $(filter %.cgfx,$(BANNER_IMAGE)),-ci,-i) "$(BANNER_IMAGE)" -a "$(BANNER_AUDIO)" -o "$(BUILD)/banner.bnr"
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(APP_ICON)" -f "$(ICON_FLAGS)" -o "$(BUILD)/icon.icn"
	@$(MAKEROM) -f cia -o "$@" -target t -exefslogo -elf "$(OUTPUT).elf" -rsf "$(RSF_PATH)" \
		-banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" \
		-major $(VER_MAJOR) -minor $(VER_MINOR) -micro $(VER_MICRO) \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)"
	@echo "built ... $@ (version $(VERSION))"

print-version:
	@echo $(VERSION)

# Push the freshly built artifacts to a 3DS running an FTP server (FTPD).
#   make ftp                       -> 192.168.1.137:5000
#   make ftp FTP_HOST=... FTP_PORT=...
ftp: all
	@echo "Uploading to ftp://$(FTP_HOST):$(FTP_PORT) ..."
	curl -T "$(OUTDIR)/$(TARGET).cia"  "ftp://$(FTP_HOST):$(FTP_PORT)/cias/$(TARGET)-$(VERSION).cia"
	curl --ftp-create-dirs -T "$(OUTDIR)/$(TARGET).3dsx" "ftp://$(FTP_HOST):$(FTP_PORT)/3ds/$(TARGET)/$(TARGET).3dsx"
	curl -T "$(OUTDIR)/$(TARGET).smdh" "ftp://$(FTP_HOST):$(FTP_PORT)/3ds/$(TARGET)/$(TARGET).smdh"

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTDIR)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf	:	$(OFILES)
$(OFILES_SRC)	:	$(HFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
