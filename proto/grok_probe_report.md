# Grok.com probe

## Result
- Same Mozilla profile cookies are present for `.grok.com` and `grok.com`.
- Direct `curl` to `https://grok.com/` is reset by the remote host.
- Headless Firefox with the same profile loads `https://grok.com/`, but the
  page body is Cloudflare security verification, not the Grok app:
  - Title: `Grok`
  - Body: `Performing security verification`
  - `Ray ID: a5bfb1e77ec1cf4c`
- `localStorage` is empty and no Grok API resources are observed.

## Difference vs DeepSeek
- DeepSeek: `cookie + userToken` is enough for direct API calls and DOM access.
- Grok: cookie alone is not enough; Cloudflare challenge blocks the app.
- Grok therefore needs the external PoW/challenge helper boundary before the
  forest scraper can reach conversation data.

## Next step
- Keep Grok auth/challenge handling in `dpsk_external.h`.
- OCR remains deferred; PoW/uinput can be tested as the first external path.
