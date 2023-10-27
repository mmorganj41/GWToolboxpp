#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include <Timer.h>

class IWindow : public ToolboxWindow {
    IWindow() = default;
    ~IWindow() = default;

public:
     enum InterruptType
     {
         SPELL = 0,
         ATTACK,
         CHANT,
         OTHER,
     };

     struct SkillInfo {
         GW::Constants::SkillID skill_id;
         GW::Constants::SkillType skill_type;
         uint32_t caster_id;
         clock_t cast_start;
         uint32_t cast_time;
         uint8_t priority;
     };

     static bool SkillInfoSorter(SkillInfo const lhs, SkillInfo const rhs) { return lhs.priority <= rhs.priority; }

    static IWindow& Instance()
    {
        static IWindow instance;
        return instance;
    }

    const char* Name() const override { return "I"; }
    const char* Icon() const override { return ICON_FA_SKULL; }

    bool SetEnabled(bool b);
    bool GetEnabled();

    void ToggleEnable() { SetEnabled(!mainSetting.enabled); }

    void Initialize() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void Update(float delta) override;

private:
     static const int out_of_combat_sleep_time = 300;

     std::unordered_map<GW::Constants::SkillType, InterruptType> skill_type_map = {{GW::Constants::SkillType::Attack, IWindow::InterruptType::ATTACK}, {GW::Constants::SkillType::Bounty, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::Chant, IWindow::InterruptType::CHANT}, {GW::Constants::SkillType::Condition, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Disguise, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::EchoRefrain, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Enchantment, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::Environmental, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::EnvironmentalTrap, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Form, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Glyph, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::Hex, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::ItemSpell, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::Passive, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::PetAttack, IWindow::InterruptType::ATTACK}, {GW::Constants::SkillType::Preparation, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Ritual, IWindow::InterruptType::SPELL},
         {GW::Constants::SkillType::Scroll, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Spell, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::Shout, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::Signet, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Skill, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Skill2, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::Spell, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::Stance, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Title, IWindow::InterruptType::OTHER},
         {GW::Constants::SkillType::Trap, IWindow::InterruptType::OTHER}, {GW::Constants::SkillType::Ward, IWindow::InterruptType::SPELL}, {GW::Constants::SkillType::WeaponSpell, IWindow::InterruptType::SPELL},
         {GW::Constants::SkillType::WeaponSpell, IWindow::InterruptType::SPELL}};
     ;

    struct SkillSetting {
        bool enabled;
        int range;
        bool spells;
        bool attacks;
        bool chants;
        bool other;
    };

     struct SkillPriority {
         uint8_t priority;
         std::optional<SkillInfo> prioritized_skill;
     };

     struct CurrentInterruptSkill {
         std::optional<int> interruptIndex;
         uint32_t target_id;
         int cast_time;
         bool adrenaline;
     };

    SkillSetting mainSetting{false, 0, true, true, true, true};
     SkillSetting interruptSkills[8] = {{false, 0, true, true, true, true}};

     CurrentInterruptSkill current_interrupt_skill = {std::nullopt, 0, 0, false};

     bool lock_on = true;
     uint32_t expertise = 0;

     int32_t sleep_time = 300;
     int32_t min_reaction_time = 50;
     int min_cast_time = 1500;

     std::optional<GW::Skill*> player_skill_info[8] = {{std::nullopt}};
     std::optional<GW::WeaponSet> weapon_sets[4] = {{std::nullopt}};

     std::unordered_map<GW::AgentID, SkillInfo> active_skills;
     std::unordered_map<GW::AgentID, float> modified_cast_times;
     std::unordered_set<GW::AgentID> dazed_agents;

     GW::HookEntry GenericModifier_Entry;
     GW::HookEntry GenericValueSelf_Entry;
     GW::HookEntry GenericValueTarget_Entry;
     GW::HookEntry Ping_Entry;

     void UpdatePlayerSkillInfo();
     void ClearCurrentInterruptSkill();
     void SetCurrentInterruptSkill(int idx, int rupt_time, uint32_t caster_id, uint32_t adrenaline);
     void BeginCastingCurrentInterruptSkill(int idx, uint32_t caster_id);
     void ResetReaction();

     void SkillCallback(uint32_t value_id, uint32_t caster_id, uint32_t value);
     void CasttimeCallback(uint32_t type, uint32_t caster_id, float value);

     static void OnServerPing(GW::HookStatus*, void* packet);

     bool CheckForDaze(GW::Constants::SkillType skill_type, const uint32_t caster_id);
     bool FindAndEquipWeaponSet(uint32_t weapon_req);

     int32_t ComputeRuptTime(int i, float attack_speed_modifier, float base_attack_speed);
     int32_t ComputeSkillTime(SkillInfo skill_info);
     int GetValidInterrupt(SkillSetting skill_setting, GW::Constants::SkillType skill_type);

     uint16_t ItemTypeToWeaponType(uint8_t item_type);
     uint32_t WeaponTypeToWeaponReq(uint16_t weapon_type);

     float SkillRangeToDistance(int i);
     float ExpertiseToSkillReduction(GW::Skill* skill);

     uint8_t PrioritizeClass(uint8_t profession);
     uint8_t PrioritizeCost(uint8_t energy_cost, uint32_t adrenaline);
     uint8_t PrioritizeRecharge(uint32_t recharge);
     uint8_t PrioritizeActivation(float activation, uint32_t weapon_req);
};
