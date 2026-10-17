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

#include "utils/account_store.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static char s_nick_buf[256] = {0};

// ---------------------------------------------------------------------------
bool AccountStore::available()
{
#ifdef __EMSCRIPTEN__
    return (bool)EM_ASM_INT({
        return (globalThis.solrallyArkiv && globalThis.crypto &&
                globalThis.crypto.subtle) ? 1 : 0;
    });
#else
    return false;
#endif
}   // available

// ---------------------------------------------------------------------------
void AccountStore::registerAccount(const std::string &nickname)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const api = globalThis.solrallyArkiv;
        const settle = function (r) {
            globalThis.__acctResult = r.ok ? 0 : (
                r.reason === 'name_taken' ? 4 :
                r.reason === 'bad_name' ? 5 :
                r.reason === 'wallet_registered' ? 9 :
                r.reason === 'no_wallet' ? 7 :
                r.reason === 'no_crypto' ? 8 : 8);
            globalThis.__acctReady = 1;
        };
        if (!api) { globalThis.__acctResult = 8; globalThis.__acctReady = 1; return; }
        api.registerAccount(api.readCString($0))
            .then(settle)
            .catch(function (e) { globalThis.__acctResult = 8; globalThis.__acctReady = 1; });
    }, (int)nickname.c_str());
#endif
}   // registerAccount

// ---------------------------------------------------------------------------
void AccountStore::login()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const api = globalThis.solrallyArkiv;
        const settle = function (r) {
            globalThis.__acctResult = r.ok ? 0 : (
                r.reason === 'no_user' ? 2 :
                r.reason === 'no_wallet' ? 7 : 8);
            globalThis.__acctReady = 1;
        };
        if (!api) { globalThis.__acctResult = 8; globalThis.__acctReady = 1; return; }
        api.loginAccount()
            .then(settle)
            .catch(function (e) { globalThis.__acctResult = 8; globalThis.__acctReady = 1; });
    });
#endif
}   // login

// ---------------------------------------------------------------------------
bool AccountStore::pollResult(Result &out)
{
#ifdef __EMSCRIPTEN__
    if (!(bool)EM_ASM_INT({ return globalThis.__acctReady ? 1 : 0; }))
        return false;

    out = (Result)EM_ASM_INT({
        const code = globalThis.__acctResult;
        globalThis.__acctReady = 0;
        return code;
    });
    return true;
#else
    (void)out;
    return false;
#endif
}   // pollResult

// ---------------------------------------------------------------------------
std::string AccountStore::describe(Result r)
{
    switch (r)
    {
        case RESULT_OK:                  return "Signed in.";
        case RESULT_IN_FLIGHT:           return "";
        case RESULT_NO_SUCH_USER:        return "No account with that nickname.";
        case RESULT_BAD_PASSWORD:       return "Wrong password.";
        case RESULT_NAME_TAKEN:          return "That nickname is already taken.";
        case RESULT_BAD_NAME:            return "Nickname must be at least 2 characters.";
        case RESULT_WALLET_REGISTERED:   return "This wallet already has an account. Log in instead.";
        case RESULT_NO_WALLET:           return "Connect a wallet first.";
        case RESULT_UNSUPPORTED:         return "Arkiv unavailable. Check your connection.";
    }
    return "";
}   // describe

// ---------------------------------------------------------------------------
bool AccountStore::isSignedIn()
{
#ifdef __EMSCRIPTEN__
    return (bool)EM_ASM_INT({
        const api = globalThis.solrallyArkiv;
        return (api && api.isSignedIn()) ? 1 : 0;
    });
#else
    return false;
#endif
}   // isSignedIn

// ---------------------------------------------------------------------------
std::string AccountStore::currentNickname()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const buf = $0;
        const api = globalThis.solrallyArkiv;
        const nick = api ? (api.sessionNickname() || '') : '';
        const enc = new TextEncoder();
        for (let i = 0; i < 256; i++) HEAPU8[buf + i] = 0;
        HEAPU8.set(enc.encode(nick), buf);
    }, (int)s_nick_buf);

    return std::string(s_nick_buf);
#else
    return std::string();
#endif
}   // currentNickname

// ---------------------------------------------------------------------------
void AccountStore::signOut()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const api = globalThis.solrallyArkiv;
        if (api) api.signOut();
    });
#endif
}   // signOut