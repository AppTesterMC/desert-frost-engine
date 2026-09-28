MODULE := engines/dune

MODULE_OBJS := \
	amiga.o \
	amiga_gfx.o \
	attack.o \
	book.o \
	cursor.o \
	cockpit.o \
	debug.o \
	desert.o \
	detection.o \
	dialogue.o \
	dune.o \
	harness.o \
	hnm.o \
	intro.o \
	intro_first.o \
	map.o \
	intro_scenes.o \
	metaengine.o \
	music.o \
	palace.o \
	panel.o \
	resource.o \
	room.o \
	saves.o \
	segacd_game.o \
	segacd_gfx.o \
	segacd_panel.o \
	segacd_resources.o \
	segacd_world.o \
	scene.o \
	sky.o \
	sprite.o \
	sound.o \
	story.o \
	story_scene.o \
	ecology.o \
	battle.o \
	troops.o \
	speedrun.o \
	text.o \
	world.o

# This module can be built as a plugin.
ifeq ($(ENABLE_DUNE), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules.
include $(srcdir)/rules.mk

# Detection objects are linked into the global detection table.
DETECT_OBJS += $(MODULE)/detection.o
