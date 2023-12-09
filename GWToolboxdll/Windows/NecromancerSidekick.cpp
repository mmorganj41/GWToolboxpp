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
#include <Windows/NecromancerSidekick.h>

#include <Logger.h>
// blood is power 224
// blood bond 889

namespace {
    clock_t monkTimer = 0;
    clock_t hexTimer = 0;
    const uint32_t blind_bin = static_cast<uint32_t>(pow(2U, static_cast<uint32_t>(GW::Constants::EffectID::blind)));
    const uint32_t dazed_bin = static_cast<uint32_t>(pow(2U, static_cast<uint32_t>(GW::Constants::EffectID::dazed)));
    const uint32_t disease_bin = static_cast<uint32_t>(pow(2U, static_cast<uint32_t>(GW::Constants::EffectID::disease)));
    const uint32_t weakness_bin = static_cast<uint32_t>(pow(2U, static_cast<uint32_t>(GW::Constants::EffectID::weakness)));
    const uint32_t poison_bin = static_cast<uint32_t>(pow(2U, static_cast<uint32_t>(GW::Constants::EffectID::poison)));
} // namespace

void NecromancerSidekick::EffectOnTarget(const uint32_t target, const uint32_t value)
{
    GW::Agent* targetAgent = GW::Agents::GetAgentByID(target);
    GW::AgentLiving* targetLiving = targetAgent ? targetAgent->GetAsAgentLiving() : nullptr;

    if (!targetLiving) return;

    Log::Info("value id %d", value);

    if (targetLiving->allegiance == GW::Constants::Allegiance::Enemy && value == 889) {
        bloodBondCenter = targetLiving->pos;
        checking_agents = GW::Constants::SkillID::Blood_Bond;
    }
    else if (party_ids.contains(target) && value == 224) {
        bloodIsPowerSet.erase(target);
    }
}

