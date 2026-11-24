# FileScope

FileScope is a native GTK3 application for Linux. This bootstrap contains the application window and the build and test harness; file-management features come later.

## Build prerequisites

On Ubuntu 22.04 or 24.04, install the required and developer packages listed in `docs/engineering/DEPENDENCIES.md`:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev libsqlite3-dev libayatana-appindicator3-dev xvfb clang-format clang-tidy
```

## Build and test

```bash
make build
make test
make verify
```

Run the application after building:

```bash
./build/debug/filescope
```

Additional checks are available through `make format-check`, `make lint`, `make architecture`, `make asan`, `make ubsan`, `make tsan`, and `make e2e`. The `integration`, `stress`, and `benchmark` targets intentionally return nonzero with `NOT_IMPLEMENTED` until those scenarios exist.
