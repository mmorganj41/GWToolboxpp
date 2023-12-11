#include "stdafx.h"

#include <optional>
#include <thread>
#include <cmath>

#include <GWCA/Constants/Constants.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Attribute.h>
#include <GWCA/GameEntities/Item.h>
#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/UIMgr.h>

#include <GWCA/Packets/StoC.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Modules/InventoryManager.h>
#include <Windows/SidekickWindow.h>

#include <Logger.h>

namespace {
    uint32_t party_invite = 0;
    InventoryManager inventory_manager;
    bool reposition = false;
    bool first_message = true;
    uint32_t dialog_id = 0;
    uint32_t dialog_type = 0;
    bool first_move = true;
    size_t sidekick_position = 0;
} // namespace

bool SidekickWindow::GetEnabled() { return enabled; }

bool SidekickWindow::SetEnabled(bool b)
{
    if (enabled != b)
        enabled = b;
    return enabled;
}

void SidekickWindow::Initialize()
{
    ToolboxWindow::Initialize();

    Setup();

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericModifier>(&GenericModifier_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericModifier* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;

        const uint32_t type = packet->type;
        const uint32_t caster_id = packet->target_id;
        const float value = packet->value;
        const uint32_t cause_id = packet->cause_id;

        if (type == GW::Packet::StoC::GenericValueID::casttime) {
            cast_times.insert_or_assign(caster_id, value);
        }

        GenericModifierCallback(type, caster_id, value, cause_id);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValue>(&GenericValueSelf_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValue* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;

        const uint32_t value_id = packet->value_id;
        const uint32_t caster_id = packet->agent_id;
        const uint32_t value = packet->value;

        GenericValueCallback(value_id, caster_id, value);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        using namespace GW::Packet::StoC::GenericValueID;
        if (!enabled) return;

        const uint32_t value_id = packet->Value_id;
        const uint32_t caster_id = packet->caster;
        const uint32_t target_id = packet->target;
        const uint32_t value = packet->value;

        const bool isSwapped = value_id == skill_activated || value_id == attack_skill_activated || value_id == attack_started;
        GenericValueCallback(value_id, isSwapped ? target_id : caster_id, value, isSwapped ? caster_id : target_id);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::PartyInviteReceived_Create>(&PartyInvite, [this](GW::HookStatus* status, GW::Packet::StoC::PartyInviteReceived_Create* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;

        party_invite = packet->target_party_id;
        Log::Info("invite received");
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::MessageCore>(&ObstructedMessage, [this](GW::HookStatus* status, GW::Packet::StoC::MessageCore* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;
        if (!packet) return;
        if (finishes_attacks && packet->message[0] == 0x8AB && TIMER_DIFF(timers.obstructedTimer) > 2000) {
            Log::Info("Obstructed");
            wardEffect = std::nullopt;
            state = Obstructed; 
            timers.obstructedTimer = TIMER_INIT();
        }
        else {
            switch (packet->message[0]) {
                case 0x793: {
                    no_combat = !no_combat;
                    break;
                }
                case 0x77B: {
                    state = Talking;
                    first_message = true;
                    [[fallthrough]];
                }
                case 0x792: {
                    leaderState = Normal;
                    break;
                }
                case 0x794: {
                    leaderState = Split;
                    break;
                }
                case 0x7BE: {
                    leaderState = Three;
                    break;
                }
                default: {
                    MessageCallBack(packet);
                }
            }
        }
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::Ping>(&Ping_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::Ping* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;

        OnServerPing(packet->ping);

     }, 0x800);

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::DialogButton>(&Dialog_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::DialogButton* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;
        if (!packet) return;
        if (packet->button_icon != 15 && packet->dialog_id != 0) {
            if (dialog_id == 0) {
                dialog_id = packet->dialog_id;
                dialog_type = packet->button_icon;
            }
            else if (dialog_type == 11 && packet->button_icon != 11) {
                dialog_id = packet->dialog_id;
                dialog_type = packet->button_icon;
            }
        }
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::AddEffect>(&AddEffect_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::AddEffect* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!enabled) return;
        if (!packet) return;

        AddEffectPacketCallback(packet);
     });
}

void SidekickWindow::Update(float delta)
{
    UNREFERENCED_PARAMETER(delta);

    if (!enabled) {
        party_ids = {};
        party_leader_id = 0;
        wardEffect = std::nullopt;
        HardReset();
        return;
    }

    timers.activityTimer = TIMER_INIT();

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading || GW::Map::GetIsInCinematic()) {
        party_ids = {};
        party_leader_id = 0;
        wardEffect = std::nullopt;
        HardReset();
        if (GW::Map::GetIsInCinematic()) {
            GW::Map::SkipCinematic();
        }
    }
    else if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        if (party_invite != 0) {
            GW::PartyMgr::RespondToPartyRequest(party_invite, true);
            party_invite = 0;
            Log::Info("Invite accept");
        }
        if (state == Talking) {
            if (!party_leader_id || leaderState != lastLeaderState) {
                GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
                if (!(party && party->players.valid() && party->players.size())) {
                    party_leader_id = 0;
                }
                else {
                    party_leader_id = GW::Agents::GetAgentIdByLoginNumber(party->players[0].login_number);
                    lastLeaderState = leaderState;
                }
                return;
            }

            GW::AgentLiving* sidekick = GW::Agents::GetCharacter();
            if (!sidekick) return;

            ResetTargets();

            GW::Agent* leader = GW::Agents::GetAgentByID(party_leader_id);
            GW::AgentLiving* party_leader = leader ? leader->GetAsAgentLiving() : nullptr;

            if (!party_leader) return;
            
           GW::AgentArray* agent_array = GW::Agents::GetAgentArray();

            if (!agent_array) return;

            for (auto* a : *agent_array) {
                if (!a) continue;
                GW::AgentLiving* agentLiving = a->GetAsAgentLiving();

                if (agentLiving) {
                    if (GW::GetDistance(sidekick->pos, agentLiving->pos) > GW::Constants::Range::Spirit) continue;
                    if (agentLiving->allegiance == GW::Constants::Allegiance::Npc_Minipet) {
                        if (!agentLiving->GetIsAlive()) continue;
                        if (!closest_npc) {
                            closest_npc = agentLiving;
                        }
                        else if (GW::GetSquareDistance(party_leader->pos, agentLiving->pos) < GW::GetSquareDistance(party_leader->pos, closest_npc->pos)) {
                            closest_npc = agentLiving;
                        }
                    }
                }
            }
            if (first_message) {
                float distance = GW::GetDistance(sidekick->pos, party_leader->pos);
                if (distance > GW::Constants::Range::Area && TIMER_DIFF(timers.followTimer) > 149 + rand() % 100) {
                    if (!sidekick->GetIsMoving()) {
                        GW::Agents::GoPlayer(party_leader);
                        timers.followTimer = TIMER_INIT();
                    }
                }
                else {
                    first_message = false;
                    if (closest_npc) {
                        GW::Agents::ChangeTarget(closest_npc->agent_id);
                        GW::GameThread::Enqueue([]() -> void {
                            GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                            return;
                        });
                    }
                }
            }
            if (dialog_id) {
                    GW::Agents::SendDialog(dialog_id);
                    dialog_id = 0;
                    dialog_type = 0;
            }
            if (GW::GetDistance(sidekick->pos, party_leader->pos) > GW::Constants::Range::Area && !sidekick->GetIsMoving()) {
                    state = Following;
            }
            
        }
        else {
            party_ids = {};
            party_leader_id = 0;
            wardEffect = std::nullopt;
        }
    }
    else {
        if (party_ids.empty()) {
            GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
            if (!party) return;
            if (party->players.valid())
                for (size_t i = 0; i < party->players.size(); i++) {
                    if (!party->players[i].login_number) continue;
                    GW::AgentID agentId = GW::Agents::GetAgentIdByLoginNumber(party->players[i].login_number);
                    if (agentId && agentId == GW::Agents::GetPlayerId()) sidekick_position = i;
                    party_ids.insert(agentId);
                }
            if (party->heroes.valid())
                for (size_t i = 0; i < party->heroes.size(); i++) {
                    if (!party->heroes[i].agent_id) continue;
                    party_ids.insert(party->heroes[i].agent_id);
                }
            if (party->henchmen.valid())
                for (size_t i = 0; i < party->henchmen.size(); i++) {
                    if (!party->henchmen[i].agent_id) continue;
                    party_ids.insert(party->henchmen[i].agent_id);
                }
            return;
        }

        GW::AgentLiving* sidekick = GW::Agents::GetCharacter();
        if (!sidekick) return;

        GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
        if (!party) return;

        if (!party_leader_id || (leaderState != lastLeaderState)) {
            if (!(party && party->players.valid() && party->players.size())) {
                party_leader_id = 0;
            }
            else {
                size_t index = 0;
                switch (leaderState) {
                    case Split: {
                        index = sidekick_position % 2;
                        break;
                    }
                    case Three: {
                        index = sidekick_position % 3;
                        break;
                    }
                }
                party_leader_id = GW::Agents::GetAgentIdByLoginNumber(party->players[index].login_number);
                lastLeaderState = leaderState;
            }
            return;
        }

        ResetTargets();

        std::optional<GW::GamePos> sum_position = std::nullopt;
        size_t party_count = 0;
        if (wardEffect && TIMER_DIFF(wardEffect->skillDuration.startTime) > static_cast<int32_t>(wardEffect->skillDuration.duration)) {
            wardEffect = std::nullopt;
        }

        if (wardEffect) {
            group_center = wardEffect->position;
        }
        else {
            if (party->players.valid())
                for (size_t i = 0; i < party->players.size(); i++) {
                    if (!party->players[i].connected()) continue;
                    switch (leaderState) {
                        case Split: {
                            if (sidekick_position % 2 != i % 2) continue;
                            break;
                        }
                        case Three: {
                            if (sidekick_position % 3 != i % 3) continue;
                            break;
                        }

                    }
                    const uint32_t agentId = GW::Agents::GetAgentIdByLoginNumber(party->players[i].login_number);
                    GW::Agent* agent = GW::Agents::GetAgentByID(agentId);
                    GW::AgentLiving* player = agent ? agent->GetAsAgentLiving() : nullptr;
                    if (!player) continue;
                    if (player->GetIsAlive() && player->pos.zplane == sidekick->pos.zplane) {
                        if (!sum_position) {
                            sum_position = player->pos;
                            party_count += 1;
                        }
                        else if (sum_position->zplane == player->pos.zplane) {
                            sum_position->x += player->pos.x;
                            sum_position->y += player->pos.y;
                            party_count += 1;
                        }
                    }
                }

            if (!sum_position || !party_count) return;

            group_center = {sum_position->x / party_count, sum_position->y / party_count, sum_position->zplane};
        }

        GW::Agent* leader = GW::Agents::GetAgentByID(party_leader_id);
        GW::AgentLiving* party_leader = leader ? leader->GetAsAgentLiving() : nullptr;

        if (!party_leader) return;

        if (sidekick->GetIsKnockedDown() || !sidekick->GetIsAlive()) {
            CantAct();
            timers.lastInteract = TIMER_INIT();
            return;
        }

        GW::AgentArray* agent_array = GW::Agents::GetAgentArray();

        if (!agent_array) return;

        CustomLoop(sidekick);

        GW::Effect* heroicRefrain = GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain);

        if (heroicRefrain) {
            hasHeroicRefrain = true;
        }
        else if (hasHeroicRefrain && !heroicRefrain) {
            hasHeroicRefrain = false;
            GW::Chat::SendChat('/', "boo");
        }

        bool still_in_combat = false;

        for (auto* a : *agent_array) {
            if (!a) continue;
            GW::AgentLiving* agentLiving = a->GetAsAgentLiving();

            if (agentLiving) {
                if (GW::GetDistance(sidekick->pos, agentLiving->pos) > GW::Constants::Range::Spirit) continue;
                switch (agentLiving->allegiance) {
                    case GW::Constants::Allegiance::Enemy: {
                        if (!agentLiving->GetIsAlive()) continue;
                        if (GW::GetDistance(sidekick->pos, agentLiving->pos) > GW::Constants::Range::Spellcast * 11 / 10 && !(agentLiving->GetIsCasting() || agentLiving->GetIsAttacking())) continue;
                        if (!closest_enemy) {
                            closest_enemy = agentLiving;
                        }
                        else if (GW::GetSquareDistance(sidekick->pos, agentLiving->pos) < GW::GetSquareDistance(sidekick->pos, closest_enemy->pos)) {
                            closest_enemy = agentLiving;
                        }
                        if (agentLiving->hp < 1.0f && GW::GetDistance(sidekick->pos, agentLiving->pos) < GW::Constants::Range::Spellcast * 6/5 && (!lowest_health_enemy || lowest_health_enemy->hp > agentLiving->hp)) lowest_health_enemy = agentLiving;
                        break;
                    }
                    case GW::Constants::Allegiance::Ally_NonAttackable: {
                        if (agentLiving->agent_id && party_ids.contains(agentLiving->agent_id) && agentLiving->GetIsAlive()) {
                            if (agentLiving->GetIsAttacking() && (((state == Fighting || called_enemy) && agentLiving->agent_id == party_leader_id) || agentLiving->agent_id != party_leader_id)) {
                                if (!starting_combat) {
                                    starting_combat = true;
                                    timers.changeStateTimer = TIMER_INIT();
                                }
                                still_in_combat = true;
                            }
                            if (agentLiving->hp < 1.0f && (!lowest_health_ally || lowest_health_ally->hp > agentLiving->hp)) lowest_health_ally = agentLiving;
                            if (agentLiving->agent_id != sidekick->agent_id && agentLiving->hp < 1.0f && (!lowest_health_other_ally || lowest_health_other_ally->hp > agentLiving->hp)) lowest_health_other_ally = agentLiving;
                        }
                        break;
                    }
                }
                AgentChecker(agentLiving, sidekick);
                if (state == Talking) {
                    if (agentLiving->allegiance == GW::Constants::Allegiance::Npc_Minipet) {
                        if (!agentLiving->GetIsAlive()) continue;
                        if (!closest_npc) {
                            closest_npc = agentLiving;
                        }
                        else if (GW::GetSquareDistance(party_leader->pos, agentLiving->pos) < GW::GetSquareDistance(party_leader->pos, closest_npc->pos)) {
                            closest_npc = agentLiving;
                        }
                    }
                }
            }
            else if (state == Following && (GW::GetDistance(party_leader->pos, sidekick->pos) <= GW::Constants::Range::Spellcast * 3 / 2)) {
                GW::AgentItem* itemAgent = a->GetAsAgentItem();
                if (!itemAgent) continue;
                if (itemAgent->owner && itemAgent->owner != sidekick->agent_id) continue;
                if (GW::GetSquareDistance(itemAgent->pos, sidekick->pos) > GW::Constants::SqrRange::Spirit || itemAgent->pos.zplane != sidekick->pos.zplane) continue;
                if (ShouldItemBePickedUp(itemAgent)) {
                    Log::Info("State: picking up");
                    item_to_pick_up = itemAgent->agent_id;
                    timers.interactTimer = TIMER_INIT();
                    GW::Agents::ChangeTarget(itemAgent->agent_id);
                    state = Picking_up;
                    GW::GameThread::Enqueue([]() -> void {
                        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                        return;
                    });
                    return;
                }
            }
        }
        if (checking_agents) {
            checking_agents = std::nullopt;
            FinishedCheckingAgentsCallback();
        }

        if (sidekick->GetIsMoving()) {
            timers.lastInteract = TIMER_INIT();
            if (first_move) {
                timers.movingTime = TIMER_INIT();
            }
        }
        else {
            first_move = true;
        }

        if (state == Following || state == Picking_up) {
            wardEffect = std::nullopt;

            if (!still_in_combat && closest_enemy && (GW::GetDistance(closest_enemy->pos, party_leader->pos) <= GW::Constants::Range::Earshot || GW::GetDistance(closest_enemy->pos, sidekick->pos) <= GW::Constants::Range::Earshot)) {
                if (!starting_combat) {
                    starting_combat = true;
                    timers.changeStateTimer = TIMER_INIT();
                }
                still_in_combat = true;
            }

            if (starting_combat && still_in_combat && TIMER_DIFF(timers.changeStateTimer) > 250 && !no_combat && GW::GetDistance(sidekick->pos, party_leader->pos) <= GW::Constants::Range::Spellcast * 3 / 2) {
                starting_combat = false;
                timers.changeStateTimer = TIMER_INIT();
                Log::Info("Entering combat...");
                StartCombat();
                state = Fighting;
                if (sidekick->GetIsMoving())
                    GW::GameThread::Enqueue([]() -> void {
                        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_CancelAction);
                        return;
                    });
                return;
            }
            else if (starting_combat && !still_in_combat) {
                starting_combat = false;
                timers.changeStateTimer = TIMER_INIT();
            }
        } else {
            if (!still_in_combat && closest_enemy && (GW::GetDistance(closest_enemy->pos, party_leader->pos) <= GW::Constants::Range::Spellcast * 5 / 4 || GW::GetDistance(sidekick->pos, sidekick->pos) <= GW::Constants::Range::Spellcast * 5 / 4)) {
                still_in_combat = true;
                timers.changeStateTimer = TIMER_INIT();
            }

            if (state == Fighting && (!still_in_combat && TIMER_DIFF(timers.changeStateTimer) > 250) || no_combat || GW::GetDistance(sidekick->pos, party_leader->pos) > GW::Constants::Range::Spellcast * 3 / 2) {
                starting_combat = false;
                state = Following;
                timers.changeStateTimer = TIMER_INIT();
                GW::GameThread::Enqueue([]() -> void {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_CancelAction);
                    return;
                    });
                StopCombat();
                wardEffect = std::nullopt;
                interrupts.clear();
                cast_times.clear();
                Log::Info("Leaving combat...");
                return;
            }
            else if (still_in_combat) {
                timers.changeStateTimer = TIMER_INIT();
            }

            if (!interrupts.empty()) {
                for (auto& it : interrupts) {
                    if (it.second.cast_time - TIMER_DIFF(it.second.cast_start) < 0 - ping) {
                        interrupts.erase(it.first);
                    }
                    else if (it.second.cast_time - TIMER_DIFF(it.second.cast_start) < 50 + 2 * ping) {
                        waitForInterrupt = true;
                    }
                }
            }

            if (waitForInterrupt && isCasting(sidekick)) {
                GW::GameThread::Enqueue([]() -> void {
                    GW::UI::Keypress(GW::UI::ControlAction::ControlAction_CancelAction);
                    return;
                    });
            }
        }

        switch (state) {
            case Following: {
                GW::PartyInfo* info = GW::PartyMgr::GetPartyInfo();

                if (!(info && info->players.valid()))
                    return;

                uint32_t called_target = info->players[0].calledTargetId;

                if (called_target && SetUpCombatSkills(called_target)) {
                    return;
                }

                if (UseOutOfCombatSkill())
                    return;

                const float distance = GW::GetDistance(sidekick->pos, party_leader->pos);
                if (distance > GW::Constants::Range::Adjacent && TIMER_DIFF(timers.followTimer) > 149 + rand() % 100)
                {
                    if (!sidekick->GetIsMoving() || distance > GW::Constants::Range::Earshot) {
                        GW::Agents::GoPlayer(party_leader);
                        reposition = true;
                        CheckStuck();
                        timers.followTimer = TIMER_INIT();
                    }
                }
                else if (distance <= GW::Constants::Range::Adjacent && reposition && !party_leader->GetIsMoving()) {
                    if (!isCasting(sidekick)) {
                        reposition = false;
                        GW::Agents::Move(CalculateInitialPosition(party_leader->pos, *group_center, sidekick_position, GW::Constants::Range::Adjacent / 2));
                    }
                }
                break;
            }
            case Fighting: {
                GW::PartyInfo* info = GW::PartyMgr::GetPartyInfo();

                if (!(info && info->players.valid()))
                    return;

                if (wardEffect && !(closest_enemy && GW::GetDistance(closest_enemy->pos, wardEffect->position) <= (GW::Constants::Range::Spellcast + GW::Constants::Range::Area / 2))) {
                    wardEffect = std::nullopt;
                }

                if (sidekick->energy < .5 && TIMER_DIFF(timers.lowEnergyTimer) > 6000 && sidekick->primary != 4 && !GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Blood_is_Power)) {
                    GW::Chat::SendChat('/', "yes");
                    timers.lowEnergyTimer = TIMER_INIT();
                }

                uint32_t called_target = info->players[0].calledTargetId;

                if (called_target) {
                    GW::Agent* called_agent = GW::Agents::GetAgentByID(called_target);
                    called_enemy = called_agent ? called_agent->GetAsAgentLiving() : nullptr;
                    if (called_enemy && !called_enemy->GetIsAlive())
                        called_enemy = nullptr;
                }

                const uint32_t target_id = GW::Agents::GetTargetId();

                if (prioritized_target) {
                    if (target_id != prioritized_target->agent_id) {
                        Log::Info("switching to priority target");
                        GW::Agents::ChangeTarget(prioritized_target->agent_id);
                        OnTargetSwitch();
                        return;
                    }
                }
                else if (called_enemy) {
                    if (target_id != called_enemy->agent_id) {
                        GW::Agents::ChangeTarget(called_enemy->agent_id);
                        OnTargetSwitch();
                        return;
                    }
                }
                else if (lowest_health_enemy) {
                    if (target_id != lowest_health_enemy->agent_id) {
                        GW::Agents::ChangeTarget(lowest_health_enemy->agent_id);
                        OnTargetSwitch();
                        return;
                    }
                }
                else if (closest_enemy) {
                    if (target_id != closest_enemy->agent_id) {
                        GW::Agents::ChangeTarget(closest_enemy->agent_id);
                        OnTargetSwitch();
                        return;
                    }
                }

                const GW::Agent* target = GW::Agents::GetTarget();

                if (group_center) {
                    float distance = GW::GetDistance(sidekick->pos, *group_center);
                    if ((!sidekick->GetIsMoving() && !isCasting(sidekick) && distance > GW::Constants::Range::Earshot) || distance > GW::Constants::Range::Spellcast * 3 / 2 && TIMER_DIFF(timers.followTimer) > 1000 + rand() % 100) {
                        Log::Info("Out of earshot");
                        float percentToMove = (distance - GW::Constants::Range::Earshot) / distance;
                        GW::GamePos new_position = {sidekick->pos.x + (group_center->x - sidekick->pos.x) * percentToMove, sidekick->pos.y + (group_center->y - sidekick->pos.y) * percentToMove, sidekick->pos.zplane};
                        GW::Agents::Move(new_position);
                        timers.followTimer = TIMER_INIT();
                        return;
                    }
                    else if (castingWard && !sidekick->GetIsMoving() && !isCasting(sidekick))
                        if (wardEffect) {
                            if (!target || (target && GW::GetDistance(wardEffect->position, target->pos) < GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4)) {
                                if (GW::GetDistance(wardEffect->position, sidekick->pos) > GW::Constants::Range::Touch) {
                                    GW::Agents::Move(wardEffect->position);
                                    return;
                                }
                                else {
                                    wardEffect = std::nullopt;
                                    return;
                                }
                            }
                        }
                        else if (target) {
                            wardEffect = std::nullopt;
                            float distanceToTarget = GW::GetDistance(target->pos, sidekick->pos);
                            if (distanceToTarget > GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4) {
                                Log::Info("Closing distance");
                                float percentToMove = (distanceToTarget - GW::Constants::Range::Earshot) / distanceToTarget;
                                GW::GamePos new_position = {sidekick->pos.x + (target->pos.x - sidekick->pos.x) * percentToMove, sidekick->pos.y + (target->pos.y - sidekick->pos.y) * percentToMove, sidekick->pos.zplane};
                                GW::Agents::Move(new_position);
                                timers.followTimer = TIMER_INIT();
                                return;
                            }
                        }
                }

                if (!waitForInterrupt && UseCombatSkill()) {
                    return;
                }

                if (should_kite && !isCasting(sidekick) && TIMER_DIFF(timers.kiteTimer) > 2250 && closest_enemy && GW::GetSquareDistance(closest_enemy->pos, sidekick->pos) <= GW::Constants::SqrRange::Adjacent * 3 / 2) {
                    if (!finishes_attacks || (finishes_attacks && TIMER_DIFF(timers.attackStartTimer) > 1000 * sidekick->weapon_attack_speed * sidekick->attack_speed_modifier / 2 + 50)) {
                        timers.kiteTimer = TIMER_INIT();
                        kiting_location = closest_enemy->pos;
                        Log::Info(wardEffect ? "Kiting With Ward" : "Kiting");
                        state = Kiting;
                        return;
                    }
                }

                if ((should_stay_near_center || wardEffect) && group_center && isUncentered(sidekick) &&
                    (!sidekick->GetIsMoving() || (sidekick->GetIsMoving() && TIMER_DIFF(timers.movingTime) > 1000 * sidekick->weapon_attack_speed * sidekick->attack_speed_modifier / 2 + 50)) && TIMER_DIFF(timers.followTimer) > 2000 + rand() % 100)
                    {
                    Log::Info("Staying near center");
                    float ratio = (GW::Constants::Range::Area - 50 ) / GW::GetDistance(*group_center, sidekick->pos);
                    
                    GW::GamePos new_position = {group_center->x + (sidekick->pos.x - group_center->x) * ratio, group_center->y + (sidekick->pos.y - group_center->y) * ratio, group_center->zplane};
                    GW::Agents::Move(new_position);
                    timers.followTimer = TIMER_INIT();
                }

                const GW::AgentLiving* targetLiving = target ? target->GetAsAgentLiving() : nullptr;

                if (!(targetLiving && targetLiving->GetIsAlive())) return;

                if (!sidekick->GetIsAttacking() && (!sidekick->GetIsMoving() || (sidekick->GetIsMoving() && TIMER_DIFF(timers.movingTime) > 1000 * sidekick->weapon_attack_speed * sidekick->attack_speed_modifier / 2 + 50 )) && !isCasting(sidekick) && TIMER_DIFF(timers.interactTimer) > 750 + rand() % 500) {
                    timers.interactTimer = TIMER_INIT();
                    CheckStuck();
                    GW::GameThread::Enqueue([&]() -> void {
                        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                        return;
                    });
                    Attack();
                }
                break;
            }
            case Kiting: {
                if (isCasting(sidekick)) {
                    timers.kiteTimer = TIMER_INIT();
                    return;
                }
                if (kiting_location && group_center) {
                    float new_angle = CalculateAngleToMoveAway(*kiting_location, sidekick->pos, *group_center, wardEffect ? GW::Constants::Range::Area : GW::Constants::Range::Earshot);
                    float distance = wardEffect ? GW::Constants::Range::Area / 2 : GW::Constants::Range::Earshot / 2;
                    GW::GamePos new_position = {sidekick->pos.x + std::cosf(new_angle) * distance, sidekick->pos.y + std::sinf(new_angle) * distance, sidekick->pos.zplane};
                    GW::Agents::Move(new_position);
                    kiting_location = std::nullopt;
                    return;
                }
                WhenKiting();
                if (TIMER_DIFF(timers.kiteTimer) >= 500 || (closest_enemy && GW::GetDistance(sidekick->pos, closest_enemy->pos) > (GW::Constants::Range::Nearby + GW::Constants::Range::Area) / 2)) {
                    state = Fighting;
                }
                break;
            }
            case Scattering: {
                if (isCasting(sidekick)) {
                    timers.scatterTimer = TIMER_INIT();
                    return;
                }
                if (shouldInputScatterMove && epicenter && group_center && area_of_effect) {
                    shouldInputScatterMove = false;
                    float new_angle = CalculateAngleToMoveAway(*kiting_location, sidekick->pos, *group_center, wardEffect ? GW::Constants::Range::Area : GW::Constants::Range::Earshot); 
                    GW::GamePos new_position = {sidekick->pos.x + std::cosf(new_angle) * area_of_effect, sidekick->pos.y + std::sinf(new_angle) * area_of_effect, sidekick->pos.zplane};
                    GW::Agents::Move(new_position);
                    timers.scatterTimer = TIMER_INIT();
                    return;
                }
                WhenKiting();
                if (TIMER_DIFF(timers.scatterTimer) >= 1000 || (epicenter && GW::GetDistance(sidekick->pos, *epicenter) > area_of_effect)) {
                    shouldInputScatterMove = false;
                    area_of_effect = 0;
                    epicenter = std::nullopt;
                    state = Fighting;
                }
                break;
            }
            case Obstructed: {
                GW::Agent* target = GW::Agents::GetTarget();
                if (!sidekick->GetIsMoving() && target && GW::GetDistance(sidekick->pos, target->pos) > GW::Constants::Range::Adjacent && TIMER_DIFF(timers.followTimer) > 149 + rand() % 100) {
                    GW::Agents::Move(target->pos);
                    timers.followTimer = TIMER_INIT();
                }
                if (TIMER_DIFF(timers.obstructedTimer) >= 1000) {
                    GW::GameThread::Enqueue([]() -> void {
                        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_CancelAction);
                        return;
                    });
                    state = Fighting;
                }
                break;
            }
            case Picking_up: {
                if (TIMER_DIFF(timers.interactTimer) > 3500 || !item_to_pick_up || !GW::Agents::GetAgentByID(item_to_pick_up) || GW::GetDistance(sidekick->pos, party_leader->pos) > GW::Constants::Range::Spellcast * 3 / 2) {
                    item_to_pick_up = 0;
                    state = Following;
                }
                break;
            }
            case Talking: {
                if (first_message) {
                    first_message = false;
                    if (closest_npc) {
                        GW::Agents::ChangeTarget(closest_npc->agent_id);
                        GW::GameThread::Enqueue([]() -> void {
                            GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                            return;
                        });
                    }
                    else {
                        state = Following;
                    }
                }
                if (dialog_id) {
                    GW::Agents::SendDialog(dialog_id);
                    dialog_id = 0;
                }
                if (GW::GetDistance(sidekick->pos, party_leader->pos) > GW::Constants::Range::Area && !sidekick->GetIsMoving()) {
                    state = Following;
                }
            }
        }
    }
}

