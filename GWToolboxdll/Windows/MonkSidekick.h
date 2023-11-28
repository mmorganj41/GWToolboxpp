#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class MonkSidekick : public SidekickWindow {
    MonkSidekick() = default;
    ~MonkSidekick() = default;

public:
    static MonkSidekick& Instance()
    {
        static MonkSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Monk"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override;
    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override;

    void HardReset() override;
    void ResetTargetValues() override;
    void StartCombat() override;
    void StopCombat() override;
    void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt) override ;
    void SkillFinishCallback(const uint32_t caster_id);

private:
    std::unordered_map<GW::AgentID, GW::AgentID> cureHexMap = {};
    GW::AgentLiving* hexedAlly = nullptr;
};
