//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2009-2015 Marianne Gagnon
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
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

// Manages includes common to all options screens
#include "states_screens/options/options_common.hpp"

#include "utils/account_store.hpp"
#include "utils/arkiv_bridge.hpp"
#include "utils/string_utils.hpp"

#include "addons/news_manager.hpp"
#include "audio/sfx_manager.hpp"
#include "challenges/unlock_manager.hpp"
#include "config/player_manager.hpp"
#include "graphics/central_settings.hpp"
#include "guiengine/screen_keyboard.hpp"
#include "guiengine/widgets/text_box_widget.hpp"
#include "online/link_helper.hpp"
#include "online/request_manager.hpp"
#include "states_screens/dialogs/message_dialog.hpp"
#include "states_screens/dialogs/kart_color_slider_dialog.hpp"
#include "states_screens/dialogs/recovery_dialog.hpp"
#include "states_screens/main_menu_screen.hpp"
#include "states_screens/online/register_screen.hpp"

using namespace GUIEngine;

// ----------------------------------------------------------------------------

BaseUserScreen::BaseUserScreen(const std::string &name) : Screen(name.c_str())
{
    m_online_cb           = NULL;
    m_new_registered_data = false;
    m_auto_login          = false;
}   // BaseUserScreen

// ----------------------------------------------------------------------------

void BaseUserScreen::loadedFromFile()
{
    // Offline-only build: the "online" checkbox was removed from the XML.
    // Null it and treat every online check as false (see uses below).
    m_online_cb = nullptr;
    m_username_tb = getWidget<TextBoxWidget >("username");
    assert(m_username_tb);
    m_players = getWidget<DynamicRibbonWidget>("players");
    assert(m_players);
    m_options_widget = getWidget<RibbonWidget>("options");
    assert(m_options_widget);
    m_info_widget = getWidget<LabelWidget>("message");
    assert(m_info_widget);

    m_wallet_label = getWidget<LabelWidget>("wallet_info");

}   // loadedFromFile

// ----------------------------------------------------------------------------
/** Stores information from the register screen. It allows this screen to 
 *  use the entered user name and password to prefill fields so that the user
 *  does not have to enter them again.
 *  \param online If the user created an online account.
 *  \param auto-login If the user should be automatically logged in online.
 *         This can not be done for newly created online accounts, since they
 *         need to be confirmed first.
 * \param online_name The online account name.
 *  \param password The password for the online account.
 */
void BaseUserScreen::setNewAccountData(bool online, bool auto_login,
                                       const core::stringw &online_name,
                                       const core::stringw &password)
{
    // Indicate for init that new user data is available.
    m_new_registered_data = true;
    m_auto_login          = auto_login;
    if (m_online_cb)
        m_online_cb->setState(online);
    m_username_tb->setText(online_name);
}   // setOnline

// ----------------------------------------------------------------------------
void BaseUserScreen::beforeAddingWidget()
{
}   // beforeAddingWidget

// ----------------------------------------------------------------------------
/** Initialises the user screen. Searches for all players to fill the 
 *  list of users with their icons, and initialises all widgets for the
 *  current user (e.g. the online flag etc).
 */
