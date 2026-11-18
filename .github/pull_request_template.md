## Summary

<!-- Briefly describe the purpose of this pull request. What problem does it solve, or what feature does it introduce? -->

## Related Issues

<!-- Link any related issues or discussions here using GitHub syntax (e.g., Fixes #123, Closes #456, Ref #789). -->
- Closes #

## Type of Change

<!-- Mark the appropriate option(s) with an [x]. -->
- [ ] **Bug fix** (non-breaking change fixing an issue or unexpected behavior)
- [ ] **New feature** (non-breaking change adding functionality)
- [ ] **New board support** (adding or refining board profile in `devices.h` / `platformio.ini`)
- [ ] **Web UI** (frontend enhancement, responsive layout, or asset minification)
- [ ] **Performance / Memory** (optimizing heap, PSRAM, Flash, or FreeRTOS task stacks)
- [ ] **Documentation** (OpenAPI specs, guides, or static site pre-rendering)
- [ ] **Testing / CI** (unit tests, coverage scripts, or GitHub Actions / GitLab CI)

## Target Environments & Testing

<!-- Which environments have you tested this change against? -->
- [ ] **Hardware Verification:**
  - [ ] ESP32-S3-CAM (MicroSD / SD_MMC)
  - [ ] ESP32-S3 DevKitC-1 (LittleFS)
  - [ ] ESP32-WROOM-32 / Classic
  - [ ] ESP32-C3 / C6 / C5
- [ ] **Native C++ Unit Tests:** `pio test -e native` passed
- [ ] **Web UI Tests:** `npm --prefix web test` passed (if UI changed)
- [ ] **OpenAPI Generation:** `python3 scripts/openapi_generator.py --strict` passed (if API changed)
- [ ] **Documentation Pre-rendering:** `python3 scripts/preview_site.py --build-only` verified (if docs changed)

## Checklist

<!-- Please confirm all items before requesting a review. -->
- [ ] My code adheres to the project coding style, thread-safety rules, and guidelines in [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).
- [ ] I have added or updated tests covering new functionality or bug fixes.
- [ ] Accessibility: keyboard navigation and semantic markup have been verified for any Web UI changes ([ACCESSIBILITY.md](ACCESSIBILITY.md)).
- [ ] Commits are written in English, descriptive, **without** Conventional Commits prefixes (no `feat:`, `fix:`, etc.), and **without** sign-off lines.
- [ ] All commits are GPG/SSH signed (`git commit -S`).
- [ ] **Privacy:** No personal email addresses, API tokens, passwords, or network credentials are included in the code, comments, or commit history.
- [ ] I agree to the Contributor License Agreement (CLA) detailed in [CONTRIBUTING.md](CONTRIBUTING.md).
