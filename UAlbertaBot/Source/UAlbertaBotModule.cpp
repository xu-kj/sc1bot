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
#include "Micro.h"
#include "UAlbertaBotModule.h"
#include "JSONTools.h"
#include "ParseUtils.h"
#include "UnitUtil.h"
#include "Global.h"
#include "StrategyManager.h"
#include "MapTools.h"

using namespace UAlbertaBot;

struct State
{
	int reservedMinerals = 400;
	int reservedGas = 0;
	int scv = 0;
	int marine = 0;
	int miningMinerals = 0;
	int miningGas = 0;
} globalstate;

enum Resource {gas, mineral};
std::unordered_set<BWAPI::Unit> marineSquad;
BWAPI::Position enemyLocation;

void reserveMinerals(int minerals)
{
	globalstate.reservedMinerals += minerals;
}

int getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - globalstate.reservedMinerals;
}

int getFreeGas()
{
	return BWAPI::Broodwar->self()->gas() - globalstate.reservedGas;
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

BWAPI::Unit getClosestRefinery(BWAPI::TilePosition startPosition)
{
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition.x, startPosition.y), BWAPI::Filter::IsRefinery);
}

BWAPI::Unit getClosestDepot(BWAPI::TilePosition startPosition)
{
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition.x, startPosition.y), BWAPI::Filter::IsResourceDepot);
}

BWAPI::Unit getClosestMineralField(BWAPI::TilePosition startPosition)
{
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition.x, startPosition.y), BWAPI::Filter::IsMineralField);
}

BWAPI::Unit getClosestGasRefinery(BWAPI::TilePosition startPosition) {
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition.x, startPosition.y), BWAPI::Filter::IsRefinery && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsOwned);
}


int getGasCount() {
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	int miningGasCount = 0;
	for (const auto& unit : units) {
		auto unitType = unit->getType();
		if (unitType != unitType.getRace().getWorker()) {
			continue;
		}

		if (unit->isCarryingGas() || unit->isGatheringGas()) {
			miningGasCount++;
		}
	}
	return miningGasCount;
}

void sendMineralWorkers() {
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit depot = getClosestDepot(startLocation);
	if (depot)
	{
		BWAPI::Unit mineralField = getClosestMineralField(depot->getTilePosition());
		if (mineralField)
		{
			BWAPI::Unitset units = BWAPI::Broodwar->self()->getUnits();
			for (auto unit : units)
			{
				auto unitType = unit->getType();
				if (unitType != unitType.getRace().getWorker() || !unit->isIdle()) {
					continue;
				}

				unit->gather(mineralField);
			}
		}
	} 
}

void sendGasWorkers(unsigned int targetWorkingCount) {
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit depot = getClosestDepot(startLocation);
	unsigned int gasWorkingCount = 0;
	if (!depot)
	{
		return;
	}

	auto myGasRefinery = getClosestGasRefinery(depot->getTilePosition());
	if (!myGasRefinery)
	{
		return;
	}
	BWAPI::Unitset units = BWAPI::Broodwar->self()->getUnits();
	gasWorkingCount = getGasCount();
	for (auto unit : units) {
		auto unitType = unit->getType();
		if (unitType != unitType.getRace().getWorker()) {
			continue;
		}

		if ( gasWorkingCount == targetWorkingCount) {
			break;
		}

		if (gasWorkingCount > targetWorkingCount) {
			if (unit->isGatheringGas() && !unit->isCarryingGas()) {
				unit->stop();
				gasWorkingCount--;
			}
			continue;
		}

		if (gasWorkingCount < targetWorkingCount) {
			if (unit->isCarryingGas() || unit->isGatheringGas() || unit->isConstructing() || unit->isAttacking()) {
				continue;
			}

			unit->gather(myGasRefinery);
			gasWorkingCount++;
		    continue;
		}
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

		if (unit->isConstructing() || unit->isCarryingGas() || unit->isCarryingMinerals())
		{
			continue;
		}

		BWAPI::TilePosition targetBuildLocation = BWAPI::Broodwar->getBuildLocation(buildingType, unit->getTilePosition());
	
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

void buildRefinery()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Refinery);
}