void BaseUserScreen::init()
{
    // The behaviour of the screen is slightly different at startup, i.e.
    // when it is the first screen: cancel will exit the game. There is no
    // floating back button on this layout any more -- Cancel/OK in the
    // buttonbar covers it -- so nothing to hide here.
    bool is_first_screen = StateManager::get()->getMenuStackSize()==1;
    getWidget<IconButtonWidget>("cancel")->setLabel(is_first_screen
                                                    ? _("Exit game")
                                                    : _("Cancel")      );

    m_sign_out_name = "";
    m_sign_in_name  = "";

    // It should always be activated ... but just in case
    m_options_widget->setActive(true);
    // Clean any error message still shown
    m_info_widget->setText("", true);
    m_info_widget->setErrorColor();

    Screen::init();

    // An empty roster would strand the player with no way forward, so seed a
    // local profile -- but with the signed-in nickname, or blank. There is
    // deliberately no placeholder name like "Player 1": whatever the player
    // types is what the game shows, and blank reads as "not set up yet"
    // rather than as a real name that never changes.
    if (PlayerManager::get()->getNumPlayers() == 0)
    {
        PlayerProfile* p = PlayerManager::get()->addNewPlayer(
            StringUtils::toWString(AccountStore::currentNickname()));
        if (p)
        {
            PlayerManager::get()->save();
            PlayerManager::get()->setCurrentPlayer(p);
        }
    }

    // A profile created before this existed still carries the old
    // placeholder, so an upgrade adopts the signed-in name on the way in.
    if (AccountStore::isSignedIn())
        applySignedInIdentity();

    m_players->clearItems();
    int current_player_index = -1;

    for (unsigned int n=0; n<PlayerManager::get()->getNumPlayers(); n++)
    {
        const PlayerProfile *player = PlayerManager::get()->getPlayer(n);
        if (player->isGuestAccount()) continue;
        std::string s = StringUtils::toString(n);
        m_players->addItem(player->getName(), s, player->getIconFilename(), 0,
                           IconButtonWidget::ICON_PATH_TYPE_ABSOLUTE);
        if(player == PlayerManager::getCurrentPlayer())
            current_player_index = n;
    }

    m_players->updateItemDisplay();

    // Select the current player. That can only be done after
    // updateItemDisplay is called.
    if (current_player_index != -1)
        selectUser(current_player_index);
    // no current player found
    // The first player is the most frequently used, so select it
    else if (PlayerManager::get()->getNumPlayers() > 0)
        selectUser(0);

    // Disable changing the user while in game
    bool in_game = StateManager::get()->getGameState() == GUIEngine::INGAME_MENU;
    getWidget<IconButtonWidget>("ok")->setActive(!in_game);

    m_new_registered_data = false;
    if (m_auto_login)
    {
        m_auto_login = false;
        login();
        return;
    }
    m_auto_login = false;
}   // init

// ----------------------------------------------------------------------------
PlayerProfile* BaseUserScreen::getSelectedPlayer()
{
    const std::string &s_id = m_players
                            ->getSelectionIDString(PLAYER_ID_GAME_MASTER);
    unsigned int n_id = 0;
    // fromString leaves n_id untouched when the ribbon has no selection, so
    // the out-param must be pre-initialised or getPlayer() gets garbage.
    if (!StringUtils::fromString(s_id, n_id))
    {
        PlayerProfile *current = PlayerManager::getCurrentPlayer();
        if (current != NULL)
            return current;
        if (PlayerManager::get()->getNumPlayers() > 0)
            return PlayerManager::get()->getPlayer(0);
        return NULL;
    }
    return PlayerManager::get()->getPlayer(n_id);
}   // getSelectedPlayer

// ----------------------------------------------------------------------------

void BaseUserScreen::tearDown()
{
    Screen::tearDown();
}   // tearDown

// ----------------------------------------------------------------------------

EventPropagation BaseUserScreen::filterActions(PlayerAction action,
    int deviceID,
    const unsigned int value,
    Input::InputType type,
    int playerId)
{
    if (action == PA_MENU_SELECT &&
        (!ScreenKeyboard::shouldUseScreenKeyboard() ||
        GUIEngine::ScreenKeyboard::hasSystemScreenKeyboard()))
    {
        if (m_username_tb != NULL && m_username_tb->isFocusedForPlayer(PLAYER_ID_GAME_MASTER))
        {
            login();
            return EVENT_BLOCK;
        }
    }

    return EVENT_LET;
}

// ----------------------------------------------------------------------------
/** Called when a user is selected. It updates the online checkbox and
 *  entry fields.
 */
