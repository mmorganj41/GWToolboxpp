#include "stdafx.h"


#include <GWCA/Constants/Maps.h>

#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/WorldContext.h>

#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Agent.h>

#include <ImGuiAddons.h>

#include <Modules/HintsModule.h>

namespace {
    struct TBHint {
        uint32_t message_id;
        const wchar_t* message;
    };

    GW::Constants::Campaign GetCharacterCampaign() {
        if (GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Island_of_Shehkah))
            return GW::Constants::Campaign::Nightfall;
        if (GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Ascalon_City_pre_searing))
            return GW::Constants::Campaign::Prophecies;
        return GW::Constants::Campaign::Factions;
    }

    std::vector<uint32_t> hints_shown;
    struct HintUIMessage {
        uint32_t message_id = 0x10000000; // Used internally to avoid queueing more than 1 of the same hint
        wchar_t* message_encoded;
        uint32_t image_file_id = 0; // e.g. mouse imaage, light bulb, exclamation mark
        uint32_t message_timeout_ms = 15000;
        uint32_t style_bitmap = 0x12; // 0x18 = hint with left padding
        HintUIMessage(const wchar_t* message, uint32_t duration = 30000, uint32_t _message_id = 0) {
            const size_t strlen = (wcslen(message) + 4) * sizeof(wchar_t);
            message_encoded = (wchar_t*)malloc(strlen);
            swprintf(message_encoded, strlen, L"\x108\x107%s\x1", message);
            if(!_message_id)
                _message_id = (uint32_t)message;
            message_id = _message_id;
            message_timeout_ms = duration;
        }
        HintUIMessage(const TBHint& hint) : HintUIMessage(hint.message, 30000, hint.message_id) {}
        ~HintUIMessage() {
            free(message_encoded);
        }
        void Show() {
            if (std::find(hints_shown.begin(), hints_shown.end(), message_id) != hints_shown.end())
                return;
            GW::UI::SendUIMessage(GW::UI::kShowHint, this);
            hints_shown.push_back(message_id);
        }
    };
    struct LastQuote {
        uint32_t item_id = 0;
        uint32_t price = 0;
    } last_quote;
    clock_t last_quoted_item_timestamp = 0;

    constexpr wchar_t* embark_beach_campaign_npcs[] = {
        L"",
        L"\x8102\x6F1E\xE846\xFFBF\x57E0", // Kenai [Tyrian Travel]
        L"\x8102\x6F05\xE3C3\xBF66\x234C", // Shirayuki [Canthan Travel]
        L"\x8102\x6F1E\xE846\xFFBF\x57E0" // Zinshao [Elonian Travel]
    };
    constexpr wchar_t* endgame_reward_npcs[] = {
    L"",
    L"\x399E\x8A19\xC3B6\x2FE4", // King Jalis (Droks Explorable)
    L"\x107\x108" "Suun\x1", // Suun (Divine Path) TODO: Encoded version of this name!
    L"\x107\x108" "Keeper of Secrets\x1", // Keeper of Secrets (Throne of secrets) TODO: Encoded version of this name!
    L"\x107\x108" "Droknar\x1",
    };
    constexpr wchar_t* endgame_reward_trophies[] = {
        L"",
        L"\x107\x108" "Deldrimor Talisman" "\x1", // King Jalis (Droks Explorable)
        L"\x107\x108" "Amulet of the Mists" "\x1", // Suun (Divine Path) TODO: Encoded version of this name!
        L"\x107\x108" "Book of Secrets" "\x1", // Keeper of Secrets (Throne of secrets) TODO: Encoded version of this name!
        L"\x107\x108" "Droknar's Key" "\x1",
    };
    
    constexpr TBHint HINT_Q9_STR_SHIELDS = { 0x20000001, L"PvP Strength shields give 9 armor when you don't meet the requirement, so unless you can meet the req on a different attribute, use a Strength shield." };
    constexpr TBHint HINT_HERO_EXP = { 0x20000002, L"Heroes in your party gain experience from quests, so remember to add your low level heroes when accepting quest rewards." };
    constexpr TBHint CHEST_CMD = { 0x20000003, L"Type '/chest' into chat to open your Xunlai Chest from anywhere in an outpost, so you won't have to run to the chest every time." };
    constexpr TBHint BULK_BUY = { 0x20000004, L"Hold Ctrl when requesting a quote to bulk buy or sell from a trader" };
    constexpr TBHint EMBARK_WITHOUT_HOMELAND = { 0x20001000, L"To get back from Embark Beach to where you came from, talk to \x1\x2%s\x2\x108\x107 or use the '/tb travel' chat command." };
    constexpr TBHint ENDGAME_TROPHY = { 0x20002000, L"Talk to \x1\x2%s\x2\x108\x107 to receive a \x1\x2%s\x2\x108\x107. Those are worth a lot of money if you sell to another player, so rather than trading it in for a weapon, search on https://kamadan.gwtoolbox.com for a buyer." };
}

