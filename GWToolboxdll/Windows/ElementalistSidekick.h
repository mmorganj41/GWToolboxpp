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

private:
};
