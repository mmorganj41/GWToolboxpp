#include "stdafx.h"

#include <optional>
#include <thread>

#include <GWCA/Constants/Constants.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Attribute.h>
#include <GWCA/GameEntities/Hero.h>
#include <GWCA/GameEntities/Item.h>
#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/EffectMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/UIMgr.h>

#include <GWCA/Context/WorldContext.h>

#include <GWCA/Packets/StoC.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Windows/LDOA.h>

#include <Logger.h>

 namespace {
     clock_t sleep_timer = 0;
     int sleep_value = 40;
     clock_t instance_delay = 0;
     clock_t combat_timer = 0;
     clock_t attack_timer = 0;
     clock_t imp_timer = 0;
     uint32_t imp_delay = 5000;
     clock_t kite_timer = 0;
     uint32_t attack_target = 0;
     uint32_t picking_up = 0;
     clock_t pick_up_timer = 0;
     clock_t attack_start_timer = 0;
     clock_t run_timer = 0;
     clock_t scatter_timer = 0;
     clock_t skill_damage_timer = 0;
     clock_t target_timer = 0;
     clock_t skill_use_timer = 0;
     clock_t move_timer = 0;
     bool starting_combat = false;
     bool should_move = true;
     clock_t mover_timer = 0;

     struct MapStruct {
         GW::Constants::MapID map_id = GW::Constants::MapID::None;
         int region_id = 0;
         int language_id = 0;
         uint32_t district_number = 0;
     };
 } // namespace

 bool LDOA::GetEnabled() { return enabled; }

 bool LDOA::SetEnabled(bool b)
{
     if (enabled != b)
         enabled = b;
     return enabled;
 }

