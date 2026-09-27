#!/usr/bin/make -f
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
#
# Contrastive Passive EQ: VST3 (and nothing else) through DPF. scripts/build.sh runs this; by hand: make -C src
# DPF lives in ../third_party/DPF at the commit pinned in scripts/build.sh.

NAME = ContrastivePassive

FILES_DSP = \
	PluginContrastivePassive.cpp

DPF_PATH = ../third_party/DPF
DPF_TARGET_DIR = ../build/bin
DPF_BUILD_DIR = ../build/obj

# the DSP needs IEEE arithmetic (the eigenvalue iteration tests against machine epsilon) and C++17
CXXFLAGS += -std=gnu++17 -fno-fast-math -I.

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
