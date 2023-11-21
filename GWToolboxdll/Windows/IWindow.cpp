#include "stdafx.h"

#include <optional>
#include <thread>

#include <GWCA/Constants/Constants.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Attribute.h>
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

#include <GWCA/Packets/StoC.h>

#include <Utils/GuiUtils.h>

#include <Modules/Resources.h>
#include <Windows/IWindow.h>

#include <Logger.h>

namespace {
    uint32_t ping = 0;
    clock_t controller_timer = 0;
    clock_t min_cast_timer = 0;
    clock_t reaction_timer = 0;
    clock_t sleep_timer = 0;
    clock_t attack_timer = 0;
    clock_t skill_wait_timer = 0;
    bool updated = false;
    bool waiting_reaction = false;
    bool waiting_to_sleep = false;
    int reaction_time = 0;
} // namespace

bool IWindow::GetEnabled() { return mainSetting.enabled; }

bool IWindow::SetEnabled(bool b)
{
    if (mainSetting.enabled != b)
        mainSetting.enabled = b;
    return mainSetting.enabled;
}

void IWindow::Initialize()
{
    ToolboxWindow::Initialize();

     GW::StoC::RegisterPacketCallback(&Ping_Entry, GAME_SMSG_PING_REPLY, OnServerPing, 0x800);

     GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericModifier>(&GenericModifier_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericModifier* packet) -> void {
         UNREFERENCED_PARAMETER(status);
         if (!mainSetting.enabled) return;

         const uint32_t type = packet->type;
         const uint32_t caster_id = packet->target_id;
         const float value = packet->value;

         CasttimeCallback(type, caster_id, value);
     });

     GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValue>(&GenericValueSelf_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValue* packet) -> void {
        UNREFERENCED_PARAMETER(status);
        if (!mainSetting.enabled) return;

         const uint32_t value_id = packet->value_id;
         const uint32_t caster_id = packet->agent_id;
         const uint32_t value = packet->value;

         SkillCallback(value_id, caster_id, value);
     });

     GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, [this](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) -> void {
         UNREFERENCED_PARAMETER(status);
        if (!mainSetting.enabled) return;
         using namespace GW::Packet::StoC::GenericValueID;

         const uint32_t value_id = packet->Value_id;
         const uint32_t caster_id = packet->caster;
         const uint32_t target_id = packet->target;
         const uint32_t value = packet->value;

         const bool isSwapped = value_id == skill_activated || value_id == attack_skill_activated || value_id == attack_started;
         SkillCallback(value_id, isSwapped ? target_id : caster_id, value);
     });
}

 void IWindow::Update(float delta)
 {
     UNREFERENCED_PARAMETER(delta);
     if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) {
         SetEnabled(false);
         return;
     }

     if (mainSetting.enabled && updated && TIMER_DIFF(controller_timer) > sleep_time) {
         controller_timer = TIMER_INIT();

         const auto* player = GW::Agents::GetPlayer();

         const auto* player_living = player ? player->GetAsAgentLiving() : nullptr;

         if (!player_living)
             return;

         if (!player_living->GetInCombatStance()) {
             if (waiting_to_sleep && TIMER_DIFF(sleep_timer) > 5000) {
                 Log::Info("Exiting Combat... Going to sleep.");
                 sleep_time = out_of_combat_sleep_time;
                 waiting_to_sleep = false;
                 return;
             }
             else if (!waiting_to_sleep) {
                 if (sleep_time >= out_of_combat_sleep_time) {
                     // clean up if ooc
                     if (!active_skills.empty()) {
                         active_skills.clear();
                     }
                     return;
                     if (!dazed_agents.empty()) {
                         dazed_agents.clear();
                     }
                 }
                 else {
                     waiting_to_sleep = true;
                     sleep_timer = TIMER_INIT();
                 }
             }
         }

         const GW::Skillbar* const skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

         if (!(skillbar && skillbar->IsValid() && skillbar->skills))
             return;

         if (!waiting_reaction) {
             if (skillbar->disabled || skillbar->casting || player_living->GetIsKnockedDown() || player_living->skill) {
                 sleep_time = 10;
                 return;
             }
             else if (sleep_time > 0) {
                 sleep_time = 0;
             }

             min_cast_time = std::numeric_limits<int32_t>::max();
             for (size_t idx = 0u; idx < 8; idx++) {
                 if (!interruptSkills[idx].enabled)
                     continue;

                 GW::Skill* skill_info = *player_skill_info[idx];

                 if (!((skill_info->adrenaline == 0 && skillbar->skills[idx].GetRecharge()) || (skill_info->adrenaline > 0 && skillbar->skills[idx].adrenaline_a < skill_info->adrenaline))) {
                     const int rupt_time = IWindow::ComputeRuptTime(idx, player_living->attack_speed_modifier, player_living->weapon_attack_speed);
                     if (rupt_time < min_cast_time)
                         min_cast_time = rupt_time;
                 }
             }

             // std::vector<SkillInfo> prioritizedSkills;
             std::optional<SkillInfo> prioritized_skill = std::nullopt;

             if (!active_skills.empty()) {
                 for (auto& it : active_skills) {
                     const auto caster_agent = GW::Agents::GetAgentByID(it.second.caster_id);
                     const auto caster = caster_agent ? caster_agent->GetAsAgentLiving() : nullptr;

                     if (!caster) {
                         continue;
                     }

                     float distance = GW::GetSquareDistance(player->pos, caster->pos);

                     if (distance > SkillRangeToDistance(mainSetting.range)) {
                         continue;
                     }

                     if (caster->GetIsDead()) {
                         continue;
                     }

                     const int32_t cast_time = it.second.cast_time - min_cast_time - min_reaction_time;

                     if (TIMER_DIFF(it.second.cast_start) > cast_time) {
                         Log::Info("Cast_time over, %d, %d", TIMER_DIFF(it.second.cast_start), cast_time);
                         active_skills.erase(it.first);
                         continue;
                     }

                     GW::Skill* skill_data = GW::SkillbarMgr::GetSkillConstantData(it.second.skill_id);

                     if (!skill_data)
                         continue;

                     // prioritizedSkills.push_back(it.second);
                     if (!prioritized_skill || prioritized_skill->priority < it.second.priority) {
                         prioritized_skill = it.second;
                     }
                 }
             }

             if (prioritized_skill) {
                 const auto caster = GW::Agents::GetAgentByID(prioritized_skill->caster_id);

                 if (!player_living || !caster)
                     return;

                 for (size_t idx = 0u; idx < 8; idx++) {
                     if (!interruptSkills[idx].enabled)
                         continue;

                     if (!GetValidInterrupt(interruptSkills[idx], prioritized_skill->skill_type)) {
                         continue;
                     }

                     if (!player_skill_info[idx])
                         continue;

                     GW::Skill* skill_info = *player_skill_info[idx];
                     // if (skill_info->weapon_req) {
                     //     if (!(skill_info->weapon_req & WeaponTypeToWeaponReq(player_living->weapon_type))) {
                     //         Log::Info("Invalid weapon");
                     //         continue;
                     //     }
                     // }

                     if ((skill_info->adrenaline == 0 && skillbar->skills[idx].GetRecharge()) || (skill_info->adrenaline > 0 && skillbar->skills[idx].adrenaline_a < skill_info->adrenaline)) {
                         continue;
                     }

                     const float distance = GW::GetSquareDistance(player->pos, caster->pos);

                     if (distance > SkillRangeToDistance(interruptSkills[idx].range)) {
                         continue;
                     }

                     const auto* player_map_agent = GW::Agents::GetMapAgentByID(player->agent_id);

                     if (player_map_agent && player_map_agent->cur_energy < skill_info->GetEnergyCost() * (expertise ? 1.0 - ExpertiseToSkillReduction(skill_info) : 1.0)) {
                         continue;
                     }

                     if (skill_info->combo_req && skill_info->combo_req != player_living->dagger_status) {
                         continue;
                     }

                     if (skill_info->skill_id == GW::Constants::SkillID::Agonizing_Chop) {
                         const auto caster_living = caster->GetAsAgentLiving();
                         if (!(caster_living && caster_living->GetIsDeepWounded()))
                             continue;
                     }

                     int rupt_time = IWindow::ComputeRuptTime(idx, player_living->attack_speed_modifier, player_living->weapon_attack_speed);
                     const int skill_time = IWindow::ComputeSkillTime(*prioritized_skill);

                     if (skill_info->weapon_req & 2 && player_living->weapon_type == 1) {
                         rupt_time += static_cast<int>(sqrt(distance) * .042);
                     }

                     const int free_time = skill_time - rupt_time;

                     if (free_time <= 0) {
                         Log::Info("Not enough free time to set interrupt skill %d = %d - %d", free_time, skill_time, rupt_time);
                         continue;
                     }

                     waiting_reaction = true;
                     reaction_timer = TIMER_INIT();

                     const int weapon_activation_time = static_cast<int>(player_living->weapon_attack_speed * 1000 * player_living->attack_speed_modifier / 2);
                     if (skill_info->activation && (TIMER_DIFF(skill_wait_timer) < weapon_activation_time) && (free_time > (weapon_activation_time - TIMER_DIFF(skill_wait_timer)))) {
                         const int wait_time = TIMER_DIFF(skill_wait_timer) - weapon_activation_time;
                         reaction_time = rand() % (free_time - wait_time - ping) + wait_time;
                     }
                     else if (!skill_info->activation && (TIMER_DIFF(attack_timer) < weapon_activation_time)) {
                         reaction_time = rand() % (weapon_activation_time - TIMER_DIFF(attack_timer)) + TIMER_DIFF(attack_timer);
                     }
                     else {
                         reaction_time = rand() % (std::min(300, ((free_time * 3 / 4 < min_reaction_time) ? 0 : (free_time * 3 / 4 - min_reaction_time))) + ((min_reaction_time > static_cast<int32_t>(ping)) ? (min_reaction_time - ping) : 0));
                     }
                     Log::Info("Set skill to interrupt with a reaction time of %d", reaction_time);
                     SetCurrentInterruptSkill(idx, rupt_time, prioritized_skill->caster_id, skill_info->adrenaline);
                     break;
                 }
                 active_skills.erase(prioritized_skill->caster_id);
             }
         }

         if (waiting_reaction) {
             const GW::Agent* caster = GW::Agents::GetAgentByID(current_interrupt_skill.target_id);

             const GW::AgentLiving* targeted_living = caster ? caster->GetAsAgentLiving() : nullptr;

             if (!(targeted_living && targeted_living->GetIsAlive() && (targeted_living->skill || TIMER_DIFF(reaction_timer) < 5))) {
                 ResetReaction();
                 return;
             }

             if (current_interrupt_skill.interruptIndex == std::nullopt) {
                 ResetReaction();
                 return;
             }

             GW::Skill* skill_info = *player_skill_info[*current_interrupt_skill.interruptIndex];

             if (!skill_info) {
                 ResetReaction();
                 return;
             }

             // if (skill_info->weapon_req) {
             //     if (!(skill_info->weapon_req & WeaponTypeToWeaponReq(player_living->weapon_type))) {
             //         ResetReaction();
             //         return;
             //     }
             // }

             if ((skill_info->adrenaline == 0 && skillbar->skills[*current_interrupt_skill.interruptIndex].GetRecharge()) || (skill_info->adrenaline > 0 && skillbar->skills[*current_interrupt_skill.interruptIndex].adrenaline_a < skill_info->adrenaline)) {
                 ResetReaction();
                 return;
             }

             if (skill_info->combo_req && skill_info->combo_req != player_living->dagger_status) {
                 ResetReaction();
                 return;
             }

             const auto* player_map_agent = GW::Agents::GetMapAgentByID(player_living->agent_id);

             if (player_map_agent && player_map_agent->cur_energy < skill_info->GetEnergyCost() * (expertise ? 1.0 - ExpertiseToSkillReduction(skill_info) : 1.0)) {
                 ResetReaction();
                 return;
             }

             if (TIMER_DIFF(reaction_timer) >= reaction_time - static_cast<int>(ping)) {
                 if (!(player_living && !player_living->GetIsKnockedDown())) {
                     ResetReaction();
                     return;
                 }

                 if (!(skillbar && skillbar->IsValid() && !(skillbar->disabled || skillbar->casting))) {
                     ResetReaction();
                     return;
                 }

                 const float distance = GW::GetSquareDistance(player_living->pos, targeted_living->pos);

                 if (distance > SkillRangeToDistance(interruptSkills[*current_interrupt_skill.interruptIndex].range)) {
                     ResetReaction();
                     return;
                 }

                 BeginCastingCurrentInterruptSkill(*current_interrupt_skill.interruptIndex, current_interrupt_skill.target_id);

                 ResetReaction();
             }
         }
     }
     else if (!updated && mainSetting.enabled) {
         IWindow::UpdatePlayerSkillInfo();
         updated = true;
     }
     else if (!mainSetting.enabled) {
         updated = false;
     }
 }

 void IWindow::UpdatePlayerSkillInfo()
 {
     const GW::Inventory* inventory = GW::Items::GetInventory();

     if (inventory) {
         std::copy(std::begin(inventory->weapon_sets), std::end(inventory->weapon_sets), std::begin(weapon_sets));
     }

     const GW::AgentID player_id = GW::Agents::GetPlayerId();

     if (player_id) {
         GW::Attribute* attributes = GW::PartyMgr::GetAgentAttributes(player_id);
         assert(attributes);
         attributes = &attributes[static_cast<uint8_t>(GW::Constants::Attribute::Expertise)];
         if (attributes && attributes->level) {
             expertise = attributes->level;
         }
         else {
             expertise = 0;
         }
     }

     const GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();

     mainSetting.range = 0;

     mainSetting.attacks = false;
     mainSetting.spells = false;
     mainSetting.other = false;
     mainSetting.chants = false;

     if (skillbar == nullptr)
         return;

     for (size_t idx = 0u; idx < 8; idx++) {
         const GW::Constants::SkillID& skill_id = skillbar->skills[idx].skill_id;
         GW::Skill* skill_data = GW::SkillbarMgr::GetSkillConstantData(skill_id);
         if (skill_data) {
             player_skill_info[idx] = skill_data;
             if (interruptSkills[idx].enabled) {
                 if (interruptSkills[idx].range > mainSetting.range) {
                     mainSetting.range = interruptSkills[idx].range;
                 }
                 if (interruptSkills[idx].attacks)
                     mainSetting.attacks = true;
                 if (interruptSkills[idx].chants)
                     mainSetting.chants = true;
                 if (interruptSkills[idx].spells)
                     mainSetting.spells = true;
                 if (interruptSkills[idx].other)
                     mainSetting.other = true;
             }
         }
         else {
             player_skill_info[idx] = std::nullopt;
         }
     }

     Log::Info("Initializing Skill Info and Weapon Sets.");
 }

 int IWindow::ComputeRuptTime(int i, float attack_speed_modifier, float base_attack_speed)
 {
     const GW::Skill* skill_info = *player_skill_info[i];

     using namespace GW::Constants;

     const int base_value = ping + min_reaction_time;

     switch (skill_info->type) {
         case SkillType::Attack:
         case SkillType::PetAttack: {
             if (skill_info->activation > 0.0f) {
                 return base_value + static_cast<int>(skill_info->activation * 1000 * attack_speed_modifier / 2);
             }
             else {
                 const int half_attack_time = static_cast<int>(base_attack_speed * 1000 * attack_speed_modifier / 2);
                 const int remaining_attack_time = half_attack_time - TIMER_DIFF(attack_timer);
                 return base_value + half_attack_time + (remaining_attack_time > 0 ? remaining_attack_time : 0);
             }
         }
         default: {
             return base_value + static_cast<int>(skill_info->activation * 1000); // todo: add cast speed mods;
         }
     }
 }

 int IWindow::ComputeSkillTime(SkillInfo skill_info) { return skill_info.cast_time - TIMER_DIFF(skill_info.cast_start) - ping; }

 void IWindow::CasttimeCallback(uint32_t type, uint32_t caster_id, float value)
 {
     if (type != GW::Packet::StoC::GenericValueID::casttime)
         return;

     modified_cast_times.insert_or_assign(caster_id, value);
 }

 void IWindow::SkillCallback(const uint32_t value_id, const uint32_t caster_id, const uint32_t value)
 {
     using namespace GW::Packet::StoC;

     switch (value_id) {
         case GenericValueID::attack_skill_activated:
         case GenericValueID::skill_activated: {
             const int cast_start = TIMER_INIT();

             const auto* target = GW::Agents::GetTarget();
             if (lock_on && !(target && caster_id == target->agent_id))
                 return;

             const auto agent = GW::Agents::GetAgentByID(caster_id);
             const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

             float cast_time = modified_cast_times[caster_id];
             modified_cast_times.erase(caster_id);

             if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy))
                 return;
             if (living_agent->GetIsDead())
                 return;

             const GW::Agent* player = GW::Agents::GetPlayer();
             if (!player)
                 return;
             const float distance = GW::GetSquareDistance(player->pos, living_agent->pos);

             if (distance > GW::Constants::SqrRange::Spirit)
                 return;

             const auto skill = GW::SkillbarMgr::GetSkillConstantData(static_cast<GW::Constants::SkillID>(value));

             GW::Constants::SkillType skill_type = GW::Constants::SkillType::Skill;

             if (skill) {
                 skill_type = skill->type;
                 if (!GetValidInterrupt(mainSetting, skill_type)) {
                     return;
                 }
                 if (living_agent->GetIsConditioned() && CheckForDaze(skill_type, living_agent->agent_id)) {
                     return;
                 }
                 if (!cast_time) {
                     cast_time = skill->activation;
                     if (value_id == GenericValueID::attack_skill_activated) {
                         if (!cast_time) {
                             cast_time = living_agent->weapon_attack_speed * living_agent->attack_speed_modifier;
                         }
                         cast_time /= 2;
                     }
                 }
             }

             uint8_t priority = 0;

             if (target && target->agent_id == caster_id) {
                 priority += 2;
             }

             if (cast_time * 1000 > min_cast_time) {
                 if (skill) {
                     priority += PrioritizeActivation(skill->activation, skill->weapon_req);
                     priority += PrioritizeClass(skill->profession);
                     priority += PrioritizeCost(skill->energy_cost, skill->adrenaline);
                     priority += PrioritizeRecharge(skill->recharge);
                     if (skill->IsElite())
                         priority += 1;

                     // if (priority <= 5)
                     //     return;
                 }

                 SkillInfo skill_info = {static_cast<GW::Constants::SkillID>(value), skill_type, caster_id, cast_start, static_cast<uint32_t>(cast_time * 1000), priority};
                 active_skills.insert_or_assign(caster_id, skill_info);
                 Log::Info("Skill found with cast_time of %f", cast_time);
             }
             else {
                 Log::Info("Cast time too slow %f, %d", cast_time, min_cast_time);
             }
             break;
         }
         case GenericValueID::skill_stopped:
         case GenericValueID::skill_finished:
         case GenericValueID::attack_skill_stopped:
         case GenericValueID::attack_skill_finished: {
             const auto agent = GW::Agents::GetAgentByID(caster_id);
             const GW::AgentLiving* living_agent = agent ? agent->GetAsAgentLiving() : nullptr;

             if (!(living_agent && living_agent->allegiance == GW::Constants::Allegiance::Enemy))
                 return;

             active_skills.erase(caster_id);
             break;
         }
         case GenericValueID::add_effect: {
             if (value == static_cast<uint32_t>(GW::Constants::EffectID::dazed)) {
                 dazed_agents.insert(caster_id);
             }
             break;
         }
         case GenericValueID::remove_effect: {
             if (value == static_cast<uint32_t>(GW::Constants::EffectID::dazed)) {
                 dazed_agents.erase(caster_id);
             }
             break;
         }
         case GenericValueID::melee_attack_finished: {
             const GW::Agent* player = GW::Agents::GetPlayer();
             if (!player)
                 return;
             if (player->agent_id == caster_id)
                 attack_timer = TIMER_INIT();
             break;
         }
         case GenericValueID::attack_started: {
             const GW::Agent* player = GW::Agents::GetPlayer();
             if (!player)
                 return;
             if (player->agent_id == caster_id)
                 skill_wait_timer = TIMER_INIT();
             break;
         }
         default: return;
     }
 }

 void IWindow::OnServerPing(GW::HookStatus*, void* packet)
 {
     uint32_t* packet_as_uint_array = (uint32_t*)packet;
     uint32_t packetPing = packet_as_uint_array[1];
     if (packetPing > 4999)
         return; // GW checks this too.
     ping = packetPing;
 }

 float IWindow::SkillRangeToDistance(int i)
 {
     float range = GW::Constants::SqrRange::Spellcast;
     if (i == 0) {
         range = GW::Constants::SqrRange::Adjacent;
     }
     else if (i == 1) {
         range = (GW::Constants::Range::Spellcast / 2) * (GW::Constants::Range::Spellcast / 2);
     }
     return range;
 }

