/*
 +----------------------------------------------------------------------+
 | UAlbertaBot                                                          |
 +----------------------------------------------------------------------+
 | University of Alberta - AIIDE StarCraft Competition                  |
 +----------------------------------------------------------------------+
 |                                                                      |
 +----------------------------------------------------------------------+
 | Author: David Churchill <dave.churchill@gmail.com>                   |
 +----------------------------------------------------------------------+
*/

#include "Common.h"
#include "UAlbertaBotModule.h"
#include "JSONTools.h"
#include "ParseUtils.h"
#include "UnitUtil.h"
#include "Global.h"
#include "StrategyManager.h"
#include "MapTools.h"

using namespace UAlbertaBot;

struct ReservedResources
{
	int minerals = 400;
	int gas = 0;
} reserved;

struct Strategy
{
	int stage = 0;
} strategy;

void reserveMinerals(int minerals)
{
	reserved.minerals += minerals;
}

int getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - reserved.minerals;
}

bool trainSCV()
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();
	for (const auto &unit : units)
	{
		const auto unitType = unit->getType();
		if (!unitType.isBuilding() || !unitType.isResourceDepot())
		{
			continue;
		}

		if (!unit->isIdle())
		{
			continue;
		}

		const auto workerType = unitType.getRace().getWorker();
		if (!unit->train(workerType))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		else
		{
			return true;
		}
		break;
	}
	return false;
}

bool trainMarine()
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();
	for (const auto &unit : units)
	{
		const auto unitType = unit->getType();
		if (!unitType.isBuilding() || unitType != BWAPI::UnitTypes::Terran_Barracks)
		{
			continue;
		}

		if (!unit->isIdle())
		{
			continue;
		}

		if (!unit->train(BWAPI::UnitTypes::Terran_Marine))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		else
		{
			return true;
		}
		break;
	}
	return false;
}

void buildBuilding(BWAPI::UnitType buildingType)
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();
	for (auto &unit : units)
	{
		auto unitType = unit->getType();
		if (!unitType.isWorker())
		{
			continue;
		}

		if (unit->isConstructing())
		{
			continue;
		}

		auto targetBuildLocation = BWAPI::Broodwar->getBuildLocation(buildingType, unit->getTilePosition());
		if (!unit->build(buildingType, targetBuildLocation))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		else
		{
			reserveMinerals(buildingType.mineralPrice());
		}
		break;
	}
}

std::map<BWAPI::UnitType, size_t> getUnitCount()
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();

	std::map<BWAPI::UnitType, size_t> counter;
	for (const auto &unit : units)
	{
		auto unitType = unit->getType();

		auto search = counter.find(unitType);
		if (search != counter.end())
		{
			search->second += 1;
		}
		else
		{
			counter[unitType] = 1;
		}
	}
	return counter;
}

UAlbertaBotModule::UAlbertaBotModule()
{
	Global::GameStart();
}

// This gets called when the bot starts!
void UAlbertaBotModule::onStart()
{
	// Parse the bot's configuration file if it has one, change this file path to where your config file is
	// Any relative path name will be relative to Starcraft installation folder
	ParseUtils::ParseConfigFile(Config::ConfigFile::ConfigFileLocation);

	// Set our BWAPI options here
	BWAPI::Broodwar->setLocalSpeed(Config::BWAPIOptions::SetLocalSpeed);
	BWAPI::Broodwar->setFrameSkip(Config::BWAPIOptions::SetFrameSkip);

	if (Config::BWAPIOptions::EnableCompleteMapInformation)
	{
		BWAPI::Broodwar->enableFlag(BWAPI::Flag::CompleteMapInformation);
	}

	if (Config::BWAPIOptions::EnableUserInput)
	{
		BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput);
	}

	if (Config::BotInfo::PrintInfoOnStart)
	{
		BWAPI::Broodwar->printf("Hello! I am %s, written by %s", Config::BotInfo::BotName.c_str(), Config::BotInfo::Authors.c_str());
	}

	// Call BWTA to read and analyze the current map
	if (Config::Modules::UsingGameCommander)
	{
		if (Config::Modules::UsingStrategyIO)
		{
			Global::Strategy().readResults();
			Global::Strategy().setLearnedStrategy();
		}
	}

	//Global::Map().saveMapToFile("map.txt");
}

void UAlbertaBotModule::onEnd(bool isWinner)
{
	if (Config::Modules::UsingGameCommander)
	{
		Global::Strategy().onEnd(isWinner);
	}
}

