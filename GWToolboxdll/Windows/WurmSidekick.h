#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class WurmSidekick : public SidekickWindow {
    WurmSidekick() = default;
    ~WurmSidekick() = default;

public:
    static WurmSidekick& Instance()
    {
        static WurmSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Wurm"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range
    void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt) override;
    void SkillFinishCallback(const uint32_t caster_id);
    void StartCombat() override;
    void StopCombat() override;

    void Setup() override;
    void ResetTargetValues() override;
    void HardReset() override;

private:
    GW::AgentLiving* deadAdjacentEnemy = nullptr;
    GW::AgentLiving* deadAlly = nullptr;
    clock_t consumeTimer = 0;
    uint32_t resurrectionCast = 0;
};
