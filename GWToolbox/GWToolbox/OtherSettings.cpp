#include "OtherSettings.h"

#include <GWCA\Managers\ChatMgr.h>

#include "MainWindow.h"
#include "ChatLogger.h"

OtherSettings::OtherSettings() {
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D7C8,
		(BYTE*)"\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 16));
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D320, (BYTE*)"\xEB", 1));
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D33D, (BYTE*)"\xEB", 1));

	BYTE* a = (BYTE*)"\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90";
	patches.push_back(new GW::MemoryPatcher((void*)0x00669846, a, 10));
	patches.push_back(new GW::MemoryPatcher((void*)0x00669892, a, 10));
	patches.push_back(new GW::MemoryPatcher((void*)0x006698CE, a, 10));
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D5D6, a, 10));
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D622, a, 10));
	patches.push_back(new GW::MemoryPatcher((void*)0x0067D65E, a, 10));
}

void OtherSettings::DrawSettings() {
	ImGui::Checkbox("Freeze Widgets", &freeze_widgets);
	GuiUtils::ShowHelp("Widgets such as timer, health, minimap will not move");
	if (ImGui::Checkbox("Open web links from templates", &open_template_links)) {
		GW::Chat().SetOpenLinks(open_template_links);
	}

	if (ImGui::Checkbox("Borderless Window", &borderless_window)) {
		ApplyBorderless(borderless_window);
	}
}

void OtherSettings::LoadSettings(CSimpleIni* ini) {
	freeze_widgets = ini->GetBoolValue(MainWindow::IniSection(), "freeze_widgets", false);
	open_template_links = ini->GetBoolValue(MainWindow::IniSection(), "openlinks", true);
	borderless_window = ini->GetBoolValue(MainWindow::IniSection(), "borderlesswindow", false);

	ApplyBorderless(borderless_window);
	GW::Chat().SetOpenLinks(open_template_links);
}

void OtherSettings::SaveSettings(CSimpleIni* ini) const {
	ini->SetBoolValue(MainWindow::IniSection(), "freeze_widgets", freeze_widgets);
	ini->SetBoolValue(MainWindow::IniSection(), "openlinks", open_template_links);
	ini->SetBoolValue(MainWindow::IniSection(), "borderlesswindow", borderless_window);
}

void OtherSettings::ApplyBorderless(bool value) {
	DWORD current_style = GetWindowLong(GW::MemoryMgr::GetGWWindowHandle(), GWL_STYLE);

	if ((current_style & WS_POPUP) != 0) { // borderless or fullscreen
		if ((current_style & WS_MAXIMIZEBOX) == 0) { // fullscreen
			if (value) {
				ChatLogger::Log("Please enable Borderless while in Windowed mode");
				value = false;
			}
			return;
		}
	}

	DWORD style = value ? WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX | WS_CLIPSIBLINGS | WS_MAXIMIZEBOX
		: WS_SIZEBOX | WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPSIBLINGS;

	//printf("old 0x%X, new 0x%X\n", current_style, style);

	if (current_style != style) {
		for (GW::MemoryPatcher* patch : patches) patch->TooglePatch(value);

		SetWindowLong(GW::MemoryMgr::GetGWWindowHandle(), GWL_STYLE, style);

		if (value) {
			int width = GetSystemMetrics(SM_CXSCREEN);
			int height = GetSystemMetrics(SM_CYSCREEN);
			MoveWindow(GW::MemoryMgr::GetGWWindowHandle(), 0, 0, width, height, TRUE);
		} else {
			RECT size;
			SystemParametersInfoW(SPI_GETWORKAREA, 0, &size, 0);
			MoveWindow(GW::MemoryMgr::GetGWWindowHandle(), size.top, size.left, size.right, size.bottom, TRUE);
		}
	}
}