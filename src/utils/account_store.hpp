//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2009-2015 Marianne Gagnon
//  Copyright (C) 2004-2015 SuperTuxKart-Team
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

#ifndef HEADER_ACCOUNT_STORE_HPP
#define HEADER_ACCOUNT_STORE_HPP

#include <string>

/** \brief Player accounts, stored as Arkiv entities.
 *
 *  The nickname is the identity: you log in with nickname + password. There
 *  is no separate user name, so whatever the player types is what appears on
 *  the leaderboard and in high scores.
 *
 *  Register writes an entity and therefore needs a connected wallet -- that
 *  signature is what stops someone claiming a nickname they do not own. Login
 *  only reads, and reads on Arkiv are free, so it needs no wallet.
 *
 *  \warning Arkiv reads are public, so the salted password hash in the stored
 *  payload can be read and brute-forced by anyone. That makes this demo-grade
 *  auth: it stops casual impersonation, not a determined attacker. Fixing it
 *  properly means verifying the hash server-side, or dropping the password in
 *  favour of a wallet signature challenge.
 *
 *  Requests are asynchronous. Call registerAccount()/login(), then poll
 *  pollResult() each frame.
 */
class AccountStore
{
public:
    /** Outcome of a register or login request. */
    enum Result
    {
        RESULT_OK = 0,
        RESULT_IN_FLIGHT,           /**< nothing settled yet */
        RESULT_NO_SUCH_USER,
        RESULT_BAD_PASSWORD,
        RESULT_NAME_TAKEN,
        RESULT_BAD_NAME,            /**< nickname shorter than 3 characters */
        RESULT_NO_WALLET,           /**< register needs a connected wallet */
        RESULT_WALLET_REGISTERED,   /**< this wallet already has an account */
        RESULT_UNSUPPORTED          /**< Arkiv module or Web Crypto missing */
    };

    /** True if accounts can be used at all in this build. */
    static bool available();

    /** Create an account for \a nickname. Requires a connected wallet,
     *  since Arkiv writes are signed. */
    static void registerAccount(const std::string &nickname);

    /** Verify credentials and, on success, establish the session. No wallet
     *  needed. */
    static void login();

    /** True once a request has settled, in which case \a out receives the
     *  outcome. Consumes the result. */
    static bool pollResult(Result &out);

    /** Human-readable form of \a r, for showing in the UI. */
    static std::string describe(Result r);

    /** True if a session is currently established on this device. */
    static bool isSignedIn();

    /** Nickname of the signed-in account, or "" if signed out. */
    static std::string currentNickname();

    /** Drop the local session. The stored account is kept. */
    static void signOut();
};   // AccountStore

#endif