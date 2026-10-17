//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2004-2015  Steve Baker <sjbaker1@airmail.net>,
//  Copyright (C) 2004-2015  SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
//

#include "utils/arkiv_bridge.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Static so the buffers outlive the EM_ASM call; Emscripten has no closure
// capture to hang a std::string off. JS writes UTF-8 into these through
// HEAPU8, and we read them back as NUL-terminated C strings.
static char s_line_buf[32768] = {0};
static char s_status_buf[2048] = {0};
static char s_nick_buf[1024] = {0};
static char s_addr_buf[1024] = {0};

// The race waiting to be submitted. Held here rather than in JS because it is
// produced by the race and consumed by a screen the player may not open for
// several minutes; a module reload must not lose it.
static std::string s_race_track;
static std::string s_race_track_id;
static std::string s_race_kart;
static std::string s_race_player;
static float        s_race_lap_ms = 0.0f;
static bool         s_race_valid  = false;

// ---------------------------------------------------------------------------
void ArkivBridge::ensureModule()
{
#ifdef __EMSCRIPTEN__
    // The bundle is a module, so it lands on globalThis asynchronously. The
    // loading flag keeps this from firing a fresh import() every frame.
    EM_ASM({
        if (!globalThis.solrallyArkiv && !globalThis.__solrallyArkivLoading) {
            globalThis.__solrallyArkivLoading = true;
            import('/arkiv.js').then(function (m) {
                globalThis.__solrallyArkivLoading = false;
            }).catch(function (e) {
                globalThis.__solrallyArkivLoading = false;
                globalThis.__solrallyArkivError = String(e);
            });
        }
    });
#endif
}   // ensureModule

// ---------------------------------------------------------------------------
bool ArkivBridge::moduleReady()
{
#ifdef __EMSCRIPTEN__
    return (bool)EM_ASM_INT({ return globalThis.solrallyArkiv ? 1 : 0; });
#else
    return false;
#endif
}   // moduleReady

// ---------------------------------------------------------------------------
void ArkivBridge::connectWallet()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        globalThis.__solrallyStatus = 'Waiting for wallet...';
        globalThis.__solrallyStatusReady = 1;
        globalThis.solrallyArkiv.connectWallet().then(function (addr) {
            globalThis.__solrallyWallet = addr;
            globalThis.__solrallyStatus = 'Wallet connected: ' + addr;
            globalThis.__solrallyStatusReady = 1;
        }).catch(function (e) {
            globalThis.__solrallyStatus = 'Wallet error: ' + String(e).slice(0, 90);
            globalThis.__solrallyStatusReady = 1;
        });
    });
#endif
}   // connectWallet

// ---------------------------------------------------------------------------
bool ArkivBridge::pollStatus(std::string &out)
{
#ifdef __EMSCRIPTEN__
    if (!(bool)EM_ASM_INT({ return globalThis.__solrallyStatusReady ? 1 : 0; }))
        return false;

    EM_ASM({
        const buf = $0;
        const msg = globalThis.__solrallyStatus || '';
        const enc = new TextEncoder();
        for (let i = 0; i < 2048; i++) HEAPU8[buf + i] = 0;
        HEAPU8.set(enc.encode(msg), buf);
        globalThis.__solrallyStatusReady = 0;
        globalThis.__solrallyStatus = '';
    }, (int)s_status_buf);

    out = std::string(s_status_buf);
    return true;
#else
    (void)out;
    return false;
#endif
}   // pollStatus

