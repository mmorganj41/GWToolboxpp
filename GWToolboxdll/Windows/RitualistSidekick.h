#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class RitualistSidekick : public SidekickWindow {
    RitualistSidekick() = default;
    ~RitualistSidekick() = default;

public:
    static RitualistSidekick& Instance()
    {
        static RitualistSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Ritualist Sidekick"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills() override;   // For setting up skills for combat

    bool InCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    bool OutOfCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override;
    
    void ResetTargetValues() override;
    virtual void AddEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    virtual void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value) override;

private:
    bool hasBoonOfCreation = false;
    bool hasPain = false;
    bool hasBloodsong = false;

    std::set<uint32_t> painfulBondSet = {};
};
