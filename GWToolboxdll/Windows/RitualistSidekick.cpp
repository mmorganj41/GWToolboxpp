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

bool RitualistSidekick::UseCombatSkill() { return false; }

bool RitualistSidekick::UseOutOfCombatSkill() { return false; }

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
