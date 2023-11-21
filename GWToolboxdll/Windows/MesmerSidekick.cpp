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
#include <Windows/MesmerSidekick.h>

#include <Logger.h>

namespace {
    int min_reaction_time = 50;
} // namespace

bool MesmerSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    if (agentLiving->allegiance == GW::Constants::Allegiance::Enemy && agentLiving->GetIsAlive()) {
        if (!enchantedEnemy && agentLiving->GetIsEnchanted() && agentLiving->hp > .3) {
            if (GW::GetSquareDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::SqrRange::Spellcast) {
                enchantedEnemy = agentLiving; 
            }
        }
        if ((!shatterDelusionsTarget)) {
            if (GW::GetSquareDistance(playerLiving->pos, agentLiving->pos) <= GW::Constants::SqrRange::Spellcast)
                if (mesmerEffectSet.contains(agentLiving->agent_id)) {
                    shatterDelusionsTarget = agentLiving;
                }
        }
        if (!hexedAllies.empty()) {
            for (auto& it : hexedAllies) {
                GW::Agent* ally = GW::Agents::GetAgentByID(it.first);
                GW::AgentLiving* allyLiving = ally ? ally->GetAsAgentLiving() : nullptr;
                if (!allyLiving) continue;
                if (GW::GetSquareDistance(allyLiving->pos, agentLiving->pos) <= GW::Constants::SqrRange::Nearby) it.second += 1;
            }
        }
    }

    return false;
}

bool MesmerSidekick::UseCombatSkill()
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

    const int int_ping = static_cast<int>(ping);
    const int interrupt_wait_time = TIMER_DIFF(currentInterruptSkill.reaction_timer) - currentInterruptSkill.reaction_time;
    if (currentInterruptSkill.interruptSkill != std::nullopt && interrupt_wait_time < 750 + 2*250 * fast_casting_activation_array[fastCasting] / 100 + 2*int_ping) {
        GW::Agent* agent = GW::Agents::GetAgentByID(currentInterruptSkill.target_id);
        GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
        Log::Info("Waiting");
        if (!(agentLiving && agentLiving->GetIsAlive() && (agentLiving->skill || TIMER_DIFF(currentInterruptSkill.reaction_timer) < 5))) {
            ResetReaction();
            return false;
        }


        if (TIMER_DIFF(currentInterruptSkill.reaction_timer) >= currentInterruptSkill.reaction_time - int_ping) {
            if (UseSkillWithTimer(*currentInterruptSkill.interruptSkill, currentInterruptSkill.target_id)) {
                ResetReaction();
                return true;
            }
            ResetReaction();
        } 
    }
    else {
        if (cur_energy > 10 && (currentInterruptSkill.interruptSkill == std::nullopt || interrupt_wait_time < 750 + 1250 * fast_casting_activation_array[fastCasting] / 100 + 2 * int_ping)) {
            GW::SkillbarSkill shatterHex = skillbar->skills[2];
            if (!shatterHex.GetRecharge() && !hexedAllies.empty()) {
                uint32_t hexedAlly = 0;
                uint8_t count = 0;
                for (auto& it : hexedAllies) {
                    if (!hexedAlly || it.second > count) {
                        hexedAlly = it.first;
                        count = it.second;
                    }
                }
                if (hexedAlly && UseSkillWithTimer(2, hexedAlly)) return true;
            }
        }

        if (enchantedEnemy && cur_energy > 5 && sidekickLiving->max_energy - cur_energy >= 10 && (currentInterruptSkill.interruptSkill == std::nullopt || interrupt_wait_time < 750 + 2250 * fast_casting_activation_array[fastCasting] / 100 + 2 * int_ping)) {
            GW::SkillbarSkill drainEnchantment = skillbar->skills[7];
            if (!drainEnchantment.GetRecharge()) {
                if (UseSkillWithTimer(7, enchantedEnemy->agent_id)) return true;
            }
        }

        if (shatterDelusionsTarget && cur_energy > 10) {
            GW::SkillbarSkill shatterDelusions = skillbar->skills[0];
            GW::SkillbarSkill cryOfPain = skillbar->skills[3];
            if (!shatterDelusions.GetRecharge() && cryOfPain.GetRecharge()) {
                if (UseSkillWithTimer(0, shatterDelusionsTarget->agent_id)) return true;
            }
        }
    }

    return false;
 }

