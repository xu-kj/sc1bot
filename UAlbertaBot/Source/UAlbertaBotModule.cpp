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

std::map<BWAPI::UnitType, size_t> unitCount;

int getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - reserved.minerals;
}

int getFreeGas()
{
	return BWAPI::Broodwar->self()->gas() - reserved.gas;
}

bool canBuild(BWAPI::UnitType unitType)
{
	return unitType.mineralPrice() <= getFreeMinerals() && unitType.gasPrice() <= getFreeGas();
}

bool trainSCV()
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();
	const auto workerType = BWAPI::Broodwar->self()->getRace().getWorker();

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
	const auto marineType = BWAPI::UnitTypes::Terran_Marine;
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

		if (!unit->train(marineType))
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

void trainWorkers(const BWAPI::Race &race, const size_t wanted)
{
	const auto workerType = race.getWorker();
	if (unitCount[workerType] >= wanted || !canBuild(workerType))
	{
		return;
	}

	const auto trained = trainSCV();
}

void trainMarines(const size_t wanted)
{
	const auto barrackType = BWAPI::UnitTypes::Terran_Barracks;
	const auto marineType = BWAPI::UnitTypes::Terran_Marine;

	if (unitCount[barrackType] <= 0 || unitCount[marineType] >= wanted || !canBuild(marineType))
	{
		return;
	}

	const auto trained = trainMarine();
}

std::vector<std::pair<BWAPI::UnitType, BWAPI::TilePosition>> reservedBuildings;

void buildBuilding(BWAPI::UnitType buildingType)
{
	const auto &units = BWAPI::Broodwar->self()->getUnits();
	for (auto &unit : units)
	{
		auto unitType = unit->getType();
		// get workers
		if (!unitType.isWorker())
		{
			continue;
		}

		// not builders
		if (unit->isConstructing())
		{
			continue;
		}

		// not gas gatherer
		if (unit->isGatheringGas())
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
			reservedBuildings.push_back({buildingType, targetBuildLocation});

			reserved.minerals += buildingType.mineralPrice();
			reserved.gas += buildingType.gasPrice();
		}
		break;
	}
}

void buildSupplyDepot(const BWAPI::Race &race)
{
	const int supplyUsed = BWAPI::Broodwar->self()->supplyUsed(race);
	const int supplyTotal = BWAPI::Broodwar->self()->supplyTotal(race);

	const double usage = static_cast<double>(supplyUsed) / static_cast<double>(supplyTotal);

	const auto supplyProviderType = race.getSupplyProvider();
	if (usage < 0.8 || !canBuild(supplyProviderType))
	{
		return;
	}

	buildBuilding(supplyProviderType);
}

void buildBarrack(const BWAPI::Race &race, const size_t wanted)
{
	const auto barrackType = BWAPI::UnitTypes::Terran_Barracks;
	if (unitCount[barrackType] >= wanted || !canBuild(barrackType))
	{
		return;
	}

	buildBuilding(BWAPI::UnitTypes::Terran_Barracks);
}

void buildRefinery(const BWAPI::Race &race, const BWAPI::TilePosition &location)
{
	const auto refineryType = race.getRefinery();
	const auto refineriesWanted = 1;

	if (unitCount[refineryType] >= refineriesWanted || !canBuild(refineryType))
	{
		return;
	}

	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(location.x, location.y), [](BWAPI::Unit unit) {
		return unit->getType().isResourceDepot();
	});
	if (pMain)
	{
		const auto geysers = pMain->getUnitsInRadius(1024, [](BWAPI::Unit unit) {
			return unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser;
		});

		auto workers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsOwned);
		for (const auto &geyser : geysers)
		{
			if (workers.empty())
			{
				break;
			}

			auto worker = *workers.begin();
			worker->build(refineryType, geyser->getTilePosition());
			workers.erase(workers.begin());
		}
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

void gatherMinerals(const BWAPI::TilePosition &location)
{
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(location.x, location.y), [](BWAPI::Unit unit) {
		return unit->getType().isResourceDepot();
	});
	if (pMain)
	{
		BWAPI::Unitset minerals = pMain->getUnitsInRadius(1024, [](BWAPI::Unit unit) {
			return unit->getType().isMineralField();
		});
		if (!minerals.empty())
		{
			BWAPI::Unitset workers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsIdle && BWAPI::Filter::IsOwned);
			for (auto u = minerals.begin(); u != minerals.end() && !workers.empty(); ++u)
			{
				auto worker = *workers.begin();
				worker->gather(*minerals.begin()); // could change to rotate to mine from different mineral fields
				workers.erase(workers.begin());
			}
		}
	}
}

void gatherGas(const BWAPI::TilePosition &location, const size_t wantedWorkersForEachGeyser)
{
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(location.x, location.y), [](BWAPI::Unit unit) {
		return unit->getType().isResourceDepot();
	});
	if (pMain)
	{
		BWAPI::Unitset refineries = pMain->getUnitsInRadius(1024, [](BWAPI::Unit unit) {
			return unit->getType().isRefinery() && unit->isCompleted();
		});
		if (!refineries.empty())
		{
			std::vector<BWAPI::Unit> mineralGatherers;
			size_t gasGatherer = 0;
			for (const auto &unit : BWAPI::Broodwar->self()->getUnits())
			{
				const auto unitType = unit->getType();
				if (!unitType.isWorker())
				{
					continue;
				}
				if (unit->isGatheringMinerals())
				{
					mineralGatherers.push_back(unit);
				}
				if (unit->isGatheringGas())
				{
					gasGatherer++;
				}
			}

			if (gasGatherer >= wantedWorkersForEachGeyser * refineries.size())
			{
				return;
			}

			for (auto u = refineries.begin(); u != refineries.end() && !mineralGatherers.empty(); ++u)
			{
				auto worker = *mineralGatherers.begin();
				worker->gather(*u);
				mineralGatherers.erase(mineralGatherers.begin());
			}
		}
	}
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

	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.update();
	}

	if (Config::Modules::UsingAutoObserver)
	{
		m_autoObserver.onFrame();
	}

	BWAPI::Broodwar->drawTextScreen(10, 5, "%cFree minerals: %d, Free gas: %d", red, getFreeMinerals(), getFreeGas());
	for (const auto &building : reservedBuildings)
	{
		const auto leftTop = BWAPI::Position(building.second);
		BWAPI::Broodwar->drawBoxMap(
			leftTop.x, leftTop.y,
			leftTop.x + building.first.width(), leftTop.y + building.first.height(),
			BWAPI::Colors::Green);
	}

	const auto &self = BWAPI::Broodwar->self();
	unitCount = getUnitCount();

	// assign building orders first since these would generate demand, e.g.:
	// * need to train marine/scv/..., need x amount of minerals + building
	// * need to research ..., need x amount of gas
	// * ...

	// building orders
	trainWorkers(self->getRace(), 20);
	trainMarines(180);

	// worker orders
	buildSupplyDepot(self->getRace());						  // building
	buildBarrack(self->getRace(), 2);						  // building
	buildRefinery(self->getRace(), self->getStartLocation()); // building
	gatherGas(self->getStartLocation(), 3);
	gatherMinerals(self->getStartLocation());
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
