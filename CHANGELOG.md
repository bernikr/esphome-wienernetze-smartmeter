<!--
SPDX-FileCopyrightText: 2026 bernikr <hi@berni.kr>

SPDX-License-Identifier: MIT
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- Changed device name format.
- Improved logging to output warnings on mismatched values.
- Removed unnecessary `tx_pin` requirement from UART configuration.

### Other
- Added project license and copyright information.

---

## [2.1.1] - 2026-10-05

### Added
- Smart meter ID logging in configuration output (`dump_config`).
- Read-write head usage instructions in documentation.

### Changed
- Improved hex data logging readability.

---

## [2.1.0] - 2026-10-05

### Added
- Support for smart meters that transmit without a serial number ([#21](https://github.com/bernikr/esphome-wienernetze-smartmeter/pull/21)).

### Changed
- Change from external `rweather/Crypto` library dependency to included `bearssl` on Arduino.
- Streamlined parser implementation using compiler macros to reduce duplicate code.

---

## [2.0.0] - 2026-10-05

### Changed
- Switched to proper parsing of the HDLC frame rather than hardcoded offsets based on meter model heuristics ([#20](https://github.com/bernikr/esphome-wienernetze-smartmeter/pull/20)).
  **This *might* break compatibility with some meters.**
  Please open an [issue](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/new) if you encounter any problems to help me test it on more meters.
- Decoupled platform-specific decryption routines.
- Overhauled parser internals, data types, and cast safety with improved documentation.
- Updated documentation and configuration examples for version 2.0.

---

## [1.4.0] - 2026-10-02

### Changed
- Shortened meter model detection prefix to 3 bytes.
- Promoted decryption failure logs to proper error level.

---

## [1.3.0] - 2026-07-21

### Added
- Support for the ESP-IDF framework on ESP32. ([#10](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/10))

### Changed
- Replaced `FastCRC` external library with an internal implementation. ([#11](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/11))
- Standardized formatting and CI checks using `clang-format` and `pre-commit`.

---

## [1.2.0] - 2025-08-28

### Added
- Support for additional Sagemcom AM550-TD0 identifier variants (including AM550-TD0.21) ([#7](https://github.com/bernikr/esphome-wienernetze-smartmeter/pull/7)).

---

## [1.1.1] - 2025-02-25

### Documentation
- Added documentation for Sagemcom AM550-TD0 meter support.

---

## [1.1.0] - 2025-02-25

### Added
- Support for Iskraemeco (ISKit) smart meter models. ([#4](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/4))
- Landis+Gyr E450 documentation. ([#5](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/5))
- Template sensor setup examples.

---

## [1.0.1] - 2025-02-14

### Changed
- Added verbose (`logv`) logging for the detected smart meter identifier.

---

## [1.0.0] - 2025-02-13

### BREAKING CHANGES
- Changed component name from `im150` to `wienernetze` to reflect the wider support. Adjust your config accordingly.

### Added
- Support for Landis+Gyr smart meters.
- Support for Wiener Netze smart meters (formerly referenced as `im150`).

### Changed
- Refactored component structure to align with ESPHome external component conventions.
- Improved unsupported smart meter warnings and diagnostic feedback.

### Fixed
- Frame offset calculation bug for Landis+Gyr payloads.

---

## [0.1.2] - 2024-05-09

### Changed
- State updates are now only published when raw values change, reducing bus noise.

---

## [0.1.1] - 2024-05-09

### Added
- Implemented `dump_config` support to print the active component version at boot.

---

## [0.1.0] - 2024-05-09

### Added
- Initial external component implementation.
- Support for text sensors.
- Generalized frame parsing for multiple smart meter models (including IM350 ([#1](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/1)), and IM151 ([#2](https://github.com/bernikr/esphome-wienernetze-smartmeter/issues/2))).
- Example configuration.
