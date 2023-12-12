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
} // namespace


void MonkSidekick::HardReset() {
    damageMap.clear();
    lowestHealthNonParty = nullptr;
    monkEffectSet.clear();
    conditionedAlly = nullptr;
    lowestHealthIncludingPet = nullptr;
    airOfEnchantmentMap.clear();
    spiritBondMap.clear();
    airOfEnchantmentMap.clear();
    seedOfLifeMap.clear();
    shieldOfAbsorptionTarget = 0;
}

void MonkSidekick::ResetTargetValues() {
    seedOfLifeTarget = 0;
    conditionedAlly = nullptr;
    lowestHealthNonParty = nullptr;
    lowestHealthIncludingPet = nullptr;
    damagedAllies = 0;
    necromancerAgent = nullptr;
    shieldOfAbsorptionTarget = 0;
}

void MonkSidekick::StartCombat() {
}

void MonkSidekick::StopCombat() {
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
        case GW::Constants::SkillID::Foul_Feast:
        case GW::Constants::SkillID::Dismiss_Condition: {
            cureConditionMap.insert_or_assign(casterLiving->agent_id, *target_id);
            break;
        }
    }
}

void MonkSidekick::SkillFinishCallback(const uint32_t caster_id) {
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;
    
    cureConditionMap.erase(casterLiving->agent_id);
}

bool MonkSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (agentLiving->GetIsAlive()) {
        if ((party_ids.contains(agentLiving->agent_id) || (agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet && !agentLiving->GetIsSpawned()))) {
            if ((!lowestHealthIncludingPet || lowestHealthIncludingPet->hp > agentLiving->hp)) lowestHealthIncludingPet = agentLiving;
            if (party_ids.contains(agentLiving->agent_id) && agentLiving->hp < .8) {
                damagedAllies += 1;
            }
            if (agentLiving->GetIsConditioned() && (!conditionedAlly || (conditionedAlly && conditionedAlly->hp > agentLiving->hp))) {
                bool already_casting = false;
                for (auto& it : cureConditionMap) {
                    if (it.second == agentLiving->agent_id) already_casting = true;
                }
                if (!already_casting) conditionedAlly = agentLiving;
            }
        }
        else if (agentLiving->allegiance == GW::Constants::Allegiance::Ally_NonAttackable) {
            if ((!lowestHealthNonParty || lowestHealthNonParty->hp > agentLiving->hp)) lowestHealthNonParty = agentLiving;
        }
    }
    return false;
}

