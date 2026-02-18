#
# Makefile
#

CIRCLEHOME = ../third_party/circle
	
APPHOME  = .
SRCDIR   = $(APPHOME)/src
INCDIR   = $(APPHOME)/include
BUILDDIR = $(APPHOME)/build
BINDIR   = $(APPHOME)/bin
TOOLCHAIN_PREFIX ?= /Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin/arm-none-eabi-
CHECK_TMPDIR ?= $(abspath ../.tmp)

# Prioritize local headers over Circle defaults
CPPFLAGS := -I$(INCDIR) $(CPPFLAGS)
CFLAGS   := -I$(INCDIR) $(CFLAGS)
EXTRAINCLUDE := -I$(INCDIR) $(EXTRAINCLUDE)

CPPFLAGS += -I$(CIRCLEHOME)/addon \
			-I$(CIRCLEHOME)/addon/wlan \
			-I$(CIRCLEHOME)/addon/wlan/hostap/src \
			-I$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant
CFLAGS   += -I$(CIRCLEHOME)/addon \
			-I$(CIRCLEHOME)/addon/wlan \
			-I$(CIRCLEHOME)/addon/wlan/hostap/src \
			-I$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant

# Place linker outputs in build so only the final image is copied to bin
TARGET   = $(BUILDDIR)/kernel

# OBJS := \
# 	$(BUILDDIR)/main.o \
# 	$(BUILDDIR)/kernel.o \
# 	$(BUILDDIR)/TFontConverter.o \
# 	$(BUILDDIR)/VT100_FontConverter.o \
# 	$(BUILDDIR)/TRenderer.o \
# 	$(BUILDDIR)/TConfig.o $(BUILDDIR)/TKeyboard.o $(BUILDDIR)/TUART.o 

OBJS := \
	$(BUILDDIR)/main.o \
	$(BUILDDIR)/kernel.o \
	$(BUILDDIR)/hal.o \
	$(BUILDDIR)/TFontConverter.o \
	$(BUILDDIR)/VT100_FontConverter.o \
	$(BUILDDIR)/TRenderer.o \
	$(BUILDDIR)/TConfig.o \
	$(BUILDDIR)/TKeyboard.o \
	$(BUILDDIR)/TUART.o \
	$(BUILDDIR)/TFileLog.o \
	$(BUILDDIR)/TWlanLog.o \
	$(BUILDDIR)/TSetup.o \
	$(BUILDDIR)/VTTest.o
	
EXTRACLEAN += $(BUILDDIR)/*


LIBS	= $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
	$(CIRCLEHOME)/addon/wlan/libwlan.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/net/libnet.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a:
	@if [ ! -d $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant ]; then \
		echo "Missing submodule: addon/wlan/hostap"; \
		echo "Run: (cd $(CIRCLEHOME) && git submodule update --init --recursive)"; \
		exit 1; \
	fi
	@$(MAKE) -C $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant -f Makefile.circle CHECK_DEPS=0

$(CIRCLEHOME)/addon/wlan/libwlan.a:
	@$(MAKE) -C $(CIRCLEHOME)/addon/wlan CHECK_DEPS=0

$(CIRCLEHOME)/lib/usb/libusb.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib/usb CHECK_DEPS=0

$(CIRCLEHOME)/addon/fatfs/libfatfs.a:
	@$(MAKE) -C $(CIRCLEHOME)/addon/fatfs CHECK_DEPS=0

$(CIRCLEHOME)/addon/SDCard/libsdcard.a:
	@$(MAKE) -C $(CIRCLEHOME)/addon/SDCard CHECK_DEPS=0

$(CIRCLEHOME)/lib/input/libinput.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib/input CHECK_DEPS=0

$(CIRCLEHOME)/lib/fs/libfs.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib/fs CHECK_DEPS=0

$(CIRCLEHOME)/lib/net/libnet.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib/net CHECK_DEPS=0

$(CIRCLEHOME)/lib/sched/libsched.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib/sched CHECK_DEPS=0

$(CIRCLEHOME)/lib/libcircle.a:
	@$(MAKE) -C $(CIRCLEHOME)/lib CHECK_DEPS=0

include $(CIRCLEHOME)/Rules.mk

.PHONY: all docs docs_clean toolchain-check toolchain-configure
.DEFAULT_GOAL := all
all: $(BINDIR)/kernel.img

toolchain-configure:
	@echo "  CONFIG $(CIRCLEHOME) with prefix $(TOOLCHAIN_PREFIX)"
	@cd $(CIRCLEHOME) && ./configure -f -p "$(TOOLCHAIN_PREFIX)"

toolchain-check:
	@echo "  TOOLCHAIN $(TOOLCHAIN_PREFIX)"
	@mkdir -p "$(CHECK_TMPDIR)"
	@$(TOOLCHAIN_PREFIX)gcc --version
	@printf '#include <stdint.h>\nint main(void){return 0;}\n' | TMPDIR="$(CHECK_TMPDIR)" $(TOOLCHAIN_PREFIX)gcc -x c - -c -o "$(CHECK_TMPDIR)/vt100_stdint_test.o"
	@echo "  OK    stdint test -> $(CHECK_TMPDIR)/vt100_stdint_test.o"
	@rm -f "$(CHECK_TMPDIR)/vt100_stdint_test.o"

kernel.img: $(BINDIR)/kernel.img
	@echo "  LINK  $@ -> $(BINDIR)/kernel.img"
	@ln -sf $(BINDIR)/kernel.img $@

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)


$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -c -o $@ $<


$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -MD -c -o $@ $<


$(BUILDDIR)/%.o: $(SRCDIR)/%.S | $(BUILDDIR)
	@echo "  AS    $@"
	@$(AS) $(AFLAGS) -c -o $@ $<

# $(BUILDDIR)/%.d: $(SRCDIR)/%.cpp | $(BUILDDIR)
# 	@$(CPP) $(CPPFLAGS) -M -MG -MT $(BUILDDIR)/$*.o -MT $@ -MF $@ $<
#
# $(BUILDDIR)/%.d: $(SRCDIR)/%.c | $(BUILDDIR)
# 	@$(CC) $(CFLAGS) -M -MG -MT $(BUILDDIR)/$*.o -MT $@ -MF $@ $<
#
# $(BUILDDIR)/%.d: $(SRCDIR)/%.S | $(BUILDDIR)
# 	@$(AS) $(AFLAGS) -M -MG -MT $(BUILDDIR)/$*.o -MT $@ -MF $@ $<

$(BINDIR)/kernel.img: $(TARGET).img | $(BINDIR)
	@echo "  COPY  $< -> $@"
	@cp $< $@

-include $(DEPS)

.PHONY: docs
docs:
	@echo "  DOXY  $(APPHOME)/Doxyfile"
	@cd $(APPHOME) && doxygen Doxyfile
	@cd $(APPHOME)/docs/doxygen && ln -sf html/index.html index.html

docs_clean:
	@echo "  RM    docs/doxygen/html docs/doxygen/latex docs/doxygen/index.html"
	@rm -rf $(APPHOME)/docs/doxygen/html $(APPHOME)/docs/doxygen/latex $(APPHOME)/docs/doxygen/index.html
