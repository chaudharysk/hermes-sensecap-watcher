# Contributing

Use a separate checkout and keep credentials, device images, and personal logs
out of issues and pull requests. State the public upstream revision, ESP-IDF
version, Hermes version/hook mode, and the steps needed to reproduce a problem.

Run the two Python test suites and the release checker from the README. Hardware
changes additionally need a build and device validation by someone who owns the
hardware; document exactly what was tested. Keep approval callbacks fast and
observer-only, preserve request IDs, and never turn a display action into consent.

The reviewed release manifest is a snapshot. For a maintained change, inspect
each added/changed file, update its SHA-256/size entry in `release-manifest.json`,
and rerun the checker. Do not include generated private configuration to make an
installation easier. If updating the upstream base, regenerate the integration
patch against that public revision and test applying it to a fresh checkout.

Retain Apache and third-party notices. New sounds need clear redistribution
rights and source attribution. This project has no official affiliation with
Seeed Studio, Nous Research, or Kenney.
