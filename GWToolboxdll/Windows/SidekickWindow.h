#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include <Timer.h>

class SidekickWindow : public ToolboxWindow {
public:
    const char* Name() const override { return "Sidekick"; }
    const char* Icon() const override { return ICON_FA_USERS; }

    struct Timers {
        clock_t activityTimer;
        clock_t changeStateTimer;
        clock_t followTimer;
        clock_t interactTimer;
        clock_t attackStartTimer;
        clock_t skillTimer;
        clock_t kiteTimer;
        clock_t scatterTimer;
    };

    enum State
    {
        Following,
        Picking_up,
        Fighting,
        Kiting,
        Scattering,
    };

    Timers timers = {0, 0, 0, 0, 0, 0, 0, 0};

    State state = Following;
    bool starting_combat = false;
    bool using_skill = false;
    bool should_kite = true;
    bool finishes_attacks = false;
    float area_of_effect = 0;
    std::optional<GW::GamePos> group_center = std::nullopt;
    std::optional<GW::GamePos> kiting_location = std::nullopt;
    std::optional<GW::GamePos> epicenter = std::nullopt;

    std::unordered_set<uint32_t> party_ids = {};

    uint32_t party_leader_id = 0;

    virtual bool UseCombatSkill();      // For using skills in combat
    virtual bool UseOutOfCombatSkill(); // For using skills out of combat
    virtual bool SetUpCombatSkills();   // For setting up skills for combat
    bool CanUseSkill(GW::SkillbarSkill skillbar_skill, GW::Skill* skill_info, float cur_energy);

    virtual bool InCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving); // For setting variables with respect to the agents in compass range
    virtual bool OutOfCombatAgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving);

    bool SetEnabled(bool b);
    bool GetEnabled();

    void ToggleEnable() { SetEnabled(!enabled); }

    void Initialize() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void Update(float delta) override;

    virtual void HardReset();
    virtual void ResetTargetValues();
    virtual void AddEffectCallback(const uint32_t agent_id, const uint32_t value);
    virtual void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value);
    virtual void SkillCallback(const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt);

    GW::AgentLiving* called_enemy = nullptr;
    GW::AgentLiving* closest_enemy = nullptr;
    GW::AgentLiving* lowest_health_enemy = nullptr;

    GW::AgentLiving* lowest_health_ally = nullptr;


private:
    bool enabled = false;
    uint32_t item_to_pick_up = 0;

    GW::HookEntry GenericValueSelf_Entry;
    GW::HookEntry GenericValueTarget_Entry;
    GW::HookEntry PartyInvite;

    void GenericValueCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt);

    float CalculateAngleToMoveAway(GW::GamePos epicenter, GW::GamePos player_position, GW::GamePos group_position);

    bool ShouldItemBePickedUp(GW::AgentItem* item);

    uint32_t expertise = 0;

    float ExpertiseToSkillReduction(GW::Skill* skill);
    void ResetTargets();
};
