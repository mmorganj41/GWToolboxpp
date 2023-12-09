#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class NecromancerSidekick : public SidekickWindow {
    NecromancerSidekick() = default;
    ~NecromancerSidekick() = default;

public:
    static NecromancerSidekick& Instance()
    {
        static NecromancerSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Necromancer"; }

    std::set<uint32_t> necromancerEffectSet = {};

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override;
    bool UseCombatSkill() override;

    void StopCombat() override;
    void StartCombat() override;

    void EffectOnTarget(const uint32_t target, const uint32_t value) override;
    void HardReset() override;
    void ResetTargetValues() override;
    void CustomLoop(GW::AgentLiving* sidekick) override;
    void FinishedCheckingAgentsCallback() override;
    void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt) override;
    void SkillFinishCallback(const uint32_t caster_id);
    void AddEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    void MessageCallBack(GW::Packet::StoC::MessageCore* packet) override;
    bool SetUpCombatSkills(uint32_t called_target_id) override;  

private:

    std::optional<GW::GamePos> bloodBondCenter = std::nullopt;
    std::unordered_set<GW::AgentID> bloodIsPowerSet = {};
    std::unordered_map<GW::AgentID, SkillDuration> bloodBondMap = {};
    std::unordered_map<GW::AgentID, GW::AgentID> cureConditionMap = {};
    std::unordered_map<GW::AgentID, GW::AgentID> cureHexMap = {};
    std::unordered_map<GW::AgentID, GW::AgentID> removeEnchantmentMap = {};
    std::unordered_map<GW::AgentID, uint32_t> conditionEffectMap = {};
    GW::AgentLiving* hexedAlly = nullptr;
    GW::AgentLiving* enchantedEnemy = nullptr;
    GW::AgentLiving* conditionedAlly = nullptr;
    GW::AgentLiving* lowEnergyAlly = nullptr;
    uint32_t conditionScore = 0;
};
