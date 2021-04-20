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

BWAPI::TilePosition startLocation;
std::map<BWAPI::UnitType, size_t> unitCount;
/*
 Iterate over all the workers and send all the workers to mining if it is idle
 */
void sendMineralWorker() {
	// Get main building closest to start location.
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (pMain) // check if pMain is valid
	{
		// Get sets of resources and workers
		BWAPI::Unitset myMinerals = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsMineralField);
		if (!myMinerals.empty()) // check if we have resources nearby
		{
			BWAPI::Unitset myWorkers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsIdle && BWAPI::Filter::IsOwned);
			// Set all worker to the first mineral to simplify logic, workers will automatically scatter
			auto m = *myMinerals.begin();
			for (auto worker : myWorkers) {
				worker->gather(m);
			}
		} // myResources not empty
	} // pMain != nullptr
}

// Use this data structure to store the workers currently gathering gas instead of relying on the isGatheringGas() method.
// This is due to a weird bug where isGatheringGas() doesn't return the correct value for workers actually gathering gas.
std::set<BWAPI::Unit> gasWorkers;

/*
 Iterate over all the workers and check if there are sufficiently many workers gathering gas.
 If not sufficient, add an arbitrary worker. If too many, send the excess workers to gather minerals instead.
 */
void sendGasWorker(unsigned int targetWorkerCount) {
	BWAPI::Unit pMain = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startLocation.x, startLocation.y), BWAPI::Filter::IsResourceDepot);
	if (pMain) // check if pMain is valid
	{
		BWAPI::Unitset myGasRefinery = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsRefinery && BWAPI::Filter::IsCompleted);
		if (myGasRefinery.empty()) {
			return;
		}
		// First check current gas workers
		std::set<BWAPI::Unit> updatedGasWorkers;
		for (auto it = gasWorkers.begin(); it != gasWorkers.end(); it++) {
			auto worker = *it;
			if (worker->exists()) {
				updatedGasWorkers.insert(worker);
			}
		}
		if (updatedGasWorkers.size() < targetWorkerCount) {
			// Insufficient workers, send more
			unsigned int workerGap = targetWorkerCount - updatedGasWorkers.size();
			BWAPI::Unitset mineralWorkers = pMain->getUnitsInRadius(1024, BWAPI::Filter::IsWorker && BWAPI::Filter::IsGatheringMinerals && BWAPI::Filter::IsOwned);
			while (!mineralWorkers.empty() && workerGap > 0) {
				auto workerIter = mineralWorkers.begin();
				mineralWorkers.erase(workerIter);
				gasWorkers.insert(*workerIter);
				(*workerIter)->gather(*myGasRefinery.begin());
				workerGap--;
			}
		}
		else if (updatedGasWorkers.size() > targetWorkerCount) {
			// Excessive workers, dismiss some
			unsigned int workerGap = updatedGasWorkers.size() - targetWorkerCount;
			while (!gasWorkers.empty() && workerGap > 0) {
				auto workerIter = gasWorkers.begin();
				gasWorkers.erase(workerIter);
				gasWorkers.insert(*workerIter);
				// Idle workers will automatically be sent to gather minerals
				(*workerIter)->stop();
				workerGap--;
			}
		}
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

std::map<BWAPI::UnitType, size_t> getUnitCount()
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();

	std::map<BWAPI::UnitType, size_t> count;
	for (const auto& unit : units)
	{
		auto unitType = unit->getType();

		auto search = count.find(unitType);
		if (search != count.end())
		{
			search->second += 1;
		}
		else
		{
			count[unitType] = 1;
		}
	}
	return count;
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

		if (unit->isIdle())
		{
			auto result = unit->train(BWAPI::UnitTypes::Terran_Marine);
			if (!result) {	
				auto lastErr = BWAPI::Broodwar->getLastError();
			}
			else {
				break;
			}
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

		if (!unit->isGatheringMinerals())
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
		if (buildingType == BWAPI::UnitTypes::Terran_Refinery) {
			// The worker building the refinery will automatically be sent to gather gas
			gasWorkers.insert(unit);
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

void buildEngineeringBay()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Engineering_Bay);
}

void buildAcademy()
{
	buildBuilding(BWAPI::UnitTypes::Terran_Academy);
}

void buildRefinery() {
	buildBuilding(BWAPI::UnitTypes::Terran_Refinery);
}

// This map stores the build commands that are issued but hadn't happened.
// This is meant to prevent the planning to underestimate the count of a certain building and unnecessarily build too many of it.
std::map<BWAPI::UnitType, int> buildQueue;

// Priority > 0 means this has some value in the strategy. The higher the priority, the more important the action needs to be done.
// Having a priority >= 5 means all other actions need to be blocked until this action is done.
// So to summarize, there are 3 tiers of priorities:
// Critical: x = 5
// Useful: 0 < x < 5
// No need: 0

float getBuildSCVPriority() {
	if (unitCount[BWAPI::UnitTypes::Terran_SCV] >= 18) {
		return 0;
	}
	return 3.0;
}

float getBuildMarinePriority() {
	return 2.5;
}

float buildRefineryPriority() {
	if (unitCount[BWAPI::UnitTypes::Terran_SCV] >= 10) {
		return 5.0;
	}
	return 0;
}

// Set to be done if we have more than 3 barracks
float getBuildAcademyPriority() {
	int barracksCount = unitCount[BWAPI::UnitTypes::Terran_Barracks];
	int academyCount = unitCount[BWAPI::UnitTypes::Terran_Academy];
	if (barracksCount >= 3 || academyCount > 0) {
		return 0;
	}
	return 5.0;
}