void IWindow::Draw(IDirect3DDevice9* pDevice)
{
    UNREFERENCED_PARAMETER(pDevice);
    if (!visible)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
        ImGui::PushStyleColor(ImGuiCol_Text, mainSetting.enabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1));
        if (ImGui::Button(mainSetting.enabled ? "Enabled###interrupttoggle" : "Disabled###interrupttoggle", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            ToggleEnable();
        }
        ImGui::PopStyleColor();
         ImGui::InputInt("Minimum Reaction MS##minreactiontime", &min_reaction_time);
         ImGui::SameLine();
         ImGui::Checkbox("Lock on", &lock_on);

         static const char* range_options[] = {
             "Touch",
             "Half",
             "Caster",
         };
         for (unsigned int i = 0; i < 8; ++i) {
             ImGui::Checkbox(("Skill " + std::to_string(i + 1)).c_str(), &interruptSkills[i].enabled);
             ImGui::SameLine();
             ImGui::Combo(("##interruptrange" + std::to_string(i + 1)).c_str(), &interruptSkills[i].range, range_options, 3);
             ImGui::SameLine();
             ImGui::Checkbox(("##attacks" + std::to_string(i + 1)).c_str(), &interruptSkills[i].attacks);
             ImGui::SameLine();
             ImGui::Checkbox(("##chants" + std::to_string(i + 1)).c_str(), &interruptSkills[i].chants);
             ImGui::SameLine();
             ImGui::Checkbox(("##spells" + std::to_string(i + 1)).c_str(), &interruptSkills[i].spells);
             ImGui::SameLine();
             ImGui::Checkbox(("##other" + std::to_string(i + 1)).c_str(), &interruptSkills[i].other);
         }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

 uint8_t IWindow::PrioritizeActivation(float activation, uint32_t weapon_req)
 {
     if (activation < .5) {
         if (activation == 0 && weapon_req > 0) {
             return 2;
         }
         return 0;
     }
     else if (activation <= 1.0) {
         return 1;
     }
     else if (activation <= 2.0) {
         return 2;
     }
     else {
         return 3;
     }
 }

 uint8_t IWindow::PrioritizeClass(uint8_t profession)
 {
     switch (profession) {
         case 3:
         case 4:
         case 8: return 3;
         case 5:
         case 6: return 2;
         case 7:
         case 2:
         case 10: return 1;
         default: return 0;
     }
 }

 uint8_t IWindow::PrioritizeCost(uint8_t energy_cost, uint32_t adrenaline)
 {
     if (energy_cost > 15) {
         return 3;
     }
     else if (energy_cost > 10) {
         return 2;
     }
     else if (energy_cost > 5) {
         return 1;
     }
     else {
         if (adrenaline == 0) {
             return 0;
         }
         else if (adrenaline <= 75) {
             return 2;
         }
         else if (adrenaline <= 150) {
             return 4;
         }
         else {
             return 6;
         }
     }
 }

 uint8_t IWindow::PrioritizeRecharge(uint32_t recharge)
 {
     if (recharge <= 3) {
         return 0;
     }
     else if (recharge <= 12) {
         return 2;
     }
     else if (recharge <= 25) {
         return 3;
     }
     else {
         return 5;
     }
 }

 int IWindow::GetValidInterrupt(SkillSetting skill_setting, GW::Constants::SkillType skill_type)
 {
     InterruptType type = skill_type_map[skill_type];
     switch (type) {
         case InterruptType::SPELL: return skill_setting.spells;
         case InterruptType::ATTACK: return skill_setting.attacks;
         case InterruptType::CHANT: return skill_setting.chants;
         default: return skill_setting.other;
     }
 }

 bool IWindow::CheckForDaze(GW::Constants::SkillType skill_type, const uint32_t caster_id)
 {
     if (skill_type_map[skill_type] == 0 && dazed_agents.contains(caster_id))
         return true;

     return false;
 }

 void IWindow::ClearCurrentInterruptSkill()
 {
     current_interrupt_skill.interruptIndex = std::nullopt;
     current_interrupt_skill.cast_time = 0;
     current_interrupt_skill.target_id = 0;
     current_interrupt_skill.adrenaline = false;
 }

 void IWindow::SetCurrentInterruptSkill(int idx, int rupt_time, uint32_t caster_id, uint32_t adrenaline)
 {
     current_interrupt_skill.interruptIndex = idx;
     current_interrupt_skill.cast_time = rupt_time;
     current_interrupt_skill.target_id = caster_id;
     current_interrupt_skill.adrenaline = !!adrenaline;
 }

 void IWindow::BeginCastingCurrentInterruptSkill(int idx, uint32_t caster_id) { GW::SkillbarMgr::UseSkill(idx, caster_id); }

 void IWindow::ResetReaction()
 {
     reaction_time = 0;
     reaction_timer = 0;
     waiting_reaction = false;
     active_skills.erase(current_interrupt_skill.target_id);
     ClearCurrentInterruptSkill();
 }

 uint16_t IWindow::ItemTypeToWeaponType(uint8_t item_type)
 {
     switch (item_type) {
         case 2: return 2;
         case 5: return 1;
         case 15: return 3;
         case 27: return 7;
         case 32: return 4;
         case 35: return 5;
         case 36: return 6;
         default: return 0;
     }
 }

 uint32_t IWindow::WeaponTypeToWeaponReq(uint16_t weapon_type)
 {
     switch (weapon_type) {
         case 3: return 16;
         case 4: return 8;
         default: return static_cast<uint32_t>(pow(2, weapon_type));
     }
 }

 //bool IWindow::FindAndEquipWeaponSet(uint32_t weapon_req)
 //{
 //    for (uint32_t idx = 0; idx < 4; idx++) {
 //        if (weapon_sets[idx] && (WeaponTypeToWeaponReq(ItemTypeToWeaponType(weapon_sets[idx]->weapon->type)) & weapon_req)) {
 //            GW::GameThread::Enqueue([&]() {
 //                GW::UI::Keypress(static_cast<GW::UI::ControlAction>(idx + 0x81));
 //            });
 //            return true;
 //        }
 //    }
 //    return false;
 //}

 float IWindow::ExpertiseToSkillReduction(GW::Skill* skill)
 {
     if (skill->profession == static_cast<uint8_t>(GW::Constants::Profession::Ranger) || skill->type == GW::Constants::SkillType::Attack || skill->type == GW::Constants::SkillType::Ritual) {
         return expertise * .04f;
     }
     else {
         return 0.0f;
     }
 }
