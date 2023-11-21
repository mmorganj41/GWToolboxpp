#include "stdafx.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>
#include <GWCA/Constants/Skills.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>

#include <Defines.h>
#include <Utils/GuiUtils.h>
#include <Modules/Resources.h>
#include <Widgets/EnergyWidget.h>

constexpr const wchar_t* HEALTH_THRESHOLD_INIFILENAME = L"HealthThreshold.ini";

void EnergyWidget::LoadSettings(ToolboxIni* ini)
{
    ToolboxWidget::LoadSettings(ini);
    LOAD_BOOL(click_to_print_energy);
    LOAD_BOOL(hide_in_outpost);
    LOAD_BOOL(show_abs_value);
    LOAD_BOOL(show_perc_value);

    if (inifile == nullptr) {
        inifile = new ToolboxIni();
    }
    ASSERT(inifile->LoadIfExists(Resources::GetSettingFile(HEALTH_THRESHOLD_INIFILENAME)) == SI_OK);

    ToolboxIni::TNamesDepend entries;
    inifile->GetAllSections(entries);

    for (const ToolboxIni::Entry& entry : entries) {
        auto threshold = new Threshold(inifile, entry.pItem);
        threshold->index = thresholds.size();
        thresholds.push_back(threshold);
    }

    if (thresholds.empty()) {
        const auto thresholdFh = new Threshold("\"Finish Him!\"", Colors::RGB(255, 255, 0), 50);
        thresholdFh->skillId = static_cast<int>(GW::Constants::SkillID::Finish_Him);
        thresholdFh->active = false;
        thresholds.push_back(thresholdFh);
        thresholds.back()->index = thresholds.size() - 1;

        const auto thresholdEoe = new Threshold("Edge of Extinction", Colors::RGB(0, 255, 0), 90);
        thresholdEoe->active = false;
        thresholds.push_back(thresholdEoe);
        thresholds.back()->index = thresholds.size() - 1;
    }
}

void EnergyWidget::SaveSettings(ToolboxIni* ini)
{
    ToolboxWidget::SaveSettings(ini);
    SAVE_BOOL(click_to_print_energy);
    SAVE_BOOL(hide_in_outpost);
    SAVE_BOOL(show_abs_value);
    SAVE_BOOL(show_perc_value);

    if (thresholds_changed && inifile) {
        inifile->Reset();

        constexpr size_t buffer_size = 32;
        char buf[buffer_size];
        for (size_t i = 0; i < thresholds.size(); i++) {
            snprintf(buf, sizeof(buf), "threshold%03zu", i);
            thresholds[i]->SaveSettings(inifile, buf);
        }

        ASSERT(inifile->SaveFile(Resources::GetSettingFile(HEALTH_THRESHOLD_INIFILENAME).c_str()) == SI_OK);
        thresholds_changed = false;
    }
}

