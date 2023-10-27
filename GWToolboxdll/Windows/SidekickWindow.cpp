#include "stdafx.h"

#include <optional>
#include <thread>

#include <GWCA/Constants/Constants.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Attribute.h>
#include <GWCA/GameEntities/Item.h>
#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Managers/AgentMgr.h>
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
    uint32_t group_position_delay_frames = 0;
    InventoryManager inventory_manager;
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

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValue>(&GenericValueSelf_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValue* packet) -> void {
        UNREFERENCED_PARAMETER(status);

        const uint32_t value_id = packet->value_id;
        const uint32_t caster_id = packet->agent_id;
        const uint32_t value = packet->value;

        GenericValueCallback(value_id, caster_id, value);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        using namespace GW::Packet::StoC::GenericValueID;

        const uint32_t value_id = packet->Value_id;
        const uint32_t caster_id = packet->caster;
        const uint32_t target_id = packet->target;
        const uint32_t value = packet->value;

        const bool isSwapped = value_id == skill_activated || value_id == attack_skill_activated || value_id == attack_started;
        GenericValueCallback(value_id, isSwapped ? target_id : caster_id, value, isSwapped ? caster_id : target_id);
    });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::PartyInviteReceived_Create>(&PartyInvite, [this](GW::HookStatus* status, GW::Packet::StoC::PartyInviteReceived_Create* packet) -> void {
        UNREFERENCED_PARAMETER(status);

        party_invite = packet->target_party_id;
        Log::Info("invite received");
    });
}

