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
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/UIMgr.h>

#include <GWCA/Packets/StoC.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Windows/AuspiciousBeginnings.h>

#include <Logger.h>
#include <Modules/InventoryManager.h>

namespace {
    uint32_t ping = 0;
    clock_t dialogue = 0;
    clock_t sleep_timer = 0;
    int sleep_value = 40;
    clock_t instance_delay = 0;
    clock_t combat_timer = 0;
    bool in_combat = false;
    clock_t attack_timer = 0;
    uint32_t attack_target = 0;
    uint32_t picking_up = 0;
    clock_t pick_up_timer = 0;
    clock_t attack_start_timer = 0;
    bool waiting_reaction = false;
    clock_t reaction_timer = 0;
    int reaction_time = 0;
    bool beginning_fight = false;
    clock_t beginning_fight_timer = 0;
    clock_t scatterTimer = 0;
    clock_t kiteTimer = 0;
    float area_of_effect = 0;
    clock_t interrupt_timer = 0;

    std::optional<GW::GamePos> kiting_location = std::nullopt;
    std::optional<GW::GamePos> epicenter = std::nullopt;
    InventoryManager inventory_manager;

    struct MapStruct {
        GW::Constants::MapID map_id = GW::Constants::MapID::None;
        int region_id = 0;
        int language_id = 0;
        uint32_t district_number = 0;
    };
} // namespace

bool AuspiciousBeginnings::GetEnabled() { return enabled; }

bool AuspiciousBeginnings::SetEnabled(bool b)
{
    if (enabled != b)
        enabled = b;
    return enabled;
}

void AuspiciousBeginnings::Initialize()
{
    ToolboxWindow::Initialize();

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) -> void {
        UNREFERENCED_PARAMETER(status);

        if (!enabled) return;

        using namespace GW::Packet::StoC::GenericValueID;

        const uint32_t value_id = packet->Value_id;
        const uint32_t target_id = packet->target;
        const uint32_t caster_id = packet->caster;
        const uint32_t value = packet->value;

        const bool isSwapped = value_id == skill_activated || value_id == attack_skill_activated || value_id == attack_started || value_id == effect_on_target;
        GenericCallback(value_id, isSwapped ? target_id : caster_id, value);
    });
}

