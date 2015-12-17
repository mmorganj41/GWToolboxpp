#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

class ChatCommands {
	typedef std::function<void(std::vector<std::wstring>)> Handler_t;

public:
	ChatCommands();

private:
	void AddCommand(std::wstring cmd, Handler_t);
	
	static std::wstring GetLowerCaseArg(std::vector<std::wstring>, int index);

	static void CmdAge2(std::vector<std::wstring>);
	static void CmdPcons(std::vector<std::wstring> args);
	static void CmdDialog(std::vector<std::wstring> args);
	static void CmdTB(std::vector<std::wstring> args);
	static void CmdTP(std::vector<std::wstring> args);
};