bool MesmerSidekick::UseOutOfCombatSkill() { 
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

bool MesmerSidekick::SetUpCombatSkills(uint32_t called_target_id) { 
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

void MesmerSidekick::HardReset()
{
    active_skills.clear();
    mesmerEffectSet.clear();
    enchantedEnemy = nullptr;
    hexedAllies.clear();
    shatterDelusionsTarget = nullptr;
    ResetReaction();
}

void MesmerSidekick::ResetTargetValues() {
    hexedAllies.clear();
    enchantedEnemy = nullptr;
    shatterDelusionsTarget = nullptr;
}

void MesmerSidekick::CustomLoop(GW::AgentLiving* sidekick) {
    if (state == Following || state == Picking_up) return;
    GetFastCasting();

    if (!party_ids.empty()) {
        for (auto it : party_ids) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
            if (!agentLiving) continue;
            if (agentLiving->GetIsHexed()) hexedAllies[it] = 0;
        }
    }

    if (!active_skills.empty() && !isCasting(sidekick)) {
        GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

        float cur_energy = sidekick->max_energy * sidekick->energy;

        GW::SkillbarSkill tease = skillbar->skills[5];
        GW::SkillbarSkill overload = skillbar->skills[1];
        GW::SkillbarSkill cryOfPain = skillbar->skills[3];
        GW::SkillbarSkill powerDrain = skillbar->skills[4];
        GW::SkillbarSkill cryOfFrustration = skillbar->skills[6];

        bool canTease = !overload.GetRecharge() && cur_energy >= 5 && sidekick->max_energy - cur_energy >= 10;
        bool canOverload = !overload.GetRecharge() && cur_energy >= 5;
        bool canCryOfPain = !cryOfPain.GetRecharge() && cur_energy >= 10;
        bool canPowerDrain = !powerDrain.GetRecharge() && cur_energy >= 5 && sidekick->max_energy-cur_energy >= 15;
        bool canCryOfFrustration = !cryOfFrustration.GetRecharge() && cur_energy >= 10;
        
        std::optional<SkillInfo> teaseSkill = std::nullopt;
        std::optional<SkillInfo> overloadSkill = std::nullopt;
        std::optional<SkillInfo> cryOfPainSkill = std::nullopt;
        std::optional<SkillInfo> powerDrainSkill = std::nullopt;
        std::optional<SkillInfo> cryOfFrustrationSkill = std::nullopt;
        std::optional<SkillInfo> prioritizedSkill = std::nullopt;

        if (!canOverload && !canPowerDrain && !canCryOfPain && !canCryOfFrustration && !canTease) return;

        std::optional<int32_t> minimum_next_sequence = std::nullopt;
        const int after_cast_two_cast = 750 + fast_casting_activation_array[fastCasting] + ping;

        for (auto& it : active_skills) {
            const auto caster_agent = GW::Agents::GetAgentByID(it.second.caster_id);
            const auto caster = caster_agent ? caster_agent->GetAsAgentLiving() : nullptr;

            if (!caster) {
                continue;
            }

            if (caster->GetIsDead()) {
                active_skills.erase(it.first);
                continue;
            }

            int32_t free_time = it.second.cast_time - 250 * fast_casting_activation_array[fastCasting] / 100 - 2 * ping;

            int32_t free_time_remaining = free_time - TIMER_DIFF(it.second.cast_start);

            if (free_time_remaining <= 0|| free_time - min_reaction_time < 0) {
                active_skills.erase(it.first);
                continue;
            }

            if (free_time_remaining >= after_cast_two_cast) {
                if (!minimum_next_sequence || free_time_remaining - after_cast_two_cast < minimum_next_sequence) minimum_next_sequence = free_time_remaining - after_cast_two_cast;
                continue;
            }
            
            if (canOverload) {
                if (!overloadSkill || overloadSkill->priority < it.second.priority) overloadSkill = it.second;                    
            }
            if (canPowerDrain && it.second.skill_type == SkillType::SPELL || it.second.skill_type == SkillType::CHANT) {
                if (!powerDrainSkill || powerDrainSkill->priority < it.second.priority) powerDrainSkill = it.second;                    
            }
            if (canTease && it.second.skill_type == SkillType::SPELL) {
                if (!teaseSkill || teaseSkill->priority < it.second.priority) teaseSkill = it.second;
            }

            if (canCryOfPain && mesmerEffectSet.contains(it.first) && caster->GetIsHexed()) {
                if (!cryOfPainSkill || cryOfPainSkill->priority < it.second.priority) cryOfPainSkill = it.second;                    
            }

            if (canCryOfFrustration && it.second.priority >= 5) {
                if (!cryOfFrustrationSkill || cryOfFrustrationSkill->priority < it.second.priority) cryOfFrustrationSkill = it.second;                    
            }
        }

        if (currentInterruptSkill.interruptSkill == std::nullopt) {
            if (powerDrainSkill) {
                SetCurrentInterruptSkill(PowerDrain, *powerDrainSkill, minimum_next_sequence);
            }
            else if (cryOfPainSkill) {
                SetCurrentInterruptSkill(CryOfPain, *cryOfPainSkill, minimum_next_sequence);
            }
            else if (cryOfFrustrationSkill) {
                SetCurrentInterruptSkill(CryOfFrustration, *cryOfFrustrationSkill, minimum_next_sequence);
            }
            else if (teaseSkill) {
                SetCurrentInterruptSkill(Tease, *teaseSkill, minimum_next_sequence);
            }
            else if (overloadSkill) {
                SetCurrentInterruptSkill(Overload, *overloadSkill, minimum_next_sequence);
            }
        }
        else if (minimum_next_sequence)
        {
            currentInterruptSkill.reaction_time = std::min(*minimum_next_sequence - 50, currentInterruptSkill.reaction_time);
        }
    } 
}

void MesmerSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(target_id);
    const int cast_start = TIMER_INIT();

    const auto agent = GW::Agents::GetAgentByID(caster_id);
    const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

    float cast_time = modified_cast_times[caster_id];
    modified_cast_times.erase(caster_id);

    if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy))
        return;
    if (living_agent->GetIsDead())
        return;
    if (living_agent->type_map & 0x40000) return;

    const GW::Agent* player = GW::Agents::GetPlayer();
    if (!player)
        return;
    const float distance = GW::GetDistance(player->pos, living_agent->pos);

    if (distance > GW::Constants::Range::Spellcast * 11 / 10)
        return;

    const auto skill = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));

    SkillType skill_type = SkillType::OTHER;

    if (skill) {
        skill_type = skill_type_map[skill->type];
        if (!cast_time) {
            cast_time = skill->activation;
            if (value_id == GW::Packet::StoC::GenericValueID::attack_skill_activated) {
                if (!cast_time) {
                    cast_time = living_agent->weapon_attack_speed * living_agent->attack_speed_modifier;
                }
                cast_time /= 2;
            }
        }
    }

    const auto* target = GW::Agents::GetTarget();

    uint8_t priority = 0;
    
    if (target && target->agent_id == caster_id) {
        priority += 2;
    }
    
    uint32_t activation_time = 250 * fast_casting_activation_array[fastCasting] / 100  + ping + min_reaction_time;

    if (cast_time * 1000 > activation_time) {
        if (skill) {
            priority += PrioritizeActivation(skill->activation, skill->weapon_req);
            priority += PrioritizeClass(skill->profession);
            priority += PrioritizeCost(skill->energy_cost, skill->adrenaline);
            priority += PrioritizeRecharge(skill->recharge);
            if (skill->IsElite())
                priority += 1;
        }

        SkillInfo skill_info = {static_cast<GW::Constants::SkillID>(value), skill_type, caster_id, cast_start, static_cast<uint32_t>(cast_time * 1000), priority};
        active_skills.insert_or_assign(caster_id, skill_info);
    }
}

