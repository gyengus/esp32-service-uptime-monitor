# Accessibility Guide & Statement

Accessibility is a fundamental priority for **ESP32 Service Uptime Monitor**. We believe that self-hosted embedded infrastructure tools should be fully accessible to everyone, including individuals with disabilities and those navigating with assistive technologies.

This document outlines our accessibility goals, architectural guardrails for contributors, and our reporting process for accessibility barriers.

---

## Accessibility Objectives

- **Target Benchmark:** We actively strive toward conformance with [WCAG 2.2 Level AA](https://www.w3.org/TR/WCAG22/) across all user-facing interfaces.
- **Embedded Web UI (Device Dashboard):**
  - Full keyboard accessibility: every button, modal, form input, and service card can be reached and activated via keyboard.
  - Visible focus indicators with sufficient color contrast across both dark and light visual themes.
  - Screen reader announcements using semantic HTML5 and appropriate ARIA live regions for dynamic events (such as status transitions, ping results, and toast messages).
  - Modal dialogs trap focus when open and can be dismissed using the Escape key.
- **Documentation & Landing Website:**
  - Clear, semantic heading hierarchies (`h1` through `h6`).
  - Meaningful text alternatives for images, architecture diagrams, and hardware pinout figures.
  - Descriptive link text avoiding vague phrases.
  - High contrast text and fluid responsive layouts supporting up to 200% text zoom without horizontal scrolling or loss of functionality.

---

## Contributor Guardrails

When contributing front-end code (in `web/`) or documentation (in `docs/` or `templates/`), please uphold these standards:

1. **Keyboard-Only Verification:**
   - Verify that all new features can be navigated and operated using only `Tab`, `Shift + Tab`, `Enter`, `Space`, and arrow keys.
   - Ensure focus does not become trapped or lost after dynamic updates.
2. **Semantic Elements:**
   - Prefer native HTML buttons, links, inputs, and dialogs over `div` or `span` elements with click handlers.
   - Use proper label elements for form inputs (`<label for="...">` or `aria-label`).
3. **Automated Testing:**
   - Run automated accessibility audits (e.g., Chrome Lighthouse, axe DevTools, or WAVE) before submitting pull requests with UI modifications.
4. **Color and Contrast:**
   - Do not rely on color alone to communicate state. For example, service status pairs color indicators (green/red) with explicit text labels ("ONLINE" / "OFFLINE").

---

## Reporting Accessibility Barriers

If you encounter any barrier that prevents you from reading, navigating, or operating ESP32 Service Uptime Monitor:

1. **Open a GitHub Issue:**
   Submit an issue using our [Bug Report Template](.github/ISSUE_TEMPLATE/bug_report.yml), noting `Accessibility` in the title or description.
2. **Contact Directly:**
   Use the accessibility/general contact channel at [https://esp32-service-uptime-monitor.com/#contact](https://esp32-service-uptime-monitor.com/#contact).

We value feedback from assistive technology users and treat accessibility issues with high priority.
