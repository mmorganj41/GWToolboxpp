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

#include <GWCA/Context/WorldContext.h>

#include <GWCA/Packets/StoC.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Modules/InventoryManager.h>
#include <Windows/RangerSidekick.h>

#include <Logger.h>
#include <GWCA/GameEntities/Hero.h>

namespace {
    bool pet_attack_finished = true;
    clock_t call_of_protection_timer = -120000;
    clock_t call_of_haste_timer = -30000;
    bool enter_combat = true;
    bool hasEdge = false;
} // namespace

bool RangerSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet && agentLiving->player_number == 2876 && GW::GetDistance(playerLiving->pos, agentLiving->pos) < GW::Constants::Range::Spellcast * 11 / 10) {
        hasEdge = true;
    }
    if (agentLiving->allegiance == GW::Constants::Allegiance::Enemy && GW::GetDistance(playerLiving->pos, agentLiving->pos) < GW::Constants::Range::Spirit) enemiesInSpiritRange += 1;

    GW::PetInfo* pet_info = GW::PartyMgr::GetPetInfo();
    if (!pet_info) return false;

    if (pet_info->agent_id == agentLiving->agent_id) {
        pet = agentLiving;
        if (!agentLiving->GetIsAlive()) {
            call_of_protection_timer = -120000;
            call_of_haste_timer = -30000;
        }
    }

    return false;
}

bool RangerSidekick::UseCombatSkill() {
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return false;
    }

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;


    GW::SkillbarSkill togetherAsOne = skillbar->skills[4];
    if (cur_energy > 4 && !togetherAsOne.GetRecharge()) {
        if (GW::Effects::GetPlayerBuffBySkillId(togetherAsOne.skill_id)) {
            if (UseSkillWithTimer(4)) return true;
        }
    }

    if (pet && pet->GetIsAlive()) {
        GW::SkillbarSkill callOfHaste = skillbar->skills[5];
        if (TIMER_DIFF(call_of_haste_timer) > 30000 && !callOfHaste.GetRecharge() && cur_energy >= 4) {
            if (UseSkillWithTimer(5)) {
                call_of_haste_timer = TIMER_INIT();
                return true;
            }
        }

        GW::SkillbarSkill callOfProtection = skillbar->skills[6];
        if (TIMER_DIFF(call_of_protection_timer) > 120000 && !callOfProtection.GetRecharge() && cur_energy >= 2) {
            if (UseSkillWithTimer(6)) {
                call_of_haste_timer = TIMER_INIT();
                return true;
            }
        }
        GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

        if (target && target->GetIsAlive() && pet_attack_finished) {
            GW::SkillbarSkill scavengerStrike = skillbar->skills[2];
            if (target->GetIsConditioned() && cur_energy > 2 && cur_energy < 13 && !scavengerStrike.GetRecharge()) {
                if (UseSkillWithTimer(2)) {
                    pet_attack_finished = false;
                    return true;
                }
            }

            GW::SkillbarSkill brutalStrike = skillbar->skills[1];
            if (target->hp < .5 && cur_energy > 4 && !brutalStrike.GetRecharge()) {
                if (UseSkillWithTimer(1)) {
                    pet_attack_finished = false;
                    return true;
                }
            }


            GW::SkillbarSkill predatorsPounce = skillbar->skills[0];
            if (cur_energy > 2 && !predatorsPounce.GetRecharge()) {
                if (UseSkillWithTimer(0)) {
                    pet_attack_finished = false;
                    return true;
                }
            }
        }
    }

    if (isCasting(sidekickLiving)) {
        return false;
    }

    GW::SkillbarSkill edgeOfExtinction = skillbar->skills[3];
    if (cur_energy > 2 && !edgeOfExtinction.GetRecharge() && !hasEdge && enemiesInSpiritRange > 4) {
        if (UseSkillWithTimer(3)) return true;
    }

    if (pet && !pet->GetIsAlive()) {
        GW::SkillbarSkill comfortAnimal = skillbar->skills[7];
        if (cur_energy > 2 && !comfortAnimal.GetRecharge()) {
            if (UseSkillWithTimer(7))
                return true;
        }
    }

    return false;
 }

