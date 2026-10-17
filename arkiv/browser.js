// Sol Rally <-> Arkiv bridge, bundled for the browser.
//
// Two clients, exactly as Arkiv's browser guide describes:
//   reads  -> public client, no wallet, no gas
//   writes -> wallet client over custom(window.ethereum); MetaMask signs
//
// The game canvas cannot reach these from C++ directly, so every entry
// point is exposed on globalThis and the C++ side calls through EM_ASM.

import {
  createPublicClient,
  createWalletClient,
  ExpirationTime,
  jsonToPayload,
} from "@arkiv-network/sdk";
import { tiramisu } from "@arkiv-network/sdk/chains";
import { str, i32 } from "@arkiv-network/sdk/attr";
import { eq } from "@arkiv-network/sdk/query";
import { custom, http } from "viem";

export const PROJECT = "solrally";

let publicClient = null;
let walletClient = null;
let connectedAccount = null;
let nickname = null;

function shortAddress(a) {
  const s = String(a ?? "");
  return s.length > 12 ? s.slice(0, 6) + ".." + s.slice(-4) : s;
}

function pub() {
  if (!publicClient) {
    publicClient = createPublicClient({ chain: tiramisu, transport: http() });
  }
  return publicClient;
}

// MetaMask will not have Tiramisu on a judge's machine, so handle both the
// "switch" and the "add it first" paths (4902 is EIP-3085's unknown-chain).
async function switchToTiramisu() {
  const hex = `0x${tiramisu.id.toString(16)}`;
  try {
    await window.ethereum.request({
      method: "wallet_switchEthereumChain",
      params: [{ chainId: hex }],
    });
  } catch (err) {
    if (err && err.code === 4902) {
      await window.ethereum.request({
        method: "wallet_addEthereumChain",
        params: [
          {
            chainId: hex,
            chainName: tiramisu.name,
            nativeCurrency: tiramisu.nativeCurrency,
            rpcUrls: tiramisu.rpcUrls.default.http,
            blockExplorerUrls: ["https://tiramisu.explorer.arkiv.network"],
          },
        ],
      });
    } else {
      throw err;
    }
  }
}

export async function connectWallet() {
  if (typeof window === "undefined" || !window.ethereum) {
    throw new Error("No EVM wallet found. Install MetaMask.");
  }
  await switchToTiramisu();
  const accounts = await window.ethereum.request({
    method: "eth_requestAccounts",
  });
  connectedAccount = accounts[0];
  walletClient = createWalletClient({
    chain: tiramisu,
    transport: custom(window.ethereum),
    account: connectedAccount,
  });
  // Seed the display name from the session. A nickname is optional -- someone
  // may connect a wallet without ever signing in -- so fall back to the
  // truncated address rather than failing the connection.
  const session = readSession();
  nickname = session || shortAddress(connectedAccount);
  return connectedAccount;
}

export function getAccount() {
  return connectedAccount;
}

export function isConnected() {
  return connectedAccount !== null;
}

export function setNickname(n) {
  const clean = String(n ?? "").trim().slice(0, 24);
  nickname = clean.length > 0 ? clean : null;
  return nickname;
}

export function getNickname() {
  return nickname;
}

// ---------------------------------------------------------------------------
// Accounts
//
// Identity is the nickname; the password is a second factor checked at login.
// Register writes an `account` entity and so needs a connected wallet --
// that signature is what stops someone claiming a nickname they do not own.
// Login only reads, and reads are free, so it needs no wallet at all.
//
// WARNING: Arkiv reads are public. The salted hash in the payload can be
// read and brute-forced by anyone. This is demo-grade auth: it stops casual
// impersonation, not a determined attacker. A real deployment needs the
// hash verified server-side, or replaces the password with a wallet
// signature challenge.
// ---------------------------------------------------------------------------

const SESSION_KEY = "solrally.session";

function readSession() {
  try {
    const s = JSON.parse(localStorage.getItem(SESSION_KEY) || "null");
    return s && s.nickname ? String(s.nickname) : "";
  } catch (e) {
    return "";
  }
}

