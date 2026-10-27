# Security

Please report vulnerabilities privately through GitHub's **Report a vulnerability** (Security tab),
not in a public issue.

Things that are in scope: anything that lets someone read or add punches without the clock key,
change the clock's server address without the PIN, run code on the clock, or read fingerprint
templates.

Deployment reminders:

- Generate a long random `CLOCK_TOKEN` and keep it out of git.
- Change the initial PIN (1234) on every clock. It also protects `http://reloj.local` and OTA.
- Fingerprint templates are biometric data: keep the server private and delete people who leave.
