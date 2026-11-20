# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# libperceptra_parsers.so - output-family decoders for nvinfer (docs/PARSERS.md).
#
# Targets:
#   test-core  core decoders + unit tests; plain C++17, NO DeepStream needed (CI)
#   all        libperceptra_parsers.so (needs DeepStream + TensorRT headers: build in the image)
#   test       test-core + adapter smoke test (needs the same headers)
#   clean

CXX ?= g++
BUILD_DIR ?= build
LIB := libperceptra_parsers.so

DS_ROOT ?= /opt/nvidia/deepstream/deepstream
DS_INCLUDES ?= $(DS_ROOT)/sources/includes
CUDA_INCLUDES ?= $(lastword $(shell ls -d /usr/local/cuda-*.*/include 2>/dev/null | sort -V))

WARN := -Wall -Wextra -Werror
OPT ?= -O2 -g
BASE_FLAGS := -std=c++17 $(OPT) $(WARN)
# The adapter includes DeepStream/TensorRT headers as system headers.
DS_FLAGS := -isystem $(DS_INCLUDES) $(if $(CUDA_INCLUDES),-isystem $(CUDA_INCLUDES))
SANITIZE ?= -fsanitize=address,undefined -fno-sanitize-recover=all

CORE_SRCS := $(wildcard core/*.cpp)
CORE_HDRS := $(wildcard core/*.hpp)
CORE_TEST_SRCS := tests/test_main.cpp tests/test_yolo.cpp tests/test_detr_nms.cpp \
                  tests/test_common.cpp
TEST_HDRS := $(wildcard tests/*.hpp)

.PHONY: all test test-core test-adapter clean check-ds

all: $(LIB)

check-ds:
	@test -f $(DS_INCLUDES)/nvdsinfer_custom_impl.h || { \
	  echo "DeepStream headers not found in $(DS_INCLUDES): build '$(LIB)' inside the" \
	       "DeepStream image, or run 'make test-core' (no DeepStream needed)." >&2; exit 1; }

# Shared library: hidden visibility; only the NvDsInferParsePerceptra_* symbols are exported.
$(LIB): $(CORE_SRCS) $(CORE_HDRS) ds_adapter.cpp | check-ds
	$(CXX) $(BASE_FLAGS) $(DS_FLAGS) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
	  -shared -Wl,--no-undefined -o $@ ds_adapter.cpp $(CORE_SRCS)

$(BUILD_DIR)/parsers_core_test: $(CORE_SRCS) $(CORE_HDRS) $(CORE_TEST_SRCS) $(TEST_HDRS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(BASE_FLAGS) $(SANITIZE) -o $@ $(CORE_TEST_SRCS) $(CORE_SRCS)

$(BUILD_DIR)/parsers_adapter_test: $(CORE_SRCS) $(CORE_HDRS) ds_adapter.cpp tests/test_adapter.cpp \
                                   $(TEST_HDRS) | check-ds
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(BASE_FLAGS) $(DS_FLAGS) $(SANITIZE) -I. -o $@ tests/test_adapter.cpp ds_adapter.cpp \
	  $(CORE_SRCS)

# The DeepStream image sets LD_PRELOAD (libgomp, libGLdispatch), which ASan refuses to run
# behind; the tests need neither, so they run without it.
RUN_TEST := env -u LD_PRELOAD

test-core: $(BUILD_DIR)/parsers_core_test
	$(RUN_TEST) ./$(BUILD_DIR)/parsers_core_test

test-adapter: $(BUILD_DIR)/parsers_adapter_test
	$(RUN_TEST) ./$(BUILD_DIR)/parsers_adapter_test

test: test-core test-adapter

clean:
	rm -rf $(BUILD_DIR) $(LIB)
