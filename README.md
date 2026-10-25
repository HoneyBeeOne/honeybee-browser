# HoneyBee Browser 🐝

**A community-built web browser from Cairo University students.**

HoneyBee is an open-source project where students from the Faculty of Computers and AI and the Faculty of Engineering contribute to building a real web browser from scratch. It's a hands-on learning opportunity for browser internals: HTML parsing, CSS layout, JavaScript engines, networking, and more.

[![Build](https://github.com/HoneyBeeOne/honeybee-browser/actions/workflows/build.yml/badge.svg)](https://github.com/HoneyBeeOne/honeybee-browser/actions/workflows/build.yml)
[![Contributors](https://img.shields.io/github/contributors/HoneyBeeOne/honeybee-browser.svg)](https://github.com/HoneyBeeOne/honeybee-browser/graphs/contributors)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Good First Issues](https://img.shields.io/github/issues/HoneyBeeOne/honeybee-browser/good%20first%20issue.svg)](https://github.com/HoneyBeeOne/honeybee-browser/labels/good%20first%20issue)
[![GitLab Mirror](https://img.shields.io/badge/GitLab-mirror-FC6D26?logo=gitlab&logoColor=white)](https://gitlab.com/amrmuhammad/honeybee-browser)

## 📊 What Works Today

HoneyBee is at **v0.1.0 (first paint)**. It parses HTML, cascades CSS, lays out block-level boxes, and paints via Cairo. Arabic text renders with correct letter-shaping through Pango.

![HoneyBee rendering docs/demo.html](docs/demo-screenshot.png)

| Feature | Status |
|---|---|
| HTML parsing (lexbor) | :white_check_mark: |
| Owned DOM tree | :white_check_mark: |
| CSS selector matching | :white_check_mark: |
| CSS stylesheet parsing | :white_check_mark: |
| Cascade (importance + specificity + inheritance) | :white_check_mark: |
| Block-flow layout | :white_check_mark: |
| Cairo paint (backgrounds, borders, text) | :white_check_mark: |
| Box shorthands (`margin: 10px 20px`) | :white_check_mark: |
| Length units (`px`, `em`, `rem`, `%`, `auto`) | :white_check_mark: |
| Arabic text shaping | :white_check_mark: |
| Text wrapping + whitespace collapsing | :white_check_mark: |
| `display` property (block, inline) | :white_check_mark: |
| `dir` attribute (ltr, rtl, auto) | :white_check_mark: |
| Inline formatting (`span`, `a`, `em`) | :white_check_mark: |
| UA stylesheet (default `<h1>`, `<p>` margins) | :white_check_mark: |
| Pseudo-classes (`:link`, `:visited`, `:hover`) | :white_check_mark: |
| Back / forward navigation | :white_check_mark: |
| Instant Back/Forward (session cache) | :white_check_mark: |
| Large-page scrolling + hover | :white_check_mark: (link hover ~20ms; wrapper hover ~8ms) |
| External CSS loading (`<link rel=stylesheet>`) | :white_check_mark: (article content styled; layout deferred to ADR-026/027) |
| `@media` queries | :x: |
| `border-radius`, `box-shadow` | :x: |

For the full picture -- including a per-property breakdown of why example.com still differs from Firefox -- see [`docs/current-state.md`](docs/current-state.md).

Try it: load [`docs/demo.html`](docs/demo.html) in the built browser to see the render above.

## 🎯 Our Vision

To create a fully functional, open-source web browser built entirely by the student community at Cairo University, providing an unparalleled learning experience in large-scale software engineering.

## ✨ How to Contribute

We welcome contributions from all Cairo University students! No prior browser experience is needed.

- **Read our [CONTRIBUTING.md](CONTRIBUTING.md) guide.**
- Look for issues labeled **`good first issue`** to get started.
- Join our community on Discord: https://discord.gg/nAEBNVw83

## 🏗️ Project Architecture

See [docs/architecture.md](docs/architecture.md) for a high-level overview of the project.

See [docs/companions.md](docs/companions.md) to meet the project companions.

## 🗺️ Roadmap

See [docs/roadmap.md](docs/roadmap.md) for our phased development plan.

## 📜 License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
