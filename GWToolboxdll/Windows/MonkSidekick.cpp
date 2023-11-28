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
#include <Windows/MonkSidekick.h>

#include <Logger.h>

namespace {
    clock_t hexTimer = 0;
} // namespace

void MonkSidekick::HardReset() {
    hexedAlly = nullptr;
    cureHexMap.clear();
}

void MonkSidekick::ResetTargetValues() {
    hexedAlly = nullptr;
}

void MonkSidekick::StartCombat() {
    hexTimer = 0;
}

void MonkSidekick::StopCombat() {
    cureHexMap.clear();
}

void MonkSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id ) 
{
    UNREFERENCED_PARAMETER(value_id);
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;
    if (!target_id) return;

    if (caster_id == GW::Agents::GetPlayerId() || casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    GW::Constants::SkillID skillId = static_cast<GW::Constants::SkillID>(value);

    switch (skillId) {
        case GW::Constants::SkillID::Shatter_Hex:
        case GW::Constants::SkillID::Cure_Hex: {
            cureHexMap.insert_or_assign(casterLiving->agent_id, *target_id);
        }
    }
}

void MonkSidekick::SkillFinishCallback(const uint32_t caster_id) {
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;
    
    cureHexMap.erase(casterLiving->agent_id);
}

bool MonkSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (party_ids.contains(agentLiving->agent_id) && agentLiving->GetIsAlive() && agentLiving->GetIsHexed()) {
        if (hexTimer == 0) hexTimer = TIMER_INIT();
        bool already_casting = false;
        if (TIMER_DIFF(hexTimer) > 80 + 2*static_cast<int32_t>(ping)) {
            for (auto& it : cureHexMap) {
                if (it.second == agentLiving->agent_id) already_casting = true;
            }
        }
        if (!already_casting && !hexedAlly) hexedAlly = agentLiving;
    }
    return false;
}

bool MonkSidekick::UseCombatSkill() {
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

    if (sidekickLiving->hp < .7) {
        GW::SkillbarSkill healingTouch = skillbar->skills[3];
        GW::Skill* healingTouchInfo = GW::SkillbarMgr::GetSkillConstantData(healingTouch.skill_id);
        if (CanUseSkill(healingTouch, healingTouchInfo, cur_energy)) {
            if (UseSkillWithTimer(3, sidekickLiving->agent_id)) {
                return true;
            }
        }
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

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    if (lowest_health_ally) {
        GW::SkillbarSkill orisonOfHealing = skillbar->skills[0];
        GW::Skill* orisonOfHealingInfo = GW::SkillbarMgr::GetSkillConstantData(orisonOfHealing.skill_id);
        GW::SkillbarSkill healingBreeze = skillbar->skills[1];
        GW::Skill* healingBreezeInfo = GW::SkillbarMgr::GetSkillConstantData(healingBreeze.skill_id);

        if (lowest_health_ally->hp < .55 && orisonOfHealingInfo && CanUseSkill(orisonOfHealing, orisonOfHealingInfo, cur_energy)) 
        {
            if (UseSkillWithTimer(0, lowest_health_ally->agent_id)) {
                return true;
            }
        }
        else if (lowest_health_ally->hp < .8 && cur_energy > 15 && !GetAgentEffectBySkillId(lowest_health_ally->agent_id, healingBreeze.skill_id) && healingBreezeInfo && CanUseSkill(healingBreeze, healingBreezeInfo, cur_energy))
        {
            if (UseSkillWithTimer(1, lowest_health_ally->agent_id)) {
                return true;
            }
        }
        else if (lowest_health_ally->hp < .7 && orisonOfHealingInfo && CanUseSkill(orisonOfHealing, orisonOfHealingInfo, cur_energy)) {
            if (UseSkillWithTimer(0, lowest_health_ally->agent_id)) {
                return true;
            }
        }
    }

    if (!target) return false;

    GW::SkillbarSkill baneSignet = skillbar->skills[2];
    GW::Skill* baneSignetInfo = GW::SkillbarMgr::GetSkillConstantData(baneSignet.skill_id);
    if (target->hp > .4 && target->GetIsAttacking() && baneSignetInfo && CanUseSkill(baneSignet, baneSignetInfo, cur_energy)) {
        if (UseSkillWithTimer(2, target->agent_id)) {
            return true;
        }
    }

    return false;
}

bool MonkSidekick::UseOutOfCombatSkill()
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

    if (sidekickLiving->hp < .7) {
        GW::SkillbarSkill healingTouch = skillbar->skills[3];
        GW::Skill* healingTouchInfo = GW::SkillbarMgr::GetSkillConstantData(healingTouch.skill_id);
        if (CanUseSkill(healingTouch, healingTouchInfo, cur_energy)) {
            if (UseSkillWithTimer(3, sidekickLiving->agent_id)) {
                return true;
            }
        }

        if (lowest_health_ally) {
            GW::SkillbarSkill orisonOfHealing = skillbar->skills[0];
            GW::Skill* orisonOfHealingInfo = GW::SkillbarMgr::GetSkillConstantData(orisonOfHealing.skill_id);
            GW::SkillbarSkill healingBreeze = skillbar->skills[1];
            GW::Skill* healingBreezeInfo = GW::SkillbarMgr::GetSkillConstantData(healingBreeze.skill_id);

            if (lowest_health_ally->hp < .55 && orisonOfHealingInfo && CanUseSkill(orisonOfHealing, orisonOfHealingInfo, cur_energy)) {
                if (UseSkillWithTimer(0, lowest_health_ally->agent_id)) {
                    return true;
                }
            }
            else if (lowest_health_ally->hp < .8 && cur_energy > 15 && !GetAgentEffectBySkillId(lowest_health_ally->agent_id, healingBreeze.skill_id) && healingBreezeInfo && CanUseSkill(healingBreeze, healingBreezeInfo, cur_energy)) {
                if (UseSkillWithTimer(1, lowest_health_ally->agent_id)) {
                    return true;
                }
            }
            else if (lowest_health_ally->hp < .7 && orisonOfHealingInfo && CanUseSkill(orisonOfHealing, orisonOfHealingInfo, cur_energy)) {
                if (UseSkillWithTimer(0, lowest_health_ally->agent_id)) {
                    return true;
                }
            }
        }
    }

    return false;
}
