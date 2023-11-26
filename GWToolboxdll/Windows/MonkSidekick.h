#pragma once

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Constants/Skills.h>

#include "ToolboxWindow.h"
#include "SidekickWindow.h"
#include <Timer.h>

class MonkSidekick : public SidekickWindow {
    MonkSidekick() = default;
    ~MonkSidekick() = default;

public:
    static MonkSidekick& Instance()
    {
        static MonkSidekick instance;
        return instance;
    }

    const char* Name() const override { return "Monk"; }

private:
};
