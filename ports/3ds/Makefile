#---------------------------------------------------------------------------------
# Name:        Makefile
# Description: SegaSonic The Hedgehog for the Nintendo 3DS (devkitARM + libctru)
# Usage:       cd ports/3ds && make          -> segasonic.3dsx (copy it to sdmc:/3ds/)
#              The game's C (src/) and its board (runtime/) are compiled in as they are. The game's data
#              is not in the repository: the first run on the console makes it from the ROM set (main.c).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

TARGET := segasonic
BUILD  := build
ROOT   := ../..

APP_TITLE       := SegaSonic The Hedgehog
APP_DESCRIPTION := Native port of the arcade game (bring your own ROM)
APP_AUTHOR      := segasonic-decomp

# The game's C, its board and this frontend. Not runtime/src/v60.c (the CPU of the translation scaffold the
# decompiler grew from). Objects keep their directories under build/: main.c, sound.c and video.c each
# exist twice (the ports', the runtime's and the game's).
SRCDIRS := $(ROOT)/ports/3ds $(ROOT)/runtime/src \
           $(ROOT)/src $(ROOT)/src/system $(ROOT)/src/video $(ROOT)/src/engine $(ROOT)/src/stage \
           $(ROOT)/src/player $(ROOT)/src/objects $(ROOT)/src/scenes $(ROOT)/src/sound
SRCS    := $(filter-out $(ROOT)/runtime/src/v60.c,$(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c)))
OBJS    := $(SRCS:$(ROOT)/%.c=$(BUILD)/%.o)

INCLUDES := $(ROOT)/runtime/include $(ROOT)/src $(ROOT)/src/sound
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# -fno-strict-aliasing: the game's variables are typed lvalues over the work RAM bytes (game/support.h)
CFLAGS  := -g -Wall -O2 -mword-relocations -fomit-frame-pointer -ffunction-sections -fno-strict-aliasing \
           $(ARCH) -D__3DS__ -std=gnu11 $(foreach dir,$(INCLUDES),-I$(dir)) -I$(CTRULIB)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map -L$(CTRULIB)/lib
LIBS    := -lctru -lm

.PHONY: all clean

all: $(TARGET).3dsx

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh

$(TARGET).elf: $(OBJS)
	@echo linking $@
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).map

-include $(OBJS:.o=.d)