void AuspiciousBeginnings::Update(float delta)
{
    UNREFERENCED_PARAMETER(delta);
    
    if (!enabled)
        return;

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
        instance_delay = TIMER_INIT();
        return;
    }
    if (TIMER_DIFF(instance_delay) > 2750 + rand() % 500 && TIMER_DIFF(sleep_timer) > sleep_value + rand() % 20) {
        sleep_timer = TIMER_INIT();

        GW::AgentLiving* playerLiving = GW::Agents::GetPlayerAsAgentLiving();

        if (!playerLiving) {
            return;
        }

        switch (GW::Map::GetMapID()) {
            case GW::Constants::MapID::Eye_of_the_North_outpost: {
                GW::Agent* HOM = GW::Agents::GetAgentByID(13);
                if (!HOM) return;

                if (!playerLiving->GetIsMoving()) {
                    GW::Agents::GoSignpost(HOM);
                    stage = HALL_OF_MONUMENTS;
                }
                break;
            }
            case GW::Constants::MapID::Hall_of_Monuments: {
                GW::Agent* scrying_pool = GW::Agents::GetAgentByID(14);
                if (!scrying_pool) return;
                if (!playerLiving->GetIsMoving() && GW::GetDistance(playerLiving->pos, scrying_pool->pos) > GW::Constants::Range::Adjacent) 
                    GW::Agents::GoNPC(scrying_pool);
                if (!playerLiving->GetIsMoving() && GW::GetDistance(playerLiving->pos, scrying_pool->pos) <= GW::Constants::Range::Adjacent) GW::Agents::SendDialog(0x632);
                break;
            }
            case GW::Constants::MapID::War_in_Kryta_Auspicious_Beginnings: {
                if (!playerLiving->GetIsAlive()) {
                    GW::PartyMgr::ReturnToOutpost();
                    state = PROGRESSING;
                    sleep_value = 3000;
                }

                GW::AgentArray* agents = GW::Agents::GetAgentArray();

                GW::AgentLiving* closest_agent = nullptr;
                GW::AgentLiving* lowest_health_agent = nullptr;
                GW::AgentLiving* hexed_enemy = nullptr;
                GW::AgentLiving* closest_ally = nullptr;

                bool found_item = false;

                for (auto* a : *agents) {
                    if (a && a->agent_id == picking_up)
                        found_item = true;
                    GW::AgentLiving* agent = a ? a->GetAsAgentLiving() : nullptr;
                    if (!agent) continue;
                    if (agent->allegiance == GW::Constants::Allegiance::Enemy) {
                        if (agent->GetIsDead()) continue; // ignore dead
                        if (agent->GetIsHexed()) {
                            hexed_enemy = agent;
                        }
                        const float sqrd = GW::GetSquareDistance(playerLiving->pos, agent->pos);
                        if (!closest_agent) {
                            closest_agent = agent;
                        }
                        else {
                            const float sqrd_close = GW::GetSquareDistance(playerLiving->pos, closest_agent->pos);
                            if (sqrd < sqrd_close) closest_agent = agent;
                        }
                        if (sqrd > GW::Constants::SqrRange::Spellcast) continue;

                        if (agent->hp < 1.0f && (!lowest_health_agent || lowest_health_agent->hp > agent->hp)) lowest_health_agent = agent;
                    }
                    else if (agent->allegiance == GW::Constants::Allegiance::Ally_NonAttackable) {
                        if (agent->GetIsDead()) continue; // ignore dead
                        if (agent->agent_id == GW::Agents::GetPlayerId()) continue;
                        const float sqrd = GW::GetSquareDistance(playerLiving->pos, agent->pos);
                        if (!closest_ally) {
                            closest_ally = agent;
                        }
                        else {
                            const float sqrd_close = GW::GetSquareDistance(playerLiving->pos, closest_ally->pos);
                            if (sqrd < sqrd_close) closest_ally = agent;
                        }
                    }
                  
                }

                if (!closest_ally) {
                    closest_ally = GW::Agents::GetPlayerAsAgentLiving();
                }

                if (!found_item)
                    picking_up = 0;

                if (closest_agent && GW::GetDistance(closest_agent->pos, playerLiving->pos) <= GW::Constants::Range::Adjacent * 1.5 && TIMER_DIFF(kiteTimer) > playerLiving->weapon_attack_speed * 1000) {
                    state = KITING;
                    kiteTimer = TIMER_INIT();
                }

                else if (closest_agent &&
                    ((playerLiving->GetInCombatStance() && GW::GetSquareDistance(closest_agent->pos, playerLiving->pos) < powf(1350.0f, 2)) || GW::GetSquareDistance(closest_agent->pos, playerLiving->pos) < GW::Constants::SqrRange::Spellcast)) {
                    if (state == PROGRESSING) {
                        beginning_fight = true;
                        beginning_fight_timer = TIMER_INIT();
                    }
                    state = FIGHTING;
                    combat_timer = TIMER_INIT();
                }
                else if (state == FIGHTING && combat_timer >= 3000) {
                    state = PROGRESSING;
                    Log::Info("exiting combat...");
                    active_skills.clear();
                    weakened_agents.clear();
                }

                if ((state == FIGHTING || state == KITING || state == SCATTERING) && closest_agent) {
                    if (beginning_fight == true) {
                        if (TIMER_DIFF(beginning_fight_timer) < 5) {
                            GW::GamePos newPos = {2 * playerLiving->pos.x - closest_agent->pos.x, 2 * playerLiving->pos.y - closest_agent->pos.y, playerLiving->pos.zplane};
                            GW::Agents::Move(newPos);
                        }
                        if (beginning_fight_timer > 500) {
                            beginning_fight = false;
                        }
                        return;
                    }
                    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

                    if (!skillbar)
                        return;

                    if (playerLiving->GetIsKnockedDown())
                        return;

                    const GW::SkillbarSkill naturesBlessing = skillbar->skills[5];

                    if (!naturesBlessing.GetRecharge() && ((closest_ally && closest_ally->hp < .5) || playerLiving->hp < .7 || playerLiving->GetIsHexed())) {
                        GW::SkillbarMgr::UseSkill(5);
                    }

                    const uint32_t target_id = GW::Agents::GetTargetId();
                    
                    if (lowest_health_agent) {
                        if (target_id != lowest_health_agent->agent_id) {
                            GW::Agents::ChangeTarget(lowest_health_agent->agent_id);
                        }
                    }
                    else if (closest_agent) {
                        if (target_id != closest_agent->agent_id) {
                            GW::Agents::ChangeTarget(closest_agent->agent_id);
                        }
                    }

                    if (!waiting_reaction) {
                        if (!skillbar->casting && !playerLiving->skill) {
                            if (state == SCATTERING) {
                                if (epicenter && closest_ally && area_of_effect) {
                                    float new_angle = CalculateAngleToMoveAway(*kiting_location, playerLiving->pos, closest_ally->pos);
                                    GW::GamePos new_position = {playerLiving->pos.x + std::cosf(new_angle) * area_of_effect, playerLiving->pos.y + std::sinf(new_angle) * area_of_effect, playerLiving->pos.zplane};
                                    GW::Agents::Move(new_position);
                                    area_of_effect = 0;
                                    return;
                                }
                                if (!playerLiving->GetIsMoving() && TIMER_DIFF(scatterTimer) >= 500) {
                                    state = FIGHTING;
                                }
                                return;
                            }

                            if (state == KITING) {
                                if (closest_agent && closest_ally) {
                                    float new_angle = CalculateAngleToMoveAway(*kiting_location, playerLiving->pos, closest_ally->pos);
                                    GW::GamePos new_position = {playerLiving->pos.x + std::cosf(new_angle) * GW::Constants::Range::Area, playerLiving->pos.y + std::sinf(new_angle) * GW::Constants::Range::Area, playerLiving->pos.zplane};
                                    GW::Agents::Move(new_position);
                                    return;
                                }
                                if ((closest_agent && GW::GetDistance(playerLiving->pos, closest_agent->pos) >= GW::Constants::Range::Nearby * 4 / 3) || TIMER_DIFF(kiteTimer) >= 1250) {
                                    state = FIGHTING;
                                }
                                return;
                            }

                            const GW::SkillbarSkill terminalVelocity = skillbar->skills[2];

                          /*  if (TIMER_DIFF(interrupt_timer) > 300 && !active_skills.empty() && !terminalVelocity.GetRecharge()) {
                                GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(terminalVelocity.skill_id);

                                if (skill_info && playerLiving->energy * playerLiving->max_energy > skill_info->energy_cost * .2f) {
                                    std::optional<SkillInfo> prioritized_skill = std::nullopt;

                                    for (auto& it : active_skills) {
                                        const auto caster_agent = GW::Agents::GetAgentByID(it.second.caster_id);
                                        const auto caster = caster_agent ? caster_agent->GetAsAgentLiving() : nullptr;

                                        if (!caster) {
                                            continue;
                                        }

                                        float distance = GW::GetSquareDistance(playerLiving->pos, caster->pos);

                                        if (distance > GW::Constants::SqrRange::Spirit) {
                                            continue;
                                        }

                                        if (caster->GetIsDead()) {
                                            continue;
                                        }

                                        const int32_t cast_time = static_cast<int32_t>(it.second.cast_time - 250 * playerLiving->attack_speed_modifier - (0.42f * sqrt(distance)));

                                        if (TIMER_DIFF(it.second.cast_start) > cast_time) {
                                            active_skills.erase(it.first);
                                            continue;
                                        }

                                        if (!prioritized_skill || prioritized_skill->priority < it.second.priority) {
                                            prioritized_skill = it.second;
                                        }
                                    }

                                    if (prioritized_skill) {
                                        const auto caster = GW::Agents::GetAgentByID(prioritized_skill->caster_id);

                                        if (caster) {
                                            float distance = GW::GetSquareDistance(playerLiving->pos, caster->pos);

                                            const int skill_time = ComputeSkillTime(*prioritized_skill);
                                            const int rupt_time =  static_cast<int>(250 * playerLiving->attack_speed_modifier +(0.42f * sqrt(distance)));

                                            const int free_time = skill_time - rupt_time;

                                            if (free_time > 0) {
                                                waiting_reaction = true;
                                                reaction_timer = TIMER_INIT();

                                                const int weapon_activation_time = static_cast<int>(playerLiving->weapon_attack_speed * 1000 * playerLiving->attack_speed_modifier / 2);
                                                if ((TIMER_DIFF(attack_timer) < weapon_activation_time) && (free_time > (weapon_activation_time - TIMER_DIFF(attack_timer)))) {
                                                    const int wait_time = TIMER_DIFF(attack_timer) - weapon_activation_time;
                                                    reaction_time = rand() % (free_time - wait_time - ping) + wait_time;
                                                }
                                                else {
                                                    reaction_time = rand() % (std::min(300, (free_time * 3 / 4)));
                                                }
                                                SetCurrentInterruptSkill(rupt_time, prioritized_skill->caster_id);
                                                return;
                                            }
                                        }
                                        active_skills.erase(prioritized_skill->caster_id);
                                    }
                                }
                            }*/
                            
                             const GW::SkillbarSkill sniperShot = skillbar->skills[0];

                             if (!sniperShot.GetRecharge() && hexed_enemy) {
                                GW::SkillbarMgr::UseSkill(0, hexed_enemy->agent_id);
                                return;
                             }

                             const GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

                             const GW::SkillbarSkill relentlessAssault = skillbar->skills[4];

                             if (target && target->GetIsAlive() && !relentlessAssault.GetRecharge() && playerLiving->GetIsConditioned()) {
                                GW::SkillbarMgr::UseSkill(4);
                                return;
                             }

                             const GW::SkillbarSkill gravestoneMarker = skillbar->skills[1];

                             if (!weakened_agents.empty() && !gravestoneMarker.GetRecharge()) {
                                for (auto a : weakened_agents) {
                                    GW::Agent* weakened = GW::Agents::GetAgentByID(a);
                                    GW::AgentLiving* weakenedLiving = weakened ? weakened->GetAsAgentLiving() : nullptr;
                                    if (weakenedLiving && weakenedLiving->GetIsAlive()) {
                                        GW::SkillbarMgr::UseSkill(1, a);
                                        return;
                                    }
                                }
                             }

                             const GW::SkillbarSkill rainOfArrows = skillbar->skills[3];

                             if (target && target->GetIsAlive() && !rainOfArrows.GetRecharge()) {
                                GW::SkillbarMgr::UseSkill(3, target->agent_id);
                                return;
                             }

                             if (TIMER_DIFF(attack_timer) >= playerLiving->weapon_attack_speed * playerLiving->attack_speed_modifier) {
                                 if (target && target->GetIsAlive() && !terminalVelocity.GetRecharge()) {
                                    GW::SkillbarMgr::UseSkill(2);
                                    return;
                                }
                             }
                        }
                    }

                    if (waiting_reaction) {
                        if (skillbar->casting || playerLiving->skill) return;

                        const GW::Agent* caster = GW::Agents::GetAgentByID(current_interrupt_skill.target_id);

                        const GW::AgentLiving* targeted_living = caster ? caster->GetAsAgentLiving() : nullptr;

                        if (!(targeted_living && targeted_living->GetIsAlive() && (targeted_living->skill || TIMER_DIFF(reaction_timer) < 5))) {
                            return;
                        }
                        if (skillbar->skills[2].GetRecharge()) {
                            ResetReaction();
                            return;
                        }

                        if (TIMER_DIFF(reaction_timer) >= reaction_time - static_cast<int>(ping)) {
                            if (!(playerLiving&& !playerLiving->GetIsKnockedDown())) {
                                ResetReaction();
                                return;
                            }

                            if (!(skillbar && skillbar->IsValid() && !(skillbar->disabled || skillbar->casting))) {
                                ResetReaction();
                                return;
                            }

                            GW::SkillbarMgr::UseSkill(2, current_interrupt_skill.target_id);
                            interrupt_timer = TIMER_INIT();
                            ResetReaction();
                            return;
                        }
                    }

                    const GW::Agent* target = GW::Agents::GetTarget();

                    if (!target)
                        return;

                    const GW::AgentLiving* targetLiving = target->GetAsAgentLiving();

                    if (!targetLiving)
                        return;

                    if (!attack_target || attack_target != targetLiving->agent_id || (!playerLiving->GetIsAttacking() && !playerLiving->GetIsMoving())) {
                        attack_target = targetLiving->agent_id;
                        if (TIMER_DIFF(attack_start_timer) > 1500) {
                            Log::Info("Attacking");

                            GW::GameThread::Enqueue([]() -> void {
                                GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                                return;
                            });
                            attack_start_timer = TIMER_INIT();
                        }
                        return;
                    }
                }
                else {
                    if (!playerLiving->GetIsIdle() && !playerLiving->GetInCombatStance() && picking_up && TIMER_DIFF(pick_up_timer) < 15000)
                        return;
                    if (TIMER_DIFF(pick_up_timer) < 2000)
                        return;
                    for (auto* a : *agents) {
                        GW::AgentItem* agent = a ? a->GetAsAgentItem() : nullptr;
                        if (!agent)
                            continue;
                        if (ShouldItemBePickedUp(agent)) {
                            picking_up = agent->agent_id;
                            Log::Info("Picking up item");
                            GW::Agents::ChangeTarget(agent->agent_id);
                            pick_up_timer = TIMER_INIT();
                            GW::GameThread::Enqueue([agent]() -> void {
                                GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                                return;
                            });
                            return;
                        }
                    }
                }
                
                if (state == PROGRESSING) {
                    switch (stage) {
                        case HALL_OF_MONUMENTS: {
                            if (MoveForStage(FIRST_FIGHT, playerLiving, 11764.95f, -4861.48f))  dialogue = TIMER_INIT();
                            break;
                        }
                        case FIRST_FIGHT: {
                            if (TIMER_DIFF(dialogue) > 35000) MoveForStage(SECOND_FIGHT, playerLiving, 10480.88f, -5807.12f);
                            break;
                        }
                        case SECOND_FIGHT: {
                            MoveForStage(THIRD_FIGHT, playerLiving, 8625.24f, -7871.56f);
                            break;
                        }
                        case THIRD_FIGHT: {
                            MoveForStage(ROUNDING_CORNER, playerLiving, 7870.44f, -8370.78f);
                            break;
                        }
                        case ROUNDING_CORNER: {
                            MoveForStage(FOURTH_FIGHT, playerLiving, 5752.88f, -8135.85f);
                            break;
                        }
                        case FOURTH_FIGHT: {
                            MoveForStage(FIFTH_FIGHT, playerLiving, 2970.72f, -9115.77f);
                            break;
                        }
                        case FIFTH_FIGHT: {
                            MoveForStage(SIXTH_FIGHT, playerLiving, 267.13f, -11238.66f);
                            break;
                        }
                        case SIXTH_FIGHT: {
                            MoveForStage(SEVENTH_FIGHT, playerLiving, -4060.97f, -11208.36f);
                            break;
                        }
                        case SEVENTH_FIGHT: {
                            MoveForStage(LEDGE, playerLiving, -5695.06f, -10892.07f);
                            break;
                        }
                        case LEDGE: {
                            MoveForStage(CROSSING, playerLiving, -8320.99f, -8804.19f);
                            break;
                        }
                        case CROSSING: {
                            MoveForStage(STRAIGHT, playerLiving, -10437.31f, -7952.65f);
                            break;
                        }
                        case STRAIGHT: {
                            MoveForStage(OPENING, playerLiving, -13127.14f, -7960.74f);
                            break;
                        }
                        case OPENING: {
                            if (MoveForStage(FINAL_FIGHT, playerLiving, -15402.45f, -8934.01f)) dialogue = TIMER_INIT();
                            break;
                        }
                        case FINAL_FIGHT: {
                            stage = HALL_OF_MONUMENTS;
                        }
                    }
                }
                break;
            }
            default: {
                Travel();
                sleep_value = 3000;
            }
        }
    }
}

