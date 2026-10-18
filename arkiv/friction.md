# Friction report

> Plain-language summary: these are the problems we hit building on Arkiv and how
> we worked around them. Each one is reproducible on Tiramisu testnet with
> `@arkiv-network/sdk` 0.8.1. See the sections below for details.

Specific, reproducible things that cost us time building on Arkiv. All reproduced
against Tiramisu testnet with `@arkiv-network/sdk` 0.8.1 / viem 2.57.7 / Node 22 / esbuild 0.28.2 / Emscripten 3.x.

---

## 1. There is no way to query every entity

`select()` with no `where()` throws `InvalidPredicateError`:

```
A query needs at least one filter — the language has no spelling for
"match every entity". Add a where(...) predicate, or narrow by
ownedBy(...) / createdBy(...).
```

**Expected:** A query with no filter returns all entities (or at least an empty
result set), so a developer can browse the database during development.

**Actual:** Throws `InvalidPredicateError` at `fetch()` time with the message:
"A query needs at least one filter — the language has no spelling for 'match every
entity'."

**Versions:** `@arkiv-network/sdk` 0.8.1, viem 2.57.7, Tiramisu testnet.

**Steps to reproduce:**
1. `npm install @arkiv-network/sdk viem`
2. Create a public client: `createPublicClient({ chain: tiramisu, transport: http() })`
3. Call `publicClient.select({ key: true }).limit(5).fetch()`
4. Observe the error at fetch time.

This is the correct design for a shared database — it stops a query scanning
every entity across every project — but the error only surfaces at `fetch()`
time rather than at build time, so it reads like a network fault instead of a
usage error.

**Workaround:** namespace on a `project` attribute from the first write and
filter on it forever. If you did not plan that attribute up front you cannot
recover the entities later.

**Ask:** could the builder throw when `.where()` is empty, rather than the query
running and failing?

---

## 2. The SDK needs a bundler configuration, and the docs only cover Vite

Arkiv's SDK loads a WASM dependency internally (`brotli-wasm`). The MetaMask guide
instructs Vite users to add:

```js
export default defineConfig({
  optimizeDeps: { exclude: ["brotli-wasm", "brotli-wasm/pkg.bundler/brotli_wasm_bg.wasm"] },
});
```

**Expected:** The SDK loads in any browser environment, including plain
`<script type="module">` imports, since it is published as an ES module.

**Actual:** The SDK internally loads a WASM dependency (`brotli-wasm`) that
requires bundler-specific configuration. Without Vite, the import fails with a
module resolution error.

**Versions:** `@arkiv-network/sdk` 0.8.1, viem 2.57.7, esbuild 0.28.2, Node 22.

**Steps to reproduce:**
1. Create an HTML file with `<script type="module">import { createPublicClient } from '@arkiv-network/sdk'</script>`
2. Serve it with no bundler (e.g. `python3 -m http.server`)
3. Open in browser — observe module resolution failure for `brotli-wasm`.

We hit this because the game is an Emscripten/vanilla-JS app, not a Vite app —
there is no `vite.config.js` to edit. We resolved it by pre-bundling with
esbuild:

```
npx esbuild browser.js --bundle --format=esm --outfile=arkiv.js \
  --loader:.wasm=binary
```

That produced a single 716KB ES module that loads with no bundler and no runtime
CDN dependency, which is what we wanted for a demo that must not break on
conference wifi.

**Ask:** a documented esbuild/rollup/webpack snippet next to the Vite one. The
WASM dependency is invisible until it fails.

---

## 3. Rate limits on the public RPC are undocumented in the SDK

The docs state the default RPC is "heavily rate limited and meant for testing"
and that you should get a Hub access key and append it to the RPC URL. Nothing
in the SDK surfaces the limit or the remaining quota — the only symptom is a
query that eventually rejects with an opaque transport error.

**Expected:** The SDK surfaces rate-limit status so an app can degrade
gracefully or retry with backoff.

**Actual:** No rate-limit headers or quota info is returned. The only symptom
is a query that eventually rejects with an opaque transport error.

**Versions:** `@arkiv-network/sdk` 0.8.1, Tiramisu testnet RPC.

**Steps to reproduce:**
1. Set up a public client pointed at the default Tiramisu RPC.
2. Call `fetch()` in a tight loop (e.g. every 100ms).
3. Observe opaque transport errors after a few requests.

