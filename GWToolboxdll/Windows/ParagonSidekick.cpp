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
#include <Windows/ParagonSidekick.h>

#include <Logger.h>

namespace {
    clock_t shoutTimer = 0;
    clock_t stanceTimer = 0;
} // namespace

bool ParagonSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (party_ids.contains(agentLiving->agent_id)) {
        if (agentLiving->GetIsMoving() && !moving_ally) moving_ally = agentLiving;
    }
    return false;
}

bool ParagonSidekick::UseCombatSkill() {
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return false;
    }

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (cur_energy > 16) {
        GW::SkillbarSkill theresNothingToFear = skillbar->skills[7];
        if (!theresNothingToFear.GetRecharge() && !isUncentered(sidekickLiving)) {
            if (UseSkillWithTimer(7)) return true;
        }
    }

    GW::SkillbarSkill goForTheEyes = skillbar->skills[3];
    GW::Skill* goForTheEyesInfo = GW::SkillbarMgr::GetSkillConstantData(goForTheEyes.skill_id);
    if (goForTheEyesInfo && goForTheEyes.adrenaline_a >= goForTheEyesInfo->adrenaline) {
        if (UseSkillWithTimer(3)) return true;
    }
    
    if (cur_energy > 11 && moving_ally) {
        GW::SkillbarSkill fallBack = skillbar->skills[6];
        if (!fallBack.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(fallBack.skill_id)) {
            if (UseSkillWithTimer(6)) return true;
        }
    }

    GW::SkillbarSkill focusedAnger = skillbar->skills[5];
    GW::SkillbarSkill forGreatJustice = skillbar->skills[1];
    if (!(GW::Effects::GetPlayerEffectBySkillId(focusedAnger.skill_id) || GW::Effects::GetPlayerEffectBySkillId(forGreatJustice.skill_id) && shoutTimer > 2500)) {
        if (cur_energy > 11 && !focusedAnger.GetRecharge()) {
            if (UseSkillWithTimer(5)) {
                shoutTimer = TIMER_INIT();
                return true;
            }
        }
        else if (cur_energy > 6 && !forGreatJustice.GetRecharge()) {
            if (UseSkillWithTimer(1)) {
                shoutTimer = TIMER_INIT();
                return true;
            }
        }
    }

    GW::SkillbarSkill theyreOnFire = skillbar->skills[4];
    if (cur_energy > 11 && !theyreOnFire.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(theyreOnFire.skill_id)) {
        if (UseSkillWithTimer(4)) return true;
    }

    if (isCasting(sidekickLiving)) {
        return false;
    }
    
    GW::SkillbarSkill wildThrow = skillbar->skills[0];
    GW::Skill* wildThrowSkillInfo = GW::SkillbarMgr::GetSkillConstantData(wildThrow.skill_id);
    if (CanUseSkill(wildThrow, wildThrowSkillInfo, cur_energy)) {
        if (wild_blow_target) {
            if (UseSkillWithTimer(0, wild_blow_target->agent_id)) {
                stanceMap.erase(wild_blow_target->agent_id);
                return true;
            }
        }

        GW::AgentLiving* targetLiving = GW::Agents::GetTargetAsAgentLiving();

        if (targetLiving && TIMER_DIFF(stanceTimer) > 8000) {
            if (UseSkillWithTimer(0, targetLiving->agent_id)) {
                stanceMap.erase(targetLiving->agent_id);
                return true;
            }

        }
    }

    return false;
 }

bool ParagonSidekick::UseOutOfCombatSkill() { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    GW::SkillbarSkill aggressiveRefrain = skillbar->skills[2];
    if (cur_energy >= 26) {
        if (!aggressiveRefrain.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(aggressiveRefrain.skill_id)) {
            if (UseSkillWithTimer(2)) {
                return true;
            }
        }
    }

    if (TIMER_DIFF(shoutTimer) < 7000) return false;

    if (cur_energy > 11 && GW::Effects::GetPlayerEffectBySkillId(aggressiveRefrain.skill_id)) {
        GW::SkillbarSkill fallBack = skillbar->skills[6];
       GW::SkillbarSkill theyreOnFire = skillbar->skills[4];
        if (!fallBack.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(fallBack.skill_id)) {
           if (UseSkillWithTimer(6)) {
               shoutTimer = TIMER_INIT();
               return true;
           }
        }
        else if (!theyreOnFire.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(theyreOnFire.skill_id)) {
           if (UseSkillWithTimer(4)) {
               shoutTimer = TIMER_INIT();
               return true;
           }
        }
    }

    return false;
}

bool ParagonSidekick::SetUpCombatSkills(uint32_t called_target_id) { 
    UNREFERENCED_PARAMETER(called_target_id);
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    //float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (isCasting(sidekickLiving)) return false;

    return false; 
}

void ParagonSidekick::Setup() {
    SidekickWindow::Settings(true, true, true);
}

void ParagonSidekick::ResetTargetValues() {
    moving_ally = nullptr;
    wild_blow_target = nullptr;
}

void ParagonSidekick::HardReset()
{
    moving_ally = nullptr;
    wild_blow_target = nullptr;
    stanceMap.clear();
}

void ParagonSidekick::StartCombat() {
    stanceTimer = TIMER_INIT();
}

void ParagonSidekick::StopCombat()
{
    stanceMap.clear();
}

void ParagonSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(target_id);
    if (value_id == GW::Packet::StoC::GenericValueID::instant_skill_activated) {
        GW::Agent* agent = GW::Agents::GetAgentByID(caster_id);
        GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

        if (!agentLiving) return;

        if (agentLiving->allegiance == GW::Constants::Allegiance::Enemy) {
           GW::Skill* skill = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
           if (!(skill && skill->type == GW::Constants::SkillType::Stance)) return;
           Log::Info("Enemy used stance");

           SkillDuration duration = {TIMER_INIT(), skill->duration15};

           stanceMap.insert_or_assign(caster_id, duration);
           stanceTimer = TIMER_INIT();
        }
        
    }
}

void ParagonSidekick::CustomLoop(GW::AgentLiving* sidekick) {
    if (state == Following || state == Picking_up) return;


    uint32_t prio_target = 0;
    std::optional<SkillDuration> prio_duration = std::nullopt;

    for (auto& it : stanceMap) {
        GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
        GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!(agentLiving && agentLiving->GetIsAlive())) {
            stanceMap.erase(it.first);
            continue;
        }

        uint32_t remaining_duration = it.second.duration - TIMER_DIFF(it.second.startTime);

        if (remaining_duration <= 1000) {
            stanceMap.erase(it.first);
            continue;
        }

        if (GW::GetDistance(agentLiving->pos, sidekick->pos) > GW::Constants::Range::Earshot) continue;
        
        if (!prio_duration || (remaining_duration > static_cast<uint32_t>(prio_duration->duration) - TIMER_DIFF(prio_duration->startTime))) {
            prio_target = it.first;
            prio_duration = it.second;
        }
    }

    if (prio_duration && prio_target) {
        GW::Agent* agent = GW::Agents::GetAgentByID(prio_target);
        GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!agentLiving) return;

        wild_blow_target = agentLiving;
    }

}
