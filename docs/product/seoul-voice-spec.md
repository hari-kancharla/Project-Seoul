# Seoul Voice Specification

Status: Current compile and runtime evidence, including the unverified
hardware/audio boundary, is maintained in the product readiness report.

The voice layer gives Seoul one conversational session shared by realtime voice,
typed text, and Canvas interaction. The product target is GPT-Live style
full-duplex voice; until the announced GPT-Live API surface is generally
available, the production bridge mints short-lived sessions for the official
realtime API model in
`native/seoul/browser/product/realtime_voice_agent.*`.

The older `native/seoul/browser/voice/` state machine remains the typed turn
model for transcript, interruption, and task-state semantics, but the Canvas
microphone button does not drive platform dictation. It creates one realtime
voice session, streams microphone audio with WebRTC, and routes browser work
through a single function tool named `seoul_browser_task`.

The voice module stores bounded transcript segments only; there is no audio
buffer type anywhere in it, so raw audio cannot be persisted by construction.

## Primary realtime agent

`RealtimeVoiceAgent` is the product voice entrypoint. It reads the API key from
the injected credential store account `voice_realtime`, calls
`/v1/realtime/client_secrets` through the injected HTTP transport, and returns
only the short-lived client secret to Canvas. The standard API key never crosses
the Mojo boundary.

The generated session uses one model constant (`gpt-realtime-2.1`) and records
the product target (`gpt-live-1`) separately. There is no silent fallback loop:
if the credential is missing or the session response is invalid, the request
fails visibly and no browser action is started.

The realtime instructions bias the model toward short, interruptible speech and
tool use for browser work. Research, page understanding, comparisons, weather,
markets, products, maps, sports, and similar data-backed questions are not
hardcoded categories in the UI. The model requests a browser task with a natural
language `goal`; the native planner and SAUI visual layer decide the right
capabilities and surface shape from that goal and verified data.

Risky mutations still go through Seoul's task approval and receipt path. Voice
may ask for the task, but native execution remains typed, window-bound, and
auditable.

Canvas follows the provider's Realtime event lifecycle rather than displaying a
generic active state: microphone request, listening, hearing, thinking,
speaking, browser-task work, and failure are distinct visible states. Completed
function calls are accepted from the canonical `response.done` output as well
as incremental events, and call ids are de-duplicated before native execution.
Verified task completion, failure, cancellation, pause, and approval state can
flow back into the conversation; page identity is refreshed after navigation
and is explicitly labeled untrusted metadata.

Session setup is cancellable. Canvas accepts only the exact official HTTPS
Realtime endpoint, rejects redirects and oversized SDP/provider events, and
stops microphone tracks, WebRTC, audio, and pending network work together.
Provider and data-channel failures are visible rather than collapsing silently
to an idle microphone button.

## Session states

`native/seoul/browser/voice/voice_types.h` defines `VoiceSessionState` with 14
values: `kIdle`, `kMicrophoneRequesting`, `kListening`, `kPartialTranscript`,
`kFinalizingTranscript`, `kUnderstanding`, `kPlanning`, `kAwaitingConfirmation`,
`kExecuting`, `kSpeaking`, `kInterrupted`, `kPaused`, `kCancelled`, and
`kFailed`. `kCancelled` and `kFailed` are terminal (`IsTerminal`).

## Transition rules

`native/seoul/browser/voice/voice_session.cc` validates every transition against
an explicit table. Each turn method returns `kIllegalTransition` and leaves the
state unchanged when called in a state that does not accept it. The main path is:

- `RequestStart` (idle only) moves to microphone-requesting; a null STT provider
  fails immediately with `kProviderUnavailable`.
- `OnMicrophoneGranted` moves to listening; `OnMicrophoneDenied` fails with
  `kMicrophoneDenied`.
- `OnPartialTranscript` appends a partial hypothesis (replacing the previous
  partial for the same utterance) and enters partial-transcript.
