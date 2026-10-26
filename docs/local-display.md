# Local image display (TRMNL X)

This fork is based on upstream `usetrmnl/trmnl-firmware` tag **v1.8.17**
(`9acd348270a493ca5f1c627ac91a9469543267ae`). It adds an optional image client,
not an application renderer. Any compatible server can provide images: the
firmware contains no task model, fonts, list layout, server address, token or
device identifier. Upstream cloud functionality remains available.

## Build and configure

1. Clone into a path **without spaces** (an ESP-IDF toolchain requirement),
   install PlatformIO Core (tested locally with 6.1.18), and build:
   ```sh
   pio run -e TRMNL_X_local
   ```
   The pinned X platform requires Python 3.11 or newer. See the upstream build
   instructions for toolchain prerequisites. This environment inherits the
   production X pin map, PSRAM and partition configuration. Other environments
   do not enable the feature or patch their FastEPD dependency.
2. Back up the device before flashing. Flash using the upstream procedure for
   your hardware; never flash a different board's binaries. This code has been
   exercised on TRMNL X hardware; other models are unsupported by this feature.
3. Enter the existing Wi-Fi setup AP and open
   `http://4.3.2.1/local-display`. This settings page is served only during setup,
   not as a permanent listener on your home network.
4. Enable local display and enter the complete polling endpoint, for example
   `http://display.example:8080/display/next`. These are illustrative values;
   replace the hostname and path with your server's. No query string, fragment,
   embedded credentials or trailing credentials are accepted. Enter an optional
   bearer token (up to 256 printable characters without spaces).
5. Save, configure Wi-Fi on the main setup page if needed, then restart with
   USB power connected. Settings are stored in a single NVS record in the
   `local-display` namespace. They survive restart and firmware updates that
   preserve NVS. The default is disabled, with no endpoint or token.

A blank token field preserves the stored token. Select **Remove saved token**
to erase it. Changing server origins requires a new token or explicitly clearing
the old one. Saved tokens are never returned in the form or logged as headers.
Configuration is not encrypted at rest; use physical control of the device and
setup AP. HTTP exposes images and tokens to the network; HTTPS uses the existing
certificate validation and needs a working NTP clock and a trusted certificate.

When disabled, normal firmware flow runs. When enabled, USB power keeps Wi-Fi
awake for live updates. Battery operation performs one poll without a long wait per
wake and sleeps for five minutes. The last frame ID and any pending drawing
acknowledgement survive deep sleep in RTC memory. Unchanged frames do not redraw;
a changed frame after sleep uses a full clear. Network failure retains the panel
contents. Local sessions suppress cloud log submission before sleeping.

A server may return `{ "changed": false, "displayMode": "normal" }` to resume
the stock cloud playlist. Omission or `"local"` selects local images. The last
selection is remembered for touchbar wake handling. Normal mode preserves stock
cached-playlist navigation; local mode treats single taps as local refreshes.
The reserved corner-reset action is preserved. A server-side mode change applies
on the next wake; the middle button can wake the device sooner. The standalone
setup checkbox still disables the client completely.

Local polling runs before the stock Wi-Fi error path, so a failed local connection
does not replace the retained image with a cloud/error screen. Settings and
credentials remain separate from upstream Wi-Fi/cloud settings.

## HTTP protocol

All requests are GET. An optional `Authorization: Bearer …` header is sent only
to the configured origin (scheme, hostname and port), including after redirects.
The firmware adds `wait=8&after=<last-displayed-id>&power=usb` (battery uses `wait=0` and `power=battery`) to the configured endpoint.
The server may long-poll for up to eight seconds. Requests have 12-second read
timeouts; successful iterations pause three seconds. Failures retain the last
image and back off from four seconds to at most 60 seconds while checking USB.
The normal upstream network transport is used, including the X's 5 GHz modem.

No new image:

```json
{"changed": false}
```

A new image (metadata must be smaller than 2048 bytes):

```json
{
  "changed": true,
  "id": "0123456789abcdef0123456789abcdef",
  "image": "/images/0123456789abcdef0123456789abcdef.png",
  "ack": "/display/ack?id=0123456789abcdef0123456789abcdef",
  "width": 1872,
  "height": 1404,
  "forceFull": false,
  "partialPasses": 8
}
```

- `id`: exactly 32 lowercase hex characters. Change it whenever image content
  or requested refresh settings change. Return the current frame after restart,
  when `after` is empty. It is a frame identifier, never a device identifier.
- `image`: origin-relative path starting with one `/`; absolute URLs,
  protocol-relative URLs, fragments, backslashes and control characters are
  rejected. Query strings are allowed. Return HTTP 200 with a PNG body.
- Image: non-interlaced **1-bit grayscale PNG**, less than 750,000 bytes, exactly
  the connected panel's width and height (1872 × 1404 for X). Decode errors and
  incompatible formats are rejected. The server controls all text and layout.
- `forceFull`: omitted defaults to `true`. Explicit `false` opts into partial
  updates where permitted by the cleanup policy.
- `partialPasses`: 6, 8 or 10; omitted defaults to **8**. Higher values trade speed
  for stronger clearing. These are waveform drive passes, not eight separate
  image downloads.
- `ack`: optional origin-relative path under the same validation as `image`.
  After drawing, the client requests it with `mode=full` or `mode=partial` appended
  (and `passes=8`, for example, only for a partial update). Return HTTP 200 with a
  nonempty body, such as `{ "ok": true }`. Make acknowledgement GETs idempotent.
  Failed acknowledgements are retried, but may be superseded by a newer frame;
  acknowledgement failure does not prevent image polling.

Use a server-issued bearer token on a trusted network. Do not put secrets in
paths or query strings because upstream HTTP diagnostics can log URLs.

## Partial refresh and limitations

The first changed image in an awake session always uses a full clear. An unchanged frame after battery sleep is retained without drawing. Later partial updates cover the range
of changed rows, and identical decoded images do not drive the panel again.
A full clear is required after three partial images, after ten minutes on the
next changed image, after a drawing failure, or when the server asks for one.
There is no timer-driven clear when content stays unchanged.

FastEPD commit `9a84c35e52849f9f22d2a7ad5231ef501be49e04` uses alternating DMA
row buffers. Its skipped-row loop neutralizes only one buffer, potentially
replaying stale drive on unchanged rows. The build hook changes that condition
to neutralize both. It checks the entire original file's SHA-256 before applying
the patch, accepts the already-patched version, and refuses unknown driver
versions. Review this patch when updating FastEPD. The vendor dependency itself
is not copied into this repository.

The row-buffer fix and eight-pass partial updates were tested on X hardware.
Ghosting still depends on panel, temperature and content. Runtime provisioning
and the generalized protocol compile and have host regression checks; they
have not yet been flashed and validated together on hardware. Existing hardware
tests used an earlier build of the same rendering path. Use full refresh if a
panel retains traces of previous content.

## Verification

```sh
python3 tools/test_local_display.py
pio test -e native -f test_http_utils
pio run -e TRMNL_X_local
```

The standalone runner needs Python 3 and a C++11 compiler, with no Python packages
or application dependencies. It checks configuration/path/token validation, PNG
constraints, row bounds, full-clear cadence, retry bounds and clock wraparound.
It compiles the actual pinned driver row loop into a DMA capture harness: the
unpatched version must reproduce stale drive; the patched version must keep
both buffers neutral, with both scan directions. The fixture retains FastEPD's
Apache-2.0 license. The upstream HTTP tests cover exact origin matching and
lookalike-host redirects.

Do not commit device flash dumps, NVS, `.env` files, private config headers,
generated binaries or build directories. Rebuild from source for each release.