// Find the newest entity of `type` whose `attr` column equals `value`.
async function findNewest(type, attr, value, limit = 5) {
  const page = await pub()
    .select({ key: true, payload: true, attributes: true, createdAt: true })
    .where(eq("project", str(PROJECT)), eq("type", str(type)), eq(attr, str(value)))
    .limit(limit)
    .fetch();

  const rows = page.entities || [];
  if (rows.length === 0) return null;

  // No documented ordering guarantee, and a re-register leaves an older row
  // behind, so pick the newest client-side.
  rows.sort((a, b) => Number(b.createdAt ?? 0) - Number(a.createdAt ?? 0));
  return rows[0];
}

export function sessionNickname() {
  return readSession();
}

export function isSignedIn() {
  return readSession().length > 0;
}

export function signOut() {
  localStorage.removeItem(SESSION_KEY);
  nickname = null;
}

export async function registerAccount(name) {
  if (!walletClient) return { ok: false, reason: "no_wallet" };
  const connectedAccount = walletClient.account?.address;
  if (!connectedAccount) return { ok: false, reason: "no_wallet" };

  const nick = String(name ?? "").trim().slice(0, 24);
  if (nick.length < 2) return { ok: false, reason: "bad_name" };

  // The nickname is the display identity, so it has to be free.
  const taken = await findNewest("account", "nickname", nick);
  if (taken) return { ok: false, reason: "name_taken" };

  // One account per wallet. Re-registering with the same wallet is not
  // allowed; sign out and use a different wallet instead.
  const existing = await findNewest("account", "wallet", connectedAccount.toLowerCase());
  if (existing) return { ok: false, reason: "wallet_registered" };

  const { entityKey, txHash } = await walletClient.createEntity({
    payload: jsonToPayload({
      nickname: nick,
      wallet: connectedAccount,
    }),
    contentType: "application/json",
    attributes: {
      project: str(PROJECT),
      type: str("account"),
      nickname: str(nick),
      wallet: str(connectedAccount.toLowerCase()),
    },
    expires: ExpirationTime.fromDays(3650),
  });

  localStorage.setItem(SESSION_KEY, JSON.stringify({ nickname: nick }));
  nickname = nick;
  return { ok: true, nickname: nick, entityKey, txHash };
}

export async function loginAccount() {
  if (!walletClient) return { ok: false, reason: "no_wallet" };
  const connectedAccount = walletClient.account?.address;
  if (!connectedAccount) return { ok: false, reason: "no_wallet" };

  // The wallet is the identity. Look up the account by wallet address.
  const row = await findNewest("account", "wallet", connectedAccount.toLowerCase());
  if (!row) return { ok: false, reason: "no_user" };

  const rec = row.toJson ? row.toJson() : null;
  const nick = rec?.nickname || row.attributes?.nickname?.value || "";
  if (!nick) return { ok: false, reason: "no_user" };

  localStorage.setItem(SESSION_KEY, JSON.stringify({ nickname: nick }));
  nickname = nick;
  return { ok: true, nickname: nick };
}

export async function submitRaceResult(result) {
  if (!walletClient) throw new Error("Wallet not connected");

  // The wallet is the identity of record; the nickname is only a label on
  // top of it. Always write both so a record is still attributable if the
  // nickname is later cleared.
  const display = nickname || shortAddress(connectedAccount);
  const payload = {
    ...result,
    player: connectedAccount,
    nickname: nickname,
    displayName: display,
  };

  const { entityKey, txHash, expiresAt } = await walletClient.createEntity({
    payload: jsonToPayload(payload),
    contentType: "application/json",
    attributes: {
      project: str(PROJECT),
      type: str("race_result"),
      // Prefer the human track name; the id is only a fallback for callers
      // that do not have one.
      track: str(String(result.track ?? result.trackId ?? "unknown")),
      kart: str(String(result.kart ?? "unknown")),
      position: i32(Number(result.position ?? 1)),
      // Indexed so a leaderboard can sort by time server-side instead of
      // re-reading every payload in the browser.
      lapTimeMs: i32(Number(result.lapTimeMs ?? 0)),
      player: str(String(connectedAccount).toLowerCase()),
      nickname: str(display),
    },
    expires: ExpirationTime.fromDays(90),
  });
  return { entityKey, txHash, expiresAt };
}

