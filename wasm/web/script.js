import pako from "https://cdn.jsdelivr.net/npm/pako@2.1.0/+esm";

let db = null;
let db_name = "stk_db";
let store_name = "stk_store";
let idbfs_mount = null;
// Bump whenever the packed assets change: the browser caches the unpacked
// tarball in IndexedDB under this version, so without a bump a stale copy
// keeps being reused and asset edits never appear.
let data_version = 50;

let start_button = document.getElementById("start_button");
let status_text = document.getElementById("status_text");
let info_container = document.getElementById("info_container");
let quality_select = document.getElementById("quality_select");

let syncing_fs = false;
let config = {};

function load_db() {
  if (db) return db;
  return new Promise((resolve, reject) => {
    let request = indexedDB.open(db_name, 1);
    request.onerror = (event) => {
      reject(event);
    };
    request.onsuccess = (event) => {
      db = event.target.result;
      resolve(db);
    };
    request.onupgradeneeded = (event) => {
      let db = event.target.result;
      db.createObjectStore(store_name);
    };
  });
}

function request_async(request) {
  return new Promise((resolve, reject) => {
    request.onerror = (event) => {
      reject(event);
    };
    request.onsuccess = () => {
      resolve(request.result);
    };
  });
}

async function delete_db() {
  if (db) {
    db.close();
    db = null;
  }
  await request_async(indexedDB.deleteDatabase(db_name));
}

async function load_store() {
  await load_db();
  let transaction = db.transaction(store_name, "readwrite");
  let store = transaction.objectStore(store_name);
  return store;
}

async function check_db(key) {
  let store = await load_store();
  return await request_async(store.count(key));
}

async function read_db(key) {
  let store = await load_store();
  return await request_async(store.get(key));
}

async function write_db(key, data) {
  let store = await load_store();
  return await request_async(store.put(data, key));
}

async function read_db_chunks(key) {
  let {size, chunk_count} = await read_db(key);
  let offset = 0;
  let array = new Uint8Array(size);
  
  for (let i = 0; i < chunk_count; i++) {
    let chunk_array = await read_db(key + "." + i);
    array.set(chunk_array, offset);
    offset += chunk_array.length;
  }

  return array;
}

async function write_db_chunks(key, data) {
  let size = data.length;
  let chunk_size = 20_000_000;
  let chunk_count = Math.ceil(size / chunk_size);

  let offset = 0;
  for (let i = 0; i < chunk_count; i++) {
    let chunk_array = data.slice(offset, offset + chunk_size);
    await write_db(key + "." + i, chunk_array);
    offset += chunk_size;
  }

  await write_db(key, {size, chunk_count});
}

async function download_chunks(url) {
  let path = url.split("/");
  let filename = path.pop();
  let base_url = path.join("/");

  status_text.textContent = `Downloading manifest for ${filename}...`;
  // Cache-bust the manifest: a CDN-cached copy from a previous pack would
  // declare the wrong byte count and fail the download.
  let r1 = await fetch(url + ".manifest?t=" + Date.now(), { cache: "no-store" });
  let manifest = (await r1.text()).split("\n");
  let size = parseInt(manifest.shift());
  manifest.pop();

  let offset = 0;
  let chunk = null;
  let array = new Uint8Array(size);
  let chunk_count = manifest.length;
  let current_chunk = 1;

  while (chunk = manifest.shift()) {
    let mb_progress = Math.floor(offset / (1024 ** 2))
    let mb_total = Math.floor(size / (1024 ** 2))
    status_text.textContent = `Downloading ${filename}... (chunk ${current_chunk}/${chunk_count}, ${mb_progress}/${mb_total}MiB)`;

    // Version chunk URLs so a new pack never collides with cached chunks
    // from a previous pack (same filenames, different bytes).
    let r2 = await fetch(base_url + "/" + chunk + "?v=" + data_version);
    if (!r2.ok)
      throw new Error(`Failed to download ${chunk}: HTTP ${r2.status}`);
    let buffer = await r2.arrayBuffer();
    let chunk_array = new Uint8Array(buffer);
    if (offset + chunk_array.length > size)
      throw new Error(`${chunk} overruns the size declared in the manifest`);
    array.set(chunk_array, offset);
    offset += chunk_array.length;  
    current_chunk++;
  }

  if (offset !== size)
    throw new Error(`Incomplete download: got ${offset} of ${size} bytes`);

  return array.buffer;
}

