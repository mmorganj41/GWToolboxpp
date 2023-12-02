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

    const char* Name() const override { return "Ritualist"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    
    void HardReset() override;
    void ResetTargetValues() override;

private:
    GW::AgentLiving* deadAlly = nullptr;
    bool hasShelter = false;
    bool hasUnion = false;
    bool hasDisplacement = false;
    bool newShelter = false;
    bool newSpirit = false;
    bool spiritInEarshot = true;
    bool lowHealthSpirit = false;

    clock_t armorOfUnfeelingTimer = 0;
};