void AuspiciousBeginnings::Draw(IDirect3DDevice9* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    if (!visible)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1));
        if (ImGui::Button(enabled ? "Enabled###farmertoggle" : "Disabled###farmertoggle", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            ToggleEnable();
        }
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

bool AuspiciousBeginnings::MoveForStage(Stage new_stage, GW::AgentLiving* playerLiving, float x, float y)
{
    if (CheckPositionWithinVariance(playerLiving, x, y)) {
        Log::Info("Performing stage: %d", new_stage);
        stage = new_stage;
        return true;
    }

    if (state == PROGRESSING && !playerLiving->GetIsMoving())
        MoveWithVariance(x, y);

    return false;
}

bool AuspiciousBeginnings::MoveWithVariance(float x, float y)
{
    float new_x = x + rand() % 50 - 25;
    float new_y = y + rand() % 50 - 25;
    return GW::Agents::Move(new_x, new_y);
}

bool AuspiciousBeginnings::CheckPositionWithinVariance(GW::AgentLiving* playerLiving, float x, float y) { return (abs(x - playerLiving->pos.x) <= 25 && abs(y - playerLiving->pos.y) <= 25); }

bool AuspiciousBeginnings::Travel()
{
    const GW::Constants::MapID MapID = GW::Constants::MapID::Eye_of_the_North_outpost;

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading)
        return false;
    if (!GW::Map::GetIsMapUnlocked(MapID)) {
        const GW::AreaInfo* map = GW::Map::GetMapInfo(MapID);
        wchar_t map_name_buf[8];
        wchar_t err_message_buf[256] = L"[Error] Your character does not have that map unlocked";
        if (map && map->name_id && GW::UI::UInt32ToEncStr(map->name_id, map_name_buf, 8))
            Log::ErrorW(L"[Error] Your character does not have \x1\x2%s\x2\x108\x107 unlocked", map_name_buf);
        else
            Log::ErrorW(err_message_buf);
        return false;
    }
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost && GW::Map::GetMapID() == MapID && GW::Constants::Region::America == GW::Map::GetRegion() && 0 == GW::Map::GetLanguage()) {
        Log::Error("[Error] You are already in the outpost");
        return false;
    }
    MapStruct* t = new MapStruct();
    t->map_id = MapID;
    t->district_number = 0;
    t->region_id = GW::Constants::Region::America;
    t->language_id = 0;

    GW::GameThread::Enqueue([t] {
        GW::UI::SendUIMessage(GW::UI::UIMessage::kTravel, (void*)t);
        delete t;
    });

    return true;
}

