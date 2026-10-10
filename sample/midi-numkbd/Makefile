#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= main.o kernel.o

LIBS	= $(CIRCLEHOME)/lib/usb/libusb.a \
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

.PHONY: test testclean
test: settingsmode_test
	./settingsmode_test

testclean:
	rm -f settingsmode_test