async function extract_tar(url, fs_path, use_cache = false) {
  //download tar file from server and decompress
  let decompressed;
  let filename = url.split("/").pop();
  console.log("extract_tar: start", filename);
  if (!use_cache || !await check_db(url)) {
    let compressed = await download_chunks(url);
    status_text.textContent = `Decompressing ${filename}...`;
    decompressed = pako.inflate(compressed);
    compressed = null;
    if (use_cache) {
      status_text.textContent = `Saving ${filename} to the cache...`;
      await write_db_chunks(url, decompressed);
    }
  }
  else {
    decompressed = await read_db_chunks(url);
  }
  console.log("extract_tar: decompressed, size =", decompressed.length);

  //read the tar file and add to emscripten's fs
  status_text.textContent = `Extracting ${filename}...`;
  let files = untar(decompressed);
  decompressed = null;
  let dirs = 0, written = 0;
  for (let file of files) {
    let relative_path = file.name.substring(1);
    let out_path = fs_path + relative_path;
    if (file.type === "5" || out_path.endsWith("/")) {
      dirs++;
      try {
        FS.mkdir(out_path);
      }
      catch {}
    }
    else {
      FS.writeFile(out_path, new Uint8Array(file.buffer));
      written++;
    }
    if (written % 2000 === 0 && written > 0)
      status_text.textContent = `Extracting files... (${written} done)`;
  }
  status_text.textContent = `Extracted ${written} files and ${dirs} directories`;
  console.log("extract_tar: done, files =", written, "dirs =", dirs);
}

function untar(data) {
  // Minimal tar reader for the ustar/pax archives produced by pack_assets.sh.
  // js-untar was unusably slow on these archives and choked on pax headers.
  let view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let base = data.byteOffset;

  function read_str(pos, len) {
    let end = pos;
    let limit = pos + len;
    while (end < limit && view.getUint8(end) !== 0) end++;
    return String.fromCharCode.apply(null, new Uint8Array(data.buffer, base + pos, end - pos));
  }

  function read_octal(pos, len) {
    let s = read_str(pos, len).replace(/\0/g, "").trim();
    if (!s) return 0;
    let v = parseInt(s, 8);
    return Number.isNaN(v) ? 0 : v;
  }

  let files = [];
  let pos = 0;
  let pax_path = null;
  let global_path = null;

  while (pos + 512 <= data.byteLength) {
    if (view.getUint32(pos, true) === 0) break;  // end-of-archive marker

    let name = read_str(pos, 100);
    let size = read_octal(pos + 124, 12);
    let type = read_str(pos + 156, 1) || "0";
    let prefix = read_str(pos + 345, 155);

    if (type === "x" || type === "g") {
      // extended header: records are "<len> <key>=<value>\n"
      let p = pos + 512;
      let end = pos + 512 + size;
      let rec_path = null;
      while (p + 1 < end) {
        let sp = p;
        while (sp < end && view.getUint8(sp) !== 32) sp++;
        let rec_len = parseInt(read_str(p, sp - p), 10);
        if (!rec_len || Number.isNaN(rec_len)) break;
        let kv = read_str(sp + 1, p + rec_len - (sp + 1)).replace(/\n$/, "");
        let eq = kv.indexOf("=");
        if (eq > 0 && kv.slice(0, eq) === "path") rec_path = kv.slice(eq + 1);
        p += rec_len;
      }
      if (type === "x") pax_path = rec_path;
      else global_path = rec_path;
    }
    else {
      let full = pax_path || global_path || (prefix ? prefix + "/" + name : name);
      files.push({
        name: full,
        type: type,
        buffer: type === "0" ? data.buffer.slice(base + pos + 512, base + pos + 512 + size) : new ArrayBuffer(0)
      });
    }

    pos += 512 + Math.ceil(size / 512) * 512;
  }

  return files;
}

async function load_data() {
  //check if we need to update the assets bundle
  if (!await check_db("/version") || !(await read_db("/version") == data_version)) {
    await delete_db();
    await write_db("/version", data_version);
  }

  let quality = quality_select.value;
  let data_url = `game/data_${quality}.tar.gz`;
  console.log("load_data: quality =", quality, "url =", data_url);
  await extract_tar(data_url, "/data", true);
  console.log("load_data: extraction complete");
}

async function load_idbfs() {
  idbfs_mount = FS.mount(IDBFS, {}, "/home").mount;
  await sync_idbfs(true);
}

