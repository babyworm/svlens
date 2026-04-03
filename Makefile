JOBS ?= $(shell nproc 2>/dev/null || echo 4)
PREFIX ?= $(HOME)/.local
CMAKE_PREFIX_PATH ?= $(HOME)/.local
CMAKE_EXTRA_ARGS ?=
CCACHE_ARG := $(shell command -v ccache >/dev/null 2>&1 && echo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)

.PHONY: help build test install clean debug

help:
	@echo "svlens build targets:"
	@echo "  make build       Release build"
	@echo "  make debug       Debug build"
	@echo "  make test        Run full test suite"
	@echo "  make install     Install svlens"
	@echo "  make clean       Remove build directory"
	@echo ""
	@echo "Variables:"
	@echo "  JOBS=N"
	@echo "  PREFIX=/path"
	@echo "  CMAKE_PREFIX_PATH=/path"

build:
	cmake -B build -DCMAKE_PREFIX_PATH="$(CMAKE_PREFIX_PATH)" $(CCACHE_ARG) $(CMAKE_EXTRA_ARGS)
	cmake --build build -j$(JOBS)

debug:
	cmake -B build -DCMAKE_PREFIX_PATH="$(CMAKE_PREFIX_PATH)" -DCMAKE_BUILD_TYPE=Debug $(CCACHE_ARG) $(CMAKE_EXTRA_ARGS)
	cmake --build build -j$(JOBS)

test: build
	ctest --test-dir build --output-on-failure

install: build
	cmake --install build --prefix "$(PREFIX)"

clean:
	rm -rf build
