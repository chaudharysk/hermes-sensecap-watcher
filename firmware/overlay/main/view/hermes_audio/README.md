# Sound assets

Source: [Kenney Interface Sounds 1.0](https://kenney.nl/assets/interface-sounds).
License: CC0-1.0; original notice is in `LICENSE.txt`. Included OGGs are original
selected sources. WAVs are 16-bit mono PCM at 16 kHz and 45% source amplitude.

| Cue | Original |
| --- | --- |
| Start | open_002.ogg |
| Search | select_002.ogg |
| Done | confirmation_002.ogg |
| Error (double) | error_004.ogg |
| Permission (double) | question_002.ogg |
| Choice | question_004.ogg |
| Offline | close_002.ogg |

Double cues use a 90 ms gap. Total embedded WAV data: 83,300 bytes. The original
OGGs and documentation are not embedded in firmware. `manifest.json` records
the original download archive hash (the archive itself is not redistributed)
and generated WAV hashes. `tools/prepare_audio.py` in the release root can
regenerate WAVs with `imageio-ffmpeg`; firmware builds do not run that tool.
