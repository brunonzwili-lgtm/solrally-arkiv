# Arkiv schema

Every record Sol Rally writes is one Arkiv **entity**. Arkiv stores entities in a
shared database, so every entity we create is tagged with `project = "solrally"` —
without that tag our reads would return other builders' data.

## TL;DR

Sol Rally stores two kinds of records in Arkiv:

- **Accounts** (nickname + wallet) — one per player, lasts 3650 days.
- **Race results** (track, kart, lap time, player) — one per submitted lap, expires after 90 days.

Nothing else goes to Arkiv. No passwords, no keys, no game assets, no progress.

**Why Arkiv?** Public reads (no gas), typed queries, built-in expiry, wallet identity,
no backend to host. Without it, we'd need a server, auth system, and trust model.

**What would break without Arkiv?** The leaderboard would need a hosted backend,
race records could be tampered with, and there'd be no public audit trail.

---

## Entity: `race_result`

Written by `submitRaceResult()` in `arkiv/browser.js`.

**Payload** — `contentType: application/json`, decoded with `entity.toJson()`:

```json
{
  "player":      "0xF7Ec2E24A4226A7D5a0F689B6387dAf3b2E5f692",
  "nickname":    "duuuh",
  "displayName": "duuuh",
  "track":       "Sol Dunes",
  "trackId":     "sol-dunes",
  "kart":        "racer",
  "mode":        "Race",
  "position":    1,
  "lapTimeMs":   92340,
  "finishedAt":  "2026-10-15T13:03:08.985Z"
}
```

`player` is always written, so a record stays attributable even if the player
later changes or clears their nickname. The leaderboard renders
`nickname → truncated address → "anon"`, in that order, so a row is never
nameless just because no nickname was set.

**Attributes** — typed, indexed, and the only fields we filter or sort on.
The payload is deliberately free-form; anything we want to *query* must be an
attribute.

| Attribute | Type | Example | Why it is an attribute |
| --- | --- | --- | --- |
| `project` | `str` | `"solrally"` | Namespace. Required — Arkiv has one shared DB. |
| `type` | `str` | `"race_result"` | Lets us add entity kinds later without breaking the filter. |
| `track` | `str` | `"Sol Dunes"` | Leaders filter by track. Human name, not the id. |
| `kart` | `str` | `"racer"` | Leaderboard grouping. |
| `position` | `i32` | `1` | Sorted numerically in the UI. |
| `lapTimeMs` | `i32` | `92340` | Lets the leaderboard sort by time server-side. |
| `player` | `str` | `"0xf7ec…f692"` | Owner, lowercased. Attribution survives a rename. |
| `nickname` | `str` | `"duuuh"` | Display name at the time of the race. |
| `position` | `i32` | `1` | Sorted numerically in the UI. |

**Lifetime** — `ExpirationTime.fromDays(90)`. Entities are not permanent; a race
result is refreshed with `extendEntity()` if the player races again before it
lapses. A leaderboard entry disappearing after 90 days is intended, not a bug:
old records should not accumulate forever.

> Note: `extendEntity()` is described in Arkiv's docs but is **not yet called**
> anywhere in the code. Race results currently lapse after 90 days without being
> refreshed. Worth wiring up before relying on them.

## Entity: `account`

Written by `registerAccount()` in `arkiv/browser.js`. This is the player's
identity — the nickname is the login name, so there is no separate user name.

**Payload** — `application/json`, decoded with `entity.toJson()`:

```json
{
  "nickname": "duuuh",
  "wallet":   "0xF7Ec2E24A4226A7D5a0F689B6387dAf3b2E5f692"
}
```

**Attributes**

| Attribute | Type | Example | Why it is an attribute |
| --- | --- | --- | --- |
| `project` | `str` | `"solrally"` | Namespace. |
| `type` | `str` | `"account"` | Separates accounts from race records. |
| `nickname` | `str` | `"duuuh"` | The login identity, so login is one indexed lookup. |
| `wallet` | `str` | `"0xf7ec…f692"` | Proves ownership; lowercased for comparison. |

**Lifetime** — `ExpirationTime.fromDays(3650)`. Deliberately long: this is
identity, not a race record, and an account silently expiring would be a bug.

**Register vs login asymmetry** — this is the important part:

