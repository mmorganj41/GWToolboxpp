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
    clock_t hexTimer = 0;
} // namespace





void NecromancerSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(value_id);
    if (!target_id) return;

    if (caster_id == GW::Agents::GetPlayerId() && static_cast<GW::Constants::SkillID>(value) == GW::Constants::SkillID::Blood_Bond) {
        GW::Agent* target = GW::Agents::GetAgentByID(*target_id);
        GW::AgentLiving* targetLiving = target ? target->GetAsAgentLiving() : nullptr;
        if (targetLiving) {
            bloodBondCenter = targetLiving->pos;
            checking_agents = GW::Constants::SkillID::Blood_Bond;
        }
    }
    else {
        GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
        GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

        if (!casterLiving) return;

        if (caster_id == GW::Agents::GetPlayerId() || casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

        GW::Constants::SkillID skillId = static_cast<GW::Constants::SkillID>(value);

        switch (skillId) {
            case GW::Constants::SkillID::Shatter_Hex:
            case GW::Constants::SkillID::Remove_Hex: {
                cureHexMap.insert_or_assign(casterLiving->agent_id, *target_id);
                break;
            }
            case GW::Constants::SkillID::Drain_Enchantment:
            case GW::Constants::SkillID::Jaundiced_Gaze: {
                removeEnchantmentMap.insert_or_assign(casterLiving->agent_id, *target_id);    
            }
        }
    }
}

void NecromancerSidekick::SkillFinishCallback(const uint32_t caster_id)
{
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    cureHexMap.erase(casterLiving->agent_id);
    removeEnchantmentMap.erase(casterLiving->agent_id);
}


bool NecromancerSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (agentLiving->allegiance == GW::Constants::Allegiance::Enemy && agentLiving->GetIsAlive()) { 
        if (checking_agents && bloodBondCenter) {
            if (bloodBondCenter->zplane == agentLiving->pos.zplane && GW::GetSquareDistance(*bloodBondCenter, agentLiving->pos) <= GW::Constants::SqrRange::Adjacent) {
                SkillDuration skillDuration = {TIMER_INIT(), static_cast<uint32_t>(agentLiving->GetHasBossGlow() ? 6000 : 11000)};
                bloodBondMap.insert_or_assign(agentLiving->agent_id, skillDuration);
            }
        }
        if (!enchantedEnemy && agentLiving->GetIsEnchanted() && GW::GetDistance(agentLiving->pos, playerLiving->pos) < GW::Constants::Range::Spellcast * 11 / 10) {
            bool already_casting = false;
            for (auto& it : removeEnchantmentMap) {
                if (it.second == agentLiving->agent_id) already_casting = true;
            }
            if (!already_casting) enchantedEnemy = agentLiving;
        }
    }
    else if (!hexedAlly && party_ids.contains(agentLiving->agent_id) && agentLiving->GetIsAlive() && agentLiving->GetIsHexed()) {
        if (hexTimer == 0) hexTimer = TIMER_INIT();
        bool already_casting = false;
        if (TIMER_DIFF(hexTimer) > 40 + static_cast<int32_t>(ping)) {
            for (auto& it : cureHexMap) {
                if (it.second == agentLiving->agent_id) already_casting = true;
            }
        }
        if (!already_casting) hexedAlly = agentLiving;
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

    if (hexedAlly) {
        GW::SkillbarSkill cureHex = skillbar->skills[4];
        GW::Skill* cureHexInfo = GW::SkillbarMgr::GetSkillConstantData(cureHex.skill_id);
        if (CanUseSkill(cureHex, cureHexInfo, cur_energy)) {
            if (UseSkillWithTimer(4, hexedAlly->agent_id)) {
                hexTimer = 0;
                return true;
            }
        }
    }

    if (enchantedEnemy) {
        GW::SkillbarSkill jaundicedGaze = skillbar->skills[7];
        GW::Skill* jaundicedGazeInfo = GW::SkillbarMgr::GetSkillConstantData(jaundicedGaze.skill_id);
        if (CanUseSkill(jaundicedGaze, jaundicedGazeInfo, cur_energy)) {
            if (UseSkillWithTimer(7, enchantedEnemy->agent_id)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill orderOfPain = skillbar->skills[2];
    GW::Skill* orderOfPainInfo = GW::SkillbarMgr::GetSkillConstantData(orderOfPain.skill_id);
    if (sidekickLiving->hp > .45 && orderOfPainInfo && CanUseSkill(orderOfPain, orderOfPainInfo, cur_energy)) {
        GW::Effect* orderOfPainEffect = GW::Effects::GetPlayerEffectBySkillId(orderOfPain.skill_id);
        if ((!orderOfPainEffect || orderOfPainEffect->GetTimeRemaining() < 1500) && UseSkillWithTimer(2)) {
            return true;
        }
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
    hexedAlly = nullptr;
    enchantedEnemy = nullptr;
    cureHexMap.clear();
    removeEnchantmentMap.clear();
}

void NecromancerSidekick::StopCombat() {
    bloodBondMap.clear();
    necromancerEffectSet.clear();
    cureHexMap.clear();
    removeEnchantmentMap.clear();
}

void NecromancerSidekick::ResetTargetValues()
{
    hexedAlly = nullptr;
    enchantedEnemy = nullptr;
}

void NecromancerSidekick::StartCombat()
{
    hexTimer = 0;
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
