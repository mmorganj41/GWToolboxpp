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

// 4223 shelter
// 4224 union
// 4217 displacement

void RitualistSidekick::ResetTargetValues() {
    hasShelter = false;
    hasUnion = false;
    hasEarthbind = false;
    spiritInEarshot = true;
    lowHealthSpirit = false;
    weaponAlly = nullptr;
}

bool RitualistSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    if (!agentLiving->GetIsAlive()) return false;

    switch (agentLiving->allegiance) {
        case GW::Constants::Allegiance::Spirit_Pet: 
         {
            const float distance = GW::GetSquareDistance(playerLiving->pos, agentLiving->pos); 
             if (distance <= GW::Constants::SqrRange::Compass) switch (agentLiving->player_number) {
                     case 4223: {
                         hasShelter = true;
                         if (distance > GW::Constants::SqrRange::Earshot)
                             spiritInEarshot = false;
                         if (agentLiving->hp < .65) lowHealthSpirit = true;
                         return true;
                     }
                     case 4224: {
                         hasUnion = true;
                         if (distance > GW::Constants::SqrRange::Earshot) spiritInEarshot = false;
                         if (agentLiving->hp < .65) lowHealthSpirit = true;
                         return true;
                     }
                     case 4222: {
                         hasEarthbind = true;
                         if (distance > GW::Constants::SqrRange::Earshot) spiritInEarshot = false;
                         if (agentLiving->hp < .65) lowHealthSpirit = true;
                         return true;
                     }
                     default: {
                         if (!weaponAlly && !agentLiving->GetIsSpawned() && !agentLiving->GetIsWeaponSpelled()) {
                             weaponAlly = agentLiving;
                         }
                     }
             }
             break;
         }
         case GW::Constants::Allegiance::Ally_NonAttackable:
         {
             if (!weaponAlly && party_ids.contains(agentLiving->agent_id) && !agentLiving->GetIsWeaponSpelled()) {
                 switch (agentLiving->weapon_type) {
                     case 2:
                     case 3:
                     case 4:
                     case 5:
                     case 6:
                     case 7:
                     {
                         weaponAlly = agentLiving;
                         break;
                     }
                     case 1:
                     {
                         if (agentLiving->attack_speed_modifier > 1) {
                             weaponAlly = agentLiving;
                         }
                         break;
                     }
                 }
             }
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

    GW::SkillbarSkill soulTwisting = skillbar->skills[0];
    GW::Skill* soulTwistingInfo = GW::SkillbarMgr::GetSkillConstantData(soulTwisting.skill_id);
    GW::Effect* soulTwistingEffect = GW::Effects::GetPlayerEffectBySkillId(soulTwisting.skill_id);
    if (!soulTwistingEffect || soulTwistingEffect->GetTimeRemaining() <= 1000) {
       if (CanUseSkill(soulTwisting, soulTwistingInfo, cur_energy)) {
             if (UseSkillWithTimer(0)) return true;
        }
    }

    if ((!spiritInEarshot && cur_energy > 10)) {
        GW::SkillbarSkill summonSpirits = skillbar->skills[5];
        if (!summonSpirits.GetRecharge()) {
             if (UseSkillWithTimer(5)) {
                 return true;
             }
        }
    }


    GW::SkillbarSkill boon_of_creation_skillbar = skillbar->skills[6];
    if (!GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillbar.skill_id)) {
        GW::Skill* boon_of_creation_skillinfo = GW::SkillbarMgr::GetSkillConstantData(boon_of_creation_skillbar.skill_id);
        if (boon_of_creation_skillinfo && CanUseSkill(boon_of_creation_skillbar, boon_of_creation_skillinfo, cur_energy) && !GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillinfo->skill_id)) {
            if (UseSkillWithTimer(6)) return true;
        }
    }    

    if (soulTwistingEffect && soulTwistingEffect->GetTimeRemaining() > 1000 && cur_energy >= 10 && (boon_of_creation_skillbar.GetRecharge() || GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillbar.skill_id))) {
        if (!hasShelter) {
            GW::SkillbarSkill shelter = skillbar->skills[1];
            if (!shelter.GetRecharge()) {
                if (UseSkillWithTimer(1, 0U, 2000)) {
                     newShelter = true;
                     newSpirit = true;
                     return true;
                }
            }
        }
        else if (!hasUnion || newShelter) {
            GW::SkillbarSkill unionSkill = skillbar->skills[2];
            if (!unionSkill.GetRecharge()) {
                if (UseSkillWithTimer(2, 0U, 2000)) {
                     newShelter = false;
                     newSpirit = true;
                     return true;
                }
            }
        }

        if (!hasEarthbind) {
            GW::SkillbarSkill earthbind = skillbar->skills[3];
            if (!earthbind.GetRecharge()) {
                if (UseSkillWithTimer(3, 0U, 2000)) {
                     newSpirit = true;
                     return true;
                }
            }
        }
    }

    if (TIMER_DIFF(armorOfUnfeelingTimer) > 36000 || newSpirit) armorOfUnfeelingTimer = 0;

    if ((lowHealthSpirit && armorOfUnfeelingTimer != 0 && cur_energy > 19)) {
        GW::SkillbarSkill summonSpirits = skillbar->skills[5];
        if (!summonSpirits.GetRecharge()) {
            if (UseSkillWithTimer(5)) {
                return true;
            }
        }
    }

    if (hasUnion && hasShelter && hasEarthbind && spiritInEarshot) {
        GW::SkillbarSkill armorOfUnfeeling = skillbar->skills[4];
        if (armorOfUnfeelingTimer == 0 && cur_energy > 5 && !armorOfUnfeeling.GetRecharge()) {
            if (UseSkillWithTimer(4)) {
                armorOfUnfeelingTimer = TIMER_INIT();
                newSpirit = false;
                return true;
            }
        }
        if (weaponAlly && cur_energy > 18) {
            GW::SkillbarSkill greatDwarfWeapon = skillbar->skills[7];
            GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(greatDwarfWeapon.skill_id);
            if (skillInfo && CanUseSkill(greatDwarfWeapon, skillInfo, cur_energy)) {
                if (UseSkillWithTimer(7, weaponAlly->agent_id)) {
                     return true;
                }
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

    GW::SkillbarSkill soulTwisting = skillbar->skills[0];
    GW::Skill* soulTwistingInfo = GW::SkillbarMgr::GetSkillConstantData(soulTwisting.skill_id);
    GW::Effect* soulTwistingEffect = GW::Effects::GetPlayerEffectBySkillId(soulTwisting.skill_id);
    if (!soulTwistingEffect || soulTwistingEffect->GetTimeRemaining() < 1000) {
        if (CanUseSkill(soulTwisting, soulTwistingInfo, cur_energy)) {
            if (UseSkillWithTimer(0)) return true;
        }
    }
    return false; 
}

bool RitualistSidekick::SetUpCombatSkills(uint32_t called_target_id) { 

    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (isCasting(sidekickLiving)) return false;

    GW::SkillbarSkill soulTwisting = skillbar->skills[0];
    GW::Skill* soulTwistingInfo = GW::SkillbarMgr::GetSkillConstantData(soulTwisting.skill_id);
    GW::Effect* soulTwistingEffect = GW::Effects::GetPlayerEffectBySkillId(soulTwisting.skill_id);
    if (!soulTwistingEffect || soulTwistingEffect->GetTimeRemaining() < 1000) {
        if (CanUseSkill(soulTwisting, soulTwistingInfo, cur_energy)) {
            if (UseSkillWithTimer(0)) return true;
        }
    }


    GW::SkillbarSkill boon_of_creation_skillbar = skillbar->skills[6];
    if (!GW::Effects::GetPlayerBuffBySkillId(boon_of_creation_skillbar.skill_id)) {
        GW::Skill* boon_of_creation_skillinfo = GW::SkillbarMgr::GetSkillConstantData(boon_of_creation_skillbar.skill_id);
        if (boon_of_creation_skillinfo && CanUseSkill(boon_of_creation_skillbar, boon_of_creation_skillinfo, cur_energy) && !GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillinfo->skill_id)) {
            if (UseSkillWithTimer(6))
                return true;
        }
    }

    GW::Agent* calledEnemy = GW::Agents::GetAgentByID(called_target_id);
    GW::AgentLiving* calledLiving = calledEnemy ? calledEnemy->GetAsAgentLiving() : nullptr;

    if (!(calledLiving && GW::GetDistance(calledLiving->pos, sidekickLiving->pos) <= GW::Constants::Range::Spellcast * 6/5)) return false;

    if (soulTwistingEffect && soulTwistingEffect->GetTimeRemaining() > 1000 && cur_energy >= 10 && (boon_of_creation_skillbar.GetRecharge() || GW::Effects::GetPlayerEffectBySkillId(boon_of_creation_skillbar.skill_id))) {
        if (!hasShelter) {
            GW::SkillbarSkill shelter = skillbar->skills[1];
            if (!shelter.GetRecharge()) {
                if (UseSkillWithTimer(1, 0U, 2000)) {
                     newShelter = true;
                     newSpirit = true;
                     return true;
                }
            }
        }
        else if (!hasUnion || newShelter) {
            GW::SkillbarSkill unionSkill = skillbar->skills[2];
            if (!unionSkill.GetRecharge()) {
                if (UseSkillWithTimer(2, 0U, 2000)) {
                     newShelter = false;
                     newSpirit = true;
                     return true;
                }
            }
        }

        if (!hasEarthbind) {
            GW::SkillbarSkill earthbind = skillbar->skills[3];
            if (!earthbind.GetRecharge()) {
                if (UseSkillWithTimer(3, 0U, 2000)) {
                     newSpirit = true;
                     return true;
                }
            }
        }
    }

    return false; 
}

void RitualistSidekick::HardReset()
{
    weaponAlly = nullptr;
    hasShelter = false;
    hasUnion = false;
    hasEarthbind = false;
    spiritInEarshot = true;
}
