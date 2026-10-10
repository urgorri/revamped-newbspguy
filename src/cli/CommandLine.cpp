#include "lang.h"
#include "CommandLine.h"
#include "log.h"
#include <filesystem>

#ifdef WIN32
#include <Windows.h>
#ifdef WIN_XP_86
#include <shellapi.h>
#endif
#endif

CommandLine::CommandLine(int argc, char* argv[])
{
	askingForHelp = false;
	for (int i = 0; i < argc; i++)
	{
		std::string arg = argv[i];
		std::string larg = toLowerCase(arg);

		if (i == 1)
		{
			command = larg;
		}
		if (i == 2)
		{
#ifdef WIN32
			int nArgs;
			LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
			if (szArglist)
			{
				bspfile = std::filesystem::path(szArglist[i]).string();
				LocalFree(szArglist);
			}
			else
			{
				bspfile = std::filesystem::path(argv[i]).string();
			}
#else
			bspfile = std::filesystem::path(argv[i]).string();
#endif
		}
		if (i > 2)
		{
			options.push_back(arg);
		}

		if ((i == 1 || i == 2) && (starts_with(larg, "help") || starts_with(larg, "/?") || starts_with(larg, "--help") || starts_with(larg, "-help") || starts_with(larg, "/help")))
		{
			askingForHelp = true;
		}
	}

	if (askingForHelp)
	{
		return;
	}

	for (int i = 0; i < (int)options.size(); i++)
	{
		std::string opt = toLowerCase(options[i]);

		if (i < (int)options.size() - 1)
		{
			optionVals[opt] = options[i + 1];
		}
		else
		{
			optionVals[opt].clear();
		}
	}

	if (argc == 2)
	{
#ifdef WIN32
		int nArgs;
		LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
		if (szArglist)
		{
			bspfile = std::filesystem::path(szArglist[1]).string();
			LocalFree(szArglist);
		}
		else
		{
			bspfile = std::filesystem::path(argv[1]).string();
		}
#else
		bspfile = std::filesystem::path(argv[1]).string();
#endif
	}
}

bool CommandLine::hasOption(const std::string& optionName)
{
	return optionVals.find(optionName) != optionVals.end();
}

bool CommandLine::hasOptionVector(const std::string& optionName)
{
	if (!hasOption(optionName))
		return false;

	std::string val = optionVals[optionName];
	std::vector<std::string> parts = splitString(val, ",");

	if (parts.size() != 3)
	{
		print_log(get_localized_string(LANG_0265), optionName);
		FlushConsoleLog(true);
		return false;
	}

	return true;
}

std::string CommandLine::getOption(const std::string& optionName)
{
	return optionVals[optionName];
}

int CommandLine::getOptionInt(const std::string& optionName)
{
	return str_to_int(optionVals[optionName]);
}

vec3 CommandLine::getOptionVector(const std::string& optionName)
{
	vec3 ret;
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	if (parts.size() != 3)
	{
		print_log(get_localized_string(LANG_1045), optionName);
		FlushConsoleLog(true);
		return ret;
	}

	ret.x = str_to_float(parts[0]);
	ret.y = str_to_float(parts[1]);
	ret.z = str_to_float(parts[2]);

	return ret;
}

std::vector<vec3> CommandLine::getOptionVectorList(const std::string& optionName)
{
	std::vector<vec3> ret;
	std::vector<std::string> parts = splitString(optionVals[optionName], ";");

	for (size_t i = 0; i < parts.size(); i++)
	{
		std::vector<std::string> vparts = splitString(parts[i], ",");
		if (vparts.size() == 3)
		{
			vec3 v;
			v.x = str_to_float(trimSpaces(vparts[0]));
			v.y = str_to_float(trimSpaces(vparts[1]));
			v.z = str_to_float(trimSpaces(vparts[2]));
			ret.push_back(v);
		}
	}

	return ret;
}

std::vector<std::string> CommandLine::getOptionList(const std::string& optionName)
{
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	for (size_t i = 0; i < parts.size(); i++)
	{
		parts[i] = trimSpaces(parts[i]);
	}

	return parts;
}

CommandLine g_cmdLine;