void SidekickWindow::Draw(IDirect3DDevice9* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    if (!visible)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(100.0f, 50.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        if (ImGui::Button(enabled ? "Enabled###sidekicktoggle" : "Disabled###sidekicktoggle", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            ToggleEnable();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void SidekickWindow::GenericValueCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id)
{
    UNREFERENCED_PARAMETER(value_id);
    UNREFERENCED_PARAMETER(caster_id);
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(target_id);

    using namespace GW::Packet::StoC;

    uint32_t playerId = GW::Agents::GetPlayerId();

    if (!playerId)
        return;

    switch (value_id) {
        case GenericValueID::skill_damage: {
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
            if (skill_info && (skill_info->aoe_range > 0 || skill_info->skill_id == GW::Constants::SkillID::Ray_of_Judgment || skill_info->skill_id == GW::Constants::SkillID::Flare) && (skill_info->duration0 > 0 && skill_info->skill_id != GW::Constants::SkillID::Ignite_Arrows || target_id != playerId))
                {
                if (!shouldInputScatterMove && TIMER_DIFF(timers.scatterTimer) > 2000) {
                    GW::Agent* sidekick = GW::Agents::GetPlayer();
                    if (!sidekick) return;
                    area_of_effect = 50.0f + std::max(skill_info->aoe_range, GW::Constants::Range::Adjacent);
                    epicenter = sidekick->pos;
                    shouldInputScatterMove = true;
                    timers.scatterTimer = TIMER_INIT();
                    Log::Info("Scattering due to damage.");
                    state = Scattering;
                }
            }
            break;
        };
        case GenericValueID::effect_on_agent:
        case GenericValueID::effect_on_target: {
            if (party_ids.contains(caster_id)) { 
                if (value == 1938 || (value == 1939 && !finishes_attacks))
                {
                    GW::Agent* agent = GW::Agents::GetAgentByID(caster_id);
                    GW::Agent* sidekick = GW::Agents::GetPlayer();
                    if (agent && sidekick && GW::GetDistance(agent->pos, sidekick->pos) < GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4) {
                        SkillDuration skillDuration = {TIMER_INIT(), 16000};
                        Ward ward = {agent->pos, skillDuration};
                        wardEffect = ward;
                    }
                }             }
            EffectOnTarget(*target_id, value);
            break;
        }
        case GenericValueID::attack_stopped:
        case GenericValueID::melee_attack_finished: {
            AttackFinished(caster_id);
            break;
        }
        case GenericValueID::attack_started: {
            if (caster_id == playerId) {
                timers.attackStartTimer = TIMER_INIT();
                timers.lastInteract = TIMER_INIT();
            }
            break;
        }
        case GenericValueID::add_effect: {
            AddEffectCallback(caster_id, value);
            break;
        }
        case GenericValueID::remove_effect: {
            RemoveEffectCallback(caster_id, value);
            break;
        }
        case GenericValueID::skill_activated: {
            if (caster_id != playerId && party_ids.contains(caster_id) && wardSkills.contains(static_cast<GW::Constants::SkillID>(value))) {
                GW::Agent* agent = GW::Agents::GetAgentByID(caster_id);
                GW::Agent* sidekick = GW::Agents::GetPlayer();
                if (agent && sidekick && GW::GetDistance(agent->pos, sidekick->pos) < GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4) {
                    float cast_time = cast_times[caster_id];
                    cast_times.erase(caster_id);
                    SkillDuration skillDuration = {TIMER_INIT(), cast_time || 1000 };
                    Ward ward = {agent->pos, skillDuration};
                    wardEffect = ward;
                }                
            }

            [[fallthrough]];
        } 
        case GenericValueID::attack_skill_activated: {
            if (caster_id == playerId) {
                timers.lastInteract = TIMER_INIT();
            }
            SkillCastScatter(caster_id, value, target_id);
            [[fallthrough]];
        }
        case GenericValueID::instant_skill_activated: {
             if (target_id == playerId && interruptSkillsWithFlightTime.contains(static_cast<GW::Constants::SkillID>(value)))
                HandleInterrupts(value_id, caster_id, value);
            SkillCallback(value_id, caster_id, value, target_id);
            break;
        }
        case GenericValueID::skill_finished:
        case GenericValueID::attack_skill_finished: {
            SkillCastFinishScatter(caster_id);
            [[fallthrough]];
        }
         case GenericValueID::attack_skill_stopped:
         case GenericValueID::skill_stopped:
         {
            if (scatterCastMap.contains(caster_id)) 
                scatterCastMap.erase(caster_id);
            if (value_id == GenericValueID::attack_skill_stopped || value_id == GenericValueID::skill_stopped) {
                if (interrupts.contains(caster_id)) interrupts.erase(caster_id);
            }   
            SkillFinishCallback(caster_id);
            break;
        }
        default: return;
    }
}

void SidekickWindow::SkillCastScatter(uint32_t caster, uint32_t value, std::optional<uint32_t> target) {
    if (!target) return;
    GW::Agent* casterAgent = GW::Agents::GetAgentByID(caster);
    GW::AgentLiving* casterLiving = casterAgent ? casterAgent->GetAsAgentLiving() : nullptr;

    if (!casterLiving || casterLiving->allegiance != GW::Constants::Allegiance::Enemy) return;

    GW::Agent* targetAgent = GW::Agents::GetAgentByID(*target);
    GW::Agent* sidekick = GW::Agents::GetPlayer();
    if (!targetAgent || !sidekick) return;
    GW::AgentLiving* targetLiving = targetAgent->GetAsAgentLiving();
    if (!targetLiving) return;
    if (!(target == sidekick->agent_id || targetLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet || targetLiving->allegiance == GW::Constants::Allegiance::Ally_NonAttackable || targetLiving->allegiance == GW::Constants::Allegiance::Minion)) return;
    GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
    if (!(skill_info && (skill_info->aoe_range >= GW::Constants::Range::Adjacent / 2 || skill_info->skill_id == GW::Constants::SkillID::Ray_of_Judgment || skill_info->skill_id == GW::Constants::SkillID::Flare) &&
          (skill_info->duration0 > 0 || sidekick->agent_id != target)
        ))
        return;

    SkillActivationPacket packet = {skill_info->skill_id, targetLiving->agent_id};

    scatterCastMap.insert_or_assign(caster, packet);
}

void SidekickWindow::SkillCastFinishScatter(uint32_t caster) {
    if (scatterCastMap.contains(caster)) {
        SkillActivationPacket packet = scatterCastMap[caster];
        GW::Agent* targetAgent = GW::Agents::GetAgentByID(packet.target);
        GW::AgentLiving* sidekick = GW::Agents::GetCharacter();
        if (!shouldInputScatterMove && targetAgent && sidekick) {
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(packet.skill);
            float aoe_range = std::max(skill_info->aoe_range, GW::Constants::Range::Adjacent);
            if (GW::GetDistance(targetAgent->pos, sidekick->pos) <= aoe_range && targetAgent->pos.zplane == sidekick->pos.zplane && state != Scattering && TIMER_DIFF(timers.scatterTimer > 3000)) {
                area_of_effect = 50.0f + aoe_range;
                epicenter = targetAgent->pos;
                shouldInputScatterMove = true;
                timers.scatterTimer = TIMER_INIT();
                Log::Info("Scattering due to cast.");
                state = Scattering;
            }
        }
        scatterCastMap.erase(caster);
    }
}


 void SidekickWindow::OnServerPing(uint32_t packetPing)
 {
     if (packetPing > 4999)
         return; // GW checks this too.
     ping = packetPing;
 }

bool SidekickWindow::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(agentLiving);
    UNREFERENCED_PARAMETER(playerLiving);
    return true;
}


bool SidekickWindow::UseCombatSkill() { return false; }

bool SidekickWindow::UseOutOfCombatSkill() { return false; }

bool SidekickWindow::SetUpCombatSkills(uint32_t called_target_id)
{
    UNREFERENCED_PARAMETER(called_target_id);
    return false;
}

float SidekickWindow::CalculateAngleToMoveAway(GW::GamePos position_away, GW::GamePos player_position, GW::GamePos group_position, float distance) {
    const float percentOfRadius = GW::GetDistance(group_position, player_position) / (distance * 1.3f);

    const float angleAwayFromEpicenter = std::atan2f(player_position.y - position_away.y, player_position.x - position_away.x);

    const float angleAwayFromGroup = std::atan2f(-player_position.y + group_position.y, -player_position.x + group_position.x);

    return ((1.0f - percentOfRadius) * angleAwayFromEpicenter + angleAwayFromGroup * percentOfRadius);
}

GW::GamePos SidekickWindow::CalculateInitialPosition(GW::GamePos player_position, GW::GamePos group_position, size_t idx, float distance) {
    if (player_position.zplane != group_position.zplane || idx == 0) {
        return group_position;
    }

    const float player_angle = std::atan2f(player_position.y - group_position.y, player_position.x - group_position.x);

    const float new_angle = player_angle + IM_PI/2 + idx * IM_PI/4;
    
    GW::GamePos new_position = { (group_position.x + player_position.x) / 2 + distance * std::cosf(new_angle), (group_position.y + player_position.y) / 2 + distance * std::sinf(new_angle), group_position.zplane };

    return new_position;
}

void SidekickWindow::ResetTargetValues() {

}

bool SidekickWindow::ShouldItemBePickedUp(GW::AgentItem* itemAgent)
{
    GW::Item* actual_item = GW::Items::GetItemById(itemAgent->item_id);
    if (!actual_item) return false;
    std::pair<GW::Bag*, uint32_t> pair = inventory_manager.GetAvailableInventorySlot(actual_item); 
    if (!pair.first) return false;
    switch (actual_item->complete_name_enc[0]) {
        case 2626:
        case 2624:
        case 2627:
            return true;
        case 2623:
            return actual_item->is_material_salvageable;
    }
    if (actual_item->GetIsMaterial()) return true;
    if (actual_item->GetIsStackable()) return true;
    return false;
}

bool SidekickWindow::CanUseSkill(GW::SkillbarSkill skillbar_skill, GW::Skill* skill_info, float cur_energy) {
    if ((skill_info->adrenaline == 0 && skillbar_skill.GetRecharge()) || (skill_info->adrenaline > 0 && skillbar_skill.adrenaline_a < skill_info->adrenaline)) {
       return false;
    }


    if (cur_energy <= skill_info->GetEnergyCost()) {
        return false;
    }

    return true;
}

void SidekickWindow::AddEffectCallback(const uint32_t agent_id, const uint32_t value) 
{
    UNREFERENCED_PARAMETER(agent_id);
    UNREFERENCED_PARAMETER(value);
}

void SidekickWindow::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    UNREFERENCED_PARAMETER(agent_id);
    UNREFERENCED_PARAMETER(value);
}
    
void SidekickWindow::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id)
{
    UNREFERENCED_PARAMETER(value_id);
    UNREFERENCED_PARAMETER(caster_id);
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(target_id);
}

