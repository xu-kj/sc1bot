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

std::unordered_set<BWAPI::Unit> marineSquad;
std::unordered_set<BWAPI::Unit> workers;
std::unordered_set<BWAPI::Unit> barracks;
std::unordered_map<BWAPI::TilePosition, BWAPI::UnitType, MyHashFunction> reservedBuildingPositions;
BWAPI::TilePosition startLocation;
BWAPI::Position enemyLocation;
BWAPI::Unit academy;
BWAPI::Unit comsat;
BWAPI::Unit refinery;
BWAPI::Unit depot;
Grid<int> reserveMap;
boolean beingAttacked = false;
std::map<BWAPI::UnitType, size_t> unitCount;
std::map<BWAPI::UnitType, size_t> completedUnitCount;


void reserveMinerals(int minerals)
{
	globalstate.reservedMinerals += minerals;
}

int getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - globalstate.reservedMinerals;
}

void reserveGas(int gas)
{
	globalstate.reservedGas += gas;
}

int getFreeGas()
{
	return BWAPI::Broodwar->self()->gas() - globalstate.reservedGas;
}

BWAPI::Unit getClosestDepot(BWAPI::TilePosition startPosition)
{
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsResourceDepot && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsOwned);
}

BWAPI::Unit getClosestMineralField(BWAPI::TilePosition startPosition)
{
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsMineralField);
}

BWAPI::Unit getClosestGasRefinery(BWAPI::TilePosition startPosition) {
	return  BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsRefinery && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsOwned);
}

boolean canBuild(BWAPI::UnitType type) {
	if (getFreeMinerals() >= type.mineralPrice() && getFreeGas() >= type.gasPrice()) {
		return true;
	}

	return false;
}

void trainSCV(BWAPI::UnitType workerType)
{
	if (depot && !depot->isUpgrading() && !depot->train(workerType)) {
		auto lastErr = BWAPI::Broodwar->getLastError();
	}
}

void trainMarine()
{
	for (const auto& unit : barracks)
	{
		if (!canBuild(BWAPI::UnitTypes::Terran_Marine)) {
			break;
		}

		if (!unit->train(BWAPI::UnitTypes::Terran_Marine))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
	}
}


boolean canBuildHere(BWAPI::TilePosition position, BWAPI::UnitType buildType, BWAPI::Unit worker)
{
	if (!BWAPI::Broodwar->canBuildHere(position, buildType, worker))
	{
		return false;
	}

	if (position.x <= 1 || position.y <= 1 || position.x + buildType.tileWidth() >= (int)reserveMap.width() || position.y + buildType.tileHeight() >= (int)reserveMap.height()) {
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
	auto topWidth = position.x + width + 1;
	auto topHeight = position.y + height + 1;
	for (int x = position.x - 1; x < topWidth && x < (int)reserveMap.width(); x++)
	{
		for (int y = position.y -1; y < topHeight && y < (int)reserveMap.height(); y++)
		{
			reserveMap.set(x, y, 1);
		}
	}
}

void freeTiles(BWAPI::TilePosition position, int width, int height)
{
	auto topWidth = position.x + width + 1;
	auto topHeight = position.y + height + 1;
	for (int x = position.x - 1; x < topWidth && x < (int)reserveMap.width(); x++)
	{
		for (int y = position.y - 1; y < topHeight && y < (int)reserveMap.height(); y++)
		{
			reserveMap.set(x, y, 0);
		}
	}
}


int getGasCount() {
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	int miningGasCount = 0;
	for (const auto& unit : workers) {
		if (unit->isCarryingGas() || unit->isGatheringGas()) {
			miningGasCount++;
		}
	}
	return miningGasCount;
}

void sendMineralWorkers() {
	if (depot)
	{
		BWAPI::Unit mineralField = getClosestMineralField(depot->getTilePosition());
		if (mineralField)
		{
			for (const auto& unit : workers)
			{
				auto unitType = unit->getType();
				if (!unit->isIdle()) {
					continue;
				}

				Micro::SmartRightClick(unit, mineralField);
			}
		}
	}
}

void sendGasWorkers(unsigned int targetWorkingCount) {
	unsigned int gasWorkingCount = 0;
	if (!depot)
	{
		return;
	}

	if (!refinery)
	{
		return;
	}
	gasWorkingCount = getGasCount();
	for (const auto& unit : workers) {
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
			if (unit->isCarryingGas() || unit->isGatheringGas() || unit->isConstructing() || unit->isAttacking() || unit->isRepairing()) {
				continue;
			}

			Micro::SmartRightClick(unit, refinery);
			gasWorkingCount++;
			continue;
		}
	}
}

