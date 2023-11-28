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
    if (agentLiving->GetIsAlive() && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && playerLiving->pos.zplane == agentLiving->pos.zplane && GW::GetDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::Range::Spellcast * 6 / 5) {
        CheckForProximity(agentLiving);
    }

    return false;
}

bool ElementalistSidekick::UseCombatSkill() {
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

    GW::SkillbarSkill auraOfRestoration = skillbar->skills[6];
    GW::Skill* auraOfRestorationInfo = GW::SkillbarMgr::GetSkillConstantData(auraOfRestoration.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(auraOfRestoration.skill_id) && auraOfRestorationInfo && CanUseSkill(auraOfRestoration, auraOfRestorationInfo, cur_energy)) {
        if (UseSkillWithTimer(6))
            return true;
    };

    GW::SkillbarSkill fireStorm = skillbar->skills[4];
    GW::Skill* fireStormInfo = GW::SkillbarMgr::GetSkillConstantData(fireStorm.skill_id);
    if (CanUseSkill(fireStorm, fireStormInfo, cur_energy)) {
        uint32_t max_proximity = 0;
        GW::AgentID best_target = 0;
        for (auto& it : enemyProximityMap) {
            if (it.second.adjacent > max_proximity) {
                max_proximity = it.second.adjacent;
                best_target = it.first;
            }
        }

        if (max_proximity > 0 && best_target) {
            GW::Agent* bestTarget = GW::Agents::GetAgentByID(best_target);
            GW::AgentLiving* bestLiving = bestTarget ? bestTarget->GetAsAgentLiving() : nullptr;
            if (bestLiving && !bestLiving->GetIsMoving()) {
                if (UseSkillWithTimer(4, best_target)) {
                    return true;
                }
            }
        }
    }

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    if (!target)
        return false;

    GW::SkillbarSkill flare = skillbar->skills[0];
    GW::Skill* flareInfo = GW::SkillbarMgr::GetSkillConstantData(flare.skill_id);
    if (flareInfo && CanUseSkill(flare, flareInfo, cur_energy)) {
        if (UseSkillWithTimer(0, target->agent_id))
            return true;
    };  

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

    GW::SkillbarSkill auraOfRestoration = skillbar->skills[6];
    GW::Skill* auraOfRestorationInfo = GW::SkillbarMgr::GetSkillConstantData(auraOfRestoration.skill_id);
    if (!GW::Effects::GetPlayerEffectBySkillId(auraOfRestoration.skill_id) && auraOfRestorationInfo && CanUseSkill(auraOfRestoration, auraOfRestorationInfo, cur_energy)) {
        if (UseSkillWithTimer(6))
            return true;
    };

    return false;
}
