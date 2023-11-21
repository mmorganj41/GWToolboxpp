#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include <Timer.h>
#include "ToolboxWindow.h"

class LDOA : public ToolboxWindow {
    LDOA() = default;
    ~LDOA() = default;

public:
    static LDOA& Instance()
    {
        static LDOA instance;
        return instance;
    }

    const char* Name() const override { return "LDOA"; }
    const char* Icon() const override { return ICON_FA_SKULL; }

     bool SetEnabled(bool b);
     bool GetEnabled();

     void ToggleEnable() { SetEnabled(!enabled); }

    void Initialize() override;
    //// Draw user interface. Will be called every frame if the element is visible
     void Draw(IDirect3DDevice9* pDevice) override;
     void Update(float delta) override;

     protected:
         enum Stage
         {
             LEAVING_TOWN,
             APPROACHING_SHRINE,
             LEAVING_FIRST_MOB,
             ROUNDING_BEND,
             FINISHING_PATH,
         };
    
         enum State {
             IDLE,
             FIGHTING,
             KITING,
             SCATTERING
         };
    
     private:
         bool enabled = false;
         Stage stage = LEAVING_TOWN;
         State state = IDLE;

         bool shouldInputScatterMove = false;

         clock_t skillTimers[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    
         uint32_t expertise = 5;

         float area_of_effect = 0;
    
         std::optional<GW::GamePos> kiting_location = std::nullopt;
         std::optional<GW::GamePos> planting_position = std::nullopt;
         std::optional<GW::GamePos> epicenter = std::nullopt;
    
         void AttackCallback(uint32_t caster_id);
         void SkillDamageCallback(uint32_t caster_id, uint32_t target_id, uint32_t value);
         void UseSkillWithTimer(uint32_t slot, uint32_t target = 0U);
    
         bool MoveForStage(Stage stage_value, GW::AgentLiving* playerLiving, float x, float y);
         bool MoveWithVariance(float x, float y);
         bool CheckPositionWithinVariance(GW::AgentLiving* playerLiving, float x, float y);
         bool Travel();
         bool CanUseSkill(GW::SkillbarSkill skillbar_skill, GW::Skill* skill_info, float cur_energy);
         bool isCasting(GW::AgentLiving* agentLiving);
    
         float ExpertiseToSkillReduction(GW::Skill* skill);
         float CalculateAngleToMoveAway(GW::GamePos position_away, GW::GamePos player_position, GW::GamePos group_position);
    
         GW::HookEntry GenericValueTarget_Entry;
         GW::HookEntry AddEffect;

        void AddEffectTargetCallback(GW::Packet::StoC::AddEffect* packet);
};

// Map 161
// 633, 7270
// 2234.59, 5582.39
// 2602.29, 3165.05
// 1553.87, 2429.15
// -716.25, 3801.24
// -2461.78, 4778.36
// Map 165
