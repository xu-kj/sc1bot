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
	int miningMinerals = 0;
	int miningGas = 0;
} globalstate;

class MyHashFunction
{
public:
	size_t operator()(const BWAPI::TilePosition& p) const
	{
		return p.x ^ p.y;
	}
};

std::unordered_set<BWAPI::Unit> marineSquad;
std::unordered_set<BWAPI::Unit> workers;
std::unordered_set<BWAPI::Unit> barracks;
std::unordered_set<BWAPI::Unit> enemySet;
std::unordered_map<BWAPI::TilePosition, BWAPI::UnitType, MyHashFunction> reservedBuildingPositions;
std::map<BWAPI::UnitType, size_t> unitCount;
std::map<BWAPI::UnitType, size_t> completedUnitCount;
Grid<int> reserveMap;

BWAPI::TilePosition startLocation;
BWAPI::Position enemyLocation;
BWAPI::Position rallyPoint;
BWAPI::Unit depot;

int buildingSupplyDepotCount = 0;

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
	return BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsResourceDepot && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsOwned);
}

BWAPI::Unit getClosestMineralField(BWAPI::TilePosition startPosition)
{
	return BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsMineralField);
}

BWAPI::Unit getClosestGasRefinery(BWAPI::TilePosition startPosition)
{
	return BWAPI::Broodwar->getClosestUnit(BWAPI::Position(startPosition), BWAPI::Filter::IsRefinery && BWAPI::Filter::IsCompleted && BWAPI::Filter::IsOwned);
}

bool canBuild(BWAPI::UnitType type)
{
	if (getFreeMinerals() >= type.mineralPrice() && getFreeGas() >= type.gasPrice())
	{
		return true;
	}

	return false;
}

void trainSCV(BWAPI::UnitType workerType)
{
	if (depot && depot->isIdle() && !depot->train(workerType))
	{
		auto lastErr = BWAPI::Broodwar->getLastError();
	}
}

void trainMarine()
{
	for (const auto& unit : barracks)
	{
		if (!canBuild(BWAPI::UnitTypes::Terran_Marine))
		{
			break;
		}
		if (!unit->isIdle())
		{
			continue;
		}
		if (!unit->train(BWAPI::UnitTypes::Terran_Marine))
		{
			auto lastErr = BWAPI::Broodwar->getLastError();
		}
	}
}

