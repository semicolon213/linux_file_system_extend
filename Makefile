.PHONY: build test format-check lint architecture asan ubsan tsan e2e integration stress benchmark verify

build:
	cmake --preset debug
	cmake --build --preset debug

test: build
	ctest --preset debug

format-check:
	clang-format --dry-run --Werror $$(find src tests -type f \( -name '*.c' -o -name '*.h' \) -print)

lint: build
	clang-tidy -p build/debug src/*.c tests/*.c

architecture:
	./scripts/check-architecture.sh

asan:
	cmake --preset asan
	cmake --build --preset asan
	ctest --preset asan

ubsan:
	cmake --preset ubsan
	cmake --build --preset ubsan
	ctest --preset ubsan

tsan:
	cmake --preset tsan
	cmake --build --preset tsan
	ctest --preset tsan

e2e: build
	./scripts/e2e-smoke.sh ./build/debug/filescope

integration stress benchmark:
	@echo "NOT_IMPLEMENTED: no $@ scenario exists at bootstrap stage" >&2
	@exit 2

verify: build test format-check lint asan ubsan e2e
