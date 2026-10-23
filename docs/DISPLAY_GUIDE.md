# Display behavior and design decisions

This guide captures the useful requirements from the project's original build
conversation and checks them against the published source. Start with the
[installation tutorial](TUTORIAL.md) to set up your own device.

## A visible companion while Hermes works elsewhere

The original goal was a small physical character that shows what Hermes is doing
and catches your attention when it needs you. Hermes and its tools run on your
computer. The Watcher receives MQTT messages, animates the display, and plays
short cues. Continue giving tasks and answering questions in your usual Hermes
interface.

## The face and text

The design moved from a rectangular panel to a circular face. White facial
features sit on a translucent tinted background, with state-dependent accents
around the edge. Smaller eyes and a smiling idle expression keep it readable on
the round screen. A separate translucent card gives active task text enough
contrast against the native Watcher interface.

The face gently floats and blinks without needing a new task message. Expressions
and colors transition as states change; visible status cards slide and fade in.
In ordinary idle mode, the face alone communicates readiness: there is no large
`HERMES ONLINE` label. Offline/resting uses closed eyes and a drifting Z. An
explicit detail message can still appear in otherwise quiet states.

These are the current expressions, rather than every animation proposed during
early development:

| State received | What the display does |
| --- | --- |
| Online / idle | Smiles, floats, blinks, and occasionally glances sideways |
| Thinking | Uses a lavender accent, an upward gaze, and subtle eye movement |
| Searching | Uses a blue accent, side-to-side scanning eyes, and a faster moving ring |
| Working / terminal | Uses a teal accent, a focused expression, and moving activity indicators |
| Done | Shows happy curved eyes, a broader smile, and a short bounce |
| Error | Uses a pink/red accent, a frown, and a brief shake |
| Offline / resting | Closes its eyes and shows a drifting Z |
| Permission / question | Uses an amber accent and a pulsing attention indicator with a persistent card |

The renderer also supports listening, speaking, and waiting states. A supported
expression appears only when a sender emits the corresponding state. The bundled
gateway status hook sends online, thinking, searching, running, and done; it does
not automatically translate every tool error or voice event into a display state.
Its `DONE` means the agent's turn ended, not independent verification that the task
succeeded. See the [MQTT protocol](PROTOCOL.md) when adding another sender.

## Keep the normal Watcher usable

Preserving normal Watcher use was a core requirement. The face and card are
translucent, and the overlay objects do not consume touch input. Native controls
remain available, although an alert can cover part of the interface while it
needs attention. The original green native UI may still be visible underneath;
the integration does not replace the entire Watcher theme.

An early issue was the native standby screen appearing after inactivity. Moving
an animation does not itself count as touch activity. The current UI periodically
resets LVGL inactivity so its face stays visible while running. This also keeps
the screen awake during the sleeping expression and consumes more power than
allowing the native screensaver. A timed return to the screensaver is not
implemented by this package.

The integration does not supply replacement camera models or factory data. Check
native Watcher features after installation and preserve your device's own
provisioning. Follow the tutorial's **app-only flash** procedure.

## Two different reasons Hermes needs you

| What Hermes asks | Watcher card | Where you respond |
| --- | --- | --- |
| Permission to run an action, such as Allow Once / Session / Always / Deny | **APPROVAL NEEDED** | The original Hermes chat or terminal |
| A question with options or a typed answer | **WAITING FOR YOU** | The original Hermes chat or terminal |

These alerts observe real Hermes request events. A generic `THINKING` or
`WORKING` status cannot tell the Watcher that permission is pending. The status
hook and permission/question plugin are separate components, so both must be
installed for the full experience. Older supported approval hooks cover
permissions only; question alerts need the native human-input hooks.

Pending alerts take visual priority. A later online/done message cannot dismiss
them: a resolution must match the request ID. If the broker link drops during a
request, the card stays visible with an unconfirmed-link hint rather than
silently reporting that it was answered. A process crash can leave a stale
retained alert; check the real request in Hermes before clearing anything.

The Watcher never grants command permission or submits a choice. For a task
started from your phone, open its original chat there. CLI requests must be
answered in the CLI. The integration is not tied to Telegram; it depends on the
events exposed by your Hermes interface and version.

## Short sounds that respect native audio

Seven embedded cues cover starting/thinking, search, completion, error,
permission, question, and offline transitions. They are short Kenney CC0 assets,
with attribution and source files included in the repository. They do not need
to be downloaded by the Watcher during use.

Cues use the device volume and wait for native audio to be idle. They can expire
or be skipped while recording, speaking, or playing audio. Repeated identical
statuses and duplicate request IDs do not continuously replay the same sound.
The visible alert remains the primary signal when audio is busy.

## Lessons from the original setup

- **A working animation is only one part of the connection.** Status messages,
  notifier heartbeats, request events, and matching display acknowledgements
  establish different parts of the path. A Watcher reply alone does not prove
  the notifier is loaded.
- **Install into the running Hermes profile, then restart that process.** Flashing
  the Watcher cannot load a missing laptop plugin. Installing or running the
  diagnostic does not activate the notifier by itself.
- **Allow enough time to observe a request.** Hermes may think for longer than
  45 seconds before asking. Use `--seconds 600` for the diagnostic; that controls
  its observation window, not Hermes thinking time or the lifetime of an alert.
- **Treat native camera failures separately.** A model download/set error needs
  its actual native logs. Neither the message alone nor a recent UI change proves
  the cause. See [troubleshooting](TROUBLESHOOTING.md).
- **Share the integration, not the configured device.** Keep factory backups,
  credentials, local settings, and compiled private images outside the public
  repository. See [privacy notes](PRIVACY.md).

## Adjusting the appearance

In your patched firmware checkout, `main/view/view_hermes.c` contains the face
and card opacity, placement, palette, expressions, and motion. The current
defaults use an opacity of 156 for the face and 176 for the card on LVGL's
0–255 scale; lower values reveal more of the native interface. Keep the text
readable against your usual Watcher screen.

Sound selection and playback timing live in `main/app/app_hermes_sound.c`.
Rebuild and app-flash your own device after source changes. The original device
was reported working by its owner; see [validation notes](VALIDATION.md) for the
separate limits of public-package testing.
