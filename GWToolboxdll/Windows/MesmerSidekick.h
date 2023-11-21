#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class MesmerSidekick : public SidekickWindow {
    MesmerSidekick() = default;
    ~MesmerSidekick() = default;

public:
    static MesmerSidekick& Instance()
    {
        static MesmerSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Mesmer"; }

    struct SkillInfo {
        GW::Constants::SkillID skill_id;
        SkillType skill_type;
        uint32_t caster_id;
        clock_t cast_start;
        uint32_t cast_time;
        uint8_t priority;
    };

    enum InterruptSkill { Overload = 1, Tease = 5, CryOfFrustration = 6, CryOfPain = 3, PowerDrain = 4 };

    bool UseCombatSkill() override;      // For using skills in combat
    bool UseOutOfCombatSkill() override; // For using skills out of combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat

    bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving) override; // For setting variables with respect to the agents in compass range

    void HardReset() override;
    void ResetTargetValues() override;
    void CustomLoop(GW::AgentLiving* sidekick) override;
    void CantAct() override;
    void WhenKiting() override;
    void AddEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value) override;
    void StopCombat() override;

    void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt) override;
    void SkillFinishCallback(const uint32_t caster_id) override;
    void GenericModifierCallback(uint32_t type, uint32_t caster_id, float value) override;


private:
    struct CurrentInterruptSkill {
        std::optional<InterruptSkill> interruptSkill;
        uint32_t target_id;
        clock_t cast_start;
        uint32_t cast_time;
        uint32_t priority;
        int reaction_time;
        clock_t reaction_timer;
    };
    std::optional<int32_t> minimum_next_sequence = std::nullopt;

    CurrentInterruptSkill currentInterruptSkill = {std::nullopt, 0, 0, 0, 0, 0, 0};

    GW::AgentLiving* shatterDelusionsTarget = nullptr;
    GW::AgentLiving* enchantedEnemy = nullptr;
    std::set<uint32_t> mesmerEffectSet = {};

    std::unordered_map<uint32_t, uint8_t> hexedAllies = {};
    std::unordered_map<GW::AgentID, SkillInfo> active_skills;
    std::unordered_map<GW::AgentID, float> modified_cast_times;
    int fast_casting_activation_array[22] = {100, 95, 91, 87, 83, 79, 76, 72, 69, 66, 63, 60, 57, 55, 52, 50, 48, 46, 44, 42, 41, 40};

    uint32_t fastCasting = 0;

    uint8_t PrioritizeClass(uint8_t profession);
    uint8_t PrioritizeCost(uint8_t energy_cost, uint32_t adrenaline);
    uint8_t PrioritizeRecharge(uint32_t recharge);
    uint8_t PrioritizeActivation(float activation, uint32_t weapon_req);
    void GetFastCasting();
    void ResetReaction();
    void ClearCurrentInterruptSkill();
    void SetCurrentInterruptSkill(InterruptSkill interruptSkill, SkillInfo skillInfo, std::optional<int32_t> next_sequence_time);
};
