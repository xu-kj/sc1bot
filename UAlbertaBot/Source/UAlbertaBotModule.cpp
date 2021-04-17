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

UAlbertaBotModule::UAlbertaBotModule()
{
	Global::GameStart();
}

void sendMineralWorker() {
	// Get main building closest to start location.
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (pMain) // check if pMain is valid
	{
		// Get sets of resources and workers
		BWAPI::Unitset myResources = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsMineralField);
		if (!myResources.empty()) // check if we have resources nearby
		{
			BWAPI::Unitset myWorkers = pMain->getUnitsInRadius(512, BWAPI::Filter::IsWorker && BWAPI::Filter::IsIdle && BWAPI::Filter::IsOwned);
			while (!myWorkers.empty()) // make sure we command all nearby idle workers, if any
			{
				for (auto u = myResources.begin(); u != myResources.end() && !myWorkers.empty(); ++u)
				{
					auto worker = *myWorkers.begin();
					worker->gather(*u);
					myWorkers.erase(myWorkers.begin());
				}
			}
		} // myResources not empty
	} // pMain != nullptr
}

void sendGasWorker() {
	// Get main building closest to start location.
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (pMain) // check if pMain is valid
	{
		// Get sets of resources and workers
		BWAPI::Unitset myGasRefinery = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsRefinery && BWAPI::Filter::IsCompleted);
		if (!myGasRefinery.empty()) // check if we have resources nearby
		{
			BWAPI::Unitset myWorkers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsOwned);
			int gasingWorkerCount = 0;
			for (auto w : myWorkers) {
				if (w->isGatheringGas()) {
					gasingWorkerCount += 1;
				}
			}
			auto refinery = *(myGasRefinery.begin());
			while (gasingWorkerCount > 2) {
				for (auto w : myWorkers) {
					if (w->isGatheringGas()) {
						w->stop();
						gasingWorkerCount -= 1;
						break;
					}
				}
			}

			while (gasingWorkerCount < 2 && !myWorkers.empty())
			{
				auto worker = *myWorkers.begin();
				worker->gather(refinery);
				myWorkers.erase(myWorkers.begin());
				gasingWorkerCount += 1;
			}
		} // myResources not empty
	} // pMain != nullptr
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

	sendMineralWorker();

	//Global::Map().saveMapToFile("map.txt");
}

void UAlbertaBotModule::onEnd(bool isWinner)
{
	if (Config::Modules::UsingGameCommander)
	{
		Global::Strategy().onEnd(isWinner);
	}
}

struct State
{
	int reservedMinerals = 400;
	int reservedGas = 0;
	int scv = 0;
	int marine = 0;
} globalstate;

void reserveMinerals(int minerals)
{
	globalstate.reservedMinerals += minerals;
}

int getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - globalstate.reservedMinerals;
}

// State globalstate;

std::map<BWAPI::UnitType, size_t> getUnitCount()
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();

	std::map<BWAPI::UnitType, size_t> counter;
	for (const auto& unit : units)
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

void trainSCV()
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	for (const auto& unit : units)
	{
		auto unitType = unit->getType();
		auto t1 = unitType.isBuilding();
		auto t2 = unitType.isResourceDepot();
		if (!unitType.isResourceDepot())
		{
			continue;
		}

		const auto workerType = unitType.getRace().getWorker();
		if (unit->isIdle() && !unit->train(workerType))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		break;
	}
}

void trainMarine()
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	for (const auto& unit : units)
	{
		auto unitType = unit->getType();
		auto isBuilding = unitType.isBuilding();
		if (unitType != BWAPI::UnitTypes::Terran_Barracks)
		{
			continue;
		}

		if (unit->isIdle() && !unit->train(BWAPI::UnitTypes::Terran_Marine))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		break;
	}
}

void buildBuilding(BWAPI::UnitType buildingType)
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	for (auto& unit : units)
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

		if (unit->isGatheringGas()) {
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

void buildSupplyDepot()
{
	const auto supplyProviderType = BWAPI::Broodwar->self()->getRace().getSupplyProvider();
	buildBuilding(supplyProviderType);
}

void buildBarracks()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Barracks);
}

void buildRefinery() {
	buildBuilding(BWAPI::UnitTypes::Terran_Refinery);
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

	// Get main building closest to start location.
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (pMain) // check if pMain is valid
	{
		BWAPI::Unitset myWorkers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsOwned);
		int gasingWorkerCount = 0;
		for (auto w : myWorkers) {
			if (w->isGatheringGas()) {
				BWAPI::Broodwar->drawCircleMap(w->getPosition().x, w->getPosition().y, 2, BWAPI::Colors::Green);
			}
		}
	}

	sendMineralWorker();
	sendGasWorker();
	if (BWAPI::Broodwar->self()->supplyTotal() < 40 && getFreeMinerals() >= 100)
	{
		int m = getFreeMinerals();
		int t = BWAPI::Broodwar->self()->supplyTotal();
		buildSupplyDepot();
		return;
	}

	std::map<BWAPI::UnitType, size_t> unitCount = getUnitCount();
	if (unitCount[BWAPI::Broodwar->self()->getRace().getWorker()] < 10 && getFreeMinerals() >= 50)
	{
		trainSCV();
		return;
	}

	if (unitCount[BWAPI::UnitTypes::Terran_Refinery] < 1 && getFreeMinerals() >= 75) {
		buildRefinery();
		return;
	}

	if (unitCount[BWAPI::UnitTypes::Terran_Barracks] < 1 && getFreeMinerals() >= 150)
	{
		buildBarracks();
		return;
	}

	if (unitCount[BWAPI::UnitTypes::Terran_Barracks] > 0 && unitCount[BWAPI::UnitTypes::Terran_Marine] < 10 && getFreeMinerals() >= 50)
	{
		trainMarine();
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
		globalstate.reservedMinerals -= unit->getType().mineralPrice();
		globalstate.reservedGas -= unit->getType().gasPrice();
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