bool RangerSidekick::UseOutOfCombatSkill() { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

     float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

     GW::SkillbarSkill togetherAsOne = skillbar->skills[4];
     if (lowest_health_ally && lowest_health_ally->hp < .5 && cur_energy >= 4 && !togetherAsOne.GetRecharge()) {
        if (GW::Effects::GetPlayerBuffBySkillId(togetherAsOne.skill_id)) {
            if (UseSkillWithTimer(4)) return true;
        }
     }

     if (pet && pet->GetIsAlive()) {
        GW::SkillbarSkill callOfHaste = skillbar->skills[5];
        if (TIMER_DIFF(call_of_haste_timer) > 30000 && !callOfHaste.GetRecharge() && cur_energy > 4) {
            if (UseSkillWithTimer(5)) {
                call_of_haste_timer = TIMER_INIT();
                return true;
            }
        }

        GW::SkillbarSkill callOfProtection = skillbar->skills[6];
        if (TIMER_DIFF(call_of_protection_timer) > 120000 && !callOfProtection.GetRecharge() && cur_energy > 2) {
            if (UseSkillWithTimer(6)) {
                call_of_haste_timer = TIMER_INIT();
                return true;
            }
        }
    }
   
    if (isCasting(sidekickLiving)) {
        return false;
    }

    if (pet && !pet->GetIsAlive()) {
        GW::SkillbarSkill comfortAnimal = skillbar->skills[7];
        if (cur_energy > 2 && !comfortAnimal.GetRecharge()) {
            if (UseSkillWithTimer(7))
                return true;
        }
    }
    return false;
}

bool RangerSidekick::SetUpCombatSkills(uint32_t called_target_id) { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    GW::Agent* agent = GW::Agents::GetAgentByID(called_target_id);
    GW::AgentLiving* called_target = agent ? agent->GetAsAgentLiving() : nullptr; 
    
    if (called_target && enter_combat) {
        uint32_t target = GW::Agents::GetTargetId();
        if (target != called_target->agent_id) {
            GW::Agents::ChangeTarget(called_target->agent_id);
        }
        else {
            GW::GameThread::Enqueue([&] {
                GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::Fight);
            });
        }
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    GW::SkillbarSkill togetherAsOne = skillbar->skills[4];
    if (cur_energy >= 4 && !togetherAsOne.GetRecharge()) {
        if (GW::Effects::GetPlayerBuffBySkillId(togetherAsOne.skill_id)) {
            if (UseSkillWithTimer(4))
                return true;
        }
    }

    GW::SkillbarSkill edgeOfExtinction = skillbar->skills[3];
    if (cur_energy > 2 && !edgeOfExtinction.GetRecharge() && !hasEdge && enemiesInSpiritRange > 4) {
        if (UseSkillWithTimer(3)) return true;
    }


    return false; 
}

void RangerSidekick::Setup() {
    SidekickWindow::Settings(true, true, false);
}

void RangerSidekick::ResetTargetValues() {
    hasEdge = false;
    pet = nullptr;
    enemiesInSpiritRange = 0;
}

void RangerSidekick::AttackFinished(uint32_t caster_id) {
    GW::PetInfo* pet_info = GW::PartyMgr::GetPetInfo();
    if (!pet_info) return;

    if (pet_info->agent_id == caster_id) {
        pet_attack_finished = true;
    }
}

void RangerSidekick::Attack() {
    GW::GameThread::Enqueue([&] {
        GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::Fight);
    });
}

void RangerSidekick::StartCombat()
{
    enter_combat = true;
    GW::GameThread::Enqueue([&] {
        GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::Guard);
    });
}

void RangerSidekick::StopCombat() {
    pet_attack_finished = true;
    GW::GameThread::Enqueue([&] {
        GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::AvoidCombat);
    });
}

void RangerSidekick::TargetSwitchEffect() {
    GW::GameThread::Enqueue([&] {
        GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::Fight);
    });
}
