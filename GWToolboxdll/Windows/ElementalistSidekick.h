#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class ElementalistSidekick : public SidekickWindow {
    ElementalistSidekick() = default;
    ~ElementalistSidekick() = default;

public:
    static ElementalistSidekick& Instance()
    {
        static ElementalistSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Elementalist"; }

    void HardReset() override;
    void ResetTargetValues() override;
    void StopCombat() override;

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    bool UseCombatSkill() override;      // For using skills in combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat
    void AddEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value) override;
private:
    std::set<uint32_t> burningEffectSet = {};
    GW::AgentLiving* burningTarget = nullptr;

};
