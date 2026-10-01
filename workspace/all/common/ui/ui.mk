# Shared UI component sources (one component per file).
#
# Usage from an app makefile (all apps live in workspace/all/<app>):
#   include ../common/ui/ui.mk
#   SOURCE += $(UI_COMPONENT_SRCS)
#
# Adds the ui include dir to CFLAGS so "ui_*.h" resolve everywhere.
# UI_DIR may be overridden before including if the app lives elsewhere.
UI_DIR ?= ../common/ui

UI_INCDIR = -I$(UI_DIR)
CFLAGS += $(UI_INCDIR)

UI_COMPONENT_SRCS = \
	$(UI_DIR)/ui_draw.c \
	$(UI_DIR)/ui_fonts.c \
	$(UI_DIR)/ui_font.c \
	$(UI_DIR)/ui_icons.c \
	$(UI_DIR)/ui_image.c \
	$(UI_DIR)/ui_message.c \
	$(UI_DIR)/ui_confirmdialog.c \
	$(UI_DIR)/ui_quitrequest.c \
	$(UI_DIR)/ui_controlshelp.c \
	$(UI_DIR)/ui_menubar.c \
	$(UI_DIR)/ui_buttonhintbar.c \
	$(UI_DIR)/ui_loadingoverlay.c \
	$(UI_DIR)/ui_splash.c \
	$(UI_DIR)/ui_downloadprogress.c \
	$(UI_DIR)/ui_emptystate.c \
	$(UI_DIR)/ui_pindialog.c \
	$(UI_DIR)/ui_ease.c \
	$(UI_DIR)/ui_fade.c

# ListView widget (depends on ui_list.c - apps that take this must also
# compile $(UI_DIR)/ui_list.c, which every list-drawing app already does).
UI_LISTVIEW_SRCS = $(UI_DIR)/ui_listview.c
