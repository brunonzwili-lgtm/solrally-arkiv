// Leaderboard screen backed by Arkiv entities -- see the header for why
// all network work lives in JS.

#include "states_screens/arkiv_leaderboard.hpp"

#include "guiengine/widgets/button_widget.hpp"
#include "guiengine/widgets/label_widget.hpp"
#include "guiengine/widgets/list_widget.hpp"
#include "graphics/irr_driver.hpp"
#include "states_screens/state_manager.hpp"
#include "utils/arkiv_bridge.hpp"
#include "utils/string_utils.hpp"
#include "utils/translation.hpp"

#include <sstream>

// ---------------------------------------------------------------------------
ArkivLeaderboard::ArkivLeaderboard() : Screen("arkiv_leaderboard.stkgui")
{
    m_refresh_timer = 0.0f;
    m_module_ready  = false;
}   // ArkivLeaderboard

// ---------------------------------------------------------------------------
void ArkivLeaderboard::loadedFromFile()
{
    m_list = getWidget<GUIEngine::ListWidget>("arkiv_list");
    m_status = getWidget<GUIEngine::LabelWidget>("status");
}   // loadedFromFile

// ---------------------------------------------------------------------------
void ArkivLeaderboard::beforeAddingWidget()
{
    // Columns must be set up after the widget is added to the screen.
    if (m_list != NULL)
    {
        m_list->clearColumns();
        m_list->addColumn(_("Race records on Arkiv"), 1);
    }
}   // beforeAddingWidget

// ---------------------------------------------------------------------------
void ArkivLeaderboard::init()
{
    if (m_status != NULL)
        m_status->setText(_("Loading records from Arkiv..."), false);
}   // init

// ---------------------------------------------------------------------------
void ArkivLeaderboard::onUpdate(float dt)
{
    m_refresh_timer += dt;

    if (!m_module_ready)
    {
        ArkivBridge::ensureModule();
        if (ArkivBridge::moduleReady())
            m_module_ready = true;
        return;
    }

    // The wallet buttons report back asynchronously; copy out a status line
    // only once JS has actually produced one.
    std::string status;
    if (ArkivBridge::pollStatus(status) && m_status != NULL)
        m_status->setText(StringUtils::toWString(status), false);

    // Poll slowly: each refresh is an RPC round trip.
    if (m_refresh_timer > 5.0f)
    {
        m_refresh_timer = 0.0f;
        refreshList();
    }
}   // onUpdate

// ---------------------------------------------------------------------------
void ArkivLeaderboard::refreshList()
{
    if (m_list == NULL)
        return;

    // The query is still in flight most of the time we get here; pollLeaderboard
    // returns false and we simply try again on the next tick.
    std::string blob;
    if (!ArkivBridge::pollLeaderboard(blob))
        return;

    m_list->clear();

    if (blob.empty() || blob == "__EMPTY__")
    {
        m_list->addItem("empty", { GUIEngine::ListWidget::ListCell
            (_("No records yet - submit a lap to add one."), -1, 1, false) });
        if (m_status != NULL)
            m_status->setText(_("Connected to Arkiv. No records yet."), false);
        return;
    }

    if (blob.rfind("__ERROR__", 0) == 0)
    {
        m_list->addItem("err", { GUIEngine::ListWidget::ListCell
            (blob.c_str(), -1, 1, false) });
        if (m_status != NULL)
            m_status->setText(_("Could not reach Arkiv."), false);
        return;
    }

    std::istringstream in(blob);
    std::string line;
    int i = 0;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        m_list->addItem(StringUtils::toString(i++),
            { GUIEngine::ListWidget::ListCell(line.c_str(), -1, 1, false) });
    }

    if (m_status != NULL)
        m_status->setText(_("Records read from Arkiv (anyone can query these)."), false);
}   // refreshList

// ---------------------------------------------------------------------------
void ArkivLeaderboard::connectWallet()
{
    if (m_status != NULL)
        m_status->setText(_("Waiting for wallet..."), false);

    ArkivBridge::connectWallet();
}   // connectWallet

// ---------------------------------------------------------------------------
void ArkivLeaderboard::submitLap()
{
    if (!ArkivBridge::hasRaceResult())
    {
        if (m_status != NULL)
            m_status->setText(_("Finish a race first, then come back."), false);
        return;
    }

    // Check up front rather than letting the write fail: the result is
    // dropped once submitted, so a "wallet not connected" rejection would
    // throw away the player's lap.
    if (ArkivBridge::connectedAccount().empty())
    {
        if (m_status != NULL)
            m_status->setText(_("Connect a wallet before submitting."), false);
        return;
    }

    if (m_status != NULL)
        m_status->setText(_("Writing your lap to Arkiv..."), false);

    ArkivBridge::submitRaceResult();
    ArkivBridge::clearRaceResult();
}   // submitLap

// ---------------------------------------------------------------------------
void ArkivLeaderboard::eventCallback(GUIEngine::Widget* /*widget*/,
                                     const std::string& name,
                                     const int /*playerID*/)
{
    if (name == "back")
    {
        StateManager::get()->popMenu();
    }
    else if (name == "connect")
    {
        connectWallet();
    }
    else if (name == "submit")
    {
        submitLap();
    }
}   // eventCallback