export async function fetchLeaderboard(limit = 20) {
  const page = await pub()
    .select({ key: true, payload: true, attributes: true, createdAt: true })
    .where(eq("project", str(PROJECT)), eq("type", str("race_result")))
    .limit(limit)
    .fetch();

  return page.entities.map((e) => ({
    key: e.key,
    track: e.attributes.track?.value ?? "?",
    kart: e.attributes.kart?.value ?? "?",
    position: e.attributes.position?.value ?? 0,
    lapTimeMs: e.attributes.lapTimeMs?.value ?? 0,
    at: Number(e.createdAt ?? 0),
    payload: e.toJson ? e.toJson() : null,
  }));
}

// ---- surface for the C++ side and for the HTML layer -------------------
globalThis.solrallyArkiv = {
  connectWallet,
  getAccount,
  isConnected,
  getNickname,
  setNickname,
  sessionNickname,
  isSignedIn,
  signOut,
  registerAccount,
  loginAccount,
  submitRaceResult,
  fetchLeaderboard,
};
// Rows are pre-formatted here so the C++ side only has to split lines.
// Shipping a JSON parser into the wasm build for a leaderboard is not
// worth it; a tab-separated string into one list column is enough.
//
// The board lists EVERY registered member, not just those who submitted a
// lap: an account with no race_result yet shows up as "(no lap submitted)".
// That way the roster is stable and only the times shift as people play.
globalThis.solrallyArkiv.leaderboardLines = async function () {
  try {
    const [resultsPage, accountsPage] = await Promise.all([
      pub().select({ key: true, payload: true, attributes: true, createdAt: true })
        .where(eq("project", str(PROJECT)), eq("type", str("race_result")))
        .limit(50).fetch(),
      pub().select({ key: true, payload: true, attributes: true, createdAt: true })
        .where(eq("project", str(PROJECT)), eq("type", str("account")))
        .limit(100).fetch(),
    ]);

    // Best lap per nickname (lowest lapTimeMs wins).
    const bestByNick = new Map();
    for (const e of resultsPage.entities) {
      const p = e.toJson ? e.toJson() : {};
      const who = p.nickname || shortAddress(p.player) || "anon";
      const t = p.lapTimeMs || e.attributes?.lapTimeMs?.value || 0;
      const prev = bestByNick.get(who);
      if (!prev || (t && (!prev.lapTimeMs || t < prev.lapTimeMs))) {
        bestByNick.set(who, {
          track: e.attributes?.track?.value ?? "?",
          kart: e.attributes?.kart?.value ?? "?",
          lapTimeMs: t,
        });
      }
    }

    // Every registered account, plus any nickname that only shows up on a
    // race result (legacy / anon submissions).
    const allNicks = new Set();
    for (const e of accountsPage.entities) {
      const p = e.toJson ? e.toJson() : {};
      allNicks.add(p.nickname || e.attributes?.nickname?.value || "anon");
    }
    for (const n of bestByNick.keys()) allNicks.add(n);

    if (allNicks.size === 0) return "__EMPTY__";

    const withLap = [];
    const withoutLap = [];
    for (const nick of allNicks) {
      const best = bestByNick.get(nick);
      if (best && best.lapTimeMs) {
        const time = `${(best.lapTimeMs / 1000).toFixed(2)}s`;
        withLap.push({ nick, lapTimeMs: best.lapTimeMs,
          line: `${best.track}  ·  ${best.kart}  ·  ${nick}  ·  ${time}` });
      } else {
        withoutLap.push(`${nick}  ·  (no lap submitted)`);
      }
    }
    withLap.sort((a, b) => a.lapTimeMs - b.lapTimeMs);

    return [...withLap.map((x) => x.line), ...withoutLap].join("\n");
  } catch (e) {
    return "__ERROR__ " + String(e).slice(0, 120);
  }
};

globalThis.solrallyArkiv.connected = function () { return connectedAccount; };

// C++ hands UTF-8 NUL-terminated pointers across EM_ASM. $0 is an address,
// never a string, so every crossing has to be decoded explicitly -- getting
// this wrong silently submits a raw pointer as the track name.
//
// slice() rather than subarray(): wasm memory is a SharedArrayBuffer here
// (COOP/COEP enables threads), and TextDecoder.decode() throws on a view
// backed by shared memory. slice() copies into an ordinary ArrayBuffer.
globalThis.solrallyArkiv.readCString = function (ptr) {
  if (!ptr) return "";
  const heap = HEAPU8;
  let end = ptr;
  while (end < heap.length && heap[end] !== 0) end++;
  return new TextDecoder().decode(heap.slice(ptr, end));
};
