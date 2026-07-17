PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ENV ?= e22-400mbh-sc
PIO ?= pio
DOCKER ?= docker
PLATFORMIO_VERSION ?= 6.1.19
PIO_IMAGE ?= meshtastic-e22-pio:$(PLATFORMIO_VERSION)
PIO_CACHE_DIR ?= $(PROJECT_DIR)/.pio-docker
HOST_UID ?= $(shell id -u)
HOST_GID ?= $(shell id -g)
ST_FLASH ?= st-flash
SERIAL_MONITOR ?= screen
TRUNK ?= trunk
CXX ?= c++
HOST_CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror
TEST_BIN ?= /tmp/e22-relay-test
PORT ?=
FIRMWARE_BIN := $(PIO_CACHE_DIR)/build/$(ENV)/firmware.bin
DOCKER_PIO = $(DOCKER) run --rm --user "$(HOST_UID):$(HOST_GID)" \
	-e HOME=/tmp \
	-e PLATFORMIO_CORE_DIR=/workspace/.pio-docker/core \
	-e PLATFORMIO_BUILD_DIR=/workspace/.pio-docker/build \
	-e PLATFORMIO_LIBDEPS_DIR=/workspace/.pio-docker/libdeps \
	-v "$(PROJECT_DIR):/workspace" -w /workspace "$(PIO_IMAGE)"

.DEFAULT_GOAL := build

.PHONY: image build build-local clean clean-local upload upload-local monitor test format check help

image:
	$(DOCKER) build --build-arg PLATFORMIO_VERSION="$(PLATFORMIO_VERSION)" -t "$(PIO_IMAGE)" "$(PROJECT_DIR)"

$(PIO_CACHE_DIR):
	mkdir -p "$@"

build: image | $(PIO_CACHE_DIR)
	$(DOCKER_PIO) run -e "$(ENV)"

build-local:
	$(PIO) run --project-dir "$(PROJECT_DIR)" -e "$(ENV)"

clean: image | $(PIO_CACHE_DIR)
	$(DOCKER_PIO) run -e "$(ENV)" -t clean

clean-local:
	$(PIO) run --project-dir "$(PROJECT_DIR)" -e "$(ENV)" -t clean

upload: build
	$(ST_FLASH) --reset write "$(FIRMWARE_BIN)" 0x08000000

upload-local:
	$(PIO) run --project-dir "$(PROJECT_DIR)" -e "$(ENV)" -t upload

monitor:
	@test -n "$(PORT)" || { echo "PORT is required, for example: make monitor PORT=/dev/cu.usbmodem1234"; exit 2; }
	$(SERIAL_MONITOR) "$(PORT)" 115200

test:
	$(CXX) $(HOST_CXXFLAGS) -I"$(PROJECT_DIR)/include" "$(PROJECT_DIR)/test/relay_logic_test.cpp" -o "$(TEST_BIN)"
	"$(TEST_BIN)"

format:
	$(TRUNK) fmt "$(PROJECT_DIR)/Dockerfile" "$(PROJECT_DIR)/Makefile" "$(PROJECT_DIR)/README.md" \
		"$(PROJECT_DIR)/platformio.ini" "$(PROJECT_DIR)/include" "$(PROJECT_DIR)/src" "$(PROJECT_DIR)/test"

check: test build

help:
	@echo "image         Build the pinned PlatformIO image"
	@echo "build         Build the $(ENV) firmware in Docker (default)"
	@echo "build-local   Build with a host PlatformIO installation"
	@echo "clean         Clean Docker/PlatformIO build output"
	@echo "clean-local   Clean with a host PlatformIO installation"
	@echo "upload        Docker-build, then host-flash with st-flash"
	@echo "upload-local  Build and flash with host PlatformIO"
	@echo "monitor       Open Type-C CDC console; requires PORT=..."
	@echo "test          Run host-side protocol tests"
	@echo "format        Format this project with Trunk"
	@echo "check         Run host tests and Docker build"