void NecromancerSidekick::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id) {
    UNREFERENCED_PARAMETER(value_id);
    if (!target_id) return;

    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (caster_id == GW::Agents::GetPlayerId() || casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    GW::Constants::SkillID skillId = static_cast<GW::Constants::SkillID>(value);

    switch (skillId) {
        case GW::Constants::SkillID::Shatter_Hex:
        case GW::Constants::SkillID::Remove_Hex: {
            cureHexMap.insert_or_assign(casterLiving->agent_id, *target_id);
            break;
        }
        case GW::Constants::SkillID::Drain_Enchantment:
        case GW::Constants::SkillID::Jaundiced_Gaze: {
            removeEnchantmentMap.insert_or_assign(casterLiving->agent_id, *target_id);    
            break;
        }
        case GW::Constants::SkillID::Foul_Feast:
        case GW::Constants::SkillID::Dismiss_Condition: {
            cureConditionMap.insert_or_assign(casterLiving->agent_id, *target_id);
            break;

        }
    }
}

void NecromancerSidekick::SkillFinishCallback(const uint32_t caster_id)
{
    GW::Agent* caster = GW::Agents::GetAgentByID(caster_id);
    GW::AgentLiving* casterLiving = caster ? caster->GetAsAgentLiving() : nullptr;

    if (!casterLiving) return;

    if (casterLiving->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    cureHexMap.erase(casterLiving->agent_id);
    removeEnchantmentMap.erase(casterLiving->agent_id);
    cureConditionMap.erase(casterLiving->agent_id);
}


bool NecromancerSidekick::AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving)
{
    UNREFERENCED_PARAMETER(playerLiving);
    if (agentLiving->allegiance == GW::Constants::Allegiance::Enemy && agentLiving->GetIsAlive()) { 
        if (checking_agents && bloodBondCenter) {
            if (GW::GetSquareDistance(*bloodBondCenter, agentLiving->pos) <= GW::Constants::SqrRange::Adjacent) {
                SkillDuration skillDuration = {TIMER_INIT(), static_cast<uint32_t>(agentLiving->GetHasBossGlow() ? 6000 : 11000)};
                bloodBondMap.insert_or_assign(agentLiving->agent_id, skillDuration);
            }
        }
        if (!enchantedEnemy && agentLiving->GetIsEnchanted() && GW::GetDistance(agentLiving->pos, playerLiving->pos) < GW::Constants::Range::Spellcast * 11 / 10) {
            bool already_casting = false;
            for (auto& it : removeEnchantmentMap) {
                if (it.second == agentLiving->agent_id) already_casting = true;
            }
            if (!already_casting) enchantedEnemy = agentLiving;
        }
    }
    else if (party_ids.contains(agentLiving->agent_id) && agentLiving->GetIsAlive()) {
        if (!hexedAlly && agentLiving->GetIsHexed()) {
            if (hexTimer == 0) hexTimer = TIMER_INIT();
            bool already_casting = false;
            if (TIMER_DIFF(hexTimer) > 40 + static_cast<int32_t>(ping)) {
                for (auto& it : cureHexMap) {
                    if (it.second == agentLiving->agent_id) already_casting = true;
                }
            }
            if (!already_casting) hexedAlly = agentLiving;
        }
        if (agentLiving->GetIsConditioned() && agentLiving->agent_id != playerLiving->agent_id) {
            bool already_casting = false;
            for (auto& it : cureConditionMap) {
                if (it.second == agentLiving->agent_id) already_casting = true;
            }
            if (!already_casting) {
                uint32_t currentScore = 0;
                if (conditionEffectMap.contains(agentLiving->agent_id)) {
                    const uint32_t conditionValue = conditionEffectMap[agentLiving->agent_id];
                    if (conditionValue & blind_bin) {
                        currentScore += 5;
                    }
                    if (conditionValue & dazed_bin) {
                        currentScore += 5;
                    }
                    if (conditionValue & disease_bin) {
                        currentScore += 4;
                    }
                    if (conditionValue & weakness_bin) {
                        currentScore += 3;
                    }
                    if (conditionValue & poison_bin) {
                        currentScore += 2;
                    }
                }
                if (agentLiving->GetIsCrippled()) {
                    currentScore += 3;
                }
                if (agentLiving->GetIsDeepWounded()) {
                    currentScore += 3;
                }
                if (agentLiving->GetIsBleeding()) {
                    currentScore += 1;
                }
                if (currentScore > conditionScore) {
                    conditionScore = currentScore;
                    conditionedAlly = agentLiving;
                }
            }
        }
        if (!lowEnergyAlly && bloodIsPowerSet.contains(agentLiving->agent_id)) {
            lowEnergyAlly = agentLiving;
        }
    }

    return false;
}

void NecromancerSidekick::CustomLoop(GW::AgentLiving* sidekick) {
    UNREFERENCED_PARAMETER(sidekick);
    if (state == Following || state == Picking_up) return;

    if (!bloodBondMap.empty()) {
        for (auto& it : bloodBondMap) {
            GW::Agent* agent = GW::Agents::GetAgentByID(it.first);
            GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

            if (!(agentLiving && agentLiving->GetIsAlive())) {
                bloodBondMap.erase(it.first);
                continue;
            }

            clock_t elapsed_time = TIMER_DIFF(it.second.startTime);

            if (elapsed_time - it.second.duration < 500) {
                bloodBondMap.erase(it.first);
                continue;
            }
            else if (elapsed_time > 500 && !(agentLiving->GetIsHexed() && necromancerEffectSet.contains(it.first))) {
                bloodBondMap.erase(it.first);
                continue;
            }
        }
    }

    for (auto& it : party_ids) {
        if (monkAgent) break;
        GW::Agent* agent = GW::Agents::GetAgentByID(it);
        GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;
        if (!agentLiving) continue;
        if (agentLiving->primary == 3 && agentLiving->GetIsAlive()) monkAgent = agentLiving;
    }
}

bool NecromancerSidekick::SetUpCombatSkills(uint32_t called_target_id){
    UNREFERENCED_PARAMETER(called_target_id);
    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
    if (!skillbar) {
        return false;
    }

    GW::AgentLiving* sidekickLiving = GW::Agents::GetPlayerAsAgentLiving();
    if (!sidekickLiving) {
        return false;
    }

    //float cur_energy = sidekickLiving->max_energy * sidekickLiving->energy;

    if (isCasting(sidekickLiving)) {
        return false;
    }

    return false;
};

bool NecromancerSidekick::UseCombatSkill() {
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

    if (conditionedAlly) {
        GW::SkillbarSkill foulFeast = skillbar->skills[6];
        GW::Skill* foulFeastInfo = GW::SkillbarMgr::GetSkillConstantData(foulFeast.skill_id);
        if (CanUseSkill(foulFeast, foulFeastInfo, cur_energy)) {
            if (UseSkillWithTimer(6, conditionedAlly->agent_id)) {
                return true;
            }
        }
    }

    if (enchantedEnemy) {
        GW::SkillbarSkill jaundicedGaze = skillbar->skills[7];
        GW::Skill* jaundicedGazeInfo = GW::SkillbarMgr::GetSkillConstantData(jaundicedGaze.skill_id);
        if (CanUseSkill(jaundicedGaze, jaundicedGazeInfo, cur_energy)) {
            if (UseSkillWithTimer(7, enchantedEnemy->agent_id)) {
                return true;
            }
        }
    }

    if (lowest_health_enemy && lowest_health_enemy->hp < .5 && (sidekickLiving->hp < .7 || sidekickLiving->energy < .5)) {
        GW::SkillbarSkill signetOfLostSouls = skillbar->skills[5];
        if (!signetOfLostSouls.GetRecharge()) {
            if (UseSkillWithTimer(5, lowest_health_enemy->agent_id)) {
                return true;
            }
        }
    }
    GW::SkillbarSkill bloodIsPower = skillbar->skills[0];
    
    if (monkAgent && sidekickLiving->hp > .65 && TIMER_DIFF(monkTimer) > 10000) {
        if (UseSkillWithTimer(0, monkAgent->agent_id)) {
            return true;
        }
    }
    if (lowEnergyAlly && sidekickLiving->hp > .7) {
        if (UseSkillWithTimer(0,lowEnergyAlly->agent_id)) {
            return true;
        }
    }

    GW::SkillbarSkill orderOfPain = skillbar->skills[2];
    GW::Skill* orderOfPainInfo = GW::SkillbarMgr::GetSkillConstantData(orderOfPain.skill_id);
    if (sidekickLiving->hp > .55 && orderOfPainInfo && CanUseSkill(orderOfPain, orderOfPainInfo, cur_energy)) {
        GW::Effect* orderOfPainEffect = GW::Effects::GetPlayerEffectBySkillId(orderOfPain.skill_id);
        if ((!orderOfPainEffect || orderOfPainEffect->GetTimeRemaining() < 1500) && UseSkillWithTimer(2)) {
            return true;
        }
    }

    GW::SkillbarSkill darkFury = skillbar->skills[1];
    GW::Skill* darkFuryInfo = GW::SkillbarMgr::GetSkillConstantData(darkFury.skill_id);
    if (sidekickLiving->hp > .55 && orderOfPainInfo && CanUseSkill(darkFury, darkFuryInfo, cur_energy)) {
        GW::Effect* darkFuryEffect = GW::Effects::GetPlayerEffectBySkillId(darkFury.skill_id);
        if ((!darkFuryEffect || darkFuryEffect->GetTimeRemaining() < 500) && UseSkillWithTimer(1)) {
            return true;
        }
    }

    GW::AgentLiving* target = GW::Agents::GetTargetAsAgentLiving();

    if (!target) return false;

    GW::SkillbarSkill bloodBond = skillbar->skills[3];
    GW::Skill* bloodBondInfo = GW::SkillbarMgr::GetSkillConstantData(bloodBond.skill_id);
    if (!bloodBondMap.contains(target->agent_id) && target->hp > .25 && bloodBondInfo && CanUseSkill(bloodBond, bloodBondInfo, cur_energy)) {
        if (UseSkillWithTimer(3, target->agent_id)) {
            return true;
        }
    }

    return false;
}

void NecromancerSidekick::FinishedCheckingAgentsCallback() {
    bloodBondCenter = std::nullopt;
}

void NecromancerSidekick::HardReset() {
    conditionEffectMap.clear();
    bloodBondCenter = std::nullopt;
    bloodBondMap.clear();
    necromancerEffectSet.clear();
    hexedAlly = nullptr;
    monkAgent = nullptr;
    conditionedAlly = nullptr;
    enchantedEnemy = nullptr;
    cureHexMap.clear();
    bloodIsPowerSet.clear();
    removeEnchantmentMap.clear();
    conditionScore = 0;
}

void NecromancerSidekick::StopCombat() {
    bloodBondMap.clear();
    bloodIsPowerSet.clear();
    necromancerEffectSet.clear();
    cureHexMap.clear();
    removeEnchantmentMap.clear();
}

void NecromancerSidekick::ResetTargetValues()
{
    hexedAlly = nullptr;
    enchantedEnemy = nullptr;
    conditionedAlly = nullptr;
    conditionScore = 0;
    monkAgent = nullptr;
}

void NecromancerSidekick::StartCombat()
{
    hexTimer = 0;
}

void NecromancerSidekick::AddEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

    if (!agentLiving) return;

    if (party_ids.contains(agent_id))
    {
        uint32_t new_value = conditionEffectMap.contains(agent_id) ? conditionEffectMap[agent_id] : 0x0;
        switch (static_cast<GW::Constants::EffectID>(value)) {
            case GW::Constants::EffectID::blind: {
                if (new_value ^ blind_bin) new_value += blind_bin;
                break;
            }
            case GW::Constants::EffectID::dazed: {
                if (new_value ^ dazed_bin) new_value += dazed_bin;
                break;
            }
            case GW::Constants::EffectID::disease: {
                if (new_value ^ disease_bin) new_value += disease_bin;
                break;
            }
            case GW::Constants::EffectID::weakness: {
                if (new_value ^ weakness_bin) new_value += weakness_bin;
                break;
            }
            case GW::Constants::EffectID::poison: {
                if (new_value ^ poison_bin) new_value += poison_bin;
                break;
            }
        }
        conditionEffectMap.insert_or_assign(agent_id, new_value);
    }

    if ((agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::necro_symbol)) return;

    necromancerEffectSet.insert(agent_id);
}