| Action | Needs a wallet | Why |
| --- | --- | --- |
| Register | **Yes** | Writing an entity requires a signature. |
| Login | **Yes** | The account is looked up by wallet address. |

A player needs their wallet on every machine. There is no password to remember
or to leak.

## Authentication — wallet-based, no password

Accounts are identified by the connected wallet. There is no password and no
secret of any kind stored in Arkiv — only a nickname and a wallet address.

- **Register** needs a connected wallet: the write is signed by the wallet,
  and the wallet address is recorded as the owner. The nickname must be free.
- **Login** needs a connected wallet too: the account is looked up by wallet
  address, so there is nothing to type and nothing to brute-force.
- **No password, no hash, no salt.** Nothing derived from a secret is ever
  written, which keeps this build compliant with the challenge rule that no
  secrets (plaintext or hashed) may be stored in Arkiv.

The wallet is the identity. A player who loses their wallet loses their
account — that is the trade-off, and it is acceptable for a game where the
worst case is losing a nickname.

## Reads

**Leaderboard** — filter on `project` + `type` only. Note there is deliberately
no `ownedBy` filter: the leaderboard is one flat, fully public table. That is
what makes it judge-friendly, and it is also why the password limitation above
exists.

```js
publicClient
  .select({ key: true, payload: true, attributes: true, createdAt: true })
  .where(eq("project", str("solrally")), eq("type", str("race_result")))
  .limit(30)
  .fetch();
```

**Login lookup** — one indexed hit on `nickname`, no scan:

```js
publicClient
  .select({ payload: true, attributes: true, createdAt: true })
  .where(eq("project", str("solrally")),
         eq("type", str("account")),
         eq("nickname", str(nickname)))
  .limit(5)
  .fetch();
```

Arkiv documents no ordering guarantee, and nothing deletes the previous row when
a nickname is re-registered, so `findNewest()` sorts by `createdAt` client-side
and takes the first. Do not trust result order.

Reads use the **public client**: no wallet, no key, no gas. The leaderboard is
therefore readable by anyone, including a judge who has never connected a
wallet.

> `friction.md` notes the default RPC is heavily rate limited with no quota
> surfaced. The leaderboard polls every 5s and each login costs one more query,
> so a room full of judges clicking around is the case that will trip it.

## Clients

| Direction | Client | Transport | Needs |
| --- | --- | --- | --- |
| Read | `createPublicClient` | `http()` | nothing |
| Write | `createWalletClient` | `custom(window.ethereum)` | connected wallet with test GLM |

## Network

| Property | Value |
| --- | --- |
| Network | Tiramisu testnet |
| Chain ID | `7738577` |
| HTTP RPC | `https://rpc.tiramisu.db-chain.testnet.arkiv.network` |
| Explorer | https://tiramisu.explorer.arkiv.network |
| SDK | `@arkiv-network/sdk` 0.8.1 |

## Addresses and transactions

| What | Value |
| --- | --- |
| Creating wallet (test only) | `0xF7Ec2E24A4226A7D5a0F689B6387dAf3b2E5f692` |
| Proof write | `0x336cd6d2c3e142a1eb7961112c8680a2e0ba7a7c05e47a0bed96365e1139f297` |
| Entity created | `0xb772b8e630a48fd59a5dc35d10570c68d7c4834c87f41b1f6e14005cef1389b8` |

Player-facing writes use the **player's own** connected wallet, so a racer owns
their own records. The address above is the development key used to produce the
proof transaction.

## What is deliberately not on Arkiv

- **No private keys, ever.** Not in a payload, not in an attribute, not
  hashed. This is the hard rule from the challenge and it still holds.
- **No passwords at all.** Identity is the wallet address. An `account`
  entity stores only a nickname and a wallet address — no secret, no hash.
  This entry used to read "no personal data, not even hashed", and that is
  true again: there is nothing derived from a secret in the store.
- **No email addresses, real names, or anything else that identifies a person
  in the world.** A nickname the player chose for a kart game is the whole of
  it. If we ever wanted to store anything more identifying, that would be the
  line.
- **No asset or geometry.** Hundreds of megabytes of track and kart data would
  burn write gas for data nothing needs to verify. Those stay in the game's own
  asset packs.
- **No escrow or prize accounting.** Sol Rally's payout program is not part of
  this integration.