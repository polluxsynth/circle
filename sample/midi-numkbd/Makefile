#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= main.o kernel.o

LIBS	= $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

-include $(DEPS)

#
# Host-side unit test (native build, not a Circle target build).
# Run with:  make test
#
# Uses HOSTCXX instead of CXX/CC because Rules.mk points those at the
# cross compiler. This rule comes after the include so that it does not
# become the default goal.
#
HOSTCXX      ?= g++
HOSTCXXFLAGS ?= -std=c++17 -Wall -fsanitize=address,undefined

settingsmode_test: settingsmode_test.cpp settingsmode.h blinkseq.h rawkeys.h
	$(HOSTCXX) $(HOSTCXXFLAGS) $< -o $@

settingsstore_test: settingsstore_test.cpp settingsstore.h
	$(HOSTCXX) $(HOSTCXXFLAGS) $< -o $@

progselect_test: progselect_test.cpp progselect.h rawkeys.h
	$(HOSTCXX) $(HOSTCXXFLAGS) $< -o $@

keyreports_test: keyreports_test.cpp keyreports.h rawkeys.h
	$(HOSTCXX) $(HOSTCXXFLAGS) $< -o $@

blinkseq_test: blinkseq_test.cpp blinkseq.h
	$(HOSTCXX) $(HOSTCXXFLAGS) $< -o $@

# Host tool to show/set/erase the stored settings on an SD card or image;
# shares settingsstore.h with the firmware.
nkstore: nkstore.cpp settingsstore.h
	$(HOSTCXX) -std=c++17 -Wall -O2 $< -o $@

.PHONY: test testclean
test: settingsmode_test settingsstore_test progselect_test keyreports_test blinkseq_test
	./settingsmode_test
	./settingsstore_test
	./progselect_test
	./keyreports_test
	./blinkseq_test

testclean:
	rm -f settingsmode_test settingsstore_test progselect_test keyreports_test blinkseq_test nkstore
