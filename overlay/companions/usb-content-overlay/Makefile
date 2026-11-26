# USB content status overlay, packaged as an Ultrahand/Tesla .ovl.

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment")
endif

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOPDIR ?= $(PROJECT_ROOT)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := usb-content-status
APP_TITLE   := Nexus USB Content
APP_VERSION := 1.3.3
APP_AUTHOR  := TR4U
BUILD       := build
SOURCES     := source
NO_ICON     := 1
IS_STATUS_MONITOR_DIRECTIVE := 1

INCLUDES := $(PROJECT_ROOT)/include

ULTRAHAND_ROOT ?= $(PROJECT_ROOT)/build-deps/libultrahand
include $(ULTRAHAND_ROOT)/ultrahand.mk

ARCH    := -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -Wall -Wno-address-of-packed-member -O2 -ffunction-sections -fdata-sections \
           $(ARCH) $(DEFINES) $(INCLUDE) -D__SWITCH__ -DIS_STATUS_MONITOR_DIRECTIVE=$(IS_STATUS_MONITOR_DIRECTIVE) \
           -DAPP_VERSION="\"$(APP_VERSION)\""
CXXFLAGS := $(CFLAGS) -std=c++23 -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -fno-exceptions -fno-rtti -flto -fuse-linker-plugin
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) \
            -Wl,--gc-sections -Wl,--as-needed -flto
LIBS    := -lnx

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR  := $(PROJECT_ROOT)
export VPATH   := $(foreach dir,$(SOURCES),$(if $(filter /%,$(dir)),$(dir),$(PROJECT_ROOT)/$(dir)))
export DEPSDIR := $(CURDIR)/$(BUILD)

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES     := $(OFILES_SRC)
export LD         := $(CXX)
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(if $(filter /%,$(dir)),$(dir),$(PROJECT_ROOT)/$(dir))) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: all clean dist $(BUILD)

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $@ -f $(CURDIR)/Makefile
	@rm -rf out/
	@mkdir -p out/switch/.overlays/
	@cp -a $(CURDIR)/config out/
	@cp $(CURDIR)/$(TARGET).ovl out/switch/.overlays/$(TARGET).ovl

clean:
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nro $(TARGET).nacp $(TARGET).elf out/

dist: all
	@rm -f $(TARGET).zip
	@cd out && zip -r ../$(TARGET).zip ./*

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).ovl

$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@printf 'ULTR' >> $@

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