void UAlbertaBotModule::onFrame()
{
	if (BWAPI::Broodwar->getFrameCount() > 100000)
	{
		BWAPI::Broodwar->restartGame();
	}

	const char red = '\x08';
	const char green = '\x07';
	const char white = '\x04';

	if (!Config::ConfigFile::ConfigFileFound)
	{
		BWAPI::Broodwar->drawBoxScreen(0, 0, 450, 100, BWAPI::Colors::Black, true);
		BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
		BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Not Found", red, Config::BotInfo::BotName.c_str());
		BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
		BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without its configuration file", white, Config::BotInfo::BotName.c_str());
		BWAPI::Broodwar->drawTextScreen(10, 45, "%cCheck that the file below exists. Incomplete paths are relative to Starcraft directory", white);
		BWAPI::Broodwar->drawTextScreen(10, 60, "%cYou can change this file location in Config::ConfigFile::ConfigFileLocation", white);
		BWAPI::Broodwar->drawTextScreen(10, 75, "%cFile Not Found (or is empty): %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
		return;
	}
	else if (!Config::ConfigFile::ConfigFileParsed)
	{
		BWAPI::Broodwar->drawBoxScreen(0, 0, 450, 100, BWAPI::Colors::Black, true);
		BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
		BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Parse Error", red, Config::BotInfo::BotName.c_str());
		BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
		BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without a properly formatted configuration file", white, Config::BotInfo::BotName.c_str());
		BWAPI::Broodwar->drawTextScreen(10, 45, "%cThe configuration file was found, but could not be parsed. Check that it is valid JSON", white);
		BWAPI::Broodwar->drawTextScreen(10, 60, "%cFile Not Parsed: %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
		return;
	}

	// if (Config::Modules::UsingGameCommander)
	// {
	// 	m_gameCommander.update();
	// }

	// if (Config::Modules::UsingAutoObserver)
	// {
	// 	m_autoObserver.onFrame();
	// }

	const auto supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	const auto freeMinerals = getFreeMinerals();
	const auto supplyNeeded = 40;
	if (strategy.stage == 0 && supplyTotal < supplyNeeded && freeMinerals >= 100)
	{
		const auto supplyProviderType = BWAPI::Broodwar->self()->getRace().getSupplyProvider();
		buildBuilding(supplyProviderType);

		if (supplyTotal + supplyProviderType.supplyProvided() >= supplyNeeded)
		{
			strategy.stage++;
		}
		return;
	}

	std::map<BWAPI::UnitType, size_t> unitCount = getUnitCount();
	const auto &race = BWAPI::Broodwar->self()->getRace();
	if (strategy.stage == 1 && unitCount[race.getWorker()] < 10 && freeMinerals >= 50)
	{
		const auto trained = trainSCV();

		if (trained && unitCount[race.getWorker()] + 1 >= 10)
		{
			strategy.stage++;
		}
		return;
	}

	if (strategy.stage == 2 && unitCount[BWAPI::UnitTypes::Terran_Barracks] < 1 && freeMinerals >= 150)
	{
		buildBuilding(BWAPI::UnitTypes::Terran_Barracks);

		if (unitCount[BWAPI::UnitTypes::Terran_Barracks] + 1 >= 1)
		{
			strategy.stage++;
		}
		return;
	}

	if (strategy.stage == 3 && unitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && unitCount[BWAPI::UnitTypes::Terran_Marine] < 10 && freeMinerals >= 50)
	{
		const auto trained = trainMarine();

		if (trained && unitCount[BWAPI::UnitTypes::Terran_Marine] + 1 >= 10)
		{
			strategy.stage++;
		}
		return;
	}
}

void UAlbertaBotModule::onUnitDestroy(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitDestroy(unit);
	}
}

void UAlbertaBotModule::onUnitMorph(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitMorph(unit);
	}
}

void UAlbertaBotModule::onSendText(std::string text)
{
	ParseUtils::ParseTextCommand(text);
}

void UAlbertaBotModule::onUnitCreate(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitCreate(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType().isBuilding())
	{
		reserved.minerals -= unit->getType().mineralPrice();
		reserved.gas -= unit->getType().gasPrice();
	}
}

void UAlbertaBotModule::onUnitComplete(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitComplete(unit);
	}
}

void UAlbertaBotModule::onUnitShow(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitShow(unit);
	}
}

void UAlbertaBotModule::onUnitHide(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitHide(unit);
	}
}

void UAlbertaBotModule::onUnitRenegade(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitRenegade(unit);
	}
}