void MonkSidekick::CustomLoop(GW::AgentLiving* sidekick)
{
    UNREFERENCED_PARAMETER(sidekick);

    if (!GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain)) {
        blessedAuraWithHeroic = false;
        divineBoonWithHeroic = false;
    }


    if (state == Following || state == Picking_up) return;

    if (!seedOfLifeMap.empty()) {
       for (auto& it : seedOfLifeMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                Log::Info("ally dead");
                seedOfLifeMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 250) {
                seedOfLifeMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsEnchanted() && monkEffectSet.contains(it.first))) {
                seedOfLifeMap.erase(it.first);
                continue;
            }
       }
    }

    if (!shieldOfAbsorptionMap.empty()) {
       for (auto& it : shieldOfAbsorptionMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                Log::Info("ally dead");
                shieldOfAbsorptionMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 250) {
                shieldOfAbsorptionMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsEnchanted() && monkEffectSet.contains(it.first))) {
                shieldOfAbsorptionMap.erase(it.first);
                continue;
            }
       }
    }

    if (!spiritBondMap.empty()) {
       for (auto& it : spiritBondMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                Log::Info("ally dead");
                spiritBondMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 250) {
                spiritBondMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsEnchanted() && monkEffectSet.contains(it.first))) {
                spiritBondMap.erase(it.first);
                continue;
            }
       }
    }

    if (!airOfEnchantmentMap.empty()) {
       for (auto& it : airOfEnchantmentMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                Log::Info("ally dead");
                airOfEnchantmentMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 250) {
                airOfEnchantmentMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsEnchanted() && monkEffectSet.contains(it.first))) {
                airOfEnchantmentMap.erase(it.first);
                continue;
            }
       }
    }

    for (auto& it : party_ids) {
       if (necromancerAgent) break;
       GW::Agent* agent = GW::Agents::GetAgentByID(it);
       GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
       if (!agentLiving) continue;
       if (agentLiving->primary == 4 && agentLiving->GetIsAlive()) necromancerAgent = agentLiving;
    }

    if (!damageMap.empty()) {
        clock_t new_time = TIMER_INIT();

        uint32_t timeStamp = new_time / 1000;

        size_t index = (timeStamp) % 5;

        uint32_t max_short = 0;
        GW::AgentID short_agent = 0;
        uint32_t max_medium = 0;
        GW::AgentID medium_agent = 0;
        uint32_t max_long = 0;
        GW::AgentID long_agent = 0;
        GW::AgentID shield_agent = 0;
        uint32_t max_shield = 0;

        for (auto& it : damageMap) {
            if (it.second[index].timeStamp != timeStamp) {
                it.second[index] = { timeStamp, 0, 0 };
            }


            uint32_t short_packets = 0;
            uint32_t medium_packets = 0;
            uint32_t long_packets = 0;
            uint32_t shield_packets = 0;

            bool isPlayer = sidekick->agent_id == it.first;
            bool hasShield = shieldOfAbsorptionMap.contains(it.first);

            for (size_t i = 0; i < 5; i++) {
                const uint32_t packets = it.second[(index + i) % 5].packets;
                
                if (i < 4 && !hasShield) {
                    shield_packets += packets;
                }

                if (isPlayer) continue;
                if (i == 0) {
                    short_packets += packets;
                }
                if (i < 3) {
                    medium_packets += packets;
                }
                long_packets += packets;
            }

            if (short_packets > max_short) {
                max_short = short_packets;
                short_agent = it.first;
            }
            if (medium_packets > max_medium) {
                max_medium = medium_packets;
                medium_agent = it.first;
            }
            if (long_packets > max_long) {
                max_long = long_packets;
                long_agent = it.first;
            }
            if (shield_packets > max_shield) {
                max_shield = shield_packets;
                shield_agent = it.first;
            }
        }

        if (max_short > 1) {
            seedOfLifeTarget = short_agent;
        }
        else if (max_medium > 4) {
            seedOfLifeTarget = medium_agent;
        }
        else if (max_long > 11) {
            seedOfLifeTarget = long_agent;
        }

        if (max_shield > 3) {
            shieldOfAbsorptionTarget = shield_agent;
        }
    }
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

    GW::SkillbarSkill blessedAura = skillbar->skills[1];
    if (cur_energy > 10 && !blessedAura.GetRecharge()) {
        if (GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain) && !blessedAuraWithHeroic) {
            GW::Effect* blessedAuraEffect = GW::Effects::GetPlayerEffectBySkillId(blessedAura.skill_id);
            if (blessedAuraEffect) {
                GW::Effects::DropBuff(blessedAuraEffect->effect_id);
                return true;
            }
            else if (UseSkillWithTimer(1)) {
                blessedAuraWithHeroic = true;
                return true;
            }
        }
        else if (!GW::Effects::GetPlayerEffectBySkillId(blessedAura.skill_id)) {
            if (UseSkillWithTimer(1)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill divineBoon = skillbar->skills[5];
    if (cur_energy > 10 && !divineBoon.GetRecharge()) {
        if (GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain) && !divineBoonWithHeroic) {
            GW::Effect* divineBoonEffect = GW::Effects::GetPlayerEffectBySkillId(divineBoon.skill_id);
            if (divineBoonEffect) {
                GW::Effects::DropBuff(divineBoonEffect->effect_id);
                return true;
            }
            else if (UseSkillWithTimer(5)) {
                divineBoonWithHeroic = true;
                return true;
            }
        }
        else if (!GW::Effects::GetPlayerEffectBySkillId(divineBoon.skill_id)) {
            if (UseSkillWithTimer(5)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill airOfEnchantment = skillbar->skills[6];
    if (cur_energy > 6 && !airOfEnchantment.GetRecharge()) {
        if (necromancerAgent && (!airOfEnchantmentMap.contains(necromancerAgent->agent_id) || airOfEnchantmentMap[necromancerAgent->agent_id].duration - TIMER_DIFF(airOfEnchantmentMap[necromancerAgent->agent_id].startTime) < 2000)) {
            if (UseSkillWithTimer(6, necromancerAgent->agent_id)) {
                return true;
            }
        }
        else if (lowest_health_other_ally && !airOfEnchantmentMap.contains(lowest_health_other_ally->agent_id) && (!necromancerAgent || airOfEnchantmentMap[necromancerAgent->agent_id].duration - TIMER_DIFF(airOfEnchantmentMap[necromancerAgent->agent_id].startTime) > 8000)) {
            if (UseSkillWithTimer(6, lowest_health_other_ally->agent_id)) {
                return true;
            }
        }
    }

    if (lowest_health_ally && lowest_health_ally->hp < .7 && seedOfLifeTarget && !seedOfLifeMap.contains(seedOfLifeTarget)) {
        GW::SkillbarSkill seedOfLife = skillbar->skills[0];
        if (cur_energy > 6 && !seedOfLife.GetRecharge()) {
            GW::Agent* seedOfLifeTargetAgent = GW::Agents::GetAgentByID(seedOfLifeTarget);
            GW::AgentLiving* seedOfLifeLiving = seedOfLifeTargetAgent ? seedOfLifeTargetAgent->GetAsAgentLiving() : nullptr;
            if (seedOfLifeLiving && seedOfLifeLiving->GetIsAlive() && UseSkillWithTimer(0, seedOfLifeTarget)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill dismissCondition = skillbar->skills[2];
    if (cur_energy > 6 && !dismissCondition.GetRecharge()) {
        if (conditionedAlly) {
            if (UseSkillWithTimer(2, conditionedAlly->agent_id)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill spiritBond = skillbar->skills[3];
    if (cur_energy > 11 && !spiritBond.GetRecharge()) {
        if (lowest_health_ally && lowest_health_ally->hp < .55 && !spiritBondMap.contains(lowest_health_ally->agent_id)) {
            if (UseSkillWithTimer(3, lowest_health_ally->agent_id)) {
                return true;
            }
        }
    }

    if (cur_energy < 6) return false;
    
    if (!dismissCondition.GetRecharge() && lowestHealthIncludingPet && lowestHealthIncludingPet->hp < .65 && lowestHealthIncludingPet->GetIsEnchanted()) {
        if (UseSkillWithTimer(2, lowestHealthIncludingPet->agent_id)) {
            return true;
        }
    }

    GW::SkillbarSkill shieldOfAbsorption = skillbar->skills[4];
    if (!shieldOfAbsorption.GetRecharge() && shieldOfAbsorptionTarget) {
        GW::Agent* shieldAgent = GW::Agents::GetAgentByID(shieldOfAbsorptionTarget);
        GW::AgentLiving* shieldAgentLiving = shieldAgent ? shieldAgent->GetAsAgentLiving() : nullptr;
        
        if (shieldAgentLiving && shieldAgentLiving->GetIsAlive() && UseSkillWithTimer(4, shieldOfAbsorptionTarget)) {
            return true;
        }
    }

    GW::SkillbarSkill aegis = skillbar->skills[7]; 
    if (cur_energy > 10 && !aegis.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(aegis.skill_id)) {
        if (UseSkillWithTimer(7)) {
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

    GW::SkillbarSkill blessedAura = skillbar->skills[1];
    if (cur_energy > 10 && !blessedAura.GetRecharge()) {
        if (GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain) && !blessedAuraWithHeroic) {
            GW::Effect* blessedAuraEffect = GW::Effects::GetPlayerEffectBySkillId(blessedAura.skill_id);
            if (blessedAuraEffect) {
                GW::Effects::DropBuff(blessedAuraEffect->effect_id);
                return true;
            }
            else if (UseSkillWithTimer(1)) {
                blessedAuraWithHeroic = true;
                return true;
            }
        }
        else if (!GW::Effects::GetPlayerEffectBySkillId(blessedAura.skill_id)) {
            if (UseSkillWithTimer(1)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill divineBoon = skillbar->skills[5];
    if (cur_energy > 10 && !divineBoon.GetRecharge()) {
        if (GW::Effects::GetPlayerEffectBySkillId(GW::Constants::SkillID::Heroic_Refrain) && !divineBoonWithHeroic) {
            GW::Effect* divineBoonEffect = GW::Effects::GetPlayerEffectBySkillId(divineBoon.skill_id);
            if (divineBoonEffect) {
                GW::Effects::DropBuff(divineBoonEffect->effect_id);
                return true;
            }
            else if (UseSkillWithTimer(5)) {
                divineBoonWithHeroic = true;
                return true;
            }
        }
        else if (!GW::Effects::GetPlayerEffectBySkillId(divineBoon.skill_id)) {
            if (UseSkillWithTimer(5)) {
                return true;
            }
        }
    }

    GW::SkillbarSkill dismissCondition = skillbar->skills[2];
    if (cur_energy > 5 && !dismissCondition.GetRecharge()) {
        if (conditionedAlly) {
            if (UseSkillWithTimer(2, conditionedAlly->agent_id)) {
                return true;
            }
        }
    }

    return false;
}

void MonkSidekick::EffectOnTarget(const uint32_t target, const uint32_t value)
{
    GW::Agent* targetAgent = GW::Agents::GetAgentByID(target);
    GW::AgentLiving* targetLiving = targetAgent ? targetAgent->GetAsAgentLiving() : nullptr;

    if (!targetLiving) return;

    if (party_ids.contains(target) && value == 489) {
        SkillDuration skillDuration = {TIMER_INIT(), blessedAuraWithHeroic ? 9000u : 8000u};
        seedOfLifeMap.insert_or_assign(target, skillDuration);
    }
    // 1254 spirit bond
    // 1650 shield of absorption
    // 1255 air of enchantment
    else if (targetLiving->allegiance == GW::Constants::Allegiance::Ally_NonAttackable) {
        switch (value) {
            case 1254: {
                SkillDuration skillDuration = {TIMER_INIT(), 4000};
                spiritBondMap.insert_or_assign(target, skillDuration);
                break;
            }
            case 1650: {
                SkillDuration skillDuration = {TIMER_INIT(), blessedAuraWithHeroic ? 14000u : 11000u};
                shieldOfAbsorptionMap.insert_or_assign(target, skillDuration);
                break;
            }
            case 1255: {
                SkillDuration skillDuration = {TIMER_INIT(), blessedAuraWithHeroic ? 20000u : 16000u};
                airOfEnchantmentMap.insert_or_assign(target, skillDuration);
                break;
            }
        }
    }
}

void MonkSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!agentLiving) return;

    if (!((party_ids.contains(agentLiving->agent_id) || agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet) && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::monk_symbol)) return;

    monkEffectSet.insert(agent_id);
}

void MonkSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!agentLiving) return;

    if (!((party_ids.contains(agentLiving->agent_id) || agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet) && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::monk_symbol)) return;

    monkEffectSet.erase(agent_id);
}

void MonkSidekick::AddEffectPacketCallback(GW::Packet::StoC::AddEffect* packet)
{
    if (packet->agent_id == GW::Agents::GetPlayerId()) {
        switch (static_cast<GW::Constants::SkillID>(packet->skill_id)) {
            case GW::Constants::SkillID::Spirit_Bond: {
                SkillDuration skillDuration = {TIMER_INIT(), 5000};
                spiritBondMap.insert_or_assign(packet->agent_id, skillDuration);
                break;
            }
            case GW::Constants::SkillID::Shield_of_Absorption: {
                SkillDuration skillDuration = {TIMER_INIT(), 11000};
                shieldOfAbsorptionMap.insert_or_assign(packet->agent_id, skillDuration);
                break;
            }
        }
    } 
}

void MonkSidekick::GenericModifierCallback(uint32_t type, uint32_t caster_id, float value, uint32_t cause_id) {
    switch (type) {
        case GW::Packet::StoC::P156_Type::damage:
        case GW::Packet::StoC::P156_Type::critical:
        case GW::Packet::StoC::P156_Type::armorignoring:
            break;
        default:
            return;
    }

    if (value > 0) {
        return;
    }

    if (!party_ids.contains(caster_id)) return;

    const GW::AgentArray* agents_ptr = GW::Agents::GetAgentArray();
    if (!agents_ptr) {
        return;
    }
    auto& agents = *agents_ptr;
    // get cause agent
    if (cause_id >= agents.size()) {
        return;
    }
    if (!agents[cause_id]) {
        return;
    }
    const GW::AgentLiving* const cause = agents[cause_id]->GetAsAgentLiving();

    if (cause == nullptr) {
        return;
    }
    if (cause->allegiance != GW::Constants::Allegiance::Enemy) {
        return;
    }

     if (caster_id >= agents.size())
        {
        return;
    }
    if (!agents[caster_id]) {
        return;
    }
    const GW::AgentLiving* const target = agents[caster_id]->GetAsAgentLiving();
    if (target == nullptr) {
        return;
    }

    long ldmg = std::lround(-value * target->max_hp);

    if (!damageMap.contains(caster_id)) {
        std::array<DamageHolder, 5> damageHolder = {{
            {0, 0, 0},
            {0, 0, 0},
            {0, 0, 0},
            {0, 0, 0},
            {0, 0, 0},
        }};
        damageMap.insert_or_assign(caster_id, damageHolder); 
    }

    const uint32_t dmg = static_cast<uint32_t>(ldmg);

    clock_t new_time = TIMER_INIT();

    uint32_t timeStamp = new_time / 1000;

    size_t index = (timeStamp) % 5;

    DamageHolder currentDamageHolder = damageMap.at(caster_id)[index];

    if (currentDamageHolder.timeStamp != timeStamp) {
        DamageHolder newDamageHolder = {timeStamp, dmg, 1};
        damageMap.at(caster_id)[index] = newDamageHolder;
    }
    else {
        damageMap.at(caster_id)[index] = {currentDamageHolder.timeStamp, currentDamageHolder.damage + dmg, currentDamageHolder.packets + 1} ;
    }
}

bool MonkSidekick::SetUpCombatSkills(uint32_t called_target_id) {
    UNREFERENCED_PARAMETER(called_target_id);
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

    GW::SkillbarSkill airOfEnchantment = skillbar->skills[6];
    if (cur_energy > 5 && !airOfEnchantment.GetRecharge()) {
        if (necromancerAgent && (!airOfEnchantmentMap.contains(necromancerAgent->agent_id) || airOfEnchantmentMap[necromancerAgent->agent_id].duration - TIMER_DIFF(airOfEnchantmentMap[necromancerAgent->agent_id].startTime) < 2000)) {
            if (UseSkillWithTimer(6, necromancerAgent->agent_id)) {
                return true;
            }
        }
    }

    return false;
}
