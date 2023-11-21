#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include <Timer.h>
#include <unordered_map>
#include <GWCA/Utilities/Hook.h>
#include <GWCA/GameEntities/Skill.h>

using namespace GW::Constants;

class AuspiciousBeginnings : public ToolboxWindow {
    AuspiciousBeginnings() = default;
    ~AuspiciousBeginnings() = default;

public:
    static AuspiciousBeginnings& Instance()
    {
        static AuspiciousBeginnings instance;
        return instance;
    }

    struct SkillInfo {
        uint32_t caster_id;
        clock_t cast_start;
        uint32_t cast_time;
        int priority;
    };

    const char* Name() const override { return "Auspicious Beginnings"; }
    const char* Icon() const override { return ICON_FA_SKULL; }

    bool SetEnabled(bool b);
    bool GetEnabled();

    void ToggleEnable() { SetEnabled(!enabled); }

    void Initialize() override;
    // Draw user interface. Will be called every frame if the element is visible
    void Draw(IDirect3DDevice9* pDevice) override;
    void Update(float delta) override;

protected:
    enum Stage { EYE_OF_THE_NORTH, HALL_OF_MONUMENTS, FIRST_FIGHT, SECOND_FIGHT, THIRD_FIGHT, ROUNDING_CORNER, FOURTH_FIGHT, FIFTH_FIGHT, SIXTH_FIGHT, SEVENTH_FIGHT, LEDGE, CROSSING, STRAIGHT, OPENING, FINAL_FIGHT };
    enum State { PROGRESSING, PICKING_UP, FIGHTING, SCATTERING, KITING};

private:
    std::unordered_map<SkillID, int> skill_interrupt_priority = {
        {SkillID::Earth_Shaker, 3},
        {SkillID::Backbreaker, 3},
        {SkillID::Forceful_Blow, 2},
        {SkillID::Counter_Blow, 2},
        {SkillID::Lions_Comfort, 1},
        {SkillID::Healing_Signet, 1},
        {SkillID::Yeti_Smash, 2},
        {SkillID::Death_Pact_Signet, 4},
        {SkillID::Crippling_Slash, 1},
        {SkillID::Griffons_Sweep, 1},
        {SkillID::Grapple, 2},
        {SkillID::Resurrection_Signet, 4},
        {SkillID::Conjure_Frost, 2},
        {SkillID::Judges_Insight, 2},
        {SkillID::Purge_Signet, 4},
        {SkillID::Vengeance, 4},
        {SkillID::Bane_Signet, 3},
        {SkillID::Eviscerate, 2},
        {SkillID::Triple_Chop, 2},
        {SkillID::Conjure_Flame, 2},
        {SkillID::Troll_Unguent, 1},
        {SkillID::Resurrection_Chant, 4},
        {SkillID::Choking_Gas, 2},
        {SkillID::Antidote_Signet, 1},
        {SkillID::Melandrus_Shot, 1},
        {SkillID::Anthem_of_Weariness, 1},
        {SkillID::Incendiary_Arrows, 2},
        {SkillID::Barbed_Arrows, 2},
        {SkillID::Glass_Arrows, 2},
        {SkillID::Boon_Signet, 2},
        {SkillID::Ballad_of_Restoration, 2},
        {SkillID::Remedy_Signet, 2},
        {SkillID::Guardian, 2},
        {SkillID::Signet_of_Devotion, 1},
        {SkillID::Heavens_Delight, 1},
        {SkillID::Mend_Condition, 2},
        {SkillID::Aegis, 2},
        {SkillID::Restore_Condition, 2},
        {SkillID::Deny_Hexes, 4},
        {SkillID::Healing_Breeze, 1},
        {SkillID::Smite_Condition, 2},
        {SkillID::Smite_Hex, 4},
        {SkillID::Divine_Healing, 1},
        {SkillID::Word_of_Healing, 2},
        {SkillID::Healing_Burst, 2},
        {SkillID::Cure_Hex, 4},
        {SkillID::Healing_Touch, 2},
        {SkillID::Recuperation, 3},
        {SkillID::Union, 3},
        {SkillID::Heal_Other, 2},
        {SkillID::Words_of_Comfort, 2},
        {SkillID::Signet_of_Rejuvenation, 1},
        {SkillID::Holy_Haste, 2},
        {SkillID::Heal_Party, 2},
        {SkillID::Healing_Whisper, 2},
        {SkillID::Hex_Eater_Signet, 4},
        {SkillID::Ray_of_Judgment, 4},
        {SkillID::Steam, 1},
        {SkillID::Icy_Veins, 2},
        {SkillID::Barbs, 2},
        {SkillID::Defile_Flesh, 1},
        {SkillID::Recovery, 3},
        {SkillID::Protective_Was_Kaolai, 1},
        {SkillID::Soothing_Memories, 1},
        {SkillID::Lingering_Curse, 1},
        {SkillID::Vampiric_Swarm, 2},
        {SkillID::Deathly_Swarm, 1},
        {SkillID::Oppressive_Gaze, 1},
        {SkillID::Vile_Miasma, 1},
        {SkillID::Shell_Shock, 1},
        {SkillID::Animate_Flesh_Golem, 3},
        {SkillID::Splinter_Weapon, 1},
        {SkillID::Animate_Bone_Horror, 2},
        {SkillID::Spoil_Victor, 2},
        {SkillID::Shadow_Strike, 2},
        {SkillID::Well_of_Blood, 3},
        {SkillID::Order_of_Pain, 2},
        {SkillID::Weaken_Knees, 1},
        {SkillID::Energy_Surge, 2},
        {SkillID::Energy_Burn, 1},
        {SkillID::Destruction, 3},
        {SkillID::Empathy, 1},
        {SkillID::Unnatural_Signet, 1},
        {SkillID::Clumsiness, 3},
        {SkillID::Sandstorm, 3},
        {SkillID::Ebon_Hawk, 2},
        {SkillID::Unsteady_Ground, 3},
        {SkillID::Meteor_Shower, 4},
        {SkillID::Purge_Conditions, 2},
        {SkillID::Renew_Life, 4},
        {SkillID::Earthquake, 3},
        {SkillID::Shatterstone, 2},
        {SkillID::Icy_Prism, 1},
        {SkillID::Liquid_Flame, 1},
        {SkillID::Lightning_Touch, 3},
        {SkillID::Shock, 3},
        {SkillID::Savannah_Heat, 4},
        {SkillID::Agony, 3},
        {SkillID::Fire_Storm, 3},
        {SkillID::Blinding_Flash, 2},
        {SkillID::Lightning_Javelin, 1},
        {SkillID::Dancing_Daggers, 1},
        {SkillID::Entangling_Asp, 3},
        {SkillID::Signet_of_Toxic_Shock, 2},
        {SkillID::Vampiric_Assault, 2},
        {SkillID::Impale, 1},
        {SkillID::Temple_Strike, 3},
        {SkillID::Plague_Touch, 2},
        {SkillID::Preservation, 3},
        {SkillID::Remove_Hex, 4},
        {SkillID::Shelter, 3},
        {SkillID::Spirit_Light, 1},
        {SkillID::Flesh_of_My_Flesh, 4},
        {SkillID::Animate_Bone_Minions, 3},
        {SkillID::Spirits_Strength, 3},
        {SkillID::Signet_of_Spirits, 3},
        {SkillID::Spirit_Burn, 2},
        {SkillID::Mend_Body_and_Soul, 2},
        {SkillID::Signet_of_Ghostly_Might, 2},
        {SkillID::Shadowsong, 3},
        {SkillID::Pain, 3},
        {SkillID::Bloodsong, 3},
        {SkillID::Anguish, 3},
        {SkillID::Song_of_Restoration, 3},
        {SkillID::Apply_Poison, 1},
        {SkillID::Cautery_Signet, 1},
        {SkillID::Signet_of_Return, 4},
        {SkillID::Reapers_Sweep, 3},
        {SkillID::Wounding_Strike, 2}};

