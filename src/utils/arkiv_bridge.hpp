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

#ifndef HEADER_ARKIV_BRIDGE_HPP
#define HEADER_ARKIV_BRIDGE_HPP

#include <string>

/** \brief The only place that talks to arkiv/browser.js.
 *
 *  Everything the game knows about Arkiv -- the ES module import, the
 *  wallet, the leaderboard query, the nickname -- lives behind this class so
 *  the rest of the codebase never has to remember two rules:
 *
 *  - **JS is async.** Nothing here blocks. Calls that want data back either
 *    publish it and you poll with the matching pollX(), or they set a status
 *    line you drain with pollStatus().
 *  - **A string crossing the boundary is an address, not a string.** EM_ASM
 *    substitutes $0 as an integer pointer. It has to be decoded on the JS
 *    side with solrallyArkiv.readCString(). Getting this wrong silently
 *    submits a raw pointer where a track name should be.
 *
 *  On a native build every method is a no-op returning false, so call sites
 *  need no #ifdef of their own.
 */
class ArkivBridge
{
public:
    /** Start the dynamic import of the ES module if nothing has tried yet.
     *  Cheap and idempotent; safe to call every frame. */
    static void ensureModule();

    /** True once the module has registered globalThis.solrallyArkiv. */
    static bool moduleReady();

    /** Ask the injected wallet for an account. Result arrives asynchronously
     *  as a status line from pollStatus(). */
    static void connectWallet();

    /** True if JS produced a status line since the last call, in which case
     *  \a out receives it. Consumes the message. */
    static bool pollStatus(std::string &out);

    /** Request the leaderboard and, if it has landed, hand back the
     *  newline-separated rows in \a out. Returns false while the query is
     *  still in flight, so the caller can try again next frame. */
    static bool pollLeaderboard(std::string &out);

    /** Remember a race the player just finished so it can be submitted later
     *  from the leaderboard screen. Called by World, not by the UI. */
    static void publishRaceResult(const std::string &track,
                                  const std::string &track_id,
                                  const std::string &kart,
                                  const std::string &player,
                                  float                lap_time_ms);

    /** True once a race has been published and not yet submitted. */
    static bool hasRaceResult();

    /** Human-readable track name of the published race, or "" if there is
     *  none. Used for the "you raced X" line on the leaderboard screen. */
    static std::string raceTrack();

    /** Drop the published race without submitting it. */
    static void clearRaceResult();

    /** Write the published race to Arkiv. Needs a connected wallet; failure
     *  is reported as a status line. */
    static void submitRaceResult();

    /** Wallet address JS is currently holding, or "". */
    static std::string connectedAccount();

    /** Set the nickname JS writes onto records. Empty clears it. */
    static void setNickname(const std::string &nickname);

};   // ArkivBridge

#endif