void BaseUserScreen::selectUser(int index)
{
    PlayerProfile *profile = PlayerManager::get()->getPlayer(index);
    assert(profile);

    // Only set focus in case of non-tabbed version (so that keyboard
    // or gamepad navigation with tabs works as expected, i.e. you can
    // select the next tab without having to go up to the tab list first.
    bool focus_it = !getWidget<RibbonWidget>("options_choice");
    m_players->setSelection(StringUtils::toString(index), PLAYER_ID_GAME_MASTER,
                            focus_it);
    
    if (!m_new_registered_data)
        m_username_tb->setText(profile->getLastOnlineName());

    
    // (No "remember password" checkbox here any more -- the account session
    // restores itself from storage.)

    // Last game was not online, so make the offline settings the default
    // (i.e. unckeck online checkbox, and make entry fields invisible).
    if (!profile->wasOnlineLastTime() || profile->getLastOnlineName() == "")
    {
        makeEntryFieldsVisible();
        return;
    }

    // Now last use was with online --> Display the saved data
    // (offline-only build: online checkbox removed, always offline)

    makeEntryFieldsVisible();
    m_username_tb->setActive(profile->getLastOnlineName().size() == 0);

    // Field visibility is decided in makeEntryFieldsVisible(); the old
    // "hide the password when a session is saved" branch belonged to the
    // online flow, where hasSavedSession() gated a saved auth token.

}   // selectUser

// ----------------------------------------------------------------------------
/** Make the entry fields either visible or invisible.
 *  \param online Online state, which dicates if the entry fields are
 *         visible (true) or not.
 */
void BaseUserScreen::makeEntryFieldsVisible()
{
    // Wallet-based identity: the nickname is the display name, the wallet is
    // the identity. No password fields any more.
    getWidget<LabelWidget>("label_username")->setVisible(true);
    m_username_tb->setVisible(true);

    if (m_online_cb != NULL)
        m_online_cb->setVisible(false);

    ButtonWidget *sign_out = getWidget<ButtonWidget>("sign_out");
    if (sign_out != NULL) sign_out->setVisible(AccountStore::isSignedIn());

    refreshModeWidgets();

    if (AccountStore::isSignedIn())
        fillFieldsFromSession();
}   // makeEntryFieldsVisible

// ----------------------------------------------------------------------------
void BaseUserScreen::setMode(AccountMode mode)
{
    m_mode = mode;
    refreshModeWidgets();
}   // setMode

// ----------------------------------------------------------------------------
void BaseUserScreen::refreshModeWidgets()
{
    const bool registering = (m_mode == MODE_REGISTER);

    // No password confirm field any more; the mode hint covers the
    // register-vs-login distinction.

    // Mark the active tab with a bullet rather than disabling the other one,
    // so both stay clickable.
    ButtonWidget *tab_login = getWidget<ButtonWidget>("tab_login");
    if (tab_login != NULL)
        tab_login->setLabel(registering ? _("Log In") : _("\xE2\x80\xA2 Log In"));
    ButtonWidget *tab_register = getWidget<ButtonWidget>("tab_register");
    if (tab_register != NULL)
        tab_register->setLabel(registering ? _("\xE2\x80\xA2 Register") : _("Register"));

    LabelWidget *hint = getWidget<LabelWidget>("mode_hint");
    if (hint != NULL)
    {
        if (!AccountStore::isSignedIn())
        {
            hint->setText(_("Not signed in: you are playing as a guest. "
                            "Connect a wallet to save your name and submit laps."), false);
        }
        else
        {
            hint->setText(registering
                ? _("Register signs your account to a wallet, so connect one first.")
                : _("Signed in. Your nickname carries into races and the leaderboard."), false);
        }
    }
}   // refreshModeWidgets

// ----------------------------------------------------------------------------
void BaseUserScreen::fillFieldsFromSession()
{
    m_username_tb->setText(StringUtils::toWString(AccountStore::currentNickname()));
}   // fillFieldsFromSession

// ----------------------------------------------------------------------------
void BaseUserScreen::doRegister()
{
    if (!AccountStore::available())
    {
        m_info_widget->setText(StringUtils::toWString(
            AccountStore::describe(AccountStore::RESULT_UNSUPPORTED)), true);
        m_info_widget->setErrorColor();
        return;
    }

    const std::string nick = StringUtils::wideToUtf8(m_username_tb->getText());

    if (nick.empty())
    {
        m_info_widget->setText(_("Enter a nickname first."), true);
        m_info_widget->setErrorColor();
        return;
    }

    setMode(MODE_REGISTER);
    m_info_widget->setText(_("Creating account..."), false);
    AccountStore::registerAccount(nick);
}   // doRegister