- `OnSilenceDetected` or `StopCapture` moves to finalizing.
- `OnFinalTranscript` requires non-empty text; an empty final is treated as
  `kMalformedProviderOutput` and the session fails visibly rather than acting on
  nothing. On success it enters understanding.
- `OnUnderstandingComplete` moves to planning; `OnPlanReady` moves to
  awaiting-confirmation or straight to executing depending on
  `requires_confirmation`; `OnConfirmation(false)` returns to idle.
- `OnExecutionFinished` speaks a summary (if TTS is present and output enabled)
  or returns to listening or idle by input mode; `OnSpeechFinished` does the
  same by input mode.

Terminal or idle sessions accept `ResetToIdle`; `Cancel` is illegal from a
terminal or idle state.

## Push-to-talk and conversation mode

`VoiceInputMode` is `kPushToTalk` (capture while held; manual `StopCapture`
finalizes; the turn ends to idle) or `kToggleConversation` (explicit on/off;
after speaking, the session returns to listening for the follow-up). The mode is
read at `OnExecutionFinished` and `OnSpeechFinished` to decide whether to return
to listening or idle.

## STT and TTS provider seams

`native/seoul/browser/voice/speech_providers.h` defines `SpeechToTextProvider`
and `TextToSpeechProvider`. Each exposes a `provider_name` and a `SpeechRoute`.
Implementations adapt one engine (a platform speech framework, an audited local
model runtime, or an official cloud API). Providers deliver transcripts and
speech events through typed callbacks only; the header states that raw audio
never crosses this boundary into Seoul state. Either provider may be null: a null
STT provider fails `RequestStart`; a null TTS provider skips the speaking state.

## Barge-in

`BargeIn` is valid only from speaking. It calls `StopImmediately` on the TTS
provider (the contract requires output to cease immediately, never at a phrase
boundary) and moves to interrupted. The session comment and code make clear that
task state and already-verified work are untouched: the session does not own the
task, and `active_task_id` is never cleared by interruption.
`ResumeListeningAfterInterrupt` returns to listening.

## Microphone policy

There is no always-on path: capture begins only after `RequestStart` leads
through microphone-requesting and an explicit `OnMicrophoneGranted`. Denial
fails the session. `VoiceSettings.persist_raw_audio` defaults to false, and the
type header notes there is deliberately no code path in this module that stores
audio. Transcript segments are bounded by `kMaxTranscriptLength` (8192) and
`kMaxTranscriptSegments` (200), with the oldest segment dropped past the cap.

## Local and cloud route visibility

`SpeechRoute` (`kLocal` or `kCloud`) is surfaced next to microphone state in the
typed voice model. For the product Canvas realtime path, route visibility comes
from `RealtimeVoiceAgentSnapshot`: the route is cloud, the configured model is
shown, and session creation errors are surfaced instead of pretending local
dictation is available.

## Spoken-reference resolution

`native/seoul/browser/voice/voice_reference_resolver.cc` resolves a spoken phrase
to exactly one `VisibleReferent` id supplied by the Canvas. A referent carries a
stable id (a SAUI component id or data selection key), a `label`, a `kind`
(singular noun class such as "result", "chart", "step", "tab"), a 1-based
`ordinal`, and `selected`/`focused` flags; it never carries screen coordinates.
Resolution is deterministic: deictic words ("this", "that", "it") resolve to the
selected item, then the focused item, then the only item; ordinal words ("second
result", "last one") resolve within an optional kind scope; otherwise an exact
label match wins, then a unique substring match. Failure is precise:
`kAmbiguousReference`, `kUnknownReference`, `kTooManyReferents`, or
`kTranscriptTooLong`.

## Typed text joins the same thread

`SubmitTypedInput` is valid whenever a new turn could begin by voice (idle,
listening, or interrupted). It appends a final segment with confidence 1.0 and
enters understanding directly, so typed and spoken input share one transcript and
one state machine.
