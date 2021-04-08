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

// This gets called when the bot starts!
void UAlbertaBotModule::onStart()
{
    const char red = '\x08';
    const char green = '\x07';
    const char white = '\x04';
    BWAPI::Unitset units = BWAPI::Broodwar->self()->getUnits();
    BWAPI::Broodwar->drawBoxScreen(0, 0, 450, 100, BWAPI::Colors::Black, true);
    BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
    BWAPI::Broodwar->drawTextScreen(10, 30, "The game start with %d units", white, units.size());
    // Save the dancer
    for (auto unit = units.begin(); unit != units.end();  unit++) {
        // Get the first unit that is a worker
        // Exceptions: Terran base, which also can build
        if ((*unit)->canBuild() && (*unit)->canAttack()) {
            this->dancer = *unit;
            break;
        }
    }

    int x = this->dancer->getPosition().x;
    int y = this->dancer->getPosition().y;
    BWAPI::Broodwar->drawTextScreen(10, 45, "The first unit is of type %d at (%d, %d)", white, (*units.begin())->getType(), x, y);

    const int dist = 150;
    BWAPI::Position cornerA;
    // Test with Map: Baekmagoji1.2
    // Check if the spawn location is in upper right corner or bottom left corner
    if (x < 1500 && y > 1500) {
        // Bottom left corner - Move up
        cornerA = BWAPI::Position(x, y - dist);
        this->cornerLocations.push_back(cornerA);
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x + dist, cornerA.y));
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x + dist, cornerA.y - dist));
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x, cornerA.y - dist));
    }
    else {
        // Upper right corner - Move down
        cornerA = BWAPI::Position(x, y + dist);
        this->cornerLocations.push_back(cornerA);
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x - dist, cornerA.y));
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x - dist, cornerA.y + dist));
        this->cornerLocations.push_back(BWAPI::Position(cornerA.x, cornerA.y + dist));
    }
    BWAPI::Broodwar->drawLine(BWAPI::CoordinateType::Map, x, y, cornerA.x, cornerA.y, green);
    this->targetCornerLocationIdx = 0;
    this->dancer->move(this->cornerLocations[this->targetCornerLocationIdx]);

    /*
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
    */
    //Global::Map().saveMapToFile("map.txt");
}

void UAlbertaBotModule::onEnd(bool isWinner) 
{
    /*
	if (Config::Modules::UsingGameCommander)
	{
		Global::Strategy().onEnd(isWinner);
	}
    */
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

    auto position = this->dancer->getPosition();
    int x = position.x;
    int y = position.y;
    BWAPI::Position nextCorner = this->cornerLocations[this->targetCornerLocationIdx];
    
    // draw the for edges of the rectangle
    if (x < 1500 && y > 1500) {
        // Bottom left corner - Move up
        BWAPI::Broodwar->drawBoxMap(this->cornerLocations[3], this->cornerLocations[1], BWAPI::Colors::Red);
    }
    else {
        // Upper right corner - Move down
        BWAPI::Broodwar->drawBoxMap(this->cornerLocations[1], this->cornerLocations[3], BWAPI::Colors::Red);
    }
    // draw the current edge the dancer is traversing
    BWAPI::Broodwar->drawLine(BWAPI::CoordinateType::Map, x, y, nextCorner.x, nextCorner.y, BWAPI::Colors::Green);

    // Take the inertia into consideration, after the new move command is issued, the unit will still move a little bit towards the old target position
    if (this->dancer->getDistance(this->cornerLocations[this->targetCornerLocationIdx]) < 5) {
        // The dancer has reached a corner, move to the next one
        this->targetCornerLocationIdx = (this->targetCornerLocationIdx + 1) % 4;
        this->dancer->move(this->cornerLocations[this->targetCornerLocationIdx]);
    }

    /*
    if (!Config::ConfigFile::ConfigFileFound)
    {
        BWAPI::Broodwar->drawBoxScreen(0,0,450,100, BWAPI::Colors::Black, true);
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
        BWAPI::Broodwar->drawBoxScreen(0,0,450,100, BWAPI::Colors::Black, true);
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
    */

    if (Config::Modules::UsingAutoObserver)
    {
        m_autoObserver.onFrame();
    }
}

void UAlbertaBotModule::onUnitDestroy(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitDestroy(unit); }
}

void UAlbertaBotModule::onUnitMorph(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitMorph(unit); }
}

void UAlbertaBotModule::onSendText(std::string text) 
{ 
	ParseUtils::ParseTextCommand(text);
}

void UAlbertaBotModule::onUnitCreate(BWAPI::Unit unit)
{ 
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitCreate(unit); }
}

void UAlbertaBotModule::onUnitComplete(BWAPI::Unit unit)
{
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitComplete(unit); }
}

void UAlbertaBotModule::onUnitShow(BWAPI::Unit unit)
{ 
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitShow(unit); }
}

void UAlbertaBotModule::onUnitHide(BWAPI::Unit unit)
{ 
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitHide(unit); }
}

void UAlbertaBotModule::onUnitRenegade(BWAPI::Unit unit)
{ 
	if (Config::Modules::UsingGameCommander) { m_gameCommander.onUnitRenegade(unit); }
}