// ----------------------------------------------------------------------------
void BaseUserScreen::doLogin()
{
    if (!AccountStore::available())
    {
        m_info_widget->setText(StringUtils::toWString(
            AccountStore::describe(AccountStore::RESULT_UNSUPPORTED)), true);
        m_info_widget->setErrorColor();
        return;
    }

    setMode(MODE_LOGIN);
    m_info_widget->setText(_("Signing in with your wallet..."), false);
    AccountStore::login();
}   // doLogin

// ----------------------------------------------------------------------------
void BaseUserScreen::pollAccountResult()
{
    AccountStore::Result result;
    if (!AccountStore::pollResult(result))
        return;

    m_info_widget->setText(StringUtils::toWString(AccountStore::describe(result)),
                           result != AccountStore::RESULT_OK);

    if (result == AccountStore::RESULT_OK)
    {
        applySignedInIdentity();
        fillFieldsFromSession();

        ButtonWidget *sign_out = getWidget<ButtonWidget>("sign_out");
        if (sign_out != NULL) sign_out->setVisible(true);
    }
    else
    {
        m_info_widget->setErrorColor();
    }
}   // pollAccountResult

// ----------------------------------------------------------------------------
void BaseUserScreen::refreshWalletLabel()
{
    if (m_wallet_label == NULL)
        return;

    const std::string addr = ArkivBridge::connectedAccount();

    if (addr.empty())
    {
        // Registration needs a wallet, so nudge in that mode only.
        m_wallet_label->setText(m_mode == MODE_REGISTER
            ? _("No wallet connected.")
            : _("No wallet connected - logging in does not need one."), false);
        return;
    }

    // Never print a full address: it is 42 characters of noise, and the tail
    // is the only part anyone uses to tell two wallets apart.
    std::string short_addr = addr;
    if (addr.size() > 14)
        short_addr = addr.substr(0, 10) + "\xE2\x80\xA6" + addr.substr(addr.size() - 4);

    m_wallet_label->setText(StringUtils::toWString("Connected: " + short_addr),
                            false);
}   // refreshWalletLabel

// ----------------------------------------------------------------------------
void BaseUserScreen::applySignedInIdentity()
{
    const std::string nick = AccountStore::currentNickname();
    if (nick.empty())
        return;

    // Adopting the nickname as the profile name is what makes it carry
    // through the game: high scores and story mode record this name. Prefer
    // the ribbon selection, but fall back to the current player so this works
    // even when nothing is selected.
    PlayerProfile *player = getSelectedPlayer();
    if (player == NULL)
        player = PlayerManager::getCurrentPlayer();
    if (player == NULL && PlayerManager::get()->getNumPlayers() > 0)
        player = PlayerManager::get()->getPlayer(0);

    if (player != NULL && player->getName() != StringUtils::toWString(nick))
    {
        player->setName(StringUtils::toWString(nick));
        PlayerManager::get()->setCurrentPlayer(player);
        PlayerManager::get()->save();
    }

    // Tell the Arkiv layer which nickname to stamp on future race records.
    // No write here: the nickname already lives in the `account` entity from
    // register/login, and submitRaceResult copies it onto each record. Writing
    // a separate entity on every sign-in would just cost gas to duplicate it.
    ArkivBridge::setNickname(nick);
}   // applySignedInIdentity

// ----------------------------------------------------------------------------
/** Called when the user selects anything on the screen.
 */
