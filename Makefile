#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
APP_TITLE       :=	WifiManager
APP_DESCRIPTION :=	Modern backup, restore & QR import for your Wi-Fi slots
APP_AUTHOR      :=	LiquidFenrir, tinaut1986

TARGET          :=	$(subst $e ,_,$(notdir $(APP_TITLE)))
BUILD           :=	build
SOURCES         :=	source source/quirc source/qrcodegen
DATA            :=	data
INCLUDES        :=	source
OUTDIR          :=	out

ICON            :=	meta/icon.png
BANNER_IMAGE    :=	meta/banner.png
BANNER_AUDIO    :=	meta/audio.wav
RSF_PATH        :=	meta/app.rsf
UNIQUE_ID       :=	0x05DC9
PRODUCT_CODE    :=	CTR-P-WIFI
ICON_FLAGS      :=	nosavebackups,visible

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DTITLE=\"$(APP_TITLE)\"

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

.PHONY: all clean cia

MAKEROM     ?= makerom
BANNERTOOL  ?= bannertool
HAVE_CIA    := $(shell command -v $(MAKEROM) >/dev/null 2>&1 && command -v $(BANNERTOOL) >/dev/null 2>&1 && echo yes)

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
	@$(BANNERTOOL) makebanner -i "$(BANNER_IMAGE)" -a "$(BANNER_AUDIO)" -o "$(BUILD)/banner.bnr"
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i "$(APP_ICON)" -f "$(ICON_FLAGS)" -o "$(BUILD)/icon.icn"
	@$(MAKEROM) -f cia -o "$@" -target t -exefslogo -elf "$(OUTPUT).elf" -rsf "$(RSF_PATH)" \
		-banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)"
	@echo "built ... $@"

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
