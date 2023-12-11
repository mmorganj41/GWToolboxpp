#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class ParagonSidekick : public SidekickWindow {
    ParagonSidekick() = default;
    ~ParagonSidekick() = default;

public:
    static ParagonSidekick& Instance()
    {
        static ParagonSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Paragon"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat

    void EffectOnTarget(const uint32_t target, const uint32_t value) override;
    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt) override;
    void StartCombat() override;
    void StopCombat() override;
    void CustomLoop(GW::AgentLiving* sidekick) override;
    void MessageCallBack(GW::Packet::StoC::MessageCore* packet) override;

    void Setup() override;
    void ResetTargetValues() override;
    void HardReset() override;

private:
    GW::AgentLiving* moving_ally = nullptr;
    GW::AgentLiving* heroicRefrainAlly = nullptr;
    GW::AgentLiving* wild_blow_target = nullptr;
    std::unordered_map<uint32_t, SkillDuration> stanceMap = {};
    std::set<GW::AgentID> heroicRefrainSet = {};
    bool heroicRefrainReady = false;
    clock_t saveYourselvesTimer = 0;
};