bool canBuildHere(BWAPI::TilePosition position, BWAPI::UnitType buildType, BWAPI::Unit worker)
{
	if (!BWAPI::Broodwar->canBuildHere(position, buildType, worker))
	{
		return false;
	}

	if (position.x <= 1 || position.y <= 1 || position.x + buildType.tileWidth() >= (int)reserveMap.width() || position.y + buildType.tileHeight() >= (int)reserveMap.height())
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
	auto topWidth = position.x + width + 1;
	auto topHeight = position.y + height + 1;
	for (int x = position.x - 1; x < topWidth && x < (int)reserveMap.width(); x++)
	{
		for (int y = position.y - 1; y < topHeight && y < (int)reserveMap.height(); y++)
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

int getGasCount()
{
	const auto& units = BWAPI::Broodwar->self()->getUnits();
	int miningGasCount = 0;
	for (const auto& unit : workers)
	{
		if (unit->isCarryingGas() || unit->isGatheringGas())
		{
			miningGasCount++;
		}
	}
	return miningGasCount;
}

void sendMineralWorkers()
{
	if (depot)
	{
		BWAPI::Unit mineralField = getClosestMineralField(depot->getTilePosition());
		if (mineralField)
		{
			for (const auto& unit : workers)
			{
				if (!unit->isIdle())
				{
					continue;
				}
				Micro::SmartRightClick(unit, mineralField);
			}
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

		const BWAPI::TilePosition targetBuildLocation = BWAPI::Broodwar->getBuildLocation(buildingType, unit->getTilePosition());
		if (!canBuildHere(targetBuildLocation, buildingType, unit))
		{
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
			break;
		}
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

	startLocation = BWAPI::Broodwar->self()->getStartLocation();
	// TODO: figure out how to find enemy location. Hardcode for now.
	for (const auto& startPosition : BWAPI::Broodwar->getStartLocations())
	{
		if (startPosition != startLocation)
		{
			enemyLocation = BWAPI::Position(startPosition);
			break;
		}
	}

	depot = getClosestDepot(startLocation);
	buildingSupplyDepotCount = 0;
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
	globalstate.miningMinerals = 0;
	globalstate.miningGas = 0;

	marineSquad.clear();
	workers.clear();
	barracks.clear();
	enemySet.clear();
	reservedBuildingPositions.clear();
}

int recycleBuildingCostFrame = 200;
const int recycleBuildingCostPeriod = 200;

void UAlbertaBotModule::onFrame()
{
	if (BWAPI::Broodwar->getFrameCount() > 100000)
	{
		BWAPI::Broodwar->restartGame();
	}

	const char red = '\x08';
	const char green = '\x07';
	const char white = '\x04';

	const auto supplyTotal = BWAPI::Broodwar->self()->supplyTotal();
	const auto supplyUsed = BWAPI::Broodwar->self()->supplyUsed();
	const auto freeMinerals = getFreeMinerals();
	const auto freeGas = getFreeGas();
	float testParameter = (float)supplyUsed / (float)supplyTotal;
	// Draw state information
	BWAPI::Broodwar->drawTextScreen(10, 5, "%cFree minerals: %d, Free gas: %d, Supply used: %d, Supply total: %d, Test parameter: %f", red, getFreeMinerals(), getFreeGas(), supplyUsed, supplyTotal, testParameter);
	BWAPI::Broodwar->drawTextScreen(10, 20, "%cBuilding supply depot: %d, marine count: %d", red, buildingSupplyDepotCount, marineSquad.size());
	auto lastErr = BWAPI::Broodwar->getLastError();
	BWAPI::Broodwar->drawTextScreen(10, 35, "%clastErr: %s", red, lastErr.c_str());

	for (auto& enemy : enemySet)
	{
		BWAPI::Broodwar->drawBoxMap(enemy->getLeft(), enemy->getTop(), enemy->getRight(), enemy->getBottom(), BWAPI::Colors::Red);
	}

	auto startLoc = BWAPI::Position(startLocation);
	rallyPoint = BWAPI::Position(startLoc.x * 0.8, startLoc.y * 0.8) + BWAPI::Position(enemyLocation.x * 0.2, enemyLocation.y * 0.2);
	BWAPI::Broodwar->drawCircleMap(rallyPoint, 10, BWAPI::Colors::Yellow);

	for (const auto& test : reservedBuildingPositions)
	{
		BWAPI::Position position1 = BWAPI::Position(test.first);
		BWAPI::Position position2 = BWAPI::Position(BWAPI::TilePosition(test.first.x + test.second.tileWidth(), test.first.y + test.second.tileHeight()));
		BWAPI::Broodwar->drawBoxMap(position1.x, position1.y, position2.x, position2.y, BWAPI::Colors::Green);
	}
	auto race = BWAPI::Broodwar->self()->getRace();

	sendMineralWorkers();

	// Recycle the building cost every 200 frames to restore resources and recycle reserved tiles
	if (recycleBuildingCostFrame == 0)
	{
		const auto& units = BWAPI::Broodwar->self()->getUnits();
		int realBuildingSupplyDepotCount = 0;
		int realBuildingBarracksCount = 0;
		for (const auto& unit : units)
		{
			switch (unit->getType())
			{
			case BWAPI::UnitTypes::Terran_Supply_Depot:
				if (unit->isBeingConstructed())
				{
					realBuildingSupplyDepotCount++;
				}
				break;
			case BWAPI::UnitTypes::Terran_Barracks:
				if (unit->isBeingConstructed())
				{
					realBuildingBarracksCount++;
				}
				break;
			default:
				break;
			}
		}
		buildingSupplyDepotCount = realBuildingSupplyDepotCount;
		recycleBuildingCostFrame = recycleBuildingCostPeriod;
	}
	else
	{
		recycleBuildingCostFrame--;
	}

	if (workers.size() < 18 && canBuild(race.getWorker()))
	{
		trainSCV(BWAPI::UnitTypes::Terran_SCV);
	}

	if (supplyUsed < supplyTotal && completedUnitCount[BWAPI::UnitTypes::Terran_Barracks] >= 1 && canBuild(BWAPI::UnitTypes::Terran_Marine))
	{
		trainMarine();
	}
	if (buildingSupplyDepotCount < 2 && testParameter > 0.8 && canBuild(BWAPI::UnitTypes::Terran_Supply_Depot))
	{
		buildSupplyDepot();
		buildingSupplyDepotCount++;
	}

	if (completedUnitCount[BWAPI::UnitTypes::Terran_Barracks] < 4 && canBuild(BWAPI::UnitTypes::Terran_Barracks))
	{
		buildBarracks();
	}
}

void UAlbertaBotModule::onUnitDestroy(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitDestroy(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Marine)
	{
		marineSquad.erase(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_SCV)
	{
		workers.erase(unit);
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && (unit->getType().isBuilding() || unit->getType().isAddon()))
	{
		auto finder = reservedBuildingPositions.find(unit->getTilePosition());
		if (finder != reservedBuildingPositions.end())
		{
			reservedBuildingPositions.erase(finder);
		}

		if (unit->isCompleted())
		{
			completedUnitCount[unit->getType()] -= 1;
		}
		else
		{
			unitCount[unit->getType()] -= 1;
		}
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && unit->getType() == BWAPI::UnitTypes::Terran_Barracks)
	{
		barracks.erase(unit);
	}

	if (BWAPI::Broodwar->enemies().find(unit->getPlayer()) != BWAPI::Broodwar->enemies().end())
	{
		enemySet.erase(unit);
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
	// ParseUtils::ParseTextCommand(text);
	BWAPI::Broodwar->sendText(text.c_str());
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

		unitCount[unit->getType()] += 1;

		auto it = reservedBuildingPositions.find(unit->getTilePosition());
		if (it != reservedBuildingPositions.end())
		{
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

	if (unit->getPlayer() == BWAPI::Broodwar->self())
	{

		if (unit->getType() == BWAPI::UnitTypes::Terran_Marine)
		{
			marineSquad.insert(unit);
			Micro::SmartAttackMove(unit, rallyPoint);
		}
		else if (unit->getType() == BWAPI::UnitTypes::Terran_Supply_Depot)
		{
			buildingSupplyDepotCount--;
		}
		if (unit->getType() == BWAPI::UnitTypes::Terran_SCV)
		{
			workers.insert(unit);
		}
	}

	BWAPI::Unit nextUnit = BWAPI::Broodwar->getClosestUnit(BWAPI::Position(enemyLocation), BWAPI::Filter::IsEnemy);
	if (marineSquad.size() >= 30)
	{
		for (auto it = marineSquad.begin(); it != marineSquad.end(); it++)
		{
			if (nextUnit)
			{
				Micro::SmartAttackMove(*it, nextUnit->getPosition());
			}
			else
			{
				Micro::SmartAttackMove(*it, enemyLocation);
			}
		}
	}

	if (unit->getPlayer() == BWAPI::Broodwar->self() && (unit->getType().isBuilding() || unit->getType().isAddon()))
	{
		completedUnitCount[unit->getType()] += 1;

		if (unit->getType() == BWAPI::UnitTypes::Terran_Barracks)
		{
			barracks.insert(unit);
		}
	}
}

void UAlbertaBotModule::onUnitShow(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitShow(unit);
	}

	if (BWAPI::Broodwar->enemies().find(unit->getPlayer()) != BWAPI::Broodwar->enemies().end())
	{
		enemySet.insert(unit);
	}
}

void UAlbertaBotModule::onUnitHide(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitHide(unit);
	}
	if (BWAPI::Broodwar->enemies().find(unit->getPlayer()) != BWAPI::Broodwar->enemies().end())
	{
		enemySet.erase(unit);
	}
}

void UAlbertaBotModule::onUnitRenegade(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander)
	{
		m_gameCommander.onUnitRenegade(unit);
	}
}
