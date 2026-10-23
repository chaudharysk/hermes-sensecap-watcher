# Publish the prepared source to GitHub

Publish **this folder only** (`hermes-watcher-public`), or the source ZIP produced
by its packaging tool. The original working firmware checkout is not the public
release and has not been approved for upload.

Suggested repository name: `hermes-sensecap-watcher`.

Project repository: https://github.com/chaudharysk/hermes-sensecap-watcher
The instructions below also apply when publishing your own reviewed fork.

Suggested description:

> Animated Hermes Agent companion for SenseCAP Watcher, with MQTT task states,
> Telegram permission alerts, short sound cues, and a setup tutorial.

Suggested topics: `hermes-agent`, `sensecap-watcher`, `esp32-s3`, `lvgl`, `mqtt`,
`telegram`, `embedded`.

## Local review

From the prepared folder:

```bash
python -B tools/check_release.py
python -B -m unittest discover -s laptop/tests -v
python -B -m unittest discover -s tests -v
```

These checks do not build/flash firmware or run a real broker. Read the
[privacy notes](PRIVACY.md). If you edit the prepared sources, review the changes
and update the file hashes in `release-manifest.json` before packaging; do not
silence the check by copying in private files.

## Upload with Git

Create an empty repository on GitHub. Choose a public repository only when ready
to publish, and do not pre-create a README/license because this package includes
them. Use your preferred public Git identity (for example GitHub's no-reply email).

In this prepared folder, not the original firmware folder:

```bash
git init -b main
git add .
git diff --cached --name-only
git diff --cached
```

After reviewing the staged files:

```bash
git commit -m "Initial Hermes Watcher source and tutorial"
git remote add origin https://github.com/YOUR_ACCOUNT/hermes-sensecap-watcher.git
git push -u origin main
```

Replace `YOUR_ACCOUNT` with your GitHub account. Never put an access token into
the remote URL. Source preparation and review are separate from publication;
these commands publish the folder when you choose to run them.

## Optional source ZIP

```bash
python -B tools/package_release.py
```

This packages only files listed in the reviewed manifest, adds no Git metadata,
and writes `dist/hermes-watcher-public.zip`. It refuses to overwrite an existing
ZIP. Extracted hidden files such as `.gitignore` and `.github/` belong in the
repository too. Do not upload a ZIP containing your whole development drive.

## First release notes

Describe the animated face, status publisher, permission observer, and sound
cues. Link the tutorial and compatibility limits. State that the original device
setup worked for its owner, but the sanitized public setup still needs a fresh
hardware build/test. Do not advertise universal hardware/firmware compatibility
or claim that mock tests validated physical playback or Telegram delivery.
