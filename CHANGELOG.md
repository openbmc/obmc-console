# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Change categories:

- Added
- Changed
- Deprecated
- Removed
- Fixed
- Security

## [Unreleased]

### Added

1. console-server: Add PTY support for testing purposes

### Changed

1. The `xyz.openbmc_project.console` interface is only published if the
   underlying TTY device is a UART and not a VUART nor PTY (where baud is not
   applicable)
