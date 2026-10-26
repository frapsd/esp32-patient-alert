# Security Policy

## Reporting a vulnerability

Please report privately through GitHub Security Advisories: open the repository's **Security** tab and choose **Report a vulnerability**. Do not open a public issue for vulnerabilities, and never include real Pushover tokens or keys in a report.

## Scope

In scope: the firmware in this repository (TLS verification, handling of secrets, alert state logic, anything that could cause missed or forged alarms).

Out of scope: vulnerabilities in Pushover itself, in PlatformIO or the Arduino-ESP32 framework (report those upstream), and physical access to the device. See the Security section of [README.md](README.md) for the known accepted risks.

## Disclaimer

This is not a certified medical device and does not replace emergency services. Without home internet or power the alarm does not fire.