void BaseUserScreen::eventCallback(Widget* widget,
                               const std::string& name,
                               const int player_id)
{
    // Clean any error message still shown
    m_info_widget->setText("", true);
    m_info_widget->setErrorColor();

    if (name == "players")
    {
        // Clicked on a name --> Find the corresponding online data
        // and display them
        const std::string &s_index = getWidget<DynamicRibbonWidget>("players")
                                   ->getSelectionIDString(player_id);
        if (s_index == "") return;  // can happen if the list is empty

        unsigned int id;
        if (StringUtils::fromString(s_index, id))
            selectUser(id);
    }
    else if (name == "options")
    {
        const std::string &button =
                             m_options_widget->getSelectionIDString(player_id);
        if (button == "ok")
        {
            // OK is the single primary action, and what it does depends on
            // the tab: register, log in, or just continue.
            if (!AccountStore::isSignedIn())
            {
                if (m_mode == MODE_REGISTER)
                    doRegister();
                else
                    doLogin();
                return;
            }

            // Never leave with a nameless profile: it would show as a blank
            // label everywhere. Adopt whatever nickname was typed, so the
            // game always displays the player's own input.
            std::string typed = StringUtils::wideToUtf8(m_username_tb->getText());
            if (!typed.empty())
            {
                PlayerProfile *player = getSelectedPlayer();
                if (player != NULL)
                {
                    player->setName(StringUtils::toWString(typed));
                    PlayerManager::get()->setCurrentPlayer(player);
                    PlayerManager::get()->save();
                }
                ArkivBridge::setNickname(typed);
            }

            login();
        }   // button==ok
        else if (button == "new_user")
        {
            RegisterScreen::getInstance()->push();
            RegisterScreen::getInstance()->setParent(this);
            // Make sure the new user will have an empty online name field
            // that can also be edited.
            m_username_tb->setText("");
            m_username_tb->setActive(true);
        }
        else if (button == "cancel")
        {
            // EscapePressed will pop this screen.
            StateManager::get()->escapePressed();
        }
        else if (button == "rename")
        {
            PlayerProfile *cp = getSelectedPlayer();
            RegisterScreen::getInstance()->setRename(cp);
            RegisterScreen::getInstance()->push();
            RegisterScreen::getInstance()->setParent(this);
            m_new_registered_data = false;
            m_auto_login          = false;
            // Init will automatically be called, which
            // refreshes the player list
        }
        else if (button == "default_kart_color")
        {
            new KartColorSliderDialog(getSelectedPlayer());
        }
        else if (button == "delete")
        {
            deletePlayer();
        }
    }   // options
    else if (name == "tab_login")
    {
        setMode(MODE_LOGIN);
    }
    else if (name == "tab_register")
    {
        setMode(MODE_REGISTER);
    }
    else if (name == "register")
    {
        doRegister();
    }
    else if (name == "sign_out")
    {
        // Standalone button rather than an entry in the buttonbar, so it
        // arrives through its own name rather than a button selection.
        AccountStore::signOut();
        fillFieldsFromSession();
        if (getWidget<ButtonWidget>("sign_out") != NULL)
            getWidget<ButtonWidget>("sign_out")->setVisible(false);
        m_info_widget->setText(_("Signed out."), false);
    }
    else if (name == "login")
    {
        doLogin();
    }
    else if (name == "connect_wallet")
    {
        if (ArkivBridge::moduleReady())
        {
            if (m_wallet_label != NULL)
                m_wallet_label->setText(_("Waiting for wallet..."), false);
            ArkivBridge::connectWallet();
        }
        else if (m_wallet_label != NULL)
        {
            m_wallet_label->setText(_("Arkiv module failed to load."), false);
        }
    }

    return;

}   // eventCallback

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
/** Closes the BaseUserScreen, and makes sure that the right screen is displayed
 *  next.
 */
void BaseUserScreen::closeScreen()
{
    if (StateManager::get()->getMenuStackSize() > 1)
    {
        StateManager::get()->popMenu();
    }
    else
    {
        StateManager::get()->resetAndGoToScreen(MainMenuScreen::getInstance());
    }
}   // closeScreen

// ----------------------------------------------------------------------------
/** Called when OK or OK-and-save is clicked.
 *  This will trigger the actual login (if requested) etc.
 *  \param remember_me True if the login details should be remembered,
 *         so that next time this menu can be skipped.
 */
