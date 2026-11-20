# SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Modifications Copyright (c) 2026 <PROJECT_OWNER>
# Changes: derived from RT-CV src/Makefile; DeepStream and CUDA versions detected from the image
# instead of hard-coded; out-of-tree objects; strict warnings for Perceptra sources only; dropped
# frame_dump.cpp, -lnvds_batch_jpegenc, DS_VERSION_MAJOR/MINOR and the X11/json-glib-only bits.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Builds the `perceptra` binary INSIDE the DeepStream container: the deepstream-app and
# apps-common sources are compiled from the image (never vendored, see docs/LICENSING.md).

APP := perceptra
BUILD_DIR ?= build

DS_ROOT ?= /opt/nvidia/deepstream/deepstream
DS_APP_DIR ?= $(DS_ROOT)/sources/apps/sample_apps/deepstream-app
DS_COMMON_DIR ?= $(DS_ROOT)/sources/apps/apps-common
DS_INCLUDES ?= $(DS_ROOT)/sources/includes
# "Version: 9.1.0" -> 9.1
NVDS_VERSION ?= $(shell sed -n 's/^Version: *\([0-9]*\.[0-9]*\).*/\1/p' $(DS_ROOT)/version 2>/dev/null)
# Newest /usr/local/cuda-X.Y in the image.
CUDA_VER ?= $(shell ls -d /usr/local/cuda-*.* 2>/dev/null | sed 's|.*/cuda-||' | sort -V | tail -1)
LIB_INSTALL_DIR ?= /opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/

TARGET_DEVICE := $(shell gcc -dumpmachine | cut -f1 -d -)
PLATFORM_FLAGS :=
ifeq ($(TARGET_DEVICE),aarch64)
  PLATFORM_FLAGS += -DPLATFORM_TEGRA
endif

PKGS := gstreamer-1.0 gstreamer-video-1.0 x11 json-glib-1.0 gio-2.0 rdkafka

# Perceptra sources (strict warnings).
APP_SRCS := main.c app_config.c probe.c perf.c log.c ds_compat.c event_model.c \
            event_serialize.c publisher.c publisher_stream.c publisher_kafka.c \
            task_registry.c task_detect.c
# DeepStream sources from the image (compiled with their own, relaxed flags).
DS_C_SRCS := $(DS_APP_DIR)/deepstream_app.c $(DS_APP_DIR)/deepstream_app_config_parser.c \
             $(wildcard $(DS_COMMON_DIR)/src/*.c)
DS_CXX_SRCS := $(DS_APP_DIR)/deepstream_app_config_parser_yaml.cpp \
               $(wildcard $(DS_COMMON_DIR)/src/deepstream-yaml/*.cpp)

# DeepStream headers are treated as system headers so -Werror applies to our code only.
# Recursive (=) so pkg-config only runs for targets that need DeepStream/GStreamer.
COMMON_FLAGS = $(PLATFORM_FLAGS) -isystem $(DS_APP_DIR) -isystem $(DS_COMMON_DIR)/includes \
                -isystem $(DS_INCLUDES) -isystem /usr/local/cuda-$(CUDA_VER)/include \
                $(shell pkg-config --cflags $(PKGS))
APP_CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Werror $(COMMON_FLAGS)
DS_CFLAGS = -O2 -g -w $(COMMON_FLAGS)
DS_CXXFLAGS = -std=c++17 -O2 -g -w $(COMMON_FLAGS)

LIBS = -L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart \
        -L$(LIB_INSTALL_DIR) -lnvdsgst_meta -lnvds_meta -lnvdsgst_helper -lnvdsgst_customhelper \
        -lnvdsgst_smartrecord -lnvds_utils -lnvds_msgbroker -lnvds_logger -lnvbufsurface \
        -lyaml-cpp -lgstrtspserver-1.0 -lm -ldl -lpthread -Wl,-rpath,$(LIB_INSTALL_DIR) \
        $(shell pkg-config --libs $(PKGS))

APP_OBJS := $(APP_SRCS:%.c=$(BUILD_DIR)/app/%.o)
DS_OBJS := $(DS_C_SRCS:$(DS_ROOT)/%.c=$(BUILD_DIR)/ds/%.o) \
           $(DS_CXX_SRCS:$(DS_ROOT)/%.cpp=$(BUILD_DIR)/ds/%.o)

.PHONY: all clean info check-ds test test-serialize test-publisher update-golden

all: check-ds $(APP)

check-ds:
	@test -n "$(NVDS_VERSION)" || { echo "DeepStream not found under $(DS_ROOT): build inside" \
	  "the DeepStream container (test-serialize needs only GLib)" >&2; exit 1; }
	@test -n "$(CUDA_VER)" || { echo "No /usr/local/cuda-X.Y found" >&2; exit 1; }

# ---- serializer golden test: GLib only, no DeepStream/GPU (CI) ----------------------------
GOLDEN_EVENT ?= ../../tests/golden/event_basic.json
SERIALIZE_SRCS := tests/test_serialize.c event_serialize.c event_model.c
SANITIZE ?= -fsanitize=address,undefined -fno-sanitize-recover=all
GLIB_FLAGS = $(shell pkg-config --cflags --libs glib-2.0)

$(BUILD_DIR)/test_serialize: $(SERIALIZE_SRCS) event_model.h event_serialize.h Makefile
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror $(SANITIZE) -I. -o $@ $(SERIALIZE_SRCS) \
	  $(GLIB_FLAGS) -lm

# The DeepStream image sets LD_PRELOAD, which ASan refuses to run behind.
test-serialize: $(BUILD_DIR)/test_serialize
	env -u LD_PRELOAD ./$(BUILD_DIR)/test_serialize $(GOLDEN_EVENT)

PUBLISHER_TEST_SRCS := tests/test_publisher.c publisher.c publisher_stream.c event_serialize.c \
                       event_model.c log.c
GIO_FLAGS = $(shell pkg-config --cflags --libs gio-2.0)

$(BUILD_DIR)/test_publisher: $(PUBLISHER_TEST_SRCS) publisher.h event_model.h log.h Makefile
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror $(SANITIZE) -I. -o $@ $(PUBLISHER_TEST_SRCS) \
	  $(GIO_FLAGS) -lm

test-publisher: $(BUILD_DIR)/test_publisher
	env -u LD_PRELOAD ./$(BUILD_DIR)/test_publisher

# All GPU-free unit tests of the app.
test: test-serialize test-publisher

update-golden: $(BUILD_DIR)/test_serialize
	env -u LD_PRELOAD ./$(BUILD_DIR)/test_serialize --write $(GOLDEN_EVENT)

info:
	@echo "NVDS_VERSION=$(NVDS_VERSION) CUDA_VER=$(CUDA_VER) TARGET_DEVICE=$(TARGET_DEVICE)"

$(BUILD_DIR)/app/%.o: %.c $(wildcard *.h) Makefile
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $(APP_CFLAGS) $<

$(BUILD_DIR)/ds/%.o: $(DS_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $(DS_CFLAGS) $<

$(BUILD_DIR)/ds/%.o: $(DS_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c -o $@ $(DS_CXXFLAGS) $<

$(APP): $(APP_OBJS) $(DS_OBJS)
	$(CXX) -o $@ $^ $(LIBS)

clean:
	rm -rf $(BUILD_DIR) $(APP)
