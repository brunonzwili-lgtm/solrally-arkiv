# Friction report

> Plain-language summary: these are the problems we hit building on Arkiv and how
> we worked around them. Each one is reproducible on Tiramisu testnet with
> `@arkiv-network/sdk` 0.8.1. See the sections below for details.

# Friction report

Specific, reproducible things that cost us time building on Arkiv. All reproduced
against Tiramisu testnet with `@arkiv-network/sdk` 0.8.1 / viem 2.57.7.

---

## 1. There is no way to query every entity

`select()` with no `where()` throws `InvalidPredicateError`:

```
A query needs at least one filter — the language has no spelling for
"match every entity". Add a where(...) predicate, or narrow by
ownedBy(...) / createdBy(...).
```

**Reproduce:** `publicClient.select({ key: true }).limit(5).fetch()`

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

**Reproduce:** load the SDK as a plain `<script type="module">` import with no
bundler at all.

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

**Reproduce:** poll `fetchLeaderboard()` in a loop and watch for failures.

A demo where judges click around is exactly the case that trips this.

**Ask:** either return rate-limit headers the client can read, or expose the
remaining quota on the client so an app can degrade instead of failing.

---

## 4. Entity expiry is easy to miss, and the API encourages late discovery

`expires` is a required argument on `createEntity()`, so you cannot forget it at
creation — you have to pick a value immediately. But there is no indication of
which lifetime is sensible for your use case, and nothing warns you later.

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

**How we overcame it:** set `-sPTHREAD_POOL_SIZE=16` in the linker flags.
This gives enough headroom for the game's audio/rendering threads plus the
Arkiv module's internal threads.

---

## 7. Widget precondition violations

STK's `ListWidget::clearColumns()` and `addColumn()` require the widget to be
added to the screen first. We called them in `loadedFromFile()`, which runs
before `add()`. This caused a "memory access out of bounds" crash when opening
the leaderboard.

**How we overcame it:** move the calls to `beforeAddingWidget()`, which runs
after `add()` but before `init()`.

---

## 8. Layout collapse to zero height

A `height="fit"` parent holding `height="fit"` rows had no definite height to
resolve against, so every row and textbox collapsed to zero width. The
`LayoutManager` logged "Statically sized widgets took all the place!!"

**How we overcame it:** change the form container to `proportion="1"` (taking
leftover space in the parent) while keeping rows at `height="fit"` (sizing to
their font). This is the stock STK pattern.

---

## 9. Unreliable ribbon event routing

The tab strip was a `<tabs>` ribbon (RibbonWidget with RIBBON_TABS type). The
event name arriving in `eventCallback` depended on the input propagation path
(mouse vs keyboard), so the Login/Register toggle was unreliable.

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