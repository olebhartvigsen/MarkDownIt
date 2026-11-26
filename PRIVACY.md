# Privacy

MarkDownIt opens files on your machine and renders them. Nothing about a
document leaves the machine: not the path, not the title, not the text, not the
size. The app works with no network at all.

## What is sent

At launch the app sends **one** event to Aptabase, an open-source analytics
service, on the EU region:

| Field | Example | Why |
|---|---|---|
| `eventName` | `app_start` | that the app started |
| `timestamp` | `2026-11-26T10:15:30.123Z` | when, in UTC |
| `sessionId` | 32 random hex characters | to tell one launch from another |
| `osName` | `Windows` or `macOS` | which build you run |
| `osVersion` | `10.0.22631` | which OS version |
| `appVersion` | `0.2.1` | which release |
| `sdkVersion` | `markdownit-cpp@1` | which client library, a constant the service requires |
| `locale` | `da-DK` | which language the system is in |
| `isDebug` | `false` | whether this is a development build |

That is the whole payload. There is no account, no device id, no MAC address,
no hardware fingerprint, and no identifier that survives a restart: the session
id is drawn from the OS entropy source on every launch and thrown away when the
app quits.

## Why

The counts answer one question that downloads cannot: whether anyone actually
opens the app, and on which platform. Nothing in them can be used to identify a
person.

## Turning it off

The switch lives on the surface each platform already has for view options:

- **Windows:** the **Usage statistics** toggle in the **View** group of the
  ribbon.
- **macOS:** **Usage Statistics** in the **View** menu, and the **Send
  anonymous usage statistics** checkbox in the settings window.

Turning either one off stops all sending immediately and permanently for that
machine. Nothing is queued and nothing is retried.

The preference is stored with the rest of the app's settings: the registry on
Windows, `~/Library/Application Support/MarkDownIt/settings.json` on macOS.

## What the service sees

The request goes to `eu.aptabase.com`, which receives the payload above plus
whatever any HTTP request carries: the source IP address of your connection and
the standard request headers. Aptabase is self-hostable and its source is
public; its own privacy terms apply to the hosted service.

## Read the code

The payload is built in `src/analytics.cpp` and the one request is made through
`platform::TelemetrySink` (`src/platform/seams.h`), implemented in
`src/windows/telemetry_win.cpp` and `src/macos/telemetry_mac.mm`. The tests in
`tests/analytics_test.cpp` pin the payload, including a case that fails if a
document field ever appears in it.
