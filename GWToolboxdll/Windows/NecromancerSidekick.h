#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class NecromancerSidekick : public SidekickWindow {
    NecromancerSidekick() = default;
    ~NecromancerSidekick() = default;

public:
    static NecromancerSidekick& Instance()
    {
        static NecromancerSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Necromancer"; }

private:

};