//#define PRINT_CHAT_PACKETS
void HintsModule::Initialize() {
    GW::UI::RegisterUIMessageCallback(&hints_entry, OnUIMessage);
}

void HintsModule::OnUIMessage(GW::HookStatus* status, uint32_t message_id, void* wparam, void*) {
    switch (message_id) {
    case GW::UI::kShowHint: {
        HintUIMessage* msg = (HintUIMessage*)wparam;
        if (Instance().block_repeat_attack_hint && wcscmp(msg->message_encoded, L"\x9c3") == 0) {
            status->blocked = true;
        }
    } break;
    case GW::UI::kWriteToChatLog: {
        GW::UI::UIChatMessage* msg = (GW::UI::UIChatMessage*)wparam;
        if (msg->channel == GW::Chat::Channel::CHANNEL_GLOBAL && wcsncmp(msg->message, L"\x8101\x4793\xfda0\xe8e2\x6844", 5) == 0) {
            HintUIMessage(HINT_HERO_EXP).Show();
        }
    } break;
    case GW::UI::kShowXunlaiChest: {
        GW::AgentLiving* chest = GW::Agents::GetTargetAsAgentLiving();
        if(chest && chest->player_number == 5001 && GW::GetDistance(GW::Agents::GetPlayer()->pos,chest->pos) < GW::Constants::Range::Nearby) {
            HintUIMessage(CHEST_CMD).Show();
        }
    } break;
    case GW::UI::kQuotedItemPrice: {
        clock_t _now = clock();
        LastQuote* q = (LastQuote*)wparam;
        if (last_quote.item_id == q->item_id && _now - last_quoted_item_timestamp < 5 * CLOCKS_PER_SEC) {
            HintUIMessage(BULK_BUY).Show();
        }
        last_quote = *q;
        last_quoted_item_timestamp = _now;
    } break;
    case GW::UI::kPvPWindowContent: {
        HintUIMessage(HINT_Q9_STR_SHIELDS).Show();
    } break;
    case GW::UI::kMapLoaded: {
        uint32_t endgame_msg_idx = 0;
        switch (GW::Map::GetMapID()) {
        case GW::Constants::MapID::Embark_Beach: {
            if (GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Kaineng_Center_outpost)
                || GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Lions_Arch_outpost)
                || GW::Map::GetIsMapUnlocked(GW::Constants::MapID::Kamadan_Jewel_of_Istan_outpost))
                break;
            wchar_t out[256];
            uint32_t campaign = (uint32_t)GetCharacterCampaign();
            swprintf(out, 256, EMBARK_WITHOUT_HOMELAND.message, embark_beach_campaign_npcs[campaign]);
            HintUIMessage(out, 30000, EMBARK_WITHOUT_HOMELAND.message_id & campaign).Show();
        } break;
        case GW::Constants::MapID::Droknars_Forge_cinematic:
            endgame_msg_idx = 1;
            break;
        case GW::Constants::MapID::Divine_Path:
            endgame_msg_idx = 2;
            break;
        case GW::Constants::MapID::Throne_of_Secrets:
            endgame_msg_idx = 3;
            break;
        case GW::Constants::MapID::Epilogue:
            endgame_msg_idx = 4;
            break;
        }
        if (endgame_msg_idx) {
            wchar_t out[256];
            swprintf(out, 256, ENDGAME_TROPHY.message, endgame_reward_npcs[endgame_msg_idx], endgame_reward_trophies[endgame_msg_idx]);
            HintUIMessage(out, 30000, ENDGAME_TROPHY.message_id & endgame_msg_idx).Show();
        }
        
    } break;
    }
}
void HintsModule::DrawSettingInternal() {
    ImGui::Checkbox("Block \"ordering your character to attack repeatedly\" hint", &block_repeat_attack_hint);
    ImGui::ShowHelp("Guild Wars keeps showing a hint to tell you not to repeatedly attack a foe.\nTick to stop it from popping up.");
}