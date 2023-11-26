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
#include <Windows/NecromancerSidekick.h>

#include <Logger.h>

namespace {
} // namespace

void NecromancerSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(value_id);
    if (caster_id == GW::Agents::GetPlayerId() && target_id && static_cast<GW::Constants::SkillID>(value) == GW::Constants::SkillID::Blood_Bond) {
        GW::Agent* target = GW::Agents::GetAgentByID(*target_id);
        GW::AgentLiving* targetLiving = target ? target->GetAsAgentLiving() : nullptr;
        if (targetLiving) {
            bloodBondCenter = targetLiving->pos;
            checking_agents = GW::Constants::SkillID::Blood_Bond;
        }
    }
}

bool NecromancerSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (checking_agents && bloodBondCenter && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && agentLiving->GetIsAlive()) {
        if (bloodBondCenter->zplane == agentLiving->pos.zplane && GW::GetSquareDistance(*bloodBondCenter, agentLiving->pos) <= GW::Constants::SqrRange::Adjacent) {
            SkillDuration skillDuration = {TIMER_INIT(), static_cast<uint32_t>(agentLiving->GetHasBossGlow() ? 2500 : 5000)};
            bloodBondMap.insert_or_assign(agentLiving->agent_id, skillDuration);
        }
    }

    return false;
}

void NecromancerSidekick::CustomLoop(GW::AgentLiving* sidekick) {
    UNREFERENCED_PARAMETER(sidekick);
    if (state == Following || state == Picking_up) return;

    if (!bloodBondMap.empty()) {
        for (auto& it : bloodBondMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                bloodBondMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 500) {
                bloodBondMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsHexed() && necromancerEffectSet.contains(it.first))) {
                bloodBondMap.erase(it.first);
                continue;
            }
        }
    }
}

bool NecromancerSidekick::UseCombatSkill() {
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return false;
    }

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (isCasting(sidekickLiving)) {
        return false;
    }

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    if (!target) return false;

    GW::SkillbarSkill bloodBond = skillbar->skills[3];
    GW::Skill* bloodBondInfo = GW::SkillbarMgr::GetSkillConstantData(bloodBond.skill_id);
    if (!bloodBondMap.contains(target->agent_id) && target->hp > .25 && bloodBondInfo && CanUseSkill(bloodBond, bloodBondInfo, cur_energy)) {
        if (UseSkillWithTimer(3, target->agent_id)) {
            return true;
        }
    }

    GW::SkillbarSkill vampiricGaze = skillbar->skills[1];
    GW::Skill* vampiricGazeInfo = GW::SkillbarMgr::GetSkillConstantData(vampiricGaze.skill_id);
    if (target->hp > .25 && vampiricGazeInfo && CanUseSkill(vampiricGaze, vampiricGazeInfo, cur_energy)) {
        if (UseSkillWithTimer(1, target->agent_id)) {
            return true;
        }
    }

    return false;
}

void NecromancerSidekick::FinishedCheckingAgentsCallback() {
    bloodBondCenter = std::nullopt;
}

void NecromancerSidekick::HardReset() {
    bloodBondCenter = std::nullopt;
    bloodBondMap.clear();
    necromancerEffectSet.clear();
}

void NecromancerSidekick::StopCombat() {
    bloodBondMap.clear();
    necromancerEffectSet.clear();
}

void NecromancerSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::necro_symbol)) return;

    necromancerEffectSet.insert(agent_id);
}

void NecromancerSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::necro_symbol)) return;

    necromancerEffectSet.erase(agent_id);
}