// Build 3 barracks in total
float getBuildBarracksPriority() {
	int barracksCount = unitCount[BWAPI::UnitTypes::Terran_Barracks];
	if (barracksCount > 3) {
		return 0;
	}
	return 3.0;
}

// Build immediately when we get 2 barracks
float getBuildEngineeringBayPriority() {
	int barracksCount = unitCount[BWAPI::UnitTypes::Terran_Barracks];
	int engineeringBayCount = unitCount[BWAPI::UnitTypes::Terran_Engineering_Bay];
	if (barracksCount >= 2 || engineeringBayCount > 0) {
		return 0;
	}
	return 5.0;
}
float getBuildSupplyDepotPriority() {
	const int supplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	const int supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	if (supplyTotal == 0) {
		return 5.0;
	}
	return 4.0 * static_cast<float>(supplyUsed) / static_cast<float>(supplyTotal);
}

float getResearchU238ShellPriorityBase() {
	int academyCount = unitCount[BWAPI::UnitTypes::Terran_Academy];
	if (academyCount == 0) {
		return 0;
	}
	const int u238Level = BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells);
	if (u238Level >= 1 || BWAPI::Broodwar->self()->isUpgrading(BWAPI::UpgradeTypes::U_238_Shells)) {
		return 0;
	}
	return 5.0;
}
float getResearchMarineAttackPriorityBase() {
	int engineeringBayCount = unitCount[BWAPI::UnitTypes::Terran_Engineering_Bay];
	if (engineeringBayCount == 0) {
		return 0;
	}
	const int infantryAttackLevel = BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Weapons);
	if (infantryAttackLevel >= 1 || BWAPI::Broodwar->self()->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Weapons)) {
		return 0;
	}
	return 5.0;
}

void takeAction(std::string actionName) {
	if (actionName == "build_scv") {
		if (getFreeMinerals() >= 50) {
			trainSCV();
		}
	}
	else if (actionName == "build_marine") {
		if (getFreeMinerals() >= 50) {
			trainMarine();
		}
	}
	else if (actionName == "build_Refinery") {
		if (getFreeMinerals() >= 75) {
			buildRefinery();
		}
	}
	else if (actionName == "build_supply_depot") {
		if (getFreeMinerals() >= 100) {
			buildSupplyDepot();
		}
	}
	else if (actionName == "build_barracks") {
		if (getFreeMinerals() >= 150) {
			buildBarracks();
		}
	}
	else if (actionName == "build_engeineering_bay") {
		if (getFreeMinerals() >= 125) {
			buildEngineeringBay();
		}
	}
	else if (actionName == "build_academy") {
		if (getFreeMinerals() >= 150) {
			buildAcademy();
		}
	}
	else if (actionName == "research_u238") {
		return;
		// researchU238();
	}
	else if (actionName == "research_infantry_attack") {
		return;
		// researchInfantryAttack();
	}
}

// Work 1 frame and then skip 1
const int totalTurns = 8;
int turn = 0;

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
	startLocation = BWAPI::Broodwar->self()->getStartLocation();
	unitCount = getUnitCount();

	sendMineralWorker();
	// sendGasWorker(1);
	// std::vector<std::pair<std::string, float>> actionQueue;
	if (turn % totalTurns == 0) {
		auto buildSCVPriority = getBuildSCVPriority();
		if (buildSCVPriority > 0) {
			takeAction("build_scv");
			// actionQueue.push_back(std::pair<std::string, float>("build_scv", buildSCVPriority));
		}
		turn = (turn + 1) % totalTurns;
	}
	else if (turn % totalTurns == 2) {
		auto buildMarinePriority = getBuildMarinePriority();
		if (buildMarinePriority > 0) {
			takeAction("build_marine");
			// actionQueue.push_back(std::pair<std::string, float>("build_marine", buildMarinePriority));
		}
		turn = (turn + 1) % totalTurns;
	}
	else if (turn % totalTurns == 4) {
		auto buildSupplyDepotPriority = getBuildSupplyDepotPriority();
		if (buildSupplyDepotPriority > 3.5) {
			takeAction("build_supply_depot");
			// actionQueue.push_back(std::pair<std::string, float>("build_supply_depot", buildSupplyDepotPriority));
		}
		turn = (turn + 1) % totalTurns;
	}
	else if (turn % totalTurns == 6) {
		auto buildBarracksPriority = getBuildBarracksPriority();
		if (buildBarracksPriority > 0) {
			takeAction("build_barracks");
			// actionQueue.push_back(std::pair<std::string, float>("build_barracks", buildBarracksPriority));
		}
		turn = (turn + 1) % totalTurns;
	}
	else if (turn % totalTurns == 7) {
		// Do some scouting work
	}
	else {
		turn += 1;
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

std::list<BWAPI::Unit> marineSquad;

void UAlbertaBotModule::onUnitComplete(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitComplete(unit);
	}

	if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) {
		marineSquad.push_back(unit);
	}
	for (auto it = marineSquad.begin(); it != marineSquad.end(); it++) {
		if (!(*it)->exists()) {
			marineSquad.erase(it);
		}
	}
	if (marineSquad.size() >= 30) {
		auto enemyLocation = BWAPI::Broodwar->enemy()->getStartLocation();
		for (auto it = marineSquad.begin(); it != marineSquad.end(); it++) {
			(*it)->attack(BWAPI::Position(enemyLocation.x, enemyLocation.y));
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