void buildEngineeringBay()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Engineering_Bay);
}

void buildAcademy()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Academy);
}

void buildComsatStation() {
	auto startLocation = BWAPI::Broodwar->self()->getStartLocation();
	BWAPI::Unit depot = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (depot) {// check if pMain is valid
		depot->buildAddon(BWAPI::UnitTypes::Terran_Comsat_Station);
	}
}

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

	Global::Map().saveMapToFile("map.txt");

	// TODO: figure out how to find enemy location. Hardcode for now.
	for (const auto startPosition : BWAPI::Broodwar->getStartLocations()) {
		if (startPosition != BWAPI::Broodwar->self()->getStartLocation()) {
			enemyLocation = BWAPI::Position(startPosition);
			break;
		}
	}
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

	//const char red = '\x08';
	//const char green = '\x07';
	//const char white = '\x04';

	//if (!Config::ConfigFile::ConfigFileFound)
	//{
	//	BWAPI::Broodwar->drawBoxScreen(0, 0, 450, 100, BWAPI::Colors::Black, true);
	//	BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
	//	BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Not Found", red, Config::BotInfo::BotName.c_str());
	//	BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
	//	BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without its configuration file", white, Config::BotInfo::BotName.c_str());
	//	BWAPI::Broodwar->drawTextScreen(10, 45, "%cCheck that the file below exists. Incomplete paths are relative to Starcraft directory", white);
	//	BWAPI::Broodwar->drawTextScreen(10, 60, "%cYou can change this file location in Config::ConfigFile::ConfigFileLocation", white);
	//	BWAPI::Broodwar->drawTextScreen(10, 75, "%cFile Not Found (or is empty): %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
	//	return;
	//}
	//else if (!Config::ConfigFile::ConfigFileParsed)
	//{
	//	BWAPI::Broodwar->drawBoxScreen(0, 0, 450, 100, BWAPI::Colors::Black, true);
//	BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
//	BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Parse Error", red, Config::BotInfo::BotName.c_str());
//	BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
//	BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without a properly formatted configuration file", white, Config::BotInfo::BotName.c_str());
//	BWAPI::Broodwar->drawTextScreen(10, 45, "%cThe configuration file was found, but could not be parsed. Check that it is valid JSON", white);
//	BWAPI::Broodwar->drawTextScreen(10, 60, "%cFile Not Parsed: %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
//	return;
//}

//if (Config::Modules::UsingGameCommander)
//{
//	m_gameCommander.update();
//}

if (Config::Modules::UsingAutoObserver)
{
	m_autoObserver.onFrame();
}

const auto supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
const auto freeMinerals = getFreeMinerals();
const auto freeGas = getFreeGas();

sendGasWorkers(2);
sendMineralWorkers();

std::map<BWAPI::UnitType, size_t> unitCount = getUnitCount();
auto race = BWAPI::Broodwar->self()->getRace();
if (unitCount[race.getWorker()] < 10 && freeMinerals >= 50)
{
	trainSCV();
	return;
} else if (unitCount[BWAPI::UnitTypes::Terran_Refinery] < 1 && freeMinerals >= BWAPI::UnitTypes::Terran_Refinery.mineralPrice()) {
	buildRefinery();
	return;
} else if (supplyTotal < 40 && freeMinerals >= BWAPI::UnitTypes::Terran_Supply_Depot.mineralPrice())
{
	buildSupplyDepot();
	return;
} else if (unitCount[BWAPI::UnitTypes::Terran_Barracks] < 1 && freeMinerals >= BWAPI::UnitTypes::Terran_Barracks.mineralPrice())
{
	buildBarracks();
	return;
} else if (unitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && unitCount[BWAPI::UnitTypes::Terran_Marine] < 10 && freeMinerals >= 50)
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

	if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.erase(unit);
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

	if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.insert(unit);
	}

	if (marineSquad.size() >= 50) {
		for (auto it = marineSquad.begin(); it != marineSquad.end(); it++) {
			Micro::SmartAttackMove(*it, BWAPI::Position(enemyLocation));
		}
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
