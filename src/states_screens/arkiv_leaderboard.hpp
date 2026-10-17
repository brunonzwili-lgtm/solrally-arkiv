// Leaderboard screen backed by Arkiv entities.
//
// The game cannot do network I/O, so all of it happens in JS (see
// wasm/web/arkiv.js, bundled from arkiv/browser.js). This screen only
// pulls a pre-formatted, newline-separated string across the boundary and
// feeds the list widget. Reads need no wallet; writing a result does.

#ifndef HEADER_ARKIV_LEADERBOARD_HPP
#define HEADER_ARKIV_LEADERBOARD_HPP

#include "guiengine/screen.hpp"
#include "guiengine/widgets/label_widget.hpp"
#include "guiengine/widgets/list_widget.hpp"

class ArkivLeaderboard : public GUIEngine::Screen
{
private:
    GUIEngine::ListWidget *m_list;
    GUIEngine::LabelWidget *m_status;

    float m_refresh_timer;
    bool  m_module_ready;

    /** Pull the current leaderboard text out of the JS layer. */
    void refreshList();

    /** Ask JS to connect a wallet. Async; result lands in m_status. */
    void connectWallet();

    /** Ask JS to write this player's best lap as a race_result entity. */
    void submitLap();

public:
    ArkivLeaderboard();
    virtual void loadedFromFile() OVERRIDE;
    virtual void init() OVERRIDE;
    virtual void onUpdate(float dt) OVERRIDE;
    virtual void eventCallback(GUIEngine::Widget* widget,
                               const std::string& name,
                               const int playerID) OVERRIDE;
    virtual std::string makeStatusMessage() const { return "Leaderboard"; }
};   // ArkivLeaderboard

#endif