void SidekickWindow::SkillFinishCallback(const uint32_t caster_id) {
    UNREFERENCED_PARAMETER(caster_id);
}


void SidekickWindow::GenericModifierCallback(uint32_t type, uint32_t caster_id, float value, uint32_t cause_id) {
    UNREFERENCED_PARAMETER(type);
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(caster_id);
    UNREFERENCED_PARAMETER(cause_id);
}

void SidekickWindow::ResetTargets()
{
    enemyProximityMap.clear();
    prioritized_target = nullptr;
    called_enemy = nullptr;
    closest_enemy = nullptr;
    lowest_health_enemy = nullptr;
    lowest_health_ally = nullptr;
    closest_npc = nullptr;
    lowest_health_other_ally = nullptr;
    ResetTargetValues();
    waitForInterrupt = false;
}

void SidekickWindow::HardReset()
{
    return;
}

bool SidekickWindow::isCasting(GW::AgentLiving* agentLiving) {

    GW::Skillbar* skillbar = GW::Agents::GetPlayerId() == agentLiving->agent_id ? GW::SkillbarMgr::GetPlayerSkillbar() : nullptr;

    return (agentLiving->skill || agentLiving->GetIsCasting() || (skillbar && skillbar->casting));
}

bool SidekickWindow::isUncentered(GW::AgentLiving* agentLiving) {
    return (group_center && GW::GetSquareDistance(agentLiving->pos, *group_center) > GW::Constants::SqrRange::Area);
}

