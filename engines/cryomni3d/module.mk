MODULE := engines/cryomni3d

MODULE_OBJS = \
	fonts/cryoextfont.o \
	fonts/cryofont.o \
	image/hlz.o \
	image/hnm.o \
	cryomni3d.o \
	datstream.o \
	dialogs_manager.o \
	fixed_image.o \
	font_manager.o \
	metaengine.o \
	mouse_boxes.o \
	objects.o \
	omni3d.o \
	sprites.o \
	wam_parser.o

ifdef ENABLE_VERSAILLES
MODULE_OBJS += \
	versailles/data.o \
	versailles/dialogs_manager.o \
	versailles/dialogs.o \
	versailles/documentation.o \
	versailles/engine.o \
	versailles/logic.o \
	versailles/menus.o \
	versailles/music.o \
	versailles/saveload.o \
	versailles/toolbar.o
endif

ifdef ENABLE_ATLANTIS
MODULE_OBJS += \
	atlantis/bigfile.o \
	atlantis/con_eval.o \
	atlantis/con_exec.o \
	atlantis/con_script.o \
	atlantis/dialog.o \
	atlantis/engine.o \
	atlantis/exetables.o \
	atlantis/inventory.o \
	atlantis/menu_layout.o \
	atlantis/menus.o \
	atlantis/music.o \
	atlantis/navigation.o \
	atlantis/npc_click.o \
	atlantis/ogre_rules.o \
	atlantis/omni3d.o \
	atlantis/place.o \
	atlantis/puzzle_assets.o \
	atlantis/puzzle_eclipse.o \
	atlantis/puzzle_ogregame.o \
	atlantis/puzzle_pieces.o \
	atlantis/saveload.o \
	atlantis/saveload_format.o \
	atlantis/sprite_anim.o \
	atlantis/sprite_decode.o \
	atlantis/sprite_renderer.o \
	atlantis/subtitle_layout.o \
	atlantis/toolbar.o \
	atlantis/wam_parser.o \
	atlantis/warp_cursor.o
endif

# This module can be built as a plugin
ifeq ($(ENABLE_CRYOMNI3D), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
