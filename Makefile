.DEFAULT_GOAL := all
MAKEFLAGS += --no-print-directory

PROJECT := micro-max-4.8-uci-core-v1.0
override DISPLAY_NAME := micro-Max 4.8 UCI (Core) v1.0

# Detect host platform for naming and platform-specific release behavior.
override UNAME_S := $(shell uname -s 2>/dev/null)
override UNAME_M := $(shell uname -m 2>/dev/null)

ifeq ($(UNAME_S),Darwin)
override PLATFORM_NAME := macos
override PLATFORM_LABEL := macOS
override BIN_EXT :=
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS :=
else ifeq ($(UNAME_S),Linux)
override PLATFORM_NAME := linux
override PLATFORM_LABEL := Linux
override BIN_EXT :=
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS :=
else ifeq ($(UNAME_S),FreeBSD)
override PLATFORM_NAME := freebsd
override PLATFORM_LABEL := FreeBSD
override BIN_EXT :=
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS :=
else ifneq (,$(filter MINGW%,$(UNAME_S)))
override PLATFORM_NAME := windows
override PLATFORM_LABEL := Windows
override BIN_EXT := .exe
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS := -Wl,--no-insert-timestamp
else ifeq ($(strip $(UNAME_S)),)
override PLATFORM_NAME := unknown
override PLATFORM_LABEL := unknown
override BIN_EXT :=
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS :=
else
override PLATFORM_NAME := $(UNAME_S)
override PLATFORM_LABEL := $(UNAME_S)
override BIN_EXT :=
override PLATFORM_CFLAGS :=
override PLATFORM_LDFLAGS :=
endif

# On Windows ARM64, x86_64 compatibility tools can report x86_64.
# Prefer the native Windows host architecture when it is available.
ifeq ($(PLATFORM_NAME),windows)
ifneq (,$(filter %-ARM64 %-arm64,$(UNAME_S)))
override UNAME_M := arm64
endif
endif

# Normalize public architecture names.
ifeq ($(UNAME_M),aarch64)
override ARCH_NAME := arm64
else ifeq ($(UNAME_M),arm64)
override ARCH_NAME := arm64
else ifeq ($(UNAME_M),amd64)
override ARCH_NAME := x86_64
else ifeq ($(strip $(UNAME_M)),)
override ARCH_NAME := unknown
else
override ARCH_NAME := $(UNAME_M)
endif

# Keep macOS release binaries compatible with older supported systems.
ifeq ($(PLATFORM_NAME),macos)
ifeq ($(ARCH_NAME),arm64)
override PLATFORM_CFLAGS := -mmacosx-version-min=11.0
override PLATFORM_LDFLAGS := -mmacosx-version-min=11.0
else ifeq ($(ARCH_NAME),x86_64)
override PLATFORM_CFLAGS := -mmacosx-version-min=10.13
override PLATFORM_LDFLAGS := -mmacosx-version-min=10.13
endif
endif

override TARGET_NAME := $(PLATFORM_NAME)-$(ARCH_NAME)
BIN ?= $(PROJECT)-$(TARGET_NAME)$(BIN_EXT)

ifeq ($(TARGET_NAME),windows-arm64)
override PLATFORM_CFLAGS := --target=aarch64-pc-windows-msvc -D_CRT_SECURE_NO_WARNINGS
override PLATFORM_LDFLAGS := --target=aarch64-pc-windows-msvc -Wl,-Brepro
endif

# Official release targets. Validation evidence is maintained separately.
override RELEASE_TARGETS := macos-x86_64 macos-arm64 linux-x86_64 linux-arm64 windows-x86_64 windows-arm64 freebsd-x86_64

ifneq ($(filter $(TARGET_NAME),$(RELEASE_TARGETS)),)
override RELEASE_TARGET := yes
else
override RELEASE_TARGET := no
endif

BUILD_DIR ?= build

SRC_DIR := src
THIRD_PARTY_DIR := third-party

INTEGRATION := $(SRC_DIR)/$(PROJECT).c
CANONICAL := $(THIRD_PARTY_DIR)/umax4_8.c

INTEGRATION_OBJ := $(BUILD_DIR)/uci_integration.o
CANONICAL_OBJ := $(BUILD_DIR)/umax4_8.o
OBJS := $(INTEGRATION_OBJ) $(CANONICAL_OBJ)