A demo where judges click around is exactly the case that trips this.

**Ask:** either return rate-limit headers the client can read, or expose the
remaining quota on the client so an app can degrade instead of failing.

---

## 4. Entity expiry is easy to miss, and the API encourages late discovery

`expires` is a required argument on `createEntity()`, so you cannot forget it at
creation — you have to pick a value immediately. But there is no indication of
which lifetime is sensible for your use case, and nothing warns you later.

**Expected:** Clear guidance on choosing entity lifetimes, and a way to check
which entities are about to expire.

**Actual:** `expires` is required on `createEntity()` so you must pick a value
immediately, but there is no documentation on sensible values per use case and
no API to list expiring entities.

**Versions:** `@arkiv-network/sdk` 0.8.1, Tiramisu testnet.

**Steps to reproduce:**
1. Call `createEntity({ ..., expires: ExpirationTime.fromDays(90) })`.
2. Wait 90 days (or inspect the entity metadata) — the entity is gone.
3. There is no `getEntityCount()` or expiry-filtered query to warn you.

We nearly shipped a leaderboard where records silently vanish. The two-part fix
we used is `extendEntity()` on re-race plus a documented 90-day window.

**Ask:** a short "picking a lifetime" note, and ideally a `getEntityCount()`
style call that can be filtered by expiry so an app can check what is about to
lapse.

---

## 5. Write cost is not surfaced per operation

`createEntity()` returns `{ entityKey, txHash, expiresAt }` but not the gas
actually spent. We measured it by diffing balances before and after
(0.000108 GLM for one race result).

**Expected:** The mutation result includes gas cost so apps can budget writes.

**Actual:** `createEntity()` returns `{ entityKey, txHash, expiresAt }` but not
the gas actually spent. We measured it by diffing balances before and after
(0.000108 GLM for one race result).

**Versions:** `@arkiv-network/sdk` 0.8.1, Tiramisu testnet, viem 2.57.7.

**Steps to reproduce:**
1. Record wallet balance before `createEntity()`.
2. Create an entity.
3. Record balance after — the difference is the gas cost.
4. Note that the mutation result does not include this value.

That is cheap enough to ignore for a leaderboard, but an app doing per-frame or
per-tick writes cannot budget without instrumenting balances itself.

**Ask:** return the gas used on the mutation result.

---

## 6. Thread pool exhaustion from async WASM operations

The game runs on Emscripten pthreads. When the Arkiv module loaded
asynchronously via `import('/arkiv.js')`, its internal WASM operations
(`brotli-wasm`) tried to spawn threads and hit the default pool limit of 4,
causing a deadlock. The console reported "Tried to spawn a new thread, but the
thread pool is exhausted."

**Expected:** Async module loading does not exhaust the thread pool, or the
SDK documents its thread requirements.

**Actual:** The console reported "Tried to spawn a new thread, but the thread
pool is exhausted. This might result in a deadlock..." The Arkiv module's
internal WASM operations could not spawn threads.

**Versions:** Emscripten 3.x, `@arkiv-network/sdk` 0.8.1, brotli-wasm (bundled).

**Steps to reproduce:**
1. Build an Emscripten app with `-pthread` and default `PTHREAD_POOL_SIZE` (4).
2. Load the Arkiv SDK via `import('/arkiv.js')` at runtime.
3. Observe thread pool exhaustion in the browser console.

**How we overcame it:** set `-sPTHREAD_POOL_SIZE=16` in the linker flags.
This gives enough headroom for the game's audio/rendering threads plus the
Arkiv module's internal threads.

---

## 7. Widget precondition violations

STK's `ListWidget::clearColumns()` and `addColumn()` require the widget to be
added to the screen first. We called them in `loadedFromFile()`, which runs
before `add()`. This caused a "memory access out of bounds" crash when opening
the leaderboard.

**Expected:** Widget configuration methods can be called in `loadedFromFile()`
without crashing, or the precondition is enforced at build time.

**Actual:** Calling `clearColumns()` and `addColumn()` in `loadedFromFile()`
caused "memory access out of bounds" at runtime. The widget had not been added
to the screen yet.

**Versions:** SuperTuxKart WASM (Emscripten 3.x), STK GUI engine.