void EnergyWidget::DrawSettingsInternal()
{
    ToolboxWidget::DrawSettingsInternal();
    ImGui::SameLine();
    ImGui::Checkbox("Hide in outpost", &hide_in_outpost);
    ImGui::SameLine();
    ImGui::Checkbox("Show absolute value", &show_abs_value);
    ImGui::SameLine();
    ImGui::Checkbox("Show percentage value", &show_perc_value);
    ImGui::Checkbox("Ctrl+Click to print target energy", &click_to_print_energy);

    const bool thresholdsNode = ImGui::TreeNodeEx("Thresholds", ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The first matching threshold will be used.");
    }
    if (thresholdsNode) {
        bool changed = false;
        for (size_t i = 0; i < thresholds.size(); i++) {
            Threshold* threshold = thresholds[i];

            if (!threshold) {
                continue;
            }

            ImGui::PushID(static_cast<int>(threshold->ui_id));

            auto op = Threshold::Operation::None;
            changed |= threshold->DrawSettings(op);

            switch (op) {
                case Threshold::Operation::None:
                    break;
                case Threshold::Operation::MoveUp:
                    if (i > 0) {
                        std::swap(thresholds[i], thresholds[i - 1]);
                    }
                    break;
                case Threshold::Operation::MoveDown:
                    if (i + 1 < thresholds.size()) {
                        std::swap(thresholds[i], thresholds[i + 1]);
                    }
                    break;
                case Threshold::Operation::Delete:
                    thresholds.erase(thresholds.begin() + static_cast<int>(i));
                    delete threshold;
                    threshold = nullptr;
                    --i;
                    break;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add Threshold")) {
            thresholds.push_back(new Threshold("<name>", 0xFFFFFFFF, 0));
            thresholds.back()->index = thresholds.size() - 1;
            changed = true;
        }

        ImGui::TreePop();

        if (changed) {
            thresholds_changed = true;
        }
    }
}

void EnergyWidget::Draw(IDirect3DDevice9*)
{
    if (!visible) {
        return;
    }
    if (hide_in_outpost && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowSize(ImVec2(150, 100), ImGuiCond_FirstUseEver);
    const bool ctrl_pressed = ImGui::IsKeyDown(ImGuiKey_ModCtrl);
    if (ImGui::Begin(Name(), nullptr, GetWinFlags(0, !(ctrl_pressed && click_to_print_energy)))) {
        constexpr size_t buffer_size = 32;
        static char energy_perc[buffer_size];
        static char energy_abs[buffer_size];
        const uint32_t target_id = GW::Agents::GetTargetId();
        const GW::MapAgent* target = GW::Agents::GetMapAgentByID(target_id);
        if (target) {
            if (show_perc_value) {
                if (target->cur_energy >= 0.0f) {
                    snprintf(energy_perc, buffer_size, "%.0f %%", target->cur_energy / target->max_energy * 100.0f);
                }
                else {
                    snprintf(energy_perc, buffer_size, "-");
                }
            }
            if (show_abs_value) {
                if (target->max_energy > 0.0f) {
                    const float abs = target->cur_energy;
                    snprintf(energy_abs, buffer_size, "%.0f / %.0f", abs, target->max_energy);
                }
                else {
                    snprintf(energy_abs, buffer_size, "-");
                }
            }

            ImColor color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const auto background = ImColor(Colors::Black());

            for (size_t i = 0; i < thresholds.size(); i++) {
                Threshold* threshold = thresholds[i];

                if (!threshold) {
                    continue;
                }
                if (!threshold->active) {
                    continue;
                }
                if (threshold->skillId) {
                    GW::Skillbar* skillbar = GW::SkillbarMgr::GetPlayerSkillbar();
                    if (!(skillbar && skillbar->IsValid())) {
                        continue;
                    }
                    const GW::SkillbarSkill* skill = skillbar->GetSkillById(static_cast<GW::Constants::SkillID>(threshold->skillId));
                    if (!skill) {
                        continue;
                    }
                }
                if (threshold->mapId) {
                    if (static_cast<GW::Constants::MapID>(threshold->mapId) != GW::Map::GetMapID()) {
                        continue;
                    }
                }

                if (target->cur_energy / target->max_energy * 100.0f < threshold->value) {
                    color = ImColor(threshold->color);
                    break;
                }
            }

            // 'health'
            ImGui::PushFont(GetFont(GuiUtils::FontSize::header1));
            ImVec2 cur = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cur.x + 1, cur.y + 1));
            ImGui::TextColored(background, "Energy");
            ImGui::SetCursorPos(cur);
            ImGui::Text("Energy");
            ImGui::PopFont();

            // perc
            if (show_perc_value) {
                ImGui::PushFont(GetFont(GuiUtils::FontSize::widget_small));
                cur = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cur.x + 2, cur.y + 2));
                ImGui::TextColored(background, "%s", energy_perc);
                ImGui::SetCursorPos(cur);
                ImGui::TextColored(color, "%s", energy_abs);
                ImGui::PopFont();
            }

            // abs
            if (show_abs_value) {
                ImGui::PushFont(GetFont(GuiUtils::FontSize::widget_label));
                cur = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cur.x + 2, cur.y + 2));
                ImGui::TextColored(background, energy_abs);
                ImGui::SetCursorPos(cur);
                ImGui::Text(energy_abs);
                ImGui::PopFont();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

unsigned int EnergyWidget::Threshold::cur_ui_id = 0;