override CANONICAL_SHA256 := 447cd24da6275221a1e9564d9282cb69f019b6c521dde1f063b9eb7de3fd34eb
override INTEGRATION_SHA256 := d818e217da8a98a324b7ea9a3564595726c5c70af037df1d22b6b7ac9193cddc

ifeq ($(TARGET_NAME),windows-arm64)
ifneq (,$(filter default undefined,$(origin CC)))
CC := clang
endif
override RELEASE_CC := clang
else
override RELEASE_CC := cc
endif

CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

V ?= 0
COLOR ?= auto
SUMMARY ?= 1
QUIET_CHECKS ?= 0

ifeq ($(V),1)
Q :=
else
Q := @
endif

# COLOR=auto uses color only when stdout is a terminal.
# COLOR=1 forces color. COLOR=0 or NO_COLOR disables it.
define print_status
color=0; case "$(COLOR)" in 1) color=1 ;; auto) [ -t 1 ] && color=1 || : ;; esac; [ -n "$(NO_COLOR)" ] && color=0 || :; [ -n "$${NO_COLOR:-}" ] && color=0 || :; if [ "$$color" -eq 1 ]; then printf '\033[$(2)m%s\033[0m\n' '$(1)'; else printf '%s\n' '$(1)'; fi
endef

# These flags are engine correctness requirements. Keep them after user CFLAGS.
override REQUIRED_CFLAGS := -std=gnu89 -fwrapv -fno-strict-aliasing -fsigned-char

override INTEGRATION_WARNINGS := -Wall -Wextra -Wpedantic -Werror

# third-party/umax4_8.c is preserved unchanged. The general flag prevents a
# caller-supplied -Werror from promoting its legacy warnings; the specific flag
# handles compilers that treat implicit function declarations as errors.
override CANONICAL_WARNINGS := -Wno-error -Wno-error=implicit-function-declaration

.DELETE_ON_ERROR:
.PHONY: all build-header preflight check-tools check-platform info verify \
	release-platform release clean verify-release

all: preflight $(BIN)
ifeq ($(SUMMARY),1)
	@printf '\nBuild '
	@$(call print_status,Completed Successfully,32)
	@printf '\nBinary: %s\n' "$(BIN)"
endif

build-header:
	@if [ "$(QUIET_CHECKS)" != "1" ]; then \
		printf '\n%s\nBuild\n\n' "$(DISPLAY_NAME)"; \
	fi

preflight: build-header check-tools check-platform

check-tools:
	@set -eu; \
	if [ "$(QUIET_CHECKS)" != "1" ]; then printf '%-28s' 'Build Tools'; fi; \
	compiler='$(firstword $(CC))'; \
	if ! command -v "$$compiler" >/dev/null 2>&1; then \
		if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,FAILED,31); fi; \
		echo "error: C compiler not found: $$compiler" >&2; \
		echo "error: see README.md for build prerequisites" >&2; \
		exit 1; \
	fi; \
	if command -v shasum >/dev/null 2>&1 || \
	   command -v sha256sum >/dev/null 2>&1; then \
		:; \
	else \
		if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,FAILED,31); fi; \
		echo "error: shasum or sha256sum is required" >&2; \
		echo "error: see README.md for build prerequisites" >&2; \
		exit 1; \
	fi; \
	if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,OK,32); fi

check-platform:
	@if [ "$(QUIET_CHECKS)" != "1" ]; then \
		printf '%-28s%s %s\n' 'Platform' "$(PLATFORM_LABEL)" "$(ARCH_NAME)"; \
	fi; \
	if [ "$(RELEASE_TARGET)" != "yes" ]; then \
		if [ "$(QUIET_CHECKS)" != "1" ]; then \
			printf '%-28s' 'Release Target'; \
			$(call print_status,WARNING,33); \
		fi; \
		echo "warning: unsupported release target: $(TARGET_NAME)" >&2; \
		echo "warning: normal build will continue; $(MAKE) release is disabled" >&2; \
	fi

info:
	@printf '%s\n' \
		"Engine: $(DISPLAY_NAME)" \
		"Host OS: $(UNAME_S)" \
		"Host architecture: $(UNAME_M)" \
		"Platform: $(PLATFORM_LABEL)" \
		"Architecture: $(ARCH_NAME)" \
		"Official release target: $(RELEASE_TARGET)" \
		"Compiler: $(CC)" \
		"Output: $(BIN)"