void AuspiciousBeginnings::GenericCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value)
{
    using namespace GW::Packet::StoC;

    switch (value_id) {
        case GenericValueID::attack_skill_activated:
        case GenericValueID::skill_activated: {
            if (!skill_interrupt_priority.contains(static_cast<GW::Constants::SkillID>(value))) return;
            const int cast_start = TIMER_INIT();

            const auto agent = GW::Agents::GetAgentByID(caster_id);
            const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy)) return;
            if (living_agent->GetIsDead()) return;

            const GW::AgentLiving* player = GW::Agents::GetPlayerAsAgentLiving();
            if (!player) return;
            const float distance = GW::GetSquareDistance(player->pos, living_agent->pos);

            if (distance > GW::Constants::SqrRange::Spirit) return;

            const auto skill = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));

            if (skill) {
                float cast_time = skill->activation;
                if (value_id == GenericValueID::attack_skill_activated) {
                    if (!cast_time) {
                        cast_time = living_agent->weapon_attack_speed * living_agent->attack_speed_modifier;
                    }
                    cast_time /= 2;
                }
                if (cast_time * 1000 > 250 * player->attack_speed_modifier) {
                    SkillInfo skill_info = {caster_id, cast_start, static_cast<uint32_t>(cast_time * 1000), skill_interrupt_priority[static_cast<GW::Constants::SkillID>(value)]};
                    active_skills.insert_or_assign(caster_id, skill_info);
                }
            }
            break;
        }
        case GenericValueID::skill_stopped:
        case GenericValueID::skill_finished:
        case GenericValueID::attack_skill_stopped:
        case GenericValueID::attack_skill_finished: {
            const auto agent = GW::Agents::GetAgentByID(caster_id);
            const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy)) return;

            active_skills.erase(caster_id);
            break;
        }
        case GenericValueID::add_effect: {
            if (value == static_cast<uint32_t>(GW::Constants::EffectID::weakness)) {
                const auto agent = GW::Agents::GetAgentByID(caster_id);
                const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

                if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy)) return;
                Log::Info("Enemy weakened");
                weakened_agents.insert(caster_id);
            }
            break;
        }
        case GenericValueID::remove_effect: {
            if (value == static_cast<uint32_t>(GW::Constants::EffectID::weakness)) {
                const auto agent = GW::Agents::GetAgentByID(caster_id);
                const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

                if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy)) return;
                weakened_agents.erase(caster_id);
            }
            break;
        }
        case GenericValueID::attack_started: {
            const GW::Agent* player = GW::Agents::GetPlayer();
            if (!player) return;
            if (player->agent_id == caster_id) attack_timer = TIMER_INIT();
            break;
        }
        case GenericValueID::skill_damage: {
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
            if (skill_info && skill_info->aoe_range > 0 && skill_info->duration0 > 0) {
                area_of_effect = skill_info->aoe_range;
                if (TIMER_DIFF(scatterTimer) > 2000) {
                    scatterTimer = TIMER_INIT();
                    Log::Info("Scattering due to damage.");
                    state = SCATTERING;
                }
            }
            break;
        };
        case GenericValueID::effect_on_target: {
            if (!caster_id) return;
            GW::Agent* target = GW::Agents::GetAgentByID(caster_id);
            GW::Agent* sidekick = GW::Agents::GetPlayer();
            if (!target || !sidekick) return;
            GW::AgentLiving* targetLiving = target->GetAsAgentLiving();
            if (!(targetLiving && (targetLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet || targetLiving->allegiance == GW::Constants::Allegiance::Ally_NonAttackable || targetLiving->allegiance == GW::Constants::Allegiance::Minion))) return;
            GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
            if (!(skill_info && skill_info->aoe_range <= 0 && skill_info->duration0 <= 0)) return;
            if (GW::GetDistance(target->pos, sidekick->pos) > skill_info->aoe_range) return;
            area_of_effect = skill_info->aoe_range;
            epicenter = target->pos;
            if (TIMER_DIFF(scatterTimer > 3000)) {
                scatterTimer = TIMER_INIT();
                Log::Info("Scattering due to cast.");
                state = SCATTERING;
            }
            break;
        }
        default:
            return;
    }
}