void SidekickWindow::Setup()
{
    return;
}

void SidekickWindow::Attack()
{
    return;
}

void SidekickWindow::WhenKiting() {
    return;
}

void SidekickWindow::AttackFinished(uint32_t caster_id)
{
    UNREFERENCED_PARAMETER(caster_id);
    return;
}

void SidekickWindow::StopCombat() {
    return;
}

void SidekickWindow::StartCombat()
{
    return;
}

void SidekickWindow::EffectOnTarget(uint32_t target, const uint32_t value) {
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(target);
    return;
}

void SidekickWindow::CustomLoop(GW::AgentLiving* sidekick)
{
    UNREFERENCED_PARAMETER(sidekick);
    return;
}

bool SidekickWindow::UseSkillWithTimer(uint32_t slot, uint32_t target, int32_t time) {
    if (TIMER_DIFF(timers.skillTimers[slot]) < time) return false;

    timers.skillTimers[slot] = TIMER_INIT();
    GW::SkillbarMgr::UseSkill(slot, target);
    return true;
}

void SidekickWindow::CantAct() {
    return;
}

void SidekickWindow::OnTargetSwitch() {
    GW::GameThread::Enqueue([&]() -> void {
        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
        return;
    });
    TargetSwitchEffect();
    return;
}

void SidekickWindow::TargetSwitchEffect() {
    return;
}

