JOBS ?= $(shell nproc 2>/dev/null || echo 4)
PREFIX ?= $(HOME)/.local
CMAKE_PREFIX_PATH ?= $(HOME)/.local
CMAKE_EXTRA_ARGS ?=
CCACHE_ARG := $(shell command -v ccache >/dev/null 2>&1 && echo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)

CLANG_FORMAT ?= clang-format
# Use git ls-files with directory roots and grep filtering -- pathspec ** can be
# brittle across git versions, and listing roots is more portable. The regex
# intentionally accepts the full range of C++ header/source suffixes so that
# future contributors using .hpp / .cxx / .cc / .hxx remain covered.
FORMAT_FILES := $(shell git ls-files src include tests fuzz 2>/dev/null | grep -E '\.(cpp|cxx|cc|h|hpp|hxx)$$')

.PHONY: help build test install clean debug format format-check bench

help:
	@echo "svlens build targets:"
	@echo "  make build         Release build"
	@echo "  make debug         Debug build"
	@echo "  make test          Run full test suite"
	@echo "  make install       Install svlens"
	@echo "  make clean         Remove build directory"
	@echo "  make format        Apply clang-format to all C++ sources"
	@echo "  make format-check  Verify all C++ sources match .clang-format"
	@echo ""
	@echo "Variables:"
	@echo "  JOBS=N"
	@echo "  PREFIX=/path"
	@echo "  CMAKE_PREFIX_PATH=/path"
	@echo "  CLANG_FORMAT=clang-format-NN"

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

format:
	@if [ -z "$(FORMAT_FILES)" ]; then \
		echo "no files matched"; exit 1; \
	fi
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	@if [ -z "$(FORMAT_FILES)" ]; then \
		echo "no files matched"; exit 1; \
	fi
	@MISMATCH=$$($(CLANG_FORMAT) --output-replacements-xml $(FORMAT_FILES) | grep -c '<replacement '); \
	if [ "$$MISMATCH" -gt 0 ]; then \
		echo "format-check: $$MISMATCH replacements suggested by clang-format"; \
		echo "Run 'make format' to apply, or run clang-format on individual files."; \
		exit 1; \
	fi
	@echo "format-check: all files match .clang-format"

bench: build
	cd bench/opentitan && bash fetch.sh && python3 gen_filelist.py && bash run.sh && python3 evaluate.py
