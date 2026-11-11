ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set)
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPPC)/base_tools

WUT_ROOT := $(DEVKITPRO)/wut
TARGET  := rtl8153_loader_capability_probe
BUILD   := build
SOURCES := .
MACHDEP := -DESPRESSO -mcpu=750 -meabi -mhard-float
RPXSPECS := -specs=$(WUT_ROOT)/share/wut.specs
CFLAGS := -O2 -Wall -Wextra -Werror $(MACHDEP) -I$(WUT_ROOT)/include
LDFLAGS := $(MACHDEP) $(RPXSPECS) -Wl,-Map,$(notdir $*.map)
LIBS := -lwut

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(CURDIR)
export OFILES := main.o
export INCLUDE := -I$(CURDIR) -I$(WUT_ROOT)/include
export LIBPATHS := -L$(WUT_ROOT)/lib
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -rf $(BUILD) $(TARGET).rpx $(TARGET).elf $(TARGET).map
else
$(OUTPUT).rpx: $(OUTPUT).elf
	@elf2rpl $< $@
$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(OFILES:.o=.d)
endif