// ---------------------------------------------------------------------------
bool ArkivBridge::pollLeaderboard(std::string &out)
{
#ifdef __EMSCRIPTEN__
    // Issue a query only when none is in flight. Without this guard every
    // poll would stack another RPC round trip on the same leaderboard.
    EM_ASM({
        if (globalThis.__solrallyLinesPending) return;
        globalThis.__solrallyLinesPending = 1;
        const buf = $0;
        const put = function (txt) {
            globalThis.__solrallyLinesPending = 0;
            const enc = new TextEncoder();
            for (let i = 0; i < 32768; i++) HEAPU8[buf + i] = 0;
            HEAPU8.set(enc.encode(txt), buf);
            globalThis.__solrallyLinesReady = 1;
        };
        if (!globalThis.solrallyArkiv) { put('__ERROR__ Arkiv module not loaded'); return; }
        globalThis.solrallyArkiv.leaderboardLines().then(put).catch(function (e) { put('__ERROR__ ' + String(e).slice(0, 120)); });
    }, (int)s_line_buf);

    if (!(bool)EM_ASM_INT({ return globalThis.__solrallyLinesReady ? 1 : 0; }))
        return false;   // still in flight; try again next poll

    EM_ASM({ globalThis.__solrallyLinesReady = 0; });
    out = std::string(s_line_buf);
    return true;
#else
    (void)out;
    return false;
#endif
}   // pollLeaderboard

// ---------------------------------------------------------------------------
void ArkivBridge::publishRaceResult(const std::string &track,
                                    const std::string &track_id,
                                    const std::string &kart,
                                    const std::string &player,
                                    float                lap_time_ms)
{
    s_race_track    = track;
    s_race_track_id = track_id;
    s_race_kart     = kart;
    s_race_player   = player;
    s_race_lap_ms   = lap_time_ms;
    s_race_valid    = true;
}   // publishRaceResult

// ---------------------------------------------------------------------------
bool ArkivBridge::hasRaceResult()
{
    return s_race_valid;
}   // hasRaceResult

// ---------------------------------------------------------------------------
std::string ArkivBridge::raceTrack()
{
    return s_race_track;
}   // raceTrack

// ---------------------------------------------------------------------------
void ArkivBridge::clearRaceResult()
{
    s_race_valid = false;
}   // clearRaceResult

// ---------------------------------------------------------------------------
void ArkivBridge::submitRaceResult()
{
#ifdef __EMSCRIPTEN__
    // NB: every comma below sits inside a call's parentheses. EM_ASM takes
    // its code as a single macro parameter stringized with #, and braces do
    // not protect commas from the preprocessor -- only parentheses do. So
    // the result object has to be a call argument, never a `const x = {...}`.
    EM_ASM({
        const api = globalThis.solrallyArkiv;
        if (!api) {
            globalThis.__solrallyStatus = 'Arkiv module not loaded';
            globalThis.__solrallyStatusReady = 1;
            return;
        }
        api.submitRaceResult({
            trackId: $2,
            track: api.readCString($0),
            kart: api.readCString($1),
            mode: 'Race',
            position: 1,
            lapTimeMs: $3,
            finishedAt: new Date().toISOString()
        }).then(function (r) {
            globalThis.__solrallyStatus = 'Recorded on Arkiv. Tx ' + String(r.txHash).slice(0, 18) + '...';
            globalThis.__solrallyStatusReady = 1;
        }).catch(function (e) {
            globalThis.__solrallyStatus = 'Submit failed: ' + String(e).slice(0, 90);
            globalThis.__solrallyStatusReady = 1;
        });
    }, (int)s_race_track.c_str(), (int)s_race_kart.c_str(),
       (int)s_race_track_id.c_str(), (double)s_race_lap_ms);
#endif
}   // submitRaceResult

// ---------------------------------------------------------------------------
std::string ArkivBridge::connectedAccount()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const buf = $0;
        const api = globalThis.solrallyArkiv;
        const addr = api ? (api.getAccount() || '') : '';
        const enc = new TextEncoder();
        for (let i = 0; i < 1024; i++) HEAPU8[buf + i] = 0;
        HEAPU8.set(enc.encode(addr), buf);
    }, (int)s_addr_buf);

    return std::string(s_addr_buf);
#else
    return std::string();
#endif
}   // connectedAccount

// ---------------------------------------------------------------------------
void ArkivBridge::setNickname(const std::string &nickname)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const api = globalThis.solrallyArkiv;
        if (!api) return;
        api.setNickname(api.readCString($0));
    }, (int)nickname.c_str());
#endif
}   // setNickname