function sync_idbfs(populate = false) {
  if (syncing_fs) return;
  syncing_fs = true;
  return new Promise((resolve, reject) => {
    idbfs_mount.type.syncfs(idbfs_mount, populate, (err) => {
      syncing_fs = false;
      if (err) reject(err);
      else resolve();
    });
  })
}

function wait_for_frame() {
  return new Promise((resolve) => {requestAnimationFrame(resolve)});
}

function set_websocket_url(url) {
  if (typeof Module.websocket === "undefined") 
    Module.websocket = {};
  if (typeof SOCKFS.websocketArgs !== "undefined") 
    SOCKFS.websocketArgs.url = url;
  Module.websocket.url = url;
}

async function load_config() {
  let response = await fetch("config.json");
  config = await response.json();
  globalThis.config = config;
}

async function main() {
  globalThis.ready = true;
  await load_config();
  await load_idbfs();
  if (config.ws_enabled) {
    set_websocket_url(config.ws_proxy);
  }

  start_button.onclick = start_game;
  start_button.disabled = false;
  status_text.innerText = "";
}

async function start_game() {
  status_text.textContent = "Loading game files...";
  start_button.disabled = true;
  try {
    await load_data();
  }
  catch (e) {
    status_text.textContent = `Failed to load game files: ${e.message || e}`;
    start_button.disabled = false;
    throw e;
  }
  await wait_for_frame();
  status_text.textContent = "Launching game...";

  await wait_for_frame();
  // Surface async runtime failures on the page: run() succeeding does not
  // mean the game survived init (abort/exception happens a frame later and
  // would otherwise leave a bare black canvas with no message).
  install_runtime_error_hooks();
  try {
    const canvas = document.getElementById("canvas");
    console.log("Canvas CSS size:", canvas.clientWidth, "x", canvas.clientHeight);
    console.log("Canvas drawing buffer:", canvas.width, "x", canvas.height);
    run();
  } catch (e) {
    console.error("run() failed:", e);
    show_fatal(`Game launch failed: ${e.message || e}`);
    start_button.disabled = false;
    throw e;
  }
  info_container.style.zIndex = 0;
  info_container.style.display = "none";
  sync_idbfs();

  console.warn("Warning: Opening devtools may harm the game's performance.");
}

let runtime_hooks_installed = false;
function install_runtime_error_hooks() {
  if (runtime_hooks_installed) return;
  runtime_hooks_installed = true;
  const orig_printErr = Module.printErr;
  Module.printErr = function (text) {
    if (orig_printErr) orig_printErr(text);
    else if (console.warn) console.warn(text);
    record_runtime_log(String(text));
  };
  Module.onAbort = function (msg) {
    console.error("Game aborted:", msg);
    show_fatal(`Game aborted: ${msg || "see console (Info level) for details"}\n${recent_runtime_log()}`);
    start_button.disabled = false;
  };
  window.addEventListener("error", (e) => {
    if (!e.filename || e.filename.includes("supertuxkart") || e.filename.includes("script.js")) {
      console.error("window.onerror:", e.message);
      record_runtime_log("window.onerror: " + e.message);
    }
  });
}

let runtime_log_lines = [];
function record_runtime_log(text) {
  runtime_log_lines.push(text);
  if (runtime_log_lines.length > 20) runtime_log_lines.shift();
}
function recent_runtime_log() {
  return runtime_log_lines.slice(-10).join("\n");
}
function show_fatal(msg) {
  status_text.textContent = msg;
  // Re-show the overlay so the message is visible instead of a black canvas.
  info_container.style.display = "";
  info_container.style.zIndex = 2;
  start_button.disabled = false;
}

Module["canvas"] = document.getElementById("canvas")

// Bust stale caches on the wasm binary.
// An old cached supertuxkart.wasm paired with the current supertuxkart.js
// aborts with "No EM_ASM constant found at address ...". Versioning the URL
// guarantees a matching pair no matter what any CDN or browser cached.
Module["locateFile"] = (path) =>
  path.endsWith(".wasm") ? path + "?v=" + data_version : path;
globalThis.main = main;
globalThis.sync_idbfs = sync_idbfs;
globalThis.load_idbfs = load_idbfs;

let poll_runtime_interval = setInterval(() => {
  if (globalThis.ready) {
    main();
    clearInterval(poll_runtime_interval);
  }
}, 100);