ifeq ($(V),1)
define run_step
$(2)
endef
else
define run_step
@set -eu; printf '%-28s' '$(1)'; log='$(BUILD_DIR)/.$(3).log'; if $(2) >"$$log" 2>&1; then rm -f "$$log"; $(call print_status,$(4),32); else status=$$?; $(call print_status,FAILED,31); cat "$$log"; rm -f "$$log"; exit $$status; fi
endef
endif

$(BIN): $(OBJS)
	$(call run_step,Executable,$(CC) $(LDFLAGS) $(PLATFORM_LDFLAGS) -o $@ $(OBJS) $(LDLIBS),link,LINKED)

$(INTEGRATION_OBJ): $(INTEGRATION) Makefile | preflight verify $(BUILD_DIR)
	$(call run_step,UCI Integration,$(CC) $(CPPFLAGS) $(CFLAGS) $(PLATFORM_CFLAGS) $(REQUIRED_CFLAGS) $(INTEGRATION_WARNINGS) -c $< -o $@,integration,COMPILED)

$(CANONICAL_OBJ): $(CANONICAL) Makefile | preflight verify $(BUILD_DIR)
	$(call run_step,micro-Max 4.8,$(CC) $(CPPFLAGS) $(CFLAGS) $(PLATFORM_CFLAGS) $(REQUIRED_CFLAGS) $(CANONICAL_WARNINGS) -Dmain=umax_main -c $< -o $@,canonical,COMPILED)

$(BUILD_DIR):
	$(Q)mkdir -p "$@"

define verify_sha256
	@set -eu; \
	file='$(1)'; expected='$(2)'; label='$(3)'; \
	if [ "$(QUIET_CHECKS)" != "1" ]; then printf '%-28s' "$$label"; fi; \
	if command -v shasum >/dev/null 2>&1; then \
		line=$$(shasum -a 256 "$$file"); actual=$${line%% *}; \
	elif command -v sha256sum >/dev/null 2>&1; then \
		line=$$(sha256sum "$$file"); actual=$${line%% *}; \
	else \
		if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,FAILED,31); fi; \
		echo "error: shasum or sha256sum is required" >&2; \
		exit 1; \
	fi; \
	if [ "$$actual" != "$$expected" ]; then \
		if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,FAILED,31); fi; \
		printf 'SHA-256 mismatch: %s\nexpected: %s\nactual:   %s\n' \
			"$$file" "$$expected" "$$actual" >&2; \
		exit 1; \
	fi; \
	if [ "$(QUIET_CHECKS)" != "1" ]; then $(call print_status,VERIFIED,32); fi
endef

verify:
	$(call verify_sha256,$(CANONICAL),$(CANONICAL_SHA256),micro-Max 4.8)

verify-release:
	$(call verify_sha256,$(INTEGRATION),$(INTEGRATION_SHA256),UCI Integration)
	$(call verify_sha256,$(CANONICAL),$(CANONICAL_SHA256),micro-Max 4.8)

release-platform:
	@printf '\n%s\nRelease Build\n\n' "$(DISPLAY_NAME)"; \
	printf '%-28s%s %s\n' 'Platform' "$(PLATFORM_LABEL)" "$(ARCH_NAME)"; \
	if [ "$(RELEASE_TARGET)" != "yes" ]; then \
		printf '%-28s' 'Release Target'; \
		$(call print_status,FAILED,31); \
		echo "error: release build is not enabled for $(TARGET_NAME)" >&2; \
		echo "error: use $(MAKE) without the release target for a local experimental build" >&2; \
		exit 1; \
	fi

release: release-platform
	$(Q)$(MAKE) BUILD_DIR=build CC=$(RELEASE_CC) check-tools
	$(Q)$(MAKE) BUILD_DIR=build clean
	$(Q)$(MAKE) BUILD_DIR=build verify-release
	$(Q)$(MAKE) BUILD_DIR=build CC=$(RELEASE_CC) CFLAGS="-O2" CPPFLAGS= LDFLAGS= LDLIBS= SUMMARY=0 QUIET_CHECKS=1 V=$(V) COLOR=$(COLOR) all
	@set -eu; \
	if command -v shasum >/dev/null 2>&1; then \
		line=$$(shasum -a 256 "$(BIN)"); hash=$${line%% *}; \
	else \
		line=$$(sha256sum "$(BIN)"); hash=$${line%% *}; \
	fi; \
	printf '\nRelease Build '; \
	$(call print_status,Completed Successfully,32); \
	printf '\nBinary: %s\nSHA-256: %s\n' "$(BIN)" "$$hash"

clean:
	$(Q)rm -rf "$(BUILD_DIR)" "$(BIN)"
