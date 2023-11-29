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
#include <Windows/RitualistSidekick.h>

#include <Logger.h>

namespace {
} // namespace


void RitualistSidekick::ResetTargetValues() {
    hasLife = false;
    hasVampirism = false;
    allyWithCondition = nullptr;
    deadAlly = nullptr;
    spiritInEarshot = false;
}

bool RitualistSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    switch (agentLiving->allegiance) {
         case GW::Constants::Allegiance::Spirit_Pet: 
         {
             if (GW::GetSquareDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::SqrRange::Earshot) switch (agentLiving->player_number) {
                     case 5723: {
                         hasVampirism = true;
                         spiritInEarshot = true;
                         return true;
                     }
                     case 4218: {
                         hasLife = true;
                         spiritInEarshot = true;
                         return true;
                     }
                     default: {
                         if (agentLiving->GetIsSpawned()) {
                             spiritInEarshot = true;
                         }
                         else if (agentLiving->GetIsConditioned() && !allyWithCondition) {
                             allyWithCondition = agentLiving;
                         }
                         return true;
                     }
                 }
             break;
             
         }
         case GW::Constants::Allegiance::Ally_NonAttackable: {
             if (party_ids.contains(agentLiving->agent_id)) {
                 if (!deadAlly && !agentLiving->GetIsAlive()) {
                     deadAlly = agentLiving;
                 }
                 if (agentLiving->GetIsConditioned() && (!allyWithCondition || allyWithCondition->hp > agentLiving->hp)) {
                     allyWithCondition = agentLiving;
                 }
             }          
             break;
         }
    }

    return false;
}

bool RitualistSidekick::UseCombatSkill() {
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

    GW::SkillbarSkill boon_of_creation_skillbar = skillbar->skills[6];
        if (!GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillbar.skill_id)) {
            GW::Skill* boon_of_creation_skillinfo = GW::SkillbarMgr::GetSkillConstantData(boon_of_creation_skillbar.skill_id);
            if (boon_of_creation_skillinfo && CanUseSkill(boon_of_creation_skillbar, boon_of_creation_skillinfo, cur_energy) && !GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillinfo->skill_id)) {
                if (UseSkillWithTimer(6)) return true;
            }
    }    
    
    if (boon_of_creation_skillbar.GetRecharge() || GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillbar.skill_id)) {
        GW::SkillbarSkill life = skillbar->skills[2];
        if (!hasLife) {
                GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(life.skill_id);
                if (skillInfo && CanUseSkill(life, skillInfo, cur_energy)) {
                    if (UseSkillWithTimer(2)) return true;
                }
            }

        GW::SkillbarSkill recuperation = skillbar->skills[4];
        if (!GW::Effects::GetPlayerEffectBySkillId(recuperation.skill_id)) {
                GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(recuperation.skill_id);
                if (skillInfo && CanUseSkill(recuperation, skillInfo, cur_energy)) {
                    if (UseSkillWithTimer(4)) return true;
                }
            }

        GW::SkillbarSkill recovery = skillbar->skills[0];
            if (!GW::Effects::GetPlayerEffectBySkillId(recovery.skill_id) && allyWithCondition) {
                GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(recovery.skill_id);
                if (skillInfo && CanUseSkill(recovery, skillInfo, cur_energy)) {
                    if (UseSkillWithTimer(0)) return true;
                }
            }


        if (!hasVampirism && target && GW::GetDistance(sidekickLiving->pos, target->pos) < GW::Constants::Range::Spirit / 2) {
            GW::SkillbarSkill vampirism = skillbar->skills[1];
            GW::Skill* vampirism_skillinfo = GW::SkillbarMgr::GetSkillConstantData(vampirism.skill_id);
            if (vampirism_skillinfo && CanUseSkill(vampirism, vampirism_skillinfo, cur_energy)) {
                if (UseSkillWithTimer(1)) return true;
            }
        }
    }

    if (lowest_health_ally && lowest_health_ally->hp < .65) {
        GW::SkillbarSkill spiritLight = skillbar->skills[3];
        GW::Skill* spiritLightSkillInfo = GW::SkillbarMgr::GetSkillConstantData(spiritLight.skill_id);
        if (spiritLightSkillInfo && CanUseSkill(spiritLight, spiritLightSkillInfo, cur_energy)) {
              if (UseSkillWithTimer(3, lowest_health_ally->agent_id)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill mendBodyAndSoul = skillbar->skills[5];
    GW::Skill* mendBodyAndSoulSkillInfo = GW::SkillbarMgr::GetSkillConstantData(mendBodyAndSoul.skill_id);
        if (mendBodyAndSoulSkillInfo && CanUseSkill(mendBodyAndSoul, mendBodyAndSoulSkillInfo, cur_energy)) {
            if (allyWithCondition && spiritInEarshot) {
                if (UseSkillWithTimer(5, allyWithCondition->agent_id)) {
                    return true;
                }
            }
            else if (lowest_health_ally && lowest_health_ally->hp < .65) {
                if (UseSkillWithTimer(5, lowest_health_ally->agent_id)) {
                    return true;
                }
            }
    }
    
    if (!(lowest_health_ally && lowest_health_ally->hp < .5) && deadAlly) {
        GW::SkillbarSkill fleshOfMyFlesh = skillbar->skills[7];
        GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(fleshOfMyFlesh.skill_id);
        if (skillInfo && CanUseSkill(fleshOfMyFlesh, skillInfo, cur_energy)) {
            if (UseSkillWithTimer(7, deadAlly->agent_id)) {
                return true;
            }
        }
    }

    return false;
 }

bool RitualistSidekick::UseOutOfCombatSkill() { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (lowest_health_ally && lowest_health_ally->hp < .6) {
        GW::SkillbarSkill spiritLight = skillbar->skills[3];
        GW::Skill* spiritLightSkillInfo = GW::SkillbarMgr::GetSkillConstantData(spiritLight.skill_id);
        if (spiritLightSkillInfo && CanUseSkill(spiritLight, spiritLightSkillInfo, cur_energy)) {
            if (UseSkillWithTimer(3, lowest_health_ally->agent_id)) {
                    return true;
            }
        }
    }

    if (!(lowest_health_ally && lowest_health_ally->hp < .5) && deadAlly) {
        GW::SkillbarSkill fleshOfMyFlesh = skillbar->skills[7];
        GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(fleshOfMyFlesh.skill_id);
        if (skillInfo && CanUseSkill(fleshOfMyFlesh, skillInfo, cur_energy)) {
            if (UseSkillWithTimer(7, deadAlly->agent_id)) {
                    return true;
            }
        }
    }



    return false; 
}

bool RitualistSidekick::SetUpCombatSkills(uint32_t called_target_id) { 
    UNREFERENCED_PARAMETER(called_target_id);
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;


    if (isCasting(sidekickLiving)) return false;

    GW::SkillbarSkill boon_of_creation_skillbar = skillbar->skills[6];
    if (!GW::Effects::GetPlayerBuffBySkillId(boon_of_creation_skillbar.skill_id)) {
        GW::Skill* boon_of_creation_skillinfo = GW::SkillbarMgr::GetSkillConstantData(boon_of_creation_skillbar.skill_id);
        if (boon_of_creation_skillinfo && CanUseSkill(boon_of_creation_skillbar, boon_of_creation_skillinfo, cur_energy) && !GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillinfo->skill_id)) {
            if (UseSkillWithTimer(6))
                return true;
        }
    }

    return false; 
}

void RitualistSidekick::HardReset()
{
    hasLife = false;
    hasVampirism = false;
    allyWithCondition = nullptr;
    deadAlly = nullptr;
}