    struct CurrentInterruptSkill {
        uint32_t target_id;
        int cast_time;
    };

    CurrentInterruptSkill current_interrupt_skill = {0, 0};

    std::unordered_map<GW::AgentID, SkillInfo> active_skills = {};
    std::unordered_set<GW::AgentID> weakened_agents = {};

    bool enabled = false;
    Stage stage = EYE_OF_THE_NORTH;
    State state = PROGRESSING;

    void SetCurrentInterruptSkill(int rupt_time, uint32_t caster_id);
    void ResetReaction();
    void GenericCallback(uint32_t value_id, uint32_t caster_id, uint32_t value);

    bool MoveForStage(Stage stage_value, GW::AgentLiving* playerLiving, float x, float y);
    bool MoveWithVariance(float x, float y);
    bool CheckPositionWithinVariance(GW::AgentLiving* playerLiving, float x, float y);
    bool Travel();

    GW::HookEntry GenericValueTarget_Entry;
    GW::HookEntry Ping_Entry;
    GW::HookEntry GenericValueSelf_Entry;

    static void OnServerPing(GW::HookStatus*, void* packet);

    int32_t ComputeSkillTime(SkillInfo skill_info);
    float CalculateAngleToMoveAway(GW::GamePos epicenter, GW::GamePos player_position, GW::GamePos group_position);

    bool ShouldItemBePickedUp(GW::AgentItem* item);
};

// Map 161
// 633, 7270
// 2234.59, 5582.39
// 2602.29, 3165.05
// 1553.87, 2429.15
// -716.25, 3801.24
// -2461.78, 4778.36
// Map 165