void LDOA::Initialize()
{
    ToolboxWindow::Initialize();

     GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) -> void {
         UNREFERENCED_PARAMETER(status);
         using namespace GW::Packet::StoC::GenericValueID;
         if (!enabled) return;

        const uint32_t value_id = packet->Value_id;
        const uint32_t target_id = packet->target;
        const uint32_t caster_id = packet->caster;
        const uint32_t value = packet->value;

        if (value_id == GW::Packet::StoC::GenericValueID::attack_started) {
            AttackCallback(target_id);
        }
        else if (value_id == GW::Packet::StoC::GenericValueID::skill_damage) {
            SkillDamageCallback(caster_id, target_id, value);
        }
    });

     GW::StoC::RegisterPacketCallback<GW::Packet::StoC::AddEffect>(&AddEffect, [this](GW::HookStatus* status, GW::Packet::StoC::AddEffect* packet) -> void {
         UNREFERENCED_PARAMETER(status);
         if (!enabled) return;

         AddEffectTargetCallback(packet);
     });
}

 void LDOA::Update(float delta)
{
     UNREFERENCED_PARAMETER(delta);

     if (!enabled)
         return;

     if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
         imp_timer = TIMER_INIT();
         instance_delay = TIMER_INIT();
         return;
     }
     if (TIMER_DIFF(instance_delay) > 2750 + rand() % 500 && TIMER_DIFF(sleep_timer) > sleep_value + rand() % 20) {
         sleep_timer = TIMER_INIT();

         GW::AgentLiving* playerLiving = GW::Agents::GetPlayerAsAgentLiving();

         if (!playerLiving)
             return;

         if (playerLiving->level >= 20)
             return;

         switch (GW::Map::GetMapID()) {
             case GW::Constants::MapID::Foibles_Fair_outpost: {
                 sleep_value = 40;

                 if (!playerLiving->GetIsMoving()) {
                     MoveWithVariance(633, 7270);
                     imp_delay = 4500 + rand() % 1000;
                     stage = LEAVING_TOWN;
                 }
                 break;
             }
             case GW::Constants::MapID::Wizards_Folly: {
                 if (!playerLiving->GetIsAlive()) {
                     Travel();
                     sleep_value = 3000;
                 }
                 GW::PetInfo* pet_info = GW::PartyMgr::GetPetInfo();
                 if (!pet_info) return;

                 GW::EffectArray* playerEffects = GW::Effects::GetPlayerEffects();

                 if (!playerEffects) {
                     return;
                 }

                 bool has_imp = false;

                 for (uint32_t i = 0; i < playerEffects->size(); i++) {
                     if (playerEffects->at(i).skill_id == GW::Constants::SkillID::Summoning_Sickness)
                         has_imp = true;
                 }

                 if (!has_imp && TIMER_DIFF(imp_timer) > 5000) {
                     imp_timer = TIMER_INIT();
                     Log::Info("using summoning stone");
                     GW::Items::UseItemByModelId(30847, 1, 4);
                 }

                 GW::AgentArray* agents = GW::Agents::GetAgentArray();

                 GW::AgentLiving* closest_agent = nullptr;
                 GW::AgentLiving* lowest_health_agent = nullptr;

                 bool found_item = false;

                 for (auto* a : *agents) {
                     if (a && a->agent_id == picking_up)
                         found_item = true;
                     GW::AgentLiving* agent = a ? a->GetAsAgentLiving() : nullptr;
                     if (!(agent && agent->allegiance == GW::Constants::Allegiance::Enemy))
                         continue; // ignore non-hostiles
                     if (agent->GetIsDead())
                         continue; // ignore dead
                     const float sqrd = GW::GetSquareDistance(playerLiving->pos, agent->pos);
                     if (!closest_agent) {
                         closest_agent = agent;
                     }
                     else {
                         const float sqrd_close = GW::GetSquareDistance(playerLiving->pos, closest_agent->pos);
                         if (sqrd < sqrd_close)
                             closest_agent = agent;
                     }
                     if (sqrd > GW::Constants::SqrRange::Spellcast)
                         continue;

                     if (agent->hp < 1.0f && (!lowest_health_agent || lowest_health_agent->hp > agent->hp))
                         lowest_health_agent = agent;
                 }

                 if (!found_item)
                     picking_up = 0;


                 if (state == KITING || state == SCATTERING) {
                     combat_timer = TIMER_INIT();
                 }
                 else if (closest_agent &&
                     ((playerLiving->GetInCombatStance() && GW::GetSquareDistance(closest_agent->pos, playerLiving->pos) < powf(1350.0f, 2)) || GW::GetSquareDistance(closest_agent->pos, playerLiving->pos) < GW::Constants::SqrRange::Spellcast)) {
                     if (!isCasting(playerLiving) && TIMER_DIFF(kite_timer) > 2250 && closest_agent && GW::GetSquareDistance(closest_agent->pos, playerLiving->pos) <= GW::Constants::SqrRange::Adjacent * 3 / 2 &&  TIMER_DIFF(attack_start_timer) > 1000 *
                     playerLiving->weapon_attack_speed * playerLiving->attack_speed_modifier / 2 + 150) {
                         state = KITING;
                         kite_timer = TIMER_INIT();
                         kiting_location = closest_agent->pos;
                     } else  {
                         state = FIGHTING;
                     }
                         combat_timer = TIMER_INIT();
                      }
                       
                 else if ((state != IDLE) && combat_timer >= 1500) {
                     starting_combat = true;
                     planting_position = std::nullopt;
                     Log::Info("exiting combat");
                     state = IDLE;
                 }

                 float cur_energy = playerLiving->energy * playerLiving->max_energy;

                 switch (state) {
                     case FIGHTING:
                     {
                         if (!planting_position) {
                             planting_position = playerLiving->pos;
                         }
                        if (closest_agent) {
                             GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

                             if (!skillbar)
                                 return;

                             if (skillbar->casting || skillbar->disabled || playerLiving->skill || playerLiving->GetIsKnockedDown())
                                 return;

                             if (TIMER_DIFF(attack_timer) < playerLiving->weapon_attack_speed * playerLiving->attack_speed_modifier)
                                 return;


                             const GW::SkillbarSkill ignite_arrows = skillbar->skills[7];
                             float closest_distance = GW::GetDistance(playerLiving->pos, closest_agent->pos);

                             if (!ignite_arrows.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(ignite_arrows.skill_id)) {
                                 GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(ignite_arrows.skill_id);

                                 if (skill_info && CanUseSkill(ignite_arrows, skill_info, cur_energy)) {
                                         UseSkillWithTimer(7);
                                 }
                             }

                             const GW::SkillbarSkill troll_ungent = skillbar->skills[6];

                             if (!troll_ungent.GetRecharge() && closest_agent->level > 7 && closest_distance > GW::Constants::Range::Nearby && (playerLiving->hp < .55 || closest_distance > GW::Constants::Range::Spellcast)) {
                                 GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(troll_ungent.skill_id);

                                 if (skill_info && CanUseSkill(troll_ungent, skill_info, cur_energy)) {
                                         UseSkillWithTimer(6);
                                     return;
                                 }
                             }

                             const GW::SkillbarSkill frenzy = skillbar->skills[1];
                             if (!frenzy.GetRecharge() && !GW::Effects::GetPlayerEffectBySkillId(frenzy.skill_id) && closest_distance >
                             GW::Constants::Range::Spellcast && closest_distance < GW::Constants::Range::Spellcast * 3/2) {
                                 GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(frenzy.skill_id);

                                 if (skill_info && CanUseSkill(frenzy, skill_info, cur_energy)) {
                                         UseSkillWithTimer(1);
                                     return;
                                 }
                             }

                             const uint32_t target_id = GW::Agents::GetTargetId();

                             if (TIMER_DIFF(target_timer) > 500) {
                                 if (GW::GetDistance(closest_agent->pos, playerLiving->pos) < GW::Constants::Range::Earshot) {
                                     if (target_id != closest_agent->agent_id) {
                                         GW::Agents::ChangeTarget(closest_agent->agent_id);
                                         Log::Info("setting target");
                                         target_timer = TIMER_INIT();
                                         return;
                                     }
                                 }
                                 else if (lowest_health_agent) {
                                     if (GW::GetDistance(lowest_health_agent->pos, playerLiving->pos) < GW::Constants::Range::Area * 3 / 2 || GW::GetDistance(closest_agent->pos, playerLiving->pos) > GW::Constants::Range::Area * 3 / 2)
                                         if (target_id != lowest_health_agent->agent_id) {
                                             GW::Agents::ChangeTarget(lowest_health_agent->agent_id);
                                             Log::Info("setting target");
                                             target_timer = TIMER_INIT();
                                             return;
                                         }
                                         else if (target_id != closest_agent->agent_id) {
                                             GW::Agents::ChangeTarget(closest_agent->agent_id);
                                             Log::Info("setting target");
                                             target_timer = TIMER_INIT();
                                             return;
                                         }
                                 }
                                 else if (target_id != closest_agent->agent_id) {
                                     GW::Agents::ChangeTarget(closest_agent->agent_id);
                                     Log::Info("setting target");
                                     target_timer = TIMER_INIT();
                                     return;
                                 }
                             }

                             const GW::Agent* target = GW::Agents::GetTarget();

                             if (!target)
                                 return;

                             const GW::AgentLiving* targetLiving = target->GetAsAgentLiving();

                             if (!targetLiving)
                                 return;

                             if (TIMER_DIFF(target_timer) > 20000) {
                                 Travel();
                                 sleep_value = 3000;
                                 return;
                             }

                             if (!attack_target || attack_target != targetLiving->agent_id || (!playerLiving->GetIsAttacking() && !playerLiving->GetIsMoving())) {
                                 attack_target = targetLiving->agent_id;
                                 if (TIMER_DIFF(attack_start_timer) > 1500) {
                                     Log::Info("Attacking");

                                     GW::GameThread::Enqueue([]() -> void {
                                         GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                                         GW::PartyMgr::SetPetBehavior(GW::HeroBehavior::Fight);
                                         return;
                                     });
                                     attack_start_timer = TIMER_INIT();
                                 }
                                 return;
                             }

                             if (targetLiving->hp >= .10 && TIMER_DIFF(attack_start_timer) > 1000 * playerLiving->weapon_attack_speed * playerLiving->attack_speed_modifier / 2 + 50) {

                                 const GW::SkillbarSkill dual_shot = skillbar->skills[2];

                                 if (targetLiving->level > 7 && cur_energy > 20 && !dual_shot.GetRecharge() && GW::GetSquareDistance(targetLiving->pos, playerLiving->pos) > GW::Constants::Range::Earshot) {
                                     GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(dual_shot.skill_id);
                                     if (skill_info && CanUseSkill(dual_shot, skill_info, cur_energy)) {
                                             UseSkillWithTimer(2, targetLiving->agent_id);
                                         return;
                                     }
                                 }

                                 const GW::SkillbarSkill point_blank_shot = skillbar->skills[0];

                                 if (cur_energy > 10 && !point_blank_shot.GetRecharge()) {
                                     GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(point_blank_shot.skill_id);

                                     if (skill_info && CanUseSkill(point_blank_shot, skill_info, cur_energy)) {
                                             UseSkillWithTimer(0, targetLiving->agent_id);
                                         return;
                                     }
                                 }
                             }
                         }
                         break;
                     }
                     case KITING: {
                         if (kiting_location && planting_position) {
                             float new_angle = CalculateAngleToMoveAway(*kiting_location, playerLiving->pos, *planting_position);
                             GW::GamePos new_position = {playerLiving->pos.x + std::cosf(new_angle) * GW::Constants::Range::Area, playerLiving->pos.y + std::sinf(new_angle) * GW::Constants::Range::Area, playerLiving->pos.zplane};
                             GW::Agents::Move(new_position);
                             kiting_location = std::nullopt;
                             return;
                         }
                         if (TIMER_DIFF(kite_timer) >= 1000 * playerLiving->weapon_attack_speed * playerLiving->attack_speed_modifier / 2 ||
                             (closest_agent && GW::GetDistance(playerLiving->pos, closest_agent->pos) > (GW::Constants::Range::Nearby + GW::Constants::Range::Area) / 2)) {
                             state = FIGHTING;
                         }
                         break;
                     }
                     case SCATTERING: {
                         if (isCasting(playerLiving)) {
                             scatter_timer = TIMER_INIT();
                             return;
                         }
                         if (shouldInputScatterMove && epicenter && planting_position && area_of_effect) {
                             shouldInputScatterMove = false;
                             float new_angle = CalculateAngleToMoveAway(*kiting_location, playerLiving->pos, *planting_position);
                             GW::GamePos new_position = {playerLiving->pos.x + std::cosf(new_angle) * area_of_effect, playerLiving->pos.y + std::sinf(new_angle) * area_of_effect, playerLiving->pos.zplane};
                             GW::Agents::Move(new_position);
                             scatter_timer = TIMER_INIT();
                             return;
                         }
                         if (TIMER_DIFF(scatter_timer) >= 1000 || (epicenter && GW::GetDistance(playerLiving->pos, *epicenter) > area_of_effect)) {
                             shouldInputScatterMove = false;
                             area_of_effect = 0;
                             epicenter = std::nullopt;
                             state = FIGHTING;
                         }
                         break; 
                     }
                     case IDLE:
                     {
                         GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

                         if (!skillbar) return;
                         if (!(isCasting(playerLiving) || playerLiving->GetIsKnockedDown())) {
                             const GW::SkillbarSkill healing_signet = skillbar->skills[3];

                             if (!healing_signet.GetRecharge() && playerLiving->hp < .5) {
                                     UseSkillWithTimer(3);
                                 return;
                             }

                             GW::Agent* pet = GW::Agents::GetAgentByID(pet_info->agent_id);
                             GW::AgentLiving* petLiving = pet ? pet->GetAsAgentLiving() : nullptr;

                             if (petLiving && !petLiving->GetIsAlive()) {
                                 const GW::SkillbarSkill comfort_animal = skillbar->skills[3];
                                 if (!comfort_animal.GetRecharge())  {
                                     GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(comfort_animal.skill_id);

                                     if (skill_info && CanUseSkill(comfort_animal, skill_info, cur_energy)) {
                                             UseSkillWithTimer(5);
                                         return;
                                     }
                                 }
                             }

                         }
                         if (!playerLiving->GetIsIdle() && !playerLiving->GetInCombatStance() && picking_up && TIMER_DIFF(pick_up_timer) < 15000)
                             return;
                         if (TIMER_DIFF(pick_up_timer) < 2000)
                             return;
                         for (auto* a : *agents) {
                             GW::AgentItem* agent = a ? a->GetAsAgentItem() : nullptr;
                             if (!agent)
                                 continue;
                             const GW::Item* actual_item = GW::Items::GetItemById(agent->item_id);

                             if (!actual_item)
                                 continue;

                             if (actual_item->model_id == GW::Constants::ItemID::GoldCoin || actual_item->model_id == GW::Constants::ItemID::GoldCoins || actual_item->model_id == 28434) {
                                 picking_up = agent->agent_id;
                                 Log::Info("Picking up item");
                                 GW::Agents::ChangeTarget(agent->agent_id);
                                 pick_up_timer = TIMER_INIT();
                                 GW::GameThread::Enqueue([agent]() -> void {
                                     GW::UI::Keypress(GW::UI::ControlAction::ControlAction_Interact);
                                     return;
                                 });
                                 return;
                             }
                         }
                     }
                 }

                 switch (stage) {
                     case LEAVING_TOWN: {
                         MoveForStage(APPROACHING_SHRINE, playerLiving, 633.0f, 7270.0f);
  /*                       GW::Attribute* attributes = GW::PartyMgr::GetAgentAttributes(GW::Agents::GetPlayerId());
                         assert(attributes);
                         attributes = &attributes[static_cast<uint8_t>(GW::Constants::Attribute::Expertise)];
                         if (attributes && attributes->level) {
                             expertise = attributes->level;
                         }
                         else {
                             expertise = 0;
                         }*/
                         run_timer = TIMER_INIT();
                         break;
                     }
                     case APPROACHING_SHRINE: {
                         MoveForStage(LEAVING_FIRST_MOB, playerLiving, 1924.90f, 6255.64f);
                         break;
                     }
                     case LEAVING_FIRST_MOB: {
                         MoveForStage(ROUNDING_BEND, playerLiving, 2602.29f, 3165.05f);
                         break;
                     }
                     case ROUNDING_BEND: {
                         MoveForStage(FINISHING_PATH, playerLiving, 475.81f, 2703.66f);
                         break;
                     }
                     case FINISHING_PATH: {
                         if (MoveForStage(FINISHING_PATH, playerLiving, -2237.56f, 4687.33f) && state == IDLE) {
                             Travel();
                             sleep_value = 3000;
                         };
                         break;
                     }
                 }

                 if (TIMER_DIFF(run_timer) > 300000) {
                     run_timer = TIMER_INIT();
                     Travel();
                     sleep_timer = 3000;
                 }
                 break;
             }
             default: {
                 Travel();
                 sleep_value = 3000;
             }
         }
     }
 }

 void LDOA::Draw(IDirect3DDevice9* pDevice)
{
     UNREFERENCED_PARAMETER(pDevice);
     if (!visible)
         return;

     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
     ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
     if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
         ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1));
         if (ImGui::Button(enabled ? "Enabled###farmertoggle" : "Disabled###farmertoggle", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
             ToggleEnable();
         }
         ImGui::PopStyleColor();
         ImGui::End();
         ImGui::PopStyleVar();
     }
 }

 void LDOA::AttackCallback(uint32_t caster_id)
{
     const GW::Agent* player = GW::Agents::GetPlayer();
     if (!player)
         return;
     if (player->agent_id == caster_id)
         attack_timer = TIMER_INIT();
 };

 bool LDOA::MoveForStage(Stage new_stage, GW::AgentLiving* playerLiving, float x, float y)
{
     if (CheckPositionWithinVariance(playerLiving, x, y)) {
         Log::Info("Performing stage: %d", new_stage);
         stage = new_stage;
         return true;
     }

     if (state == IDLE && !playerLiving->GetIsMoving() && !isCasting(playerLiving))
         MoveWithVariance(x, y);

     return false;
 }

 bool LDOA::MoveWithVariance(float x, float y)
{
     float new_x = x + rand() % 50 - 25;
     float new_y = y + rand() % 50 - 25;
     return GW::Agents::Move(new_x, new_y);
 }

 bool LDOA::CheckPositionWithinVariance(GW::AgentLiving* playerLiving, float x, float y) { return (abs(x - playerLiving->pos.x) <= 25 && abs(y - playerLiving->pos.y) <= 25); }

 bool LDOA::Travel()
{
     const GW::Constants::MapID MapID = GW::Constants::MapID::Foibles_Fair_outpost;

     if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading)
         return false;
     if (!GW::Map::GetIsMapUnlocked(MapID)) {
         const GW::AreaInfo* map = GW::Map::GetMapInfo(MapID);
         wchar_t map_name_buf[8];
         wchar_t err_message_buf[256] = L"[Error] Your character does not have that map unlocked";
         if (map && map->name_id && GW::UI::UInt32ToEncStr(map->name_id, map_name_buf, 8))
             Log::ErrorW(L"[Error] Your character does not have \x1\x2%s\x2\x108\x107 unlocked", map_name_buf);
         else
             Log::ErrorW(err_message_buf);
         return false;
     }
     if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost && GW::Map::GetMapID() == MapID && GW::Constants::Region::America == GW::Map::GetRegion() && 0 == GW::Map::GetLanguage()) {
         Log::Error("[Error] You are already in the outpost");
         return false;
     }
     MapStruct* t = new MapStruct();
     t->map_id = MapID;
     t->district_number = 0;
     t->region_id = GW::Constants::Region::America;
     t->language_id = 0;

     GW::GameThread::Enqueue([t] {
         GW::UI::SendUIMessage(GW::UI::UIMessage::kTravel, (void*)t);
         delete t;
     });

     return true;
 }

 bool LDOA::CanUseSkill(GW::SkillbarSkill skillbar_skill, GW::Skill* skill_info, float cur_energy) {
     if ((skill_info->adrenaline == 0 && skillbar_skill.GetRecharge()) || (skill_info->adrenaline > 0 && skillbar_skill.adrenaline_a < skill_info->adrenaline)) {
        return false;
     }


     if (static_cast<int>(cur_energy) <= std::lrint(skill_info->GetEnergyCost() * (expertise ? 1.0 - ExpertiseToSkillReduction(skill_info) : 1.0))) {
         return false;
     }

     return true;
 }

 float LDOA::ExpertiseToSkillReduction(GW::Skill* skill)
{
     if (skill->profession == static_cast<uint8_t>(GW::Constants::Profession::Ranger) || skill->type == GW::Constants::SkillType::Attack || skill->type == GW::Constants::SkillType::Ritual) {
         return expertise * .04f;
     }
     else {
         return 0.0f;
     }
 }

 float LDOA::CalculateAngleToMoveAway(GW::GamePos position_away, GW::GamePos player_position, GW::GamePos group_position) {
     const float percentOfRadius = GW::GetDistance(group_position, player_position) / GW::Constants::Range::Earshot;

     const float angleAwayFromEpicenter = std::atan2f(player_position.y - position_away.y, player_position.x - position_away.x);

     const float angleAwayFromGroup = std::atan2f(player_position.y - group_position.y, player_position.x - group_position.x);

     return (angleAwayFromEpicenter - angleAwayFromGroup * percentOfRadius) / 2;
 }

 bool LDOA::isCasting(GW::AgentLiving* agentLiving) {
     GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

     return (agentLiving->skill || agentLiving->GetIsCasting() || (skillbar && skillbar->casting));
 }

 void LDOA::UseSkillWithTimer(uint32_t slot, uint32_t target)
 {
     if (TIMER_DIFF(skillTimers[slot]) < 500) return;

     skillTimers[slot] = TIMER_INIT();
     GW::SkillbarMgr::UseSkill(slot, target);
     return;
 }

 void LDOA::AddEffectTargetCallback(GW::Packet::StoC::AddEffect* packet)
 {
     if (!packet->agent_id) return;
     if (shouldInputScatterMove) return;
     GW::Agent* target = GW::Agents::GetAgentByID(packet->agent_id);
     GW::Agent* playerLiving = GW::Agents::GetPlayer();
     if (!target || !playerLiving) return;
     GW::AgentLiving* targetLiving = target->GetAsAgentLiving();
     if (!targetLiving) return;
     if (!packet->skill_id) return;
     if (targetLiving->allegiance == GW::Constants::Allegiance::Spirit_Pet || targetLiving->allegiance == GW::Constants::Allegiance::Ally_NonAttackable || targetLiving->allegiance == GW::Constants::Allegiance::Minion) return;
     GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(packet->skill_id));
     if (!(skill_info && (skill_info->aoe_range > 0 || skill_info->skill_id == GW::Constants::SkillID::Ray_of_Judgment) && (skill_info->duration0 > 0 || playerLiving->agent_id != target->agent_id))) return;
     if (GW::GetDistance(target->pos, playerLiving->pos) > skill_info->aoe_range || target->pos.zplane != playerLiving->pos.zplane) return;
     if (state != SCATTERING && TIMER_DIFF(scatter_timer > 3000)) {
         area_of_effect = 50.0f + std::max(skill_info->aoe_range, GW::Constants::Range::Adjacent);
         epicenter = target->pos;
         shouldInputScatterMove = true;
         scatter_timer = TIMER_INIT();
         Log::Info("Scattering due to cast.");
         state = SCATTERING;
     }
 }

 void LDOA::SkillDamageCallback(uint32_t caster_id, uint32_t target_id, uint32_t value) {
     uint32_t playerId = GW::Agents::GetPlayerId();

     if (!playerId) return;
     if (caster_id == playerId) {
         Log::Info("You dealt damage");
         skill_damage_timer = TIMER_INIT();
     }
     else {
         GW::Skill* skill_info = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));
         if (skill_info && (skill_info->aoe_range > 0 || skill_info->skill_id == GW::Constants::SkillID::Ray_of_Judgment) && (skill_info->duration0 > 0 || target_id != playerId)) {
             if (!shouldInputScatterMove && TIMER_DIFF(scatter_timer > 2000)) {
                 GW::Agent* sidekick = GW::Agents::GetPlayer();
                 if (!sidekick) return;
                 area_of_effect = 50.0f + std::max(skill_info->aoe_range, GW::Constants::Range::Adjacent);
                 epicenter = sidekick->pos;
                 shouldInputScatterMove = true;
                 scatter_timer = TIMER_INIT();
                 Log::Info("Scattering due to damage.");
                 state = SCATTERING;
             }
         }
     }
 }