EnergyWidget::Threshold::Threshold(const ToolboxIni* ini, const char* section)
    : ui_id(++cur_ui_id)
{
    active = ini->GetBoolValue(section, VAR_NAME(active));
    GuiUtils::StrCopy(name, ini->GetValue(section, VAR_NAME(name), ""), sizeof(name));
    modelId = ini->GetLongValue(section, VAR_NAME(modelId), modelId);
    skillId = ini->GetLongValue(section, VAR_NAME(skillId), skillId);
    mapId = ini->GetLongValue(section, VAR_NAME(mapId), mapId);
    value = ini->GetLongValue(section, VAR_NAME(value), value);
    color = Colors::Load(ini, section, VAR_NAME(color), color);
}

EnergyWidget::Threshold::Threshold(const char* _name, const Color _color, const int _value)
    : ui_id(++cur_ui_id)
    , value(_value)
    , color(_color)
{
    GuiUtils::StrCopy(name, _name, sizeof(name));
}

bool EnergyWidget::Threshold::DrawHeader()
{
    constexpr size_t buffer_size = 64;
    char mapbuf[buffer_size] = {'\0'};
    if (mapId) {
        if (mapId < sizeof(GW::Constants::NAME_FROM_ID) / sizeof(*GW::Constants::NAME_FROM_ID)) {
            snprintf(mapbuf, buffer_size, "[%s]", GW::Constants::NAME_FROM_ID[mapId]);
        }
        else {
            snprintf(mapbuf, buffer_size, "[Map %d]", mapId);
        }
    }

    ImGui::SameLine(0, 18);
    const bool changed = ImGui::Checkbox("##active", &active);
    ImGui::SameLine();
    ImGui::ColorButton("", ImColor(color));
    ImGui::SameLine();
    ImGui::Text("%s (<%d%%) %s", name, value, mapbuf);
    return changed;
}

bool EnergyWidget::Threshold::DrawSettings(Operation& op)
{
    bool changed = false;

    if (ImGui::TreeNodeEx("##params", ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap)) {
        changed |= DrawHeader();

        ImGui::PushID(static_cast<int>(ui_id));

        changed |= ImGui::InputText("Name", name, 128);
        ImGui::ShowHelp("A name to help you remember what this is. Optional.");
        changed |= ImGui::InputInt("Model ID", &modelId);
        ImGui::ShowHelp("The Agent to which this threshold will be applied. Optional. Leave 0 for any agent");
        changed |= ImGui::InputInt("Skill ID", &skillId);
        ImGui::ShowHelp("Only apply if this skill is on your bar. Optional. Leave 0 for any skills");
        changed |= ImGui::InputInt("Map ID", &mapId);
        ImGui::ShowHelp("The map where it will be applied. Optional. Leave 0 for any map");
        changed |= ImGui::InputInt("Percentage", &value);
        ImGui::ShowHelp("Percentage below which this color should be used");
        changed |= Colors::DrawSettingHueWheel("Color", &color, 0);
        ImGui::ShowHelp("The custom color for this threshold.");

        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float width = (ImGui::CalcItemWidth() - spacing * 2) / 3;
        if (ImGui::Button("Move Up", ImVec2(width, 0))) {
            op = Operation::MoveUp;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move the threshold up in the list");
        }
        ImGui::SameLine(0, spacing);
        if (ImGui::Button("Move Down", ImVec2(width, 0))) {
            op = Operation::MoveDown;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move the threshold down in the list");
        }
        ImGui::SameLine(0, spacing);
        if (ImGui::Button("Delete", ImVec2(width, 0))) {
            op = Operation::Delete;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete the threshold");
        }

        ImGui::TreePop();
        ImGui::PopID();
    }
    else {
        changed |= DrawHeader();
    }

    return changed;
}

void EnergyWidget::Threshold::SaveSettings(ToolboxIni* ini, const char* section) const
{
    ini->SetBoolValue(section, VAR_NAME(active), active);
    ini->SetValue(section, VAR_NAME(name), name);
    ini->SetLongValue(section, VAR_NAME(modelId), modelId);
    ini->SetLongValue(section, VAR_NAME(skillId), skillId);
    ini->SetLongValue(section, VAR_NAME(mapId), mapId);

    ini->SetLongValue(section, VAR_NAME(value), value);
    Colors::Save(ini, section, VAR_NAME(color), color);
}