void MesmerSidekick::SkillFinishCallback(const uint32_t caster_id) {
    const auto agent = GW::Agents::GetAgentByID(caster_id);
    const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy))
        return;

    active_skills.erase(caster_id);
}

void MesmerSidekick::GenericModifierCallback(uint32_t type, uint32_t caster_id, float value)
{
    if (type != GW::Packet::StoC::GenericValueID::casttime)
        return;

    modified_cast_times.insert_or_assign(caster_id, value);
}

uint8_t MesmerSidekick::PrioritizeActivation(float activation, uint32_t weapon_req)
{
    if (activation < .5) {
        if (activation == 0 && weapon_req > 0) {
            return 2;
        }
        return 0;
    }
    else if (activation <= 1.0) {
        return 1;
    }
    else if (activation <= 2.0) {
        return 2;
    }
    else {
        return 3;
    }
}

uint8_t MesmerSidekick::PrioritizeClass(uint8_t profession)
{
    switch (profession) {
        case 3:
        case 4:
        case 8: return 3;
        case 5:
        case 6: return 2;
        case 7:
        case 2:
        case 10: return 1;
        default: return 0;
    }
}

uint8_t MesmerSidekick::PrioritizeCost(uint8_t energy_cost, uint32_t adrenaline)
{
    if (energy_cost > 15) {
        return 3;
    }
    else if (energy_cost > 10) {
        return 2;
    }
    else if (energy_cost > 5) {
        return 1;
    }
    else {
        if (adrenaline == 0) {
            return 0;
        }
        else if (adrenaline <= 75) {
            return 2;
        }
        else if (adrenaline <= 150) {
            return 4;
        }
        else {
            return 6;
        }
    }
}

