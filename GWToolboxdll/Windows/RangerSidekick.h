#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class RangerSidekick : public SidekickWindow {
    RangerSidekick() = default;
    ~RangerSidekick() = default;

public:
    static RangerSidekick& Instance()
    {
        static RangerSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Ranger"; }

    void Attack() override;
    void AttackFinished(uint32_t caster_id) override;
    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat
    void StartCombat() override;
    void StopCombat() override;
    void TargetSwitchEffect() override;

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    
    void Setup() override;
    void ResetTargetValues() override;

private:
    GW::AgentLiving* pet = nullptr;
    uint32_t enemiesInSpiritRange = 0;
};