void BaseUserScreen::login()
{
    // If an error occurs, the callback informing this screen about the
    // problem will activate the widget again.
    m_options_widget->setActive(false);
    m_state = STATE_NONE;

    PlayerProfile *player = getSelectedPlayer();
    PlayerProfile *current = PlayerManager::getCurrentPlayer();
    core::stringw  new_username = m_username_tb->getText();
    // If a different player is connecting, or the same local player with
    // a different online account, log out the current player.
    if(current && current->isLoggedIn() &&
        (player!=current ||
        current->getLastOnlineName()!=new_username) )
    {
        m_sign_out_name = current->getLastOnlineName();
        current->requestSignOut();
        m_state = (UserScreenState)(m_state | STATE_LOGOUT);

        // If the online user name was changed, reset the save data
        // for this user (otherwise later the saved session will be
        // resumed, not logging the user with the new account).
        if(player==current &&
            current->getLastOnlineName()!=new_username)
            current->clearSession();
    }
    PlayerManager::get()->setCurrentPlayer(player);
    assert(player);

    // If no online login requested, log the player out (if necessary)
    // and go to the main menu screen (though logout needs to finish first)
    // (offline-only build: checkbox removed, always take the offline path)
    if(true)
    {
        if(player->isLoggedIn())
        {
            m_sign_out_name =player->getLastOnlineName();
            player->requestSignOut();
            m_state =(UserScreenState)(m_state| STATE_LOGOUT);
        }

        player->setWasOnlineLastTime(false);
        if(m_state==STATE_NONE)
        {
            closeScreen();
        }
        return;
    }

    // Player wants to be online, and is already online - nothing to do
    if(player->isLoggedIn())
    {
        player->setWasOnlineLastTime(true);
        closeScreen();
        return;
    }
    m_state = (UserScreenState) (m_state | STATE_LOGIN);
    // Now we need to start a login request to the server
    // This implies that this screen will wait till the server responds, so
    // that error messages ('invalid password') can be shown, and the user
    // can decide what to do about them.
    if (player->hasSavedSession())
    {
        m_sign_in_name = player->getLastOnlineName();
        // Online login with saved token
        player->requestSavedSession();
    }
    else
    {
        // Wallet-based auth: no password. The session is established by
        // connecting a wallet on the account screen, so this legacy online
        // login path is a no-op.
        m_options_widget->setActive(true);
        return;
    }   // !hasSavedSession

}   // login

// ----------------------------------------------------------------------------
/** Called once every frame. It will replace this screen with the main menu
 *  screen if a successful login happened.
 */
void BaseUserScreen::onUpdate(float dt)
{
    // Register/login settle asynchronously (password hashing goes through
    // Web Crypto), so drain the outcome here rather than blocking.
    pollAccountResult();

    // Action feedback goes to the message line; the wallet line below it is
    // reserved for connection state only. Keeping them separate stops a
    // one-off "Waiting for wallet..." from replacing the address.
    std::string status;
    if (ArkivBridge::pollStatus(status) && !status.empty() &&
        status != "Waiting for wallet...")
    {
        m_info_widget->setText(StringUtils::toWString(status), false);
    }

    if (m_wallet_label != NULL)
        refreshWalletLabel();

    if (!m_options_widget->isActivated())
    {
        core::stringw message = (m_state & STATE_LOGOUT)
                              ? _(L"Logging out '%s'",m_sign_out_name.c_str())
                              : _(L"Logging in '%s'", m_sign_in_name.c_str());
        m_info_widget->setText(StringUtils::loadingDots(message.c_str()),
                               false                                      );
    }
}   // onUpdate

// ----------------------------------------------------------------------------
/** Callback from player profile if login was successful.
 */
void BaseUserScreen::loginSuccessful()
{
    PlayerProfile *player  = getSelectedPlayer();
    player->setWasOnlineLastTime(true);
    m_options_widget->setActive(true);
    // Clean any error message still shown
    m_info_widget->setText("", true);
    m_info_widget->setErrorColor();
    // The callback is done from the main thread, so no need to sync
    // access to m_success. OnUpdate will check this flag
    closeScreen();
}   // loginSuccessful

// ----------------------------------------------------------------------------
/** Callback from player profile if login was unsuccessful.
 *  \param error_message Contains the error message.
 */
void BaseUserScreen::loginError(const irr::core::stringw & error_message,
                                bool clear_password)
{
    m_state = (UserScreenState) (m_state & ~STATE_LOGIN);
    PlayerProfile *player = getSelectedPlayer();
    // Clear information about saved session in case of a problem,
    // which allows the player to enter a new password.
    // Only if not a download error
    if(clear_password && player && player->hasSavedSession())
        player->clearSession();
    player->setLastOnlineName("");
    makeEntryFieldsVisible();
    SFXManager::get()->quickSound("anvil");
    m_info_widget->setErrorColor();
    m_info_widget->setText(error_message, false);
    m_options_widget->setActive(true);
}   // loginError

// ----------------------------------------------------------------------------
/** Callback from player profile if logout was successful.
 */
