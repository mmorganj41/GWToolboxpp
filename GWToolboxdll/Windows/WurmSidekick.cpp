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
#include <Windows/WurmSidekick.h>

#include <Logger.h>

namespace {
    clock_t shoutTimer = 0;
    clock_t stanceTimer = 0;
} // namespace

bool WurmSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (party_ids.contains(agentLiving->agent_id) && !resurrectionCast && !agentLiving->GetIsAlive() && GW::GetDistance(agentLiving->pos, playerLiving->pos) <= GW::Constants::Range::Earshot) {
        deadAlly = agentLiving;
    }
    return false;
}

bool WurmSidekick::UseCombatSkill() {
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return false;
    }

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    GW::SkillbarSkill tunnel = skillbar->skills[4]; 
    if (cur_energy > 5 && !tunnel.GetRecharge() && (!target || !target->GetIsKnockedDown())) {
        if (UseSkillWithTimer(4)) {
            return true;
        }
    }

    if (!isCasting(sidekickLiving)) return false; 

    GW::SkillbarSkill bite = skillbar->skills[2];
    if (target && target->GetIsKnockedDown() && !bite.GetRecharge()) {
        if (UseSkillWithTimer(2, target->agent_id)) {
            return true;
        }
    }

    GW::SkillbarSkill strike = skillbar->skills[0];
    if (target && !strike.GetRecharge()) {
        if (UseSkillWithTimer(0, target->agent_id)) {
            return true;
        }
    }

    GW::SkillbarSkill smash = skillbar->skills[1];
    GW::Skill* smashInfo = GW::SkillbarMgr::GetSkillConstantData(smash.skill_id);
    if (CanUseSkill(smash, smashInfo, cur_energy) && target && !target->GetIsMoving() && GW::GetDistance(target->pos, sidekickLiving->pos) <= GW::Constants::Range::Adjacent) {
        if (UseSkillWithTimer(1, target->agent_id)) {
            return true;
        }
    }

    // if (cur_energy < 5) return false;

    // GW::SkillbarSkill feast = skillbar->skills[5];
    // switch (feast.skill_id) {
    //     case GW::Constants::SkillID::Junundu_Feast: {
    //         if (deadAdjacentEnemy && TIMER_DIFF(consumeTimer) > 5000) {
    //             if (UseSkillWithTimer(5)) {
    //                 return true;
    //             }
    //         }
    //     }
    //     default
    // }

    if (deadAlly && cur_energy > 15) {
        GW::SkillbarSkill wail = skillbar->skills[6];
        if (UseSkillWithTimer(6)) {
            return true;
        }
    }


    return false;
 }

bool WurmSidekick::UseOutOfCombatSkill() { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    GW::SkillbarSkill wail = skillbar->skills[6];
    if (sidekickLiving && sidekickLiving->hp < .8) {
        if (UseSkillWithTimer(6)) {
            return true;
        }
    }


    return false;
}

bool WurmSidekick::SetUpCombatSkills(uint32_t called_target_id) { 
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

void WurmSidekick::Setup() {
    SidekickWindow::Settings(true, false, false);
}

void WurmSidekick::ResetTargetValues() {
    deadAlly = nullptr;
}

void WurmSidekick::HardReset()
{
    deadAlly = nullptr;
}

void WurmSidekick::StartCombat() {
}

void WurmSidekick::StopCombat()
{
}

void WurmSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(value_id);
    if (!target_id) return;

    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (caster_id == GW::Agents::GetPlayerId() || casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    GW::Constants::SkillID skillId = static_cast<GW::Constants::SkillID>(value);

    switch (skillId) {
        case GW::Constants::SkillID::Junundu_Wail: {
            resurrectionCast = casterLiving->agent_id;
            break;
        }
    }
}

void WurmSidekick::SkillFinishCallback(const uint32_t caster_id)
{
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    if (resurrectionCast == casterLiving->agent_id) {
        resurrectionCast = 0;
    }

}
