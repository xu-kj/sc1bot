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

class MyHashFunction {
public:
	size_t operator()(const BWAPI::TilePosition& p) const
	{
		return p.x ^ p.y;
	}
};

enum Resource { gas, mineral };
std::unordered_set<BWAPI::Unit> marineSquad;
std::unordered_map<BWAPI::TilePosition, BWAPI::UnitType, MyHashFunction> reservedBuildingPositions;
BWAPI::TilePosition startLocation;
BWAPI::Position enemyLocation;
Grid<int> reserveMap;

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


boolean canBuild(BWAPI::UnitType type) {
	if (getFreeMinerals() >= type.mineralPrice() && getFreeGas() >= type.gasPrice()) {
		return true;
	}

	return false;
}


boolean canBuildHere(BWAPI::TilePosition position, BWAPI::UnitType buildType, BWAPI::Unit worker)
{
	if (!BWAPI::Broodwar->canBuildHere(position, buildType, worker))
	{
		return false;
	}

	// check the reserve map
	for (int x = position.x; x < position.x + buildType.tileWidth(); x++)
	{
		for (int y = position.y; y < position.y + buildType.tileHeight(); y++)
		{
			if (reserveMap.get(x, y) == 1)
			{
				return false;
			}
		}
	}

	return true;
}


void reserveTiles(BWAPI::TilePosition position, int width, int height)
{
	for (int x = position.x; x < position.x + width && x < (int)reserveMap.width(); x++)
	{
		for (int y = position.y; y < position.y + height && y < (int)reserveMap.height(); y++)
		{
			reserveMap.set(x, y, 1);
		}
	}
}

void freeTiles(BWAPI::TilePosition position, int width, int height)
{
	for (int x = position.x; x < position.x + width && x < (int)reserveMap.width(); x++)
	{
		for (int y = position.y; y < position.y + height && y < (int)reserveMap.height(); y++)
		{
			reserveMap.set(x, y, 0);
		}
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

		if (!canBuild(BWAPI::UnitTypes::Terran_Marine)) {
			break;
		}

		if (unit->isIdle() && !unit->train(BWAPI::UnitTypes::Terran_Marine))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
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

		if (gasWorkingCount == targetWorkingCount) {
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
		if (!canBuildHere(targetBuildLocation, buildingType, unit)) {
			continue;
		}

		if (!unit->build(buildingType, targetBuildLocation))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
		else
		{
			reservedBuildingPositions[targetBuildLocation] = buildingType;
			reserveTiles(targetBuildLocation, buildingType.tileWidth(), buildingType.tileHeight());
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
	// TODO: Fix the performance problem running under 42x speed
	Config::BWAPIOptions::SetLocalSpeed = 15;
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

	Config::Modules::UsingGameCommander = false;

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

	startLocation = BWAPI::Broodwar->self()->getStartLocation();
	// TODO: figure out how to find enemy location. Hardcode for now.
	for (const auto startPosition : BWAPI::Broodwar->getStartLocations()) {
		if (startPosition != startLocation) {
			enemyLocation = BWAPI::Position(startPosition);
			break;
		}
	}

	globalstate = { 400, 0, 0, 0, 0, 0 };
	reserveMap = Grid<int>(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0);
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

	const auto supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	const auto supplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	const auto freeMinerals = getFreeMinerals();
	const auto freeGas = getFreeGas();
	float testParameter = (float)supplyUsed / (float)supplyTotal;
	// The height for each line of text is about 15px
	BWAPI::Broodwar->drawTextScreen(10, 5, "%cLocal speed: %d", red, Config::BWAPIOptions::SetLocalSpeed);
	BWAPI::Broodwar->drawTextScreen(10, 20, "%cFree minerals: %d, Free gas: %d, Supply used: %d, Supply total: %d, Test parameter: %f", red, getFreeMinerals(), getFreeGas(), supplyUsed, supplyTotal, testParameter);
	BWAPI::Broodwar->drawTextScreen(10, 35, "%cReserved minerals: %d, Reserved gas: %d", red, globalstate.reservedMinerals, globalstate.reservedGas);
	for (const auto& test : reservedBuildingPositions) {
		BWAPI::Position position1 = BWAPI::Position(test.first);
		BWAPI::Position position2 = BWAPI::Position(BWAPI::TilePosition(test.first.x + test.second.tileWidth(), test.first.y + test.second.tileHeight()));
		BWAPI::Broodwar->drawBoxMap(position1.x, position1.y, position2.x, position2.y, BWAPI::Colors::Green);
	}
	std::map<BWAPI::UnitType, size_t> unitCount = getUnitCount();
	auto race = BWAPI::Broodwar->self()->getRace();

	if (unitCount[BWAPI::UnitTypes::Terran_Comsat_Station] >= 1 || getFreeGas() >= BWAPI::UnitTypes::Terran_Comsat_Station.gasPrice()) {
		sendGasWorkers(0);
	}
	else {
		sendGasWorkers(1);
	}
	sendMineralWorkers();

	if (testParameter > 0.7 && canBuild(BWAPI::UnitTypes::Terran_Supply_Depot))
	{
		buildSupplyDepot();
	}
	else {
		if (testParameter < 0.8 && unitCount[race.getWorker()] < 20 && canBuild(race.getWorker()))
		{
			trainSCV();
		} else if (BWAPI::Broodwar->self()->supplyUsed() < supplyTotal && unitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && canBuild(BWAPI::UnitTypes::Terran_Marine))
		{
			trainMarine();
		}
	}

	if (unitCount[BWAPI::UnitTypes::Terran_Refinery] < 1 && canBuild(BWAPI::UnitTypes::Terran_Refinery)) {
		buildRefinery();
	}
	else {
		if (unitCount[BWAPI::UnitTypes::Terran_Barracks] < 2 && canBuild(BWAPI::UnitTypes::Terran_Barracks))
		{
			buildBarracks();
		}

		if (unitCount[BWAPI::UnitTypes::Terran_Academy] < 1 && canBuild(BWAPI::UnitTypes::Terran_Academy)) {
			buildAcademy();
		}
		else if (unitCount[BWAPI::UnitTypes::Terran_Comsat_Station] < 1 && canBuild(BWAPI::UnitTypes::Terran_Comsat_Station))
		{
			BWAPI::Unit depot = getClosestDepot(startLocation);
			if (depot && depot->canBuildAddon(BWAPI::UnitTypes::Terran_Comsat_Station)) {
				buildComsatStation();
			}
		} else if (unitCount[BWAPI::UnitTypes::Terran_Barracks] < 6 && canBuild(BWAPI::UnitTypes::Terran_Barracks))
		{
			buildBarracks();
		}
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

// ?????? TODO: Building refinery doesn't trigger onUnitCreate ??????
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

		auto it = reservedBuildingPositions.find(unit->getTilePosition());
		if (it != reservedBuildingPositions.end()) {
			freeTiles(it->first, it->second.tileWidth(), it->second.tileHeight());
		}
	}
}

void UAlbertaBotModule::onUnitComplete(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitComplete(unit);
	}

	/*if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType().isBuilding())
	{
		globalstate.reservedMinerals -= unit->getType().mineralPrice();
		globalstate.reservedGas -= unit->getType().gasPrice();

		auto it = reservedBuildingPositions.find(unit->getTilePosition());
		if (it != reservedBuildingPositions.end()) {
			freeTiles(it->first, it->second.tileWidth(), it->second.tileHeight());
		}
	}*/

	if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.insert(unit);
	}

	if (marineSquad.size() >= 40) {
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