**Steps to reproduce:**
1. Create a Screen subclass.
2. In `loadedFromFile()`, call `getWidget<ListWidget>("list")->clearColumns()`.
3. Run the game and open the screen.
4. Observe "memory access out of bounds" in the console.

**How we overcame it:** move the calls to `beforeAddingWidget()`, which runs
after `add()` but before `init()`.

---

## 8. Layout collapse to zero height

A `height="fit"` parent holding `height="fit"` rows had no definite height to
resolve against, so every row and textbox collapsed to zero width. The
`LayoutManager` logged "Statically sized widgets took all the place!!"

**Expected:** A `height="fit"` parent resolves its children's heights
correctly, or the LayoutManager warns at build time.

**Actual:** `LayoutManager: Statically sized widgets took all the place!!` and
`Widget 'username' has a width of 0`. All textboxes collapsed to zero width.

**Versions:** SuperTuxKart WASM (Emscripten 3.x), STK LayoutManager.

**Steps to reproduce:**
1. Create a stkgui with a `<div layout="vertical-row" height="fit">` container.
2. Add rows with `height="fit"` and textboxes with `proportion="2"`.
3. Run the game — observe zero-width widgets and LayoutManager warnings.

**How we overcame it:** change the form container to `proportion="1"` (taking
leftover space in the parent) while keeping rows at `height="fit"` (sizing to
their font). This is the stock STK pattern.

---

## 9. Unreliable ribbon event routing

The tab strip was a `<tabs>` ribbon (RibbonWidget with RIBBON_TABS type). The
event name arriving in `eventCallback` depended on the input propagation path
(mouse vs keyboard), so the Login/Register toggle was unreliable.

**Expected:** A `<tabs>` ribbon reliably reports selection changes via
`eventCallback` for both mouse and keyboard input.

**Actual:** The event name arriving in `eventCallback` depended on the input
propagation path. Mouse clicks and keyboard navigation produced different
event names, so the Login/Register toggle was unreliable.

**Versions:** SuperTuxKart WASM (Emscripten 3.x), STK RibbonWidget.

**Steps to reproduce:**
1. Create a `<tabs>` ribbon in stkgui.
2. Click tabs with mouse — observe one event name.
3. Navigate tabs with keyboard — observe a different event name.
4. The toggle fails for one input method.

**How we overcame it:** replace the ribbon with plain `<button>` widgets whose
own names always arrive in `eventCallback` regardless of input method.

---

## 10. Stale data in the asset packer

The asset packer (`pack_assets.sh`) skips repacking if a manifest already
exists, so stkgui edits were not reflected in the tarball served to the
browser. The game loaded the old layout and the fix appeared not to work.

**How we overcame it:** delete the manifests and cached `data_low/` directories
before repacking. The packer then regenerates from the updated source files.

---

## 11. Cross-origin isolation requirements

Emscripten pthreads require COOP (`same-origin`) and COEP (`require-corp`)
headers. Any missing header breaks `SharedArrayBuffer` and the game fails to
start. This also constrains which CDNs can be used (they must serve
`Cross-Origin-Resource-Policy: cross-origin`).

**Expected:** Emscripten pthreads work with standard HTTP headers, or the
requirement is documented.

**Actual:** Without COOP (`same-origin`) and COEP (`require-corp`) headers,
`SharedArrayBuffer` is unavailable and the game fails to start with a
SharedArrayBuffer error.

**Versions:** Emscripten 3.x, Python SimpleHTTPServer, Vercel.

**Steps to reproduce:**
1. Serve the WASM game without COOP/COEP headers.
2. Open in browser — observe `SharedArrayBuffer is not defined` error.
3. Game fails to start.

**How we overcame it:** set both headers in `run_server.py` (local dev) and
`vercel.json` (production). Verified by checking `crossOriginIsolated` in the
browser console.

---

## What went well

Worth saying, because it is the reason this integration is small:

- The read path is genuinely free and walletless. A leaderboard that anyone can
  query without connecting anything is exactly the property we wanted, and it
  needed no special support.
- Typed attribute predicates caught our own mistakes at build time rather than
  in production.
- `@arkiv-network/sdk` is a thin layer over viem, so every viem pattern we
  already knew applied unchanged. The browser guide's
  `custom(window.ethereum)` transport is the whole wallet story.
- There was no contract to write, no deployment step and no migration to plan.
  The entire write path worked on the first attempt against a funded key.