void buildBuilding(BWAPI::UnitType buildingType)
{
	for (const auto& unit : workers)
	{
		if (unit->isConstructing() || unit->isCarryingGas() || unit->isCarryingMinerals() || unit->isRepairing() || unit->isAttacking())
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
			reserveGas(buildingType.gasPrice());
		}
		break;
	}
}

void repair(BWAPI::Unit building) {
	for (const auto& unit : workers)
	{
		if (unit->isConstructing() || unit->isCarryingGas() || unit->isCarryingMinerals() || unit->isRepairing() || unit->isAttacking())
		{
			continue;
		}

		if (unit->canRepair(building)) {
			if (!unit->repair(building)) {
				auto lastErr = BWAPI::Broodwar->getLastError();
			}
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
		reserveGas(BWAPI::UnitTypes::Terran_Comsat_Station.gasPrice());
		reserveMinerals(BWAPI::UnitTypes::Terran_Comsat_Station.mineralPrice());
	}
}

void upgradeResearchType(BWAPI::UpgradeType researchType) {
	const int researchLevel = BWAPI::Broodwar->self()->getUpgradeLevel(researchType);
	if (researchLevel < BWAPI::Broodwar->self()->getMaxUpgradeLevel(researchType)
		&& !BWAPI::Broodwar->self()->isUpgrading(researchType)
		&& getFreeMinerals() > researchType.mineralPrice()
		&& getFreeGas() > researchType.gasPrice()) {
		if (academy && academy->canUpgrade(researchType)) {
			academy->upgrade(researchType);
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
	for (const auto& startPosition : BWAPI::Broodwar->getStartLocations()) {
		if (startPosition != startLocation) {
			enemyLocation = BWAPI::Position(startPosition);
			break;
		}
	}

	depot = getClosestDepot(startLocation);

	reserveMap = Grid<int>(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0);
}

void UAlbertaBotModule::onEnd(bool isWinner)
{
	if (Config::Modules::UsingGameCommander)
	{
		Global::Strategy().onEnd(isWinner);
	}

	globalstate.reservedMinerals = 400;
	globalstate.reservedGas = 0;
	globalstate.scv = 0;
	globalstate.marine = 0;
	globalstate.miningMinerals = 0;
	globalstate.miningGas = 0;

	marineSquad.clear();
	reservedBuildingPositions.clear();
	Grid<int> reserveMap;
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

	//const auto& units = BWAPI::Broodwar->self()->getUnits();

	//std::map<BWAPI::UnitType, size_t> counter;
	//for (const auto& unit : units)
	//{
	//	if (unit->isUnderAttack()) {
	//		BWAPI::Unit attacker = BWAPI::Broodwar->getClosestUnit(unit->getPosition(), BWAPI::Filter::IsEnemy);
	//		if (!attacker) {
	//			if (comsat->canUseTech(BWAPI::TechTypes::Scanner_Sweep)) {
	//				comsat->useTech(BWAPI::TechTypes::Scanner_Sweep, unit->getPosition());
	//			}
	//			attacker = BWAPI::Broodwar->getClosestUnit(unit->getPosition(), BWAPI::Filter::IsEnemy);
	//		}

	//		if (attacker) {
	//			beingAttacked = true;
	//			for (const auto& unit : marineSquad) {
	//				unit->attack(attacker);
	//			}
	//		}

	//		if (!attacker) {
	//			beingAttacked = false;
	//		}

	//		if (unit->getType().isBuilding() && unit->canRepair()) {
	//			repair(unit);
	//		}
	//	}
	//}

	//if (beingAttacked) {
	//	auto attacker = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation), BWAPI::Filter::IsEnemy);
	//	if (attacker) {
	//		for (const auto& unit : marineSquad) {
	//			unit->attack(attacker);
	//		}
	//	}
	//	else {
	//		beingAttacked = false;
	//	}
	//}

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

	if (refinery == nullptr) {
		refinery = getClosestGasRefinery(startLocation);
		if (refinery) {
			unitCount[BWAPI::UnitTypes::Terran_Refinery] = 1;
			completedUnitCount[BWAPI::UnitTypes::Terran_Refinery] = 1;
			globalstate.reservedMinerals -= BWAPI::UnitTypes::Terran_Refinery.mineralPrice();
			globalstate.reservedGas -= BWAPI::UnitTypes::Terran_Refinery.gasPrice();
		}
	}

	const auto supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	const auto supplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	const auto freeMinerals = getFreeMinerals();
	const auto freeGas = getFreeGas();
	float testParameter = (float)supplyUsed / (float)supplyTotal;
	BWAPI::Broodwar->drawTextScreen(10, 5, "%cFree minerals: %d, Free gas: %d, Supply used: %d, Supply total: %d, Test parameter: %f", red, getFreeMinerals(), getFreeGas(), supplyUsed, supplyTotal, testParameter);
	for (const auto& test : reservedBuildingPositions) {
		BWAPI::Position position1 = BWAPI::Position(test.first);
		BWAPI::Position position2 = BWAPI::Position(BWAPI::TilePosition(test.first.x + test.second.tileWidth(), test.first.y + test.second.tileHeight()));
		BWAPI::Broodwar->drawBoxMap(position1.x, position1.y, position2.x, position2.y, BWAPI::Colors::Green);
	}
	auto race = BWAPI::Broodwar->self()->getRace();

	if (completedUnitCount[BWAPI::UnitTypes::Terran_Comsat_Station] < 1 || getFreeGas() < 150) {
		sendGasWorkers(1);
	}
	else {
		sendGasWorkers(0);
	}
	sendMineralWorkers();

	if (unitCount[BWAPI::UnitTypes::Terran_Refinery] < 1 && canBuild(BWAPI::UnitTypes::Terran_Refinery)) {
		buildRefinery();
		return;
	}
	else if (completedUnitCount[BWAPI::UnitTypes::Terran_Refinery] >= 1) {
		if (unitCount[BWAPI::UnitTypes::Terran_Barracks] < 1 && canBuild(BWAPI::UnitTypes::Terran_Barracks))
		{
			buildBarracks();
			return;
		}
		else if (completedUnitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && unitCount[BWAPI::UnitTypes::Terran_Academy] < 1 && canBuild(BWAPI::UnitTypes::Terran_Academy)) {
			buildAcademy();
			return;
		}
		else if (completedUnitCount[BWAPI::UnitTypes::Terran_Academy] >= 1) {
			if (unitCount[BWAPI::UnitTypes::Terran_Comsat_Station] < 1 && canBuild(BWAPI::UnitTypes::Terran_Comsat_Station)) {
				BWAPI::Unit depot = getClosestDepot(startLocation);
				if (depot && depot->canBuildAddon(BWAPI::UnitTypes::Terran_Comsat_Station)) {
					buildComsatStation();
					return;
				}
			}

			//upgradeResearchType(BWAPI::UpgradeTypes::U_238_Shells);

			if (completedUnitCount[BWAPI::UnitTypes::Terran_Barracks] < 4 && canBuild(BWAPI::UnitTypes::Terran_Barracks))
			{
				buildBarracks();
				return;
			}

		}
	}

	if (testParameter > 0.8 && canBuild(BWAPI::UnitTypes::Terran_Supply_Depot))
	{
		buildSupplyDepot();
		return;
	}
	else {
		if (testParameter < 0.7 && unitCount[race.getWorker()] < 20 && canBuild(race.getWorker()))
		{
			trainSCV(BWAPI::UnitTypes::Terran_SCV);
			return;
		}

		if (supplyUsed < supplyTotal && completedUnitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && canBuild(BWAPI::UnitTypes::Terran_Marine))
		{
			trainMarine();
			return;
		}
	}
}

void UAlbertaBotModule::onUnitDestroy(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitDestroy(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.erase(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_SCV) {
		workers.erase(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && (unit->getType().isBuilding() || unit->getType().isAddon())) {
		auto finder = reservedBuildingPositions.find(unit->getTilePosition());
		if (finder != reservedBuildingPositions.end()) {
			reservedBuildingPositions.erase(finder);
		}

		if (unit->isCompleted()) {
			completedUnitCount[unit->getType()] -= 1;
		}
		else {
			unitCount[unit->getType()] -= 1;
		}
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Barracks) {
		barracks.erase(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Academy) {
		academy = nullptr;
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Refinery) {
		refinery = nullptr;
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Comsat_Station) {
		comsat = nullptr;
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

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_SCV) {
		workers.insert(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && (unit->getType().isBuilding() || unit->getType().isAddon()))
	{
		globalstate.reservedMinerals -= unit->getType().mineralPrice();
		globalstate.reservedGas -= unit->getType().gasPrice();

		unitCount[unit->getType()] += 1;

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

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.insert(unit);
	}

	BWAPI::Unit nextUnit = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(enemyLocation), BWAPI::Filter::IsEnemy);
	if (marineSquad.size() >= 40) {
		for (auto it = marineSquad.begin(); it != marineSquad.end(); it++) {
			if (nextUnit) {
				Micro::SmartAttackMove(*it, nextUnit->getPosition());
			}
			else {
				Micro::SmartAttackMove(*it, enemyLocation);
			}
		}
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && (unit->getType().isBuilding() || unit->getType().isAddon()))
	{
		completedUnitCount[unit->getType()] += 1;

		if (unit->getType() == BWAPI::UnitTypes::Terran_Barracks) {
			barracks.insert(unit);
		}
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Academy) {
		academy = unit;
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Comsat_Station) {
		comsat = unit;
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
