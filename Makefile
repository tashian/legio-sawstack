# Project Name
TARGET = sawstack

# Sources — extended as new modules are added.
CPP_SOURCES = src/main.cpp

# Pull in float-printf so PrintLine("%f") emits floats (newlib-nano strips it).
LDFLAGS += -u _printf_float

# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR  = lib/DaisySP

# Project source includes
C_INCLUDES += -Isrc

# Use Daisy's stock build system
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