void SidekickWindow::Update(float delta)
{
    UNREFERENCED_PARAMETER(delta);

    if (!enabled)
        return;

    if (TIMER_DIFF(timers.activityTimer) < 20)
        return;


    timers.activityTimer = TIMER_INIT();

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        party_ids = {};
        party_leader_id = 0;
        HardReset();
    }
    else if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        party_ids = {};
        party_leader_id = 0;
        if (party_invite != 0) {
            GW::PartyMgr::RespondToPartyRequest(party_invite, true);
            party_invite = 0;
            Log::Info("Invite accept");
        }
    }
    else {
        if (!party_leader_id) {
            GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
            if (!(party && party->players.valid() && party->players.size())) {
                party_leader_id = 0;
            }
            else {
                party_leader_id = GW::Agents::GetAgentIdByLoginNumber(party->players[0].login_number);
            }
            return;
        }

        if (party_ids.empty()) {
            GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
            if (!party)
                return;
            if (party->players.valid())
                for (size_t i = 0; i < party->players.size(); i++) {
                    if (!party->players[i].login_number)
                        continue;
                    party_ids.insert(GW::Agents::GetAgentIdByLoginNumber(party->players[i].login_number));
                }
            if (party->heroes.valid())
                for (size_t i = 0; i < party->heroes.size(); i++) {
                    if (!party->heroes[i].agent_id)
                        continue;
                    party_ids.insert(party->heroes[i].agent_id);
                }
            if (party->henchmen.valid())
                for (size_t i = 0; i < party->henchmen.size(); i++) {
                    if (!party->henchmen[i].agent_id)
                        continue;
                    party_ids.insert(party->henchmen[i].agent_id);
                }
            return;
        }

        GW::PartyInfo* party = GW::PartyMgr::GetPartyInfo();
        if (!party) return;

        ResetTargets();

        if (group_position_delay_frames == 0) {
            GW::Attribute* attributes = GW::PartyMgr::GetAgentAttributes(GW::Agents::GetPlayerId());
            assert(attributes);
            attributes = &attributes[static_cast<uint8_t>(GW::Constants::Attribute::Expertise)];
            if (attributes && attributes->level) {
                expertise = attributes->level;
            }
            else {
                expertise = 0;
            }

            std::optional<GW::GamePos> sum_position = std::nullopt;
            size_t party_count = 0;
            if (party->players.valid())
                for (size_t i = 0; i < party->players.size(); i++) {
                    const uint32_t agentId = GW::Agents::GetAgentIdByLoginNumber(party->players[i].login_number);
                    GW::Agent* player = GW::Agents::GetAgentByID(agentId);
                    if (player) {
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
            if (party->heroes.valid())
                for (size_t i = 0; i < party->heroes.size(); i++) {
                    if (!party->heroes[i].agent_id) continue;
                    GW::Agent* hero = GW::Agents::GetAgentByID(party->heroes[i].agent_id);
                    if (hero) {
                        if (!sum_position) {
                            sum_position = hero->pos;
                            party_count += 1;
                        }
                        else if (sum_position->zplane == hero->pos.zplane) {
                            sum_position->x += hero->pos.x;
                            sum_position->y += hero->pos.y;
                            party_count += 1;
                        }
                    }
                }
            if (party->henchmen.valid())
                for (size_t i = 0; i < party->henchmen.size(); i++) {
                    if (!party->henchmen[i].agent_id) continue;
                    GW::Agent* henchman = GW::Agents::GetAgentByID(party->henchmen[i].agent_id);
                    if (henchman) {
                        if (!sum_position) {
                            sum_position = henchman->pos;
                            party_count += 1;
                        }
                        else if (sum_position->zplane == henchman->pos.zplane) {
                            sum_position->x += henchman->pos.x;
                            sum_position->y += henchman->pos.y;
                            party_count += 1;
                        }
                    }
                }

            if (!sum_position || !party_count) return;

            group_center = {sum_position->x / party_count, sum_position->y / party_count, sum_position->zplane};
        }

        group_position_delay_frames += 1;

        if (group_position_delay_frames >= 10) {
            group_position_delay_frames = 0;
        }

        GW::AgentLiving* sidekick = GW::Agents::GetCharacter();
        if (!sidekick)
            return;

        GW::Agent* leader = GW::Agents::GetAgentByID(party_leader_id);
        GW::AgentLiving* party_leader = leader ? leader->GetAsAgentLiving() : nullptr;

        if (!party_leader)
            return;

        if (sidekick->skill || sidekick->GetIsKnockedDown() || sidekick->GetIsCasting() || !sidekick->GetIsAlive() || (TIMER_DIFF(timers.skillTimer) < 4000 && using_skill))
            return;

        switch (state) {
            case Following: {
                if (GW::GetSquareDistance(sidekick->pos, party_leader->pos) > GW::Constants::SqrRange::Area) {
                    if (TIMER_DIFF(timers.followTimer) > 4000) {
                        GW::Agents::GoPlayer(party_leader);
                        timers.followTimer = TIMER_INIT();
                    }
                    return;
                }

                GW::PartyInfo* info = GW::PartyMgr::GetPartyInfo();

                if (!(info && info->players.valid()))
                    return;

                uint32_t called_target = info->players[0].calledTargetId;

                GW::AgentArray* agent_array = GW::Agents::GetAgentArray();

                if (!agent_array)
                    return;

                bool still_in_combat = false;

                for (auto* a : *agent_array) {
                    if (!a)
                        continue;
                    GW::AgentLiving* agentLiving = a->GetAsAgentLiving();

                    if (agentLiving) {
                        if (!agentLiving->GetIsAlive()) continue;
                        switch (agentLiving->allegiance) {
                            case GW::Constants::Allegiance::Enemy: {
                                if (!closest_enemy) {
                                    closest_enemy = agentLiving;
                                }
                                else if (GW::GetSquareDistance(sidekick->pos, agentLiving->pos) < GW::GetSquareDistance(sidekick->pos, closest_enemy->pos)) {
                                    closest_enemy = agentLiving;
                                }
                                break;
                            }
                            case GW::Constants::Allegiance::Ally_NonAttackable: {
                                if (agentLiving->agent_id && party_ids.contains(agentLiving->agent_id)) {
                                    OutOfCombatAgentChecker(agentLiving, sidekick);
                                    if (agentLiving->GetInCombatStance() || agentLiving->GetIsAttacking()) {
                                        if (!starting_combat) {
                                            starting_combat = true;
                                            timers.changeStateTimer = TIMER_INIT();
                                        }
                                        still_in_combat = true;
                                    }
                                    if (agentLiving->hp < 1.0f && (!lowest_health_ally || lowest_health_ally->hp > agentLiving->hp)) lowest_health_ally = agentLiving;
                                }
                            }
                        }
                    }
                    else {
                        GW::AgentItem* itemAgent = a->GetAsAgentItem();
                        if (!itemAgent) continue;
                        if (itemAgent->owner && itemAgent->owner != sidekick->agent_id) continue;
                        if (GW::GetSquareDistance(itemAgent->pos, sidekick->pos) > GW::Constants::SqrRange::Spirit) continue;
                        if (ShouldItemBePickedUp(itemAgent)) {
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

                if (closest_enemy) {
                    if (GW::GetSquareDistance(sidekick->pos, closest_enemy->pos) < GW::Constants::SqrRange::Earshot || GW::GetSquareDistance(party_leader->pos, closest_enemy->pos) < GW::Constants::SqrRange::Earshot) {
                        if (!starting_combat) {
                            starting_combat = true;
                            timers.changeStateTimer = TIMER_INIT();
                        }
                        still_in_combat = true;
                    }
                }

                if (called_target && SetUpCombatSkills()) {
                    timers.skillTimer = TIMER_INIT();
                    using_skill = true;
                    return;
                }

                if (starting_combat && still_in_combat && TIMER_DIFF(timers.changeStateTimer) > 350) {
                    starting_combat = false;
                    timers.changeStateTimer = TIMER_INIT();
                    Log::Info("Entering combat...");
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
                }

                if (UseOutOfCombatSkill()) {
                    timers.skillTimer = TIMER_INIT();
                    using_skill = true;
                    return;
                }

                if (GW::GetSquareDistance(sidekick->pos, party_leader->pos) > GW::Constants::SqrRange::Adjacent && TIMER_DIFF(timers.followTimer) > 149 + rand() % 100) {
                    GW::Agents::GoPlayer(party_leader);
                    timers.followTimer = TIMER_INIT();
                }

                break;
            }
            case Fighting: {
                GW::PartyInfo* info = GW::PartyMgr::GetPartyInfo();

                if (!(info && info->players.valid()))
                    return;

                uint32_t called_target = info->players[0].calledTargetId;

                if (called_target) {
                    GW::Agent* called_agent = GW::Agents::GetAgentByID(called_target);
                    called_enemy = called_agent ? called_agent->GetAsAgentLiving() : nullptr;
                    if (called_enemy && !called_enemy->GetIsAlive())
                        called_enemy = nullptr;
                }

                GW::AgentArray* agent_array = GW::Agents::GetAgentArray();

                if (!agent_array)
                    return;

                bool still_in_combat = false;

                for (auto* a : *agent_array) {
                    if (!a)
                        continue;
                    GW::AgentLiving* agentLiving = a->GetAsAgentLiving();

                    if (agentLiving) {
                        switch (agentLiving->allegiance) {
                            case GW::Constants::Allegiance::Enemy: {
                                if (agentLiving->GetIsAlive()) {
                                    if (GW::GetSquareDistance(agentLiving->pos, sidekick->pos) < GW::Constants::SqrRange::Spellcast) {
                                        if (!closest_enemy) {
                                            closest_enemy = agentLiving;
                                        }
                                        else if (GW::GetSquareDistance(sidekick->pos, agentLiving->pos) < GW::GetSquareDistance(sidekick->pos, closest_enemy->pos)) {
                                            closest_enemy = agentLiving;
                                        }
                                    }
                                    if (agentLiving->hp < 1.0f && (!lowest_health_enemy || lowest_health_enemy->hp > agentLiving->hp)) lowest_health_enemy = agentLiving;
                                }
                                break;
                            }
                            case GW::Constants::Allegiance::Spirit_Pet: {
                                break;
                            }
                            case GW::Constants::Allegiance::Ally_NonAttackable: {
                                if (agentLiving->agent_id && party_ids.contains(agentLiving->agent_id)) {
                                    if (agentLiving->GetInCombatStance() || agentLiving->GetIsAttacking()) {
                                        if (!still_in_combat) {
                                            still_in_combat = true;
                                            timers.changeStateTimer = TIMER_INIT();
                                        }
                                    }
                                    if (agentLiving->hp < 1.0f && (!lowest_health_ally || lowest_health_ally->hp > agentLiving->hp)) lowest_health_ally = agentLiving;
                                }
                            }
                        }
                        InCombatAgentChecker(agentLiving, sidekick);
                    }
                }

                if (!still_in_combat && closest_enemy && GW::GetSquareDistance(closest_enemy->pos, sidekick->pos) <= GW::Constants::SqrRange::Spellcast) {
                    still_in_combat = true;
                    timers.changeStateTimer = TIMER_INIT();
                }

                if (!still_in_combat && TIMER_DIFF(timers.changeStateTimer) > 350) {
                    starting_combat = false;
                    state = Following;
                    timers.changeStateTimer = TIMER_INIT();
                    Log::Info("Leaving combat...");
                    return;
                }

                if (should_kite && TIMER_DIFF(timers.kiteTimer) > 4000 && closest_enemy && GW::GetSquareDistance(closest_enemy->pos, sidekick->pos) <= GW::Constants::SqrRange::Adjacent) {
                    if (!finishes_attacks || (finishes_attacks && TIMER_DIFF(timers.attackStartTimer) > sidekick->weapon_attack_speed * sidekick->attack_speed_modifier / 2)) {
                        timers.kiteTimer = TIMER_INIT();
                        kiting_location = closest_enemy->pos;
                        state = Kiting;
                        return;
                    }
                }

                const uint32_t target_id = GW::Agents::GetTargetId();

                if (called_enemy) {
                    if (target_id != called_enemy->agent_id) {
                        GW::Agents::ChangeTarget(called_enemy->agent_id);
                        return;
                    }
                }
                else if (lowest_health_enemy) {
                    if (target_id != lowest_health_enemy->agent_id) {
                        GW::Agents::ChangeTarget(lowest_health_enemy->agent_id);
                        return;
                    }
                }
                else if (closest_enemy) {
                    if (target_id != closest_enemy->agent_id) {
                        GW::Agents::ChangeTarget(closest_enemy->agent_id);
                        return;
                    }
                }

                if (UseCombatSkill()) {
                    timers.skillTimer = TIMER_INIT();
                    using_skill = true;
                    return;
                }

                const GW::Agent* target = GW::Agents::GetTarget();

                const GW::AgentLiving* targetLiving = target ? target->GetAsAgentLiving() : nullptr;

                if (!(targetLiving && targetLiving->GetIsAlive())) return;

                if (!sidekick->GetIsAttacking() && !sidekick->GetIsMoving() && TIMER_DIFF(timers.interactTimer) > 1500) {
                    timers.interactTimer = TIMER_INIT();
                    GW::GameThread::Enqueue([]() -> void {
                        GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                        return;
                    });
                }
                break;
            }
            case Kiting: {
                if (kiting_location && group_center) {
                    float new_angle = CalculateAngleToMoveAway(*kiting_location, sidekick->pos, *group_center);
                    GW::GamePos new_position = {sidekick->pos.x + std::cosf(new_angle) * GW::Constants::Range::Area, sidekick->pos.y + std::sinf(new_angle) * GW::Constants::Range::Area, sidekick->pos.zplane};
                    GW::Agents::Move(new_position);
                    kiting_location = std::nullopt;
                    return;
                }
                if (TIMER_DIFF(timers.kiteTimer) >= 1250) {
                    state = Fighting;
                }
                break;
            }
            case Scattering: {
                if (epicenter && group_center && area_of_effect) {
                    float new_angle = CalculateAngleToMoveAway(*kiting_location, sidekick->pos, *group_center); 
                    GW::GamePos new_position = {sidekick->pos.x + std::cosf(new_angle) * area_of_effect, sidekick->pos.y + std::sinf(new_angle) * area_of_effect, sidekick->pos.zplane};
                    GW::Agents::Move(new_position);
                    area_of_effect = 0;
                    return;
                }
                if (!sidekick->GetIsMoving() && TIMER_DIFF(timers.scatterTimer) >= 1000) {
                    state = Fighting;
                }
                break;
            }
            case Picking_up: {
                if (TIMER_DIFF(timers.interactTimer) > 7000 || !item_to_pick_up || !GW::Agents::GetAgentByID(item_to_pick_up)) {
                    item_to_pick_up = 0;
                    state = Following;
                }
                break;
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
    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
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

    using namespace GW::Packet::StoC;

    uint32_t playerId = GW::Agents::GetPlayerId();

    if (!playerId)
        return;

    switch (value_id) {
        case GenericValueID::skill_damage: {
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
            if (skill_info && skill_info->aoe_range > 0 && skill_info->duration0 > 0) {
                area_of_effect = skill_info->aoe_range;
                if (TIMER_DIFF(timers.scatterTimer) > 2000) {
                    timers.scatterTimer = TIMER_INIT();
                    state = Scattering;
                }
            }
            break;
        };
        case GenericValueID::effect_on_target: {
            if (!target_id) return;
            GW::Agent* target = GW::Agents::GetAgentByID(*target_id);
            GW::Agent* sidekick = GW::Agents::GetPlayer();
            if (!target || !sidekick) return;
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
            if (!(skill_info && skill_info->aoe_range <= 0)) return;
            if (GW::GetDistance(target->pos, sidekick->pos) > skill_info->aoe_range) return;
            area_of_effect = skill_info->aoe_range;
            epicenter = target->pos; 
            if (TIMER_DIFF(timers.scatterTimer > 4000)) {
                timers.scatterTimer = TIMER_INIT();
                state = Scattering;
            }
            break;
        }
        case GenericValueID::attack_started: {
            if (caster_id == playerId)
                timers.attackStartTimer = TIMER_INIT();
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
        case GenericValueID::skill_activated: 
        case GenericValueID::attack_skill_activated:
        case GenericValueID::instant_skill_activated: {
            SkillCallback(caster_id, value, target_id);
            [[fallthrough]];
        }
       case GenericValueID::skill_stopped:
        case GenericValueID::attack_skill_stopped: {
            if (caster_id == playerId)
                using_skill = false;
            break;
        }
        default: return;
    }
}

bool SidekickWindow::InCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(agentLiving);
    UNREFERENCED_PARAMETER(playerLiving);
    return true;
}

bool SidekickWindow::OutOfCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(agentLiving);
    UNREFERENCED_PARAMETER(playerLiving);
    return true;
}

bool SidekickWindow::UseCombatSkill() { return false; }

bool SidekickWindow::UseOutOfCombatSkill() { return false; }

bool SidekickWindow::SetUpCombatSkills() { return false; }

float SidekickWindow::CalculateAngleToMoveAway(GW::GamePos position_away, GW::GamePos player_position, GW::GamePos group_position) {
    const float percentOfRadius = GW::GetDistance(group_position, player_position) / GW::Constants::Range::Spellcast * 100;

    const float angleAwayFromEpicenter = std::atan2f(player_position.y - position_away.y, player_position.x - position_away.x);

    const float angleAwayFromGroup = std::atan2f(player_position.y - group_position.y, player_position.x - group_position.x);

    return (angleAwayFromEpicenter - angleAwayFromGroup * percentOfRadius) / 2;
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


    if (cur_energy < skill_info->GetEnergyCost() * (expertise ? 1.0 - ExpertiseToSkillReduction(skill_info) : 1.0)) {
        return false;
    }

    return true;
}

float SidekickWindow::ExpertiseToSkillReduction(GW::Skill* skill)
{
    if (skill->profession == static_cast<uint8_t>(GW::Constants::Profession::Ranger) || skill->type == GW::Constants::SkillType::Attack || skill->type == GW::Constants::SkillType::Ritual) {
        return expertise * .04f;
    }
    else {
        return 0.0f;
    }
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
    
void SidekickWindow::SkillCallback(const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id)
{
    UNREFERENCED_PARAMETER(caster_id);
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(target_id);
}

void SidekickWindow::ResetTargets()
{
    called_enemy = nullptr;
    closest_enemy = nullptr;
    lowest_health_enemy = nullptr;
    lowest_health_ally = nullptr;
    ResetTargetValues();
}

void SidekickWindow::HardReset()
{
    return;
}
