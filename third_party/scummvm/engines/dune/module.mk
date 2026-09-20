MODULE := engines/dune

MODULE_OBJS := \
	cursor.o \
	debug.o \
	detection.o \
	dune.o \
	hnm.o \
	intro.o \
	metaengine.o \
	music.o \
	palace.o \
	panel.o \
	resource.o \
	room.o \
	scene.o \
	sky.o \
	sprite.o \
	sound.o

# This module can be built as a plugin.
ifeq ($(ENABLE_DUNE), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules.
include $(srcdir)/rules.mk

# Detection objects are linked into the global detection table.
DETECT_OBJS += $(MODULE)/detection.o
