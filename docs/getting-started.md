# Getting Started with HoneyBee Browser

A step-by-step guide for building and running HoneyBee Browser on your machine for the first time.

If you get stuck at any step, join the [Discord](https://discord.gg/nAEBNVw83) and ask in the `#help` channel.

## What you'll need

- A computer running Linux (Ubuntu 22.04 or newer recommended), macOS, or Windows with WSL2
- Terminal access
- About 15 minutes

The instructions below are written for Ubuntu/Debian. macOS and Windows/WSL2 instructions are coming soon.

## Step 1 — Install dependencies

HoneyBee is built with C++20, CMake, GTK 4, and libcurl. Install the toolchain and development libraries:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libgtkmm-4.0-dev \
  libcurl4-openssl-dev
```

Verify that GTK 4 bindings are available:

```bash
pkg-config --modversion gtkmm-4.0
```

You should see a version like `4.10.0` or higher. If you see an error, your Ubuntu version may be too old for gtkmm-4.

## Step 2 — Clone the repository

```bash
git clone https://github.com/HoneyBeeOne/honeybee-browser.git
cd honeybee-browser
```

## Step 3 — Build

```bash
mkdir -p build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja
```

The build should finish in a few seconds and produce an executable at `build/bin/honeybee`.

If you see `Package gtkmm-4.0 was not found`, revisit Step 1 — the dev package isn't installed.

## Step 4 — Run

```bash
./bin/honeybee
```

A window titled "HoneyBee Browser" should open. Right now it displays a placeholder — that's expected. The browser engine hasn't been built yet. This is Phase 0.

## Step 5 — Make your first change

To confirm your setup works end-to-end, try this:

1. Open `src/main.cpp` in your editor.
2. Change the window title from `"HoneyBee Browser"` to `"HoneyBee Browser — [your name]"`.
3. Rebuild:

```bash
cd build
ninja
```

4. Run again:

```bash
./bin/honeybee
```

You should see your name in the window title. If it works, your environment is ready.

5. Revert your change before committing anything:

```bash
git checkout src/main.cpp
```

## Step 6 — Find your first issue

Every task in HoneyBee is tracked as a GitHub issue. Beginner-friendly tasks are labeled `good first issue`.

Browse the list:

https://github.com/HoneyBeeOne/honeybee-browser/labels/good%20first%20issue

Pick one that looks interesting, comment on the issue to claim it, and follow the pull request process in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Troubleshooting

### `Package gtkmm-4.0 was not found`

gtkmm 4 is not installed, or your Ubuntu is too old. Run:

```bash
apt search gtkmm
```

If only `libgtkmm-3.0-dev` appears, you need a newer Ubuntu (22.04 or later).

### `ninja: command not found`

Ninja isn't installed. Either install it with `sudo apt install ninja-build`, or use Make instead by running `cmake ..` (without `-G Ninja`) and then `make`.

### `CMake Error: Could not find a package configuration file`

`pkg-config` is missing. Install it with `sudo apt install pkg-config`.

### Compilation errors about GTK APIs

Some GTK 4 API details differ slightly between gtkmm versions. If you hit a compile error, paste the full error into the `#help` channel on Discord and we'll help you fix it.

### The window doesn't open (over SSH)

If you're connected via SSH without X forwarding, the GTK window can't display. Either enable X forwarding (`ssh -X`), or run the browser on your local machine.

## Next steps

Once you've built the project and made your first pull request, consider:

- Reading [docs/architecture.md](architecture.md) to understand how the browser is structured
- Reading [docs/decisions/001-tech-stack.md](decisions/001-tech-stack.md) to understand why we chose C++ and GTK 4
- Browsing the [roadmap](roadmap.md) to see where the project is heading

Welcome to HoneyBee. 🐝
