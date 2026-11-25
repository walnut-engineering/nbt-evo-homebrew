# evo_padd - Xbox wireless controller support for the homebrew launcher: a QNX/C port
# of xow that drives an Xbox One Wireless Adapter over raw libusbdi (the unit has no
# USB HID or Wi-Fi driver). GPL-2.0-or-later. See README.md and ../../shared/app.mk.
#
#   make            -> ../../bin/evo_padd      (the daemon)
#   make probe      -> ../../bin/evo_usbprobe  (USB descriptor / chip-check tool)
#   make fake       -> ../../bin/evo_padfake   (pretend gamepad, for scripted app tests)
APP        ?= evo_padd
ifeq ($(APP),evo_usbprobe)
SRCS       := evo_usbprobe.c
else ifeq ($(APP),evo_padfake)
SRCS       := evo_padfake.c
else
SRCS       := evo_padd.c usbq.c mt76.c dongle.c gip.c padout.c
endif
GRAPHICS   := 0
OPT        := -O2
EXTRA_LIBS := -l:libusbdi.so.2
include ../../shared/app.mk

.PHONY: probe fake
probe:
	$(MAKE) APP=evo_usbprobe
fake:
	$(MAKE) APP=evo_padfake
