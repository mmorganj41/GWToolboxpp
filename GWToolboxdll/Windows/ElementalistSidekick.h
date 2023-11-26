#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class ElementalistSidekick : public SidekickWindow {
    ElementalistSidekick() = default;
    ~ElementalistSidekick() = default;

public:
    static ElementalistSidekick& Instance()
    {
        static ElementalistSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Elementalist"; }

    bool UseCombatSkill() override;      // For using skills in combat
    bool SetUpCombatSkills(uint32_t called_target_id) override;   // For setting up skills for combat
private:
};
