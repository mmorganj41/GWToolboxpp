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
    damageMap.clear();
    deadAlly = nullptr;
    vigorousSpiritAlly = nullptr;
    lowestHealthNonParty = nullptr;
    vigorousSpiritMap.clear();
    monkEffectSet.clear();
    hexedAlly = nullptr;
    cureHexMap.clear();
    lowestHealthIncludingPet = nullptr;
}

void MonkSidekick::ResetTargetValues() {
    seedOfLifeTarget = 0;
    deadAlly = nullptr;
    hexedAlly = nullptr;
    vigorousSpiritAlly = nullptr;
    lowestHealthNonParty = nullptr;
    lowestHealthIncludingPet = nullptr;
    damagedAllies = 0;
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
        case GW::Constants::SkillID::Remove_Hex: {
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
    if (!agentLiving->GetIsAlive()) {
        if (!deadAlly && party_ids.contains(agentLiving->agent_id)) {
            deadAlly = agentLiving;
        }
    } 
    else {
        if ((party_ids.contains(agentLiving->agent_id) || (agentLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet && !agentLiving->GetIsSpawned()))) {
            if (agentLiving->GetIsHexed()) {
                if (hexTimer == 0) hexTimer = TIMER_INIT();
                bool already_casting = false;
                if (TIMER_DIFF(hexTimer) > 80 + 2 * static_cast<int32_t>(ping)) {
                    for (auto& it : cureHexMap) {
                        if (it.second == agentLiving->agent_id) already_casting = true;
                    }
                }
                if (!already_casting && !hexedAlly) hexedAlly = agentLiving;
            }
            if (!vigorousSpiritMap.contains(agentLiving->agent_id)) {
                if ((!vigorousSpiritAlly || vigorousSpiritAlly->hp > agentLiving->hp)) vigorousSpiritAlly = agentLiving;
            }
            if ((!lowestHealthIncludingPet || lowestHealthIncludingPet->hp > agentLiving->hp)) lowestHealthIncludingPet = agentLiving;
            if (party_ids.contains(agentLiving->agent_id) && agentLiving->hp < .8) {
                damagedAllies += 1;
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
    if (state == Following || state == Picking_up) return;

    if (!vigorousSpiritMap.empty()) {
        for (auto& it : vigorousSpiritMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                Log::Info("ally dead");
                vigorousSpiritMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 250) {
                vigorousSpiritMap.erase(it.first);
                Log::Info("Vigorous ran out");
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsEnchanted() && monkEffectSet.contains(it.first))) {
                vigorousSpiritMap.erase(it.first);
                Log::Info("Vigorous removed");
                continue;
            }
       }
    }

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

       for (auto& it : damageMap) {
           if (it.second[index].timeStamp != timeStamp) {
                it.second[index] = {timeStamp, 0, 0};
           }

           if (sidekick->agent_id == it.first) continue;

           uint32_t short_packets = 0;
           uint32_t medium_packets = 0;
           uint32_t long_packets = 0;

           for (size_t i = 0; i < 5; i++) {
                const uint32_t packets = it.second[(index + i) % 5].packets;
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
       }

       if (max_short > 2) {
           seedOfLifeTarget = short_agent; 
       }
       else if (max_medium > 5) {
           seedOfLifeTarget = medium_agent;
       }
       else if (max_long > 16) {
           seedOfLifeTarget = long_agent;
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

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    GW::SkillbarSkill vanguardBannerOfWisdom = skillbar->skills[5];
    bool canUseWisdom = !vanguardBannerOfWisdom.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(vanguardBannerOfWisdom.skill_id);
    if (cur_energy > 10 && canUseWisdom) {
        if ((wardEffect && GW::GetDistance(wardEffect->position, sidekickLiving->pos) <= GW::Constants::Range::Adjacent) ||
            (target && !wardEffect && GW::GetDistance(target->pos, sidekickLiving->pos) <= GW::Constants::Range::Earshot + GW::Constants::Range::Area / 4)) {
            if (UseSkillWithTimer(5)) {
                castingWard = false;
                return true;
            }
        }
        else {
            castingWard = true;
        }
    }
    else {
        castingWard = false;
    }

    if (castingWard) return false;

    if (lowest_health_ally && lowest_health_ally->hp < .6 && seedOfLifeTarget && !seedOfLifeMap.contains(seedOfLifeTarget)) {
        GW::SkillbarSkill seedOfLife = skillbar->skills[0];
        if (cur_energy > 5 && !seedOfLife.GetRecharge()) {
            GW::Agent* seedOfLifeTargetAgent = GW::Agents::GetAgentByID(seedOfLifeTarget);
            GW::AgentLiving* seedOfLifeLiving = seedOfLifeTargetAgent ? seedOfLifeTargetAgent->GetAsAgentLiving() : nullptr;
            if (seedOfLifeLiving && UseSkillWithTimer(0, seedOfLifeTarget)) {
                return true;
            }
        }
    }

    if (sidekickLiving->hp < .6) {
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

    GW::SkillbarSkill burstOfHealing = skillbar->skills[6];
    GW::SkillbarSkill vigorousSpirit = skillbar->skills[1];
    GW::Skill* vigorousSpiritInfo = GW::SkillbarMgr::GetSkillConstantData(vigorousSpirit.skill_id);
    
    if (lowestHealthIncludingPet && (lowestHealthIncludingPet->hp < .6 || damagedAllies > 3)) {
        if (UseSkillWithTimer(6, lowestHealthIncludingPet->agent_id)) {
            return true;
        }
    }
   else if (vigorousSpiritAlly && vigorousSpiritAlly->hp < .95 && vigorousSpiritInfo && CanUseSkill(vigorousSpirit, vigorousSpiritInfo, cur_energy)) {
        if (UseSkillWithTimer(1, vigorousSpiritAlly->agent_id)) {
            return true;
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

    if (!(lowest_health_ally && lowest_health_ally->hp < .6) && deadAlly) {
        GW::SkillbarSkill resurrect = skillbar->skills[7];
        GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(resurrect.skill_id);
        if (skillInfo && CanUseSkill(resurrect, skillInfo, cur_energy)) {
            if (UseSkillWithTimer(7, deadAlly->agent_id)) {
                return true;
            }
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
   }

    GW::SkillbarSkill burstOfHealing = skillbar->skills[6];

   if (lowestHealthIncludingPet && (lowestHealthIncludingPet->hp < .6 || damagedAllies > 3)) {
        if (UseSkillWithTimer(6, lowestHealthIncludingPet->agent_id)) {
            return true;
        }
   }

    if (!(lowest_health_ally && lowest_health_ally->hp < .6) && deadAlly) {
        GW::SkillbarSkill resurrect = skillbar->skills[7];
        GW::Skill* skillInfo = GW::SkillbarMgr::GetSkillConstantData(resurrect.skill_id);
        if (skillInfo && CanUseSkill(resurrect, skillInfo, cur_energy)) {
            if (UseSkillWithTimer(7, deadAlly->agent_id)) {
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
    Log::Info("Value %d", value);

    if ((party_ids.contains(target) || (targetLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet && !targetLiving->GetIsSpawned())) && value == 465) {
        SkillDuration skillDuration = {TIMER_INIT(), 30000};
        Log::Info("vigorous applied");
        vigorousSpiritMap.insert_or_assign(target, skillDuration);
    }
    else if (party_ids.contains(target) && value == 489) {
        SkillDuration skillDuration = {TIMER_INIT(), 5000};
        Log::Info("seed of life applied");
        seedOfLifeMap.insert_or_assign(target, skillDuration);
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

void MonkSidekick::AddEffectPacketCallback(GW::Packet::StoC::AddEffect* packet) {
    if (packet->agent_id == GW::Agents::GetPlayerId() && packet->skill_id == static_cast<uint32_t>(GW::Constants::SkillID::Vigorous_Spirit)) {
        SkillDuration skillDuration = {TIMER_INIT(), 30000};
        Log::Info("vigorous applied to self");
        vigorousSpiritMap.insert_or_assign(packet->agent_id, skillDuration);
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