void BaseUserScreen::logoutSuccessful()
{
    m_state = (UserScreenState) (m_state & ~STATE_LOGOUT);
    if(m_state==STATE_NONE)
        closeScreen();
    // Otherwise the screen still has to wait for a login request to finish.
}   // loginSuccessful

// ----------------------------------------------------------------------------
/** Callback from player profile if login was unsuccessful.
 *  \param error_message Contains the error message.
 */
void BaseUserScreen::logoutError(const irr::core::stringw & error_message)
{
    m_state = (UserScreenState) (m_state & ~STATE_LOGOUT);
    PlayerProfile *player = getSelectedPlayer();
    // Clear information about saved session in case of a problem,
    // which allows the player to enter a new password.
    if(player && player->hasSavedSession())
        player->clearSession();
    makeEntryFieldsVisible();
    SFXManager::get()->quickSound("anvil");
    m_info_widget->setErrorColor();
    m_info_widget->setText(error_message, false);
    m_options_widget->setActive(true);
}   // logoutError

// ----------------------------------------------------------------------------
/** Called when a player will be deleted.
 */
void BaseUserScreen::deletePlayer()
{
    // Check that there is at least one player left: we need to have a
    // valid current player, so the last player can not be deleted.
    if(PlayerManager::get()->getNumNonGuestPlayers()==1)
    {
        m_info_widget->setText(_("You can't delete the only player."), true);
        m_info_widget->setErrorColor();
        return;
    }

    PlayerProfile *player = getSelectedPlayer();
    irr::core::stringw message =
        //I18N: In the player info dialog (when deleting)
        _("Do you really want to delete player '%s'?", player->getName());

    class ConfirmServer : public MessageDialog::IConfirmDialogListener
    {
        BaseUserScreen *m_parent_screen;
    public:
        virtual void onConfirm()
        {
            m_parent_screen->doDeletePlayer();
        }   // onConfirm
        // ------------------------------------------------------------
        ConfirmServer(BaseUserScreen *parent)
        {
            m_parent_screen = parent;
        }
    };   // ConfirmServer

    new MessageDialog(message, MessageDialog::MESSAGE_DIALOG_CONFIRM,
                      new ConfirmServer(this), true);
}   // deletePlayer

// ----------------------------------------------------------------------------
/** Callback when the user confirms to delete a player. This function actually
 *  deletes the player, discards the dialog, and re-initialised the BaseUserScreen
 *  to display only the available players.
 */
void BaseUserScreen::doDeletePlayer()
{
    PlayerProfile *player = getSelectedPlayer();
    PlayerManager::get()->deletePlayer(player);
    GUIEngine::ModalDialog::dismiss();

    // Special case: the current player was deleted. We have to make sure
    // that there is still a current player (all of STK depends on that).
    if(!PlayerManager::getCurrentPlayer())
    {
        for(unsigned int i=0; i<PlayerManager::get()->getNumPlayers(); i++)
        {
            PlayerProfile *player = PlayerManager::get()->getPlayer(i);
            if(!player->isGuestAccount())
            {
                PlayerManager::get()->setCurrentPlayer(player);
                break;
            }
        }
    }
    init();
}   // doDeletePlayer

// ----------------------------------------------------------------------------
void BaseUserScreen::unloaded()
{
}   // unloaded

// ============================================================================
/** In the tab version, make sure the right tab is selected.
 */
void TabbedUserScreen::init()
{
    RibbonWidget* tab_bar = getWidget<RibbonWidget>("options_choice");
    assert(tab_bar != NULL);
    tab_bar->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
    tab_bar->select("tab_players", PLAYER_ID_GAME_MASTER);
    BaseUserScreen::init();
}   // init

// ----------------------------------------------------------------------------
/** Switch to the correct tab.
 */
void TabbedUserScreen::eventCallback(GUIEngine::Widget* widget,
                                     const std::string& name,
                                     const int player_id)
{
    if (name == "options_choice")
    {
        std::string selection = ((RibbonWidget*)widget)->getSelectionIDString(PLAYER_ID_GAME_MASTER);

        if (selection != "tab_players")
            OptionsCommon::switchTab(selection);
    }
    else
        BaseUserScreen::eventCallback(widget, name, player_id);

}   // eventCallback
