.PHONY: build test format-check lint architecture asan ubsan tsan e2e ui-interaction integration stress benchmark verify

build:
	cmake --preset debug
	cmake --build --preset debug

test: build
	ctest --preset debug

format-check:
	clang-format --dry-run --Werror $$(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print)

lint: build
	clang-tidy -p build/debug $$(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print)

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
	TSAN_OPTIONS="halt_on_error=1:suppressions=$(CURDIR)/tests/tsan.supp" \
		ctest --test-dir build/tsan --output-on-failure

e2e: build
	./scripts/e2e-smoke.sh ./build/debug/filescope

ui-interaction: build
	bash tests/ui/run.sh ./build/debug/filescope

integration: build
	ctest --preset debug -L integration --no-tests=error

stress: build
	ctest --preset debug -L stress --no-tests=error

benchmark: build
	ctest --preset debug -L benchmark --no-tests=error

verify: build test format-check lint architecture asan ubsan tsan e2e
