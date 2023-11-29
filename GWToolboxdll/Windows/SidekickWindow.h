#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include <Timer.h>

class SidekickWindow : public ToolboxWindow {
public:
    const char* Name() const override { return "Sidekick"; }
    const char* Icon() const override { return ICON_FA_USERS; }

    struct SkillDuration {
        clock_t startTime;
        uint32_t duration;
    };

    struct Timers {
        clock_t activityTimer;
        clock_t changeStateTimer;
        clock_t followTimer;
        clock_t interactTimer;
        clock_t attackStartTimer;
        clock_t skillTimer;
        clock_t kiteTimer;
        clock_t scatterTimer;
        clock_t obstructedTimer;
        clock_t skillTimers[8];
        clock_t lastInteract;
        clock_t stuckTimer;
    };

    struct Proximity {
        GW::GamePos position;
        uint32_t adjacent;
        uint32_t nearby;
        uint32_t area;
    };

    struct SkillActivationPacket {
        GW::Constants::SkillID skill;
        GW::AgentID target;
    };

    enum State
    {
        Following,
        Picking_up,
        Fighting,
        Kiting,
        Scattering,
        Obstructed,
        Talking,
    };

    void Settings(bool finish_attacks = false, bool kite = true, bool center = false) {
        should_kite = kite;
        finishes_attacks = finish_attacks;
        should_stay_near_center = center;
    }
    bool should_kite = true;
    bool finishes_attacks = false;
    bool should_stay_near_center = false;

    uint32_t ping = 0;

    Timers timers = {0, 0, 0, 0, 0, 0, 0, 0, 0, {0,0,0,0,0,0,0,0}, 0, 0};

    State state = Following;
    bool using_skill = false;
    bool starting_combat = false;
    std::optional<GW::Constants::SkillID> checking_agents = std::nullopt;

    float area_of_effect = 0;
    std::optional<GW::GamePos> group_center = std::nullopt;
    std::optional<GW::GamePos> kiting_location = std::nullopt;
    std::optional<GW::GamePos> epicenter = std::nullopt;

    std::unordered_map<GW::AgentID, Proximity> enemyProximityMap = {};

    std::unordered_set<uint32_t> party_ids = {};

    uint32_t party_leader_id = 0;

    virtual void Attack();
    virtual void StartCombat();
    virtual void StopCombat();
    virtual void AttackFinished(uint32_t caster_id);
    virtual bool UseCombatSkill();      // For using skills in combat
    virtual bool UseOutOfCombatSkill(); // For using skills out of combat
    virtual bool SetUpCombatSkills(uint32_t called_target_id);   // For setting up skills for combat
    bool CanUseSkill(GW::SkillbarSkill skillbar_skill, GW::Skill* skill_info, float cur_energy);

    virtual bool AgentChecker(GW::AgentLiving* agentLiving, GW::AgentLiving* playerLiving); // For setting variables with respect to the agents in compass range

    bool SetEnabled(bool b);
    bool GetEnabled();
    bool UseSkillWithTimer(uint32_t slot, uint32_t target = 0U, int32_t time = 500);

    void ToggleEnable() { SetEnabled(!enabled); }

    void Initialize() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void Update(float delta) override;

    bool isCasting(GW::AgentLiving* agentLiving);
    bool isUncentered(GW::AgentLiving* agentLiving);