void SidekickWindow::CheckStuck() {
    const clock_t stuckTimerElapse = TIMER_DIFF(timers.stuckTimer);

    if (stuckTimerElapse > 2000)
    {
        timers.stuckTimer = TIMER_INIT();
    }
    else if (stuckTimerElapse > 1000)
    {
        if (TIMER_DIFF(timers.lastInteract) > 5000) {
            Log::Info("stuck");
            GW::Chat::SendChat('/', "stuck");
            timers.stuckTimer = TIMER_INIT();
        }
    }

    return;
}

void SidekickWindow::FinishedCheckingAgentsCallback()
{
    return;
}

void SidekickWindow::CheckForProximity(GW::AgentLiving* agentLiving) {
    Proximity proximity = { agentLiving->pos, 0, 0, 0 };

    for (auto& it : enemyProximityMap) {
        if (it.first == agentLiving->agent_id) continue;
        float distance = GW::GetSquareDistance(proximity.position, it.second.position);
        if (distance <= GW::Constants::SqrRange::Adjacent) {
            proximity.adjacent += 1;
            proximity.nearby += 1;
            proximity.area += 1;
            it.second.adjacent += 1;
            it.second.nearby += 1;
            it.second.area += 1;
        }
        else if (distance <= GW::Constants::SqrRange::Nearby) {
            proximity.nearby += 1;
            proximity.area += 1;
            it.second.nearby += 1;
            it.second.area += 1;
        }
        else if (distance <= GW::Constants::SqrRange::Area) {
            proximity.area += 1;
            it.second.area += 1;
        }
    }
    enemyProximityMap.insert_or_assign(agentLiving->agent_id, proximity);
}

