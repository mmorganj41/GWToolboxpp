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
#include <Windows/ElementalistSidekick.h>

#include <Logger.h>

namespace {
    
} // namespace

bool ElementalistSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    if (agentLiving->GetIsAlive() && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && GW::GetDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::Range::Spellcast * 6 / 5) {
        CheckForProximity(agentLiving);
        if (burningEffectSet.contains(agentLiving->agent_id) && (!burningTarget || burningTarget->hp > agentLiving->hp)) burningTarget = agentLiving;
    }

    return false;
}

bool ElementalistSidekick::UseCombatSkill()
{
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

    GW::SkillbarSkill elementalLord = skillbar->skills[0];
    GW::Skill* elementalLordInfo = GW::SkillbarMgr::GetSkillConstantData(elementalLord.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(elementalLord.skill_id) && elementalLordInfo && CanUseSkill(elementalLord, elementalLordInfo, cur_energy)) {
        if (UseSkillWithTimer(0)) return true;
    };

    GW::SkillbarSkill fireAttunement = skillbar->skills[7];
    GW::Skill* fireAttunementInfo = GW::SkillbarMgr::GetSkillConstantData(fireAttunement.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(fireAttunement.skill_id) && fireAttunementInfo && CanUseSkill(fireAttunement, fireAttunementInfo, cur_energy)) {
        if (UseSkillWithTimer(7)) return true;
    };

    GW::SkillbarSkill auraOfRestoration = skillbar->skills[6];
    GW::Skill* auraOfRestorationInfo = GW::SkillbarMgr::GetSkillConstantData(auraOfRestoration.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(auraOfRestoration.skill_id) && auraOfRestorationInfo && CanUseSkill(auraOfRestoration, auraOfRestorationInfo, cur_energy)) {
        if (UseSkillWithTimer(6)) return true;
    };

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    GW::SkillbarSkill vanguardBannerOfHonor = skillbar->skills[5];
    GW::SkillbarSkill vanguardBannerOfWisdom = skillbar->skills[4];
    bool canUseHonor = !vanguardBannerOfHonor.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(vanguardBannerOfHonor.skill_id);
    bool canUseWisdom = !vanguardBannerOfWisdom.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(vanguardBannerOfWisdom.skill_id);
    if (target && cur_energy > 10 && (canUseHonor || canUseWisdom)) {
        if (GW::GetDistance(target->pos, sidekickLiving->pos) <= GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4) {
            if (canUseWisdom) 
                if (UseSkillWithTimer(4)) {
                    closeDistance = false;
                    return true;
                }


            if (canUseHonor)
                if (UseSkillWithTimer(5)) {
                    closeDistance = false;
                    return true;
                }
        }
        closeDistance = true;
        
    }
    else {
        closeDistance = false;
    }


    if (sidekickLiving->max_energy - cur_energy > 10 && burningTarget) {
        GW::SkillbarSkill glowingGaze = skillbar->skills[3];
        GW::Skill* glowingGazeInfo = GW::SkillbarMgr::GetSkillConstantData(glowingGaze.skill_id);
        if (glowingGazeInfo && CanUseSkill(glowingGaze, glowingGazeInfo, cur_energy)) {
            if (UseSkillWithTimer(3, target && burningEffectSet.contains(target->agent_id) ? target->agent_id : burningTarget->agent_id)) return true;
        };
    }

    if (!closeDistance) {
        GW::SkillbarSkill searingFlames = skillbar->skills[1];
        if (cur_energy > 15 && !searingFlames.GetRecharge()) {
            uint32_t max_nearby = 0;
            GW::AgentID best_nearby_target = 0;
            for (auto& it : enemyProximityMap) {
                GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
                GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
                if (!agentLiving) continue;

                if (GW::GetDistance(agentLiving->pos, sidekickLiving->pos) > GW::Constants::Range::Spellcast * 6 / 5) continue;

                if (it.second.nearby > max_nearby) {
                    max_nearby = it.second.nearby;
                    best_nearby_target = it.first;
                }
            }

            if (max_nearby > 0 && best_nearby_target) {
                if (UseSkillWithTimer(1, best_nearby_target)) return true;
            }
            else if (target) {
                if (UseSkillWithTimer(1, target->agent_id)) return true;
            }
        }
    }

    return false;
}

bool ElementalistSidekick::SetUpCombatSkills(uint32_t called_target) {
    UNREFERENCED_PARAMETER(called_target);
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

    GW::SkillbarSkill elementalLord = skillbar->skills[0];
    GW::Skill* elementalLordInfo = GW::SkillbarMgr::GetSkillConstantData(elementalLord.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(elementalLord.skill_id) && elementalLordInfo && CanUseSkill(elementalLord, elementalLordInfo, cur_energy)) {
        if (UseSkillWithTimer(0)) return true;
    };

    GW::SkillbarSkill fireAttunement = skillbar->skills[7];
    GW::Skill* fireAttunementInfo = GW::SkillbarMgr::GetSkillConstantData(fireAttunement.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(fireAttunement.skill_id) && fireAttunementInfo && CanUseSkill(fireAttunement, fireAttunementInfo, cur_energy)) {
        if (UseSkillWithTimer(7)) return true;
    };

    GW::SkillbarSkill auraOfRestoration = skillbar->skills[6];
    GW::Skill* auraOfRestorationInfo = GW::SkillbarMgr::GetSkillConstantData(auraOfRestoration.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(auraOfRestoration.skill_id) && auraOfRestorationInfo && CanUseSkill(auraOfRestoration, auraOfRestorationInfo, cur_energy)) {
        if (UseSkillWithTimer(6)) return true;
    };

    return false;
}

void ElementalistSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!agentLiving) return;

    if (!(agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::burning)) return;

    burningEffectSet.insert(agent_id);
}

void ElementalistSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!agentLiving) return;

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::burning)) return;

    burningEffectSet.erase(agent_id);
}

void ElementalistSidekick::HardReset()
{
    burningEffectSet.clear();
    burningTarget = nullptr;
}

void ElementalistSidekick::StopCombat()
{
    burningEffectSet.clear();
}

void ElementalistSidekick::ResetTargetValues()
{
    burningTarget = nullptr;
}