    virtual void HardReset();
    virtual void ResetTargetValues();
    virtual void Setup();
    virtual void CustomLoop(GW::AgentLiving* sidekick);
    virtual void FinishedCheckingAgentsCallback();
    virtual void CantAct();
    virtual void WhenKiting();
    virtual void TargetSwitchEffect();
    virtual void AddEffectCallback(const uint32_t agent_id, const uint32_t value);
    virtual void RemoveEffectCallback(const uint32_t agent_id, const uint32_t value);
    virtual void SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt);
    virtual void SkillFinishCallback(const uint32_t caster_id);
    virtual void EffectOnTarget(const uint32_t target, const uint32_t value);
    virtual void GenericModifierCallback(uint32_t type, uint32_t caster_id, float value);
    
    void CheckForProximity(GW::AgentLiving* agentLiving);

    GW::AgentLiving* called_enemy = nullptr;
    GW::AgentLiving* closest_enemy = nullptr;
    GW::AgentLiving* lowest_health_enemy = nullptr;
    GW::AgentLiving* prioritized_target = nullptr;

    GW::AgentLiving* lowest_health_ally = nullptr;
    GW::AgentLiving* lowest_health_other_ally = nullptr;

    enum SkillType {
        SPELL = 0,
        ATTACK,
        CHANT,
        OTHER,
    };

     std::unordered_map<GW::Constants::SkillType, SkillType> skill_type_map = {
        {GW::Constants::SkillType::Attack, SidekickWindow::SkillType::ATTACK},     {GW::Constants::SkillType::Bounty, SidekickWindow::SkillType::OTHER},        {GW::Constants::SkillType::Chant, SidekickWindow::SkillType::CHANT},
        {GW::Constants::SkillType::Condition, SidekickWindow::SkillType::OTHER},   {GW::Constants::SkillType::Disguise, SidekickWindow::SkillType::OTHER},      {GW::Constants::SkillType::EchoRefrain, SidekickWindow::SkillType::OTHER},
        {GW::Constants::SkillType::Enchantment, SidekickWindow::SkillType::SPELL}, {GW::Constants::SkillType::Environmental, SidekickWindow::SkillType::OTHER}, {GW::Constants::SkillType::EnvironmentalTrap, SidekickWindow::SkillType::OTHER},
        {GW::Constants::SkillType::Form, SidekickWindow::SkillType::OTHER},        {GW::Constants::SkillType::Glyph, SidekickWindow::SkillType::OTHER},         {GW::Constants::SkillType::Hex, SidekickWindow::SkillType::SPELL},
        {GW::Constants::SkillType::ItemSpell, SidekickWindow::SkillType::SPELL},   {GW::Constants::SkillType::Passive, SidekickWindow::SkillType::OTHER},       {GW::Constants::SkillType::PetAttack, SidekickWindow::SkillType::ATTACK},
        {GW::Constants::SkillType::Preparation, SidekickWindow::SkillType::OTHER}, {GW::Constants::SkillType::Ritual, SidekickWindow::SkillType::SPELL},        {GW::Constants::SkillType::Scroll, SidekickWindow::SkillType::OTHER},
        {GW::Constants::SkillType::Spell, SidekickWindow::SkillType::SPELL},       {GW::Constants::SkillType::Shout, SidekickWindow::SkillType::OTHER},         {GW::Constants::SkillType::Signet, SidekickWindow::SkillType::OTHER},
        {GW::Constants::SkillType::Skill, SidekickWindow::SkillType::OTHER},       {GW::Constants::SkillType::Skill2, SidekickWindow::SkillType::OTHER},        {GW::Constants::SkillType::Spell, SidekickWindow::SkillType::SPELL},
        {GW::Constants::SkillType::Stance, SidekickWindow::SkillType::OTHER},      {GW::Constants::SkillType::Title, SidekickWindow::SkillType::OTHER},         {GW::Constants::SkillType::Trap, SidekickWindow::SkillType::OTHER},
        {GW::Constants::SkillType::Ward, SidekickWindow::SkillType::SPELL},        {GW::Constants::SkillType::WeaponSpell, SidekickWindow::SkillType::SPELL},   {GW::Constants::SkillType::WeaponSpell, SidekickWindow::SkillType::SPELL}};
    ;

    GW::Effect* GetAgentEffectBySkillId(GW::AgentID agent_id, GW::Constants::SkillID skill_id);

private:
    bool enabled = false;
    bool no_combat = false;
    bool shouldInputScatterMove = false;
    uint32_t item_to_pick_up = 0;

    GW::HookEntry GenericValueSelf_Entry;
    GW::HookEntry GenericValueTarget_Entry;
    GW::HookEntry GenericModifier_Entry;
    GW::HookEntry PartyInvite;
    GW::HookEntry ObstructedMessage;
    GW::HookEntry Ping_Entry;
    GW::HookEntry Dialog_Entry;

    GW::AgentLiving* closest_npc = nullptr;

    std::unordered_map<GW::AgentID, SkillActivationPacket> scatterCastMap = {};
    
    void SkillCastScatter(uint32_t caster, uint32_t value, std::optional<uint32_t> target = std::nullopt);
    void SkillCastFinishScatter(uint32_t caster);
    void GenericValueCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value, const std::optional<uint32_t> target_id = std::nullopt);
    void OnServerPing(uint32_t packetPing);

    float CalculateAngleToMoveAway(GW::GamePos epicenter, GW::GamePos player_position, GW::GamePos group_position);
    GW::GamePos CalculateInitialPosition(GW::GamePos player_position, GW::GamePos group_position, size_t idx, float distance = GW::Constants::Range::Area);

    bool ShouldItemBePickedUp(GW::AgentItem* item);

    void ResetTargets();
    void CheckStuck();
    void OnTargetSwitch();
};
