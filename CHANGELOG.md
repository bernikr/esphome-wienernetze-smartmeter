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

## [2.1.1]

### Added
- Smart meter ID logging in configuration output (`dump_config`).
- Read-write head usage instructions in documentation.

### Changed
- Improved hex data logging readability.

---

## [2.1.0]

### Added
- Support for smart meters that transmit without a serial number (#21).

### Changed
- Change from external `rweather/Crypto` library dependency to included `bearssl` on Arduino.
- Streamlined parser implementation using compiler macros to reduce duplicate code.

---

## [2.0.0]

### Added
- Explicit handling and validation for the Cipher Length field.
- Additional frame format and start-byte checks.

### Fixed
- Masking bit calculation for segmented/fragmented messages.

### Changed
- Switched to finding payload offsets dynamically via the HDLC header rather than hardcoded meter model heuristics (#20).
- Decoupled platform-specific decryption routines.
- Overhauled parser internals, data types, and cast safety with improved documentation.
- Updated documentation and configuration examples for version 2.0.

---

## [1.4.0]

### Changed
- Shortened meter model detection prefix to 3 bytes.
- Promoted decryption failure logs to proper error level.

---

## [1.3.0]

### Added
- Support for the ESP-IDF framework on ESP32.

### Changed
- Replaced `FastCRC` external library with an internal implementation.
- Standardized formatting and CI checks using `clang-format` and `pre-commit`.

---

## [1.2.0]

### Added
- Support for additional Sagemcom AM550-TD0 identifier variants (including AM550-TD0.21) (#7).

---

## [1.1.1]

### Documentation
- Added documentation for Sagemcom AM550-TD0 meter support.

---

## [1.1.0]

### Added
- Support for Iskraemeco (ISKit) smart meter models.
- Landis+Gyr E450 documentation.
- Template sensor setup examples.

---

## [1.0.1]

### Changed
- Added verbose (`logv`) logging for the detected smart meter identifier.

---

## [1.0.0]

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

## [0.1.2]

### Changed
- State updates are now only published when raw values change, reducing bus noise.

---

## [0.1.1]

### Added
- Implemented `dump_config` support to print the active component version at boot.

---

## [0.1.0]

### Added
- Initial external component implementation.
- Support for text sensors.
- Generalized frame parsing for multiple smart meter models (including IM350).
- Example configuration.