void NecromancerSidekick::RemoveEffectCallback(const uint32_t agent_id, const uint32_t value)
{
    GW::Agent* agent = GW::Agents::GetAgentByID(agent_id);
    GW::AgentLiving* agentLiving = agent ? agent->GetAsAgentLiving() : nullptr;

        if (!agentLiving) return;

    if (party_ids.contains(agent_id)) {
        uint32_t new_value = conditionEffectMap.contains(agent_id) ? conditionEffectMap[agent_id] : 0x0;
        switch (static_cast<GW::Constants::EffectID>(value)) {
            case GW::Constants::EffectID::blind: {
                if (new_value & blind_bin) new_value -= blind_bin;
                break;
            }
            case GW::Constants::EffectID::dazed: {
                if (new_value & dazed_bin) new_value -= dazed_bin;
                break;
            }
            case GW::Constants::EffectID::disease: {
                if (new_value & disease_bin) new_value -= disease_bin;
                break;
            }
            case GW::Constants::EffectID::weakness: {
                if (new_value & weakness_bin) new_value -= weakness_bin;
                break;
            }
            case GW::Constants::EffectID::poison: {
                if (new_value & poison_bin) new_value -= poison_bin;
                break;
            }
        }
        conditionEffectMap.insert_or_assign(agent_id, new_value);
    }

    if (!(agentLiving && agentLiving->allegiance == GW::Constants::Allegiance::Enemy && static_cast<GW::Constants::EffectID>(value) == GW::Constants::EffectID::necro_symbol)) return;

    necromancerEffectSet.erase(agent_id);
}

void NecromancerSidekick::MessageCallBack(GW::Packet::StoC::MessageCore* packet) {
    if (packet->message[0] == 0x7BF) {
        GW::Player* player = GW::PlayerMgr::GetPlayerByID(packet->message[2]-256);
        Log::Info("my player id: %d sender id: %d", GW::PlayerMgr::GetPlayerNumber(), packet->message[2] - 256);
        if (!player) return;
        bloodIsPowerSet.insert(player->agent_id);
    }
}