uint8_t MesmerSidekick::PrioritizeRecharge(uint32_t recharge)
{
    if (recharge <= 3) {
        return 0;
    }
    else if (recharge <= 12) {
        return 2;
    }
    else if (recharge <= 25) {
        return 3;
    }
    else {
        return 5;
    }
}

void MesmerSidekick::GetFastCasting() {
    GW::Attribute * attributes = GW::PartyMgr::GetAgentAttributes(GW::Agents::GetPlayerId());
    assert(attributes);
    attributes = &attributes[static_cast<uint8_t>(GW::Constants::Attribute::FastCasting)];
    if (attributes && attributes->level) {
        if (attributes->level <= 21) 
            fastCasting = attributes->level;
    }
}

void MesmerSidekick::ResetReaction() {
    ClearCurrentInterruptSkill();
}

void MesmerSidekick::CantAct() {
    ResetReaction();
}

void MesmerSidekick::WhenKiting()
{
    if (TIMER_DIFF(currentInterruptSkill.reaction_timer) >= currentInterruptSkill.reaction_time - 5 - static_cast<int>(ping)) {
        state = Fighting;
    }
}

void MesmerSidekick::ClearCurrentInterruptSkill() {
    currentInterruptSkill.interruptSkill = std::nullopt;
    currentInterruptSkill.target_id = 0;
    currentInterruptSkill.cast_start = 0;
    currentInterruptSkill.cast_time = 0;
    currentInterruptSkill.priority = 0;
    currentInterruptSkill.reaction_time = 0;
    currentInterruptSkill.reaction_timer = 0;
}
void MesmerSidekick::SetCurrentInterruptSkill(InterruptSkill interruptSkill, SkillInfo skillInfo, std::optional<int32_t> minimum_next_sequence)
{
    Log::Info("Set interrupt skill");
    currentInterruptSkill.interruptSkill = interruptSkill;
    currentInterruptSkill.target_id = skillInfo.caster_id;
    currentInterruptSkill.cast_start = skillInfo.cast_start;
    currentInterruptSkill.cast_time = skillInfo.cast_time;
    currentInterruptSkill.priority = skillInfo.priority;
    currentInterruptSkill.reaction_timer = TIMER_INIT();
    int free_time = skillInfo.cast_time - 250 * fast_casting_activation_array[fastCasting] / 100 - 2 * ping - std::max(0L, min_reaction_time - TIMER_DIFF(currentInterruptSkill.cast_start));
    if (interruptSkill != Overload) {
        currentInterruptSkill.reaction_time = free_time > 0 ? -rand() % 50 + free_time - 50 : 0;
    }
    else {
        currentInterruptSkill.reaction_time = std::min(min_reaction_time, free_time) - 50;
    }

    if (minimum_next_sequence) {
        currentInterruptSkill.reaction_time = std::min(*minimum_next_sequence - 50 + static_cast<int>(TIMER_DIFF(currentInterruptSkill.reaction_timer)), currentInterruptSkill.reaction_time);
    }
}

void MesmerSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value) {
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::mesmer_symbol)) return;

    mesmerEffectSet.insert(agent_id);
}

void MesmerSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value) {
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::mesmer_symbol)) return;

    mesmerEffectSet.erase(agent_id);
}

void MesmerSidekick::StopCombat() {
    active_skills.clear();
    mesmerEffectSet.clear();
}
