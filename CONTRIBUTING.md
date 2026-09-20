# Contributing to HoneyBee Browser (HoneyBeeOne)

Thank you for considering contributing to HoneyBee! We're excited to have you.

## 📜 Code of Conduct

This project is governed by our [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## 🐛 How to Report a Bug

1. Search existing issues to avoid duplicates.
2. Open a new issue using the **Bug Report** template.
3. Include steps to reproduce, expected vs. actual behavior, and your environment.

## ✨ How to Request a Feature

1. Search existing issues to avoid duplicates.
2. Open a new issue using the **Feature Request** template.
3. Explain the problem it solves and your proposed solution.

## 💻 How to Submit a Pull Request

1. **Fork** the repository.
2. **Create a branch** with a descriptive name: `feat/url-parser` or `fix/html-entity`.
3. **Make your changes** and add tests if applicable.
4. **Commit** your changes following our convention:
   - `feat(renderer): add CSS selector matching`
   - `fix(net): handle HTTP 301 redirects`
   - `docs(readme): clarify setup steps`
5. **Push** to your fork and open a **Pull Request** against the `main` branch.
6. Fill out the **PR template** completely.

**All pull requests are reviewed by the maintainer before merging.** Expect feedback within 48 hours. If you don't hear back, ping the maintainer in Discord.

## 🛠️ Development Setup

### Prerequisites

- A C++20 compiler (GCC 10+ or Clang 12+)
- CMake 3.20 or newer
- Ninja (recommended) or Make
- pkg-config
- GTK 4 with C++ bindings (gtkmm-4)
- libcurl development headers

### Ubuntu / Debian

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

### Build

```bash
git clone https://github.com/HoneyBeeOne/honeybee-browser.git
cd honeybee-browser
mkdir -p build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja
```

### Run

```bash
./bin/honeybee
```

For a full walkthrough with explanations and troubleshooting, see [docs/getting-started.md](docs/getting-started.md).

## 🧪 Running Tests

Test infrastructure is not yet in place (Phase 0). Once tests exist, they will run via:

```bash
cd build
ctest --output-on-failure
```

## 🙋 Getting Help

Join our Discord community: [discord invite](https://discord.gg/nAEBNVw83)

If you're stuck on a task for more than an hour, ask for help. Nobody is expected to figure everything out alone.