void AuspiciousBeginnings::OnServerPing(GW::HookStatus*, void* packet)
{
    uint32_t* packet_as_uint_array = (uint32_t*)packet;
    uint32_t packetPing = packet_as_uint_array[1];
    if (packetPing > 4999) return; // GW checks this too.
    ping = packetPing;
}

int AuspiciousBeginnings::ComputeSkillTime(SkillInfo skill_info)
{
    return skill_info.cast_time - TIMER_DIFF(skill_info.cast_start) - ping;
}

void AuspiciousBeginnings::SetCurrentInterruptSkill(int rupt_time, uint32_t caster_id)
{
    current_interrupt_skill.cast_time = rupt_time;
    current_interrupt_skill.target_id = caster_id;
}

void AuspiciousBeginnings::ResetReaction()
{
    reaction_time = 0;
    reaction_timer = 0;
    waiting_reaction = false;
    active_skills.erase(current_interrupt_skill.target_id);
    SetCurrentInterruptSkill(0, 0);
}

float AuspiciousBeginnings::CalculateAngleToMoveAway(GW::GamePos position_away, GW::GamePos player_position, GW::GamePos group_position)
{
    const float percentOfRadius = GW::GetDistance(group_position, player_position) / GW::Constants::Range::Spellcast * 100;

    const float angleAwayFromEpicenter = std::atan2f(player_position.y - position_away.y, player_position.x - position_away.x);

    const float angleAwayFromGroup = std::atan2f(player_position.y - group_position.y, player_position.x - group_position.x);

    return (angleAwayFromEpicenter - angleAwayFromGroup * percentOfRadius) / 2;
}

bool AuspiciousBeginnings::ShouldItemBePickedUp(GW::AgentItem* itemAgent)
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
    }
    if (actual_item->GetIsMaterial()) return true;
    if (actual_item->GetIsStackable()) return true;
    return false;
}
