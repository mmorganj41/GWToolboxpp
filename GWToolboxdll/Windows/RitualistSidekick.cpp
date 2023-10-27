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
    hasBloodsong = false;
    hasPain = false;

}

bool RitualistSidekick::InCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    if (agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet) {
        if (GW::GetSquareDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::SqrRange::Earshot)
            switch (agentLiving->player_number) {
                case 4124: {
                    hasPain = true;
                    return true;
                }
                case 4227: {
                    hasBloodsong = true;
                    return true;
                }
            }
    }

    return false;
}

bool RitualistSidekick::OutOfCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet) {
        switch (agentLiving->player_number) {
            case 4124: {
                hasPain = true;
                return true;
            }
            case 4227: {
                hasBloodsong = true;
                return true;
            }
        }
    }

    return false;
}

bool RitualistSidekick::UseCombatSkill() {
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::MapAgent* sidekick = GW::Agents::GetMapAgentByID(GW::Agents::GetPlayerId());
    if (!sidekick) return false;

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) return false;
    
    GW::Agent* target = GW::Agents::GetTarget();

    if (!hasBloodsong && target && GW::GetDistance(sidekickLiving->pos, target->pos) < GW::Constants::Range::Spirit / 2) {
        GW::SkillbarSkill bloodsong_skillbar = skillbar->skills[0];
        GW::Skill* bloodsong_skillinfo = GW::SkillbarMgr::GetSkillConstantData(bloodsong_skillbar.skill_id);
        if (bloodsong_skillinfo && CanUseSkill(bloodsong_skillbar, bloodsong_skillinfo, sidekick->cur_energy)) {
            GW::SkillbarMgr::UseSkill(0);
            return true;
        }
    }

        if (!hasPain && target && GW::GetDistance(sidekickLiving->pos, target->pos) < GW::Constants::Range::Spirit / 2)
        {
            GW::SkillbarSkill pain_skillbar = skillbar->skills[1];
            GW::Skill* pain_skillinfo = GW::SkillbarMgr::GetSkillConstantData(pain_skillbar.skill_id);
            if (pain_skillinfo && CanUseSkill(pain_skillbar, pain_skillinfo, sidekick->cur_energy)) {
                GW::SkillbarMgr::UseSkill(1);
                return true;
            };
        }

        if (lowest_health_ally && lowest_health_ally->hp < .7 && !lowest_health_ally->GetIsWeaponSpelled() && sidekick->cur_energy >= 17) {
            GW::SkillbarSkill weapon_of_warding_skillbar = skillbar->skills[2];
            GW::Skill* weapon_of_warding_skillinfo = GW::SkillbarMgr::GetSkillConstantData(weapon_of_warding_skillbar.skill_id);
            if (weapon_of_warding_skillinfo && CanUseSkill(weapon_of_warding_skillbar, weapon_of_warding_skillinfo, sidekick->cur_energy)) {
                GW::SkillbarMgr::UseSkill(2, lowest_health_ally->agent_id);
                return true;
            }
        }

        if (sidekick->cur_energy > 17 && (painTarget || bloodsongTarget)) {
            GW::SkillbarSkill painful_bond_skillbar = skillbar->skills[3];
            GW::Skill* painful_bond_skillinfo = GW::SkillbarMgr::GetSkillConstantData(painful_bond_skillbar.skill_id);
            if (painful_bond_skillinfo && CanUseSkill(painful_bond_skillbar, painful_bond_skillinfo, sidekick->cur_energy)) {
                if (painTarget && !painfulBondSet.contains(*painTarget)) {
                    GW::Agent* spiritTarget = GW::Agents::GetAgentByID(*painTarget);
                    if (spiritTarget) {
                        GW::AgentLiving* targetLiving = spiritTarget->GetAsAgentLiving();
                        if (targetLiving && targetLiving->GetIsAlive()) {
                            GW::SkillbarMgr::UseSkill(3, targetLiving->agent_id);
                            return true;
                        }
                    }
                    painTarget = std::nullopt;
                }
                if (bloodsongTarget && !painfulBondSet.contains(*bloodsongTarget)) {
                    GW::Agent* spiritTarget = GW::Agents::GetAgentByID(*bloodsongTarget);
                    if (spiritTarget) {
                        GW::AgentLiving* targetLiving = spiritTarget->GetAsAgentLiving();
                        if (targetLiving && targetLiving->GetIsAlive()) {
                            GW::SkillbarMgr::UseSkill(3, targetLiving->agent_id);
                            return true;
                        }
                    }
                    bloodsongTarget = std::nullopt;
                }
            }
        }

    return false;
 }

bool RitualistSidekick::UseOutOfCombatSkill() { 
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) return false;

    GW::MapAgent* sidekick = GW::Agents::GetMapAgentByID(GW::Agents::GetPlayerId());
    if (!sidekick) return false;

    if (lowest_health_ally && lowest_health_ally->hp < .7 && !lowest_health_ally->GetIsWeaponSpelled() && sidekick->cur_energy >= 17) {
        GW::SkillbarSkill weapon_of_warding_skillbar = skillbar->skills[2];
        GW::Skill* weapon_of_warding_skillinfo = GW::SkillbarMgr::GetSkillConstantData(weapon_of_warding_skillbar.skill_id);
        if (weapon_of_warding_skillinfo && CanUseSkill(weapon_of_warding_skillbar, weapon_of_warding_skillinfo, sidekick->cur_energy)) {
            GW::SkillbarMgr::UseSkill(2, lowest_health_ally->agent_id);
            return true;
        }
    }

    return false; 
}

bool RitualistSidekick::SetUpCombatSkills() { return false; }

void RitualistSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    if (agent_id == GW::Agents::GetPlayerId()) {
        switch (static_cast<GW::Constants::SkillID>(value)) {
            case GW::Constants::SkillID::Boon_of_Creation: {
                hasBoonOfCreation = true;
                return;
            }
        }
    }
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    if (!agent) return;
    GW::AgentLiving* agentLiving = agent->GetAsAgentLiving();
    if (!agentLiving) return;
    switch (agentLiving->allegiance) {
        case GW::Constants::Allegiance::Enemy: {
            painfulBondSet.insert(agent_id);
            return;
        }
    }
}

void RitualistSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    if (agent_id == GW::Agents::GetPlayerId()) {
        switch (static_cast<GW::Constants::SkillID>(value)) {
            case GW::Constants::SkillID::Boon_of_Creation: {
                hasBoonOfCreation = false;
                return;
            }
        }
    }
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    if (!agent) return;
    GW::AgentLiving* agentLiving = agent->GetAsAgentLiving();
    if (!agentLiving) return;
    switch (agentLiving->allegiance) {
        case GW::Constants::Allegiance::Enemy: {
            painfulBondSet.erase(agent_id);
            return;
        }
    }
}

void RitualistSidekick::HardReset()
{
    painfulBondSet.clear();
    painTarget = std::nullopt;
    bloodsongTarget = std::nullopt;
}

void RitualistSidekick::SkillCallback(const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(caster_id);
    if (!agent) return;
    GW::AgentLiving* agentLiving = agent->GetAsAgentLiving();
    if (!agentLiving) return;
    switch (agentLiving->allegiance) {
        case GW::Constants::Allegiance::Spirit_Pet: {
            switch (static_cast<GW::Constants::SkillID>(value)) {
                case GW::Constants::SkillID::Bloodsong_attack: {
                    bloodsongTarget = target_id;
                    break;
                }
                case GW::Constants::SkillID::Pain_attack: {
                    painTarget = target_id;
                    break;
                }
                                                        
            }
        }
    }
}