void SidekickWindow::AddEffectPacketCallback(GW::Packet::StoC::AddEffect* packet){
    UNREFERENCED_PARAMETER(packet);
}

void SidekickWindow::HandleInterrupts(const uint32_t value_id, const uint32_t caster_id, const uint32_t value) {
    float cast_time = cast_times[caster_id];
    cast_times.erase(caster_id);

    const auto agent = GW::Agents::GetAgentByID(caster_id);
    const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!living_agent) return;

     GW::Constants::SkillID skill_id = static_cast<GW::Constants::SkillID>(value);

    const auto skill = GW::SkillbarMgr::GetSkillConstantData(skill_id);

      if (skill) {
        if (!cast_time) {
            cast_time = skill->activation;
            if (value_id == GW::Packet::StoC::GenericValueID::attack_skill_activated) {
                if (!cast_time) {
                    cast_time = living_agent->weapon_attack_speed * living_agent->attack_speed_modifier;
                }
            }
        }
      }
      if (value_id == GW::Packet::StoC::GenericValueID::attack_skill_activated) cast_time /= 2;

      if (interruptSkillsWithFlightTime[skill_id]) {
        const auto* sidekick =  GW::Agents::GetPlayerAsAgentLiving();
        if (sidekick) cast_time += static_cast<uint32_t>(GW::GetDistance(sidekick->pos, living_agent->pos) * .042);
      }

      Interrupt interrupt = {
          caster_id,
          TIMER_INIT(),
          static_cast<uint32_t>(cast_time * 1000),
      };
      interrupts.insert_or_assign(caster_id, interrupt);
}

void SidekickWindow::MessageCallBack(GW::Packet::StoC::MessageCore* packet) {
      UNREFERENCED_PARAMETER(packet);
}
