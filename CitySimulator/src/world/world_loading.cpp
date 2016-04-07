#include <boost/lexical_cast.hpp>
#include "service/config_service.hpp"
#include "service/logging_service.hpp"
#include "service/world_service.hpp"

WorldService::WorldLoader::WorldLoader() : lastWorldID(0)
{
}


World *WorldService::WorldLoader::loadWorlds(const std::string &mainWorldName,
	WorldConnectionTable &connectionLookup, WorldTreeNode &treeRoot)
{
	// load main world
	LoadedWorld &mainWorld = loadWorld(mainWorldName, false);
	if (mainWorld.failed())
	{
		Logger::logError("Failed to load main world");
		return nullptr;
	}

	treeRoot.value = mainWorld.world;

  	// allocate main world building IDs
	for (auto &building : mainWorld.buildings)
	{
		Logger::logDebuggier(format("Found building %1% '%2%' in main world", 
          			_str(building.insideWorldID), building.insideWorldName));
    	Logger::pushIndent();

    	loadWorld(building.insideWorldName, true, building.insideWorldID);

    	Logger::popIndent();
	}

	// transfer building IDs to doors
	for (auto &door : mainWorld.doors)
	{
		UnloadedBuilding *owningBuilding = findDoorBuilding(mainWorld, door);
		if (owningBuilding == nullptr)
		{
			Logger::logError(format("A door at (%1%, %2%) is not in any buildings!",
			            _str(door.tile.x), _str(door.tile.y)));
			return nullptr;
		}

		door.doorTag = DOORTAG_WORLD_ID;
		door.worldID = owningBuilding->insideWorldID;
	}

  	std::set<WorldID> visitedWorlds;

	// load all worlds recursively without connecting doors
	discoverAndLoadAllWorlds(mainWorld, visitedWorlds);
	visitedWorlds.clear();

	// connect up the doors
	connectDoors(treeRoot, mainWorld, connectionLookup, visitedWorlds);

	return mainWorld.world;
}


void WorldService::WorldLoader::discoverAndLoadAllWorlds(LoadedWorld &world, 
		std::set<WorldID> &visitedWorlds)
{
  	if (visitedWorlds.find(world.world->getID()) != visitedWorlds.end())
      	return;
  	visitedWorlds.insert(world.world->getID());

	// iterate all doors found in last world
	for (UnloadedDoor &door : world.doors)
	{
		// only positive IDs load worlds
		if (door.doorID <= 0)
			continue;

    	LoadedWorld *newWorld = nullptr;

		// find the other door with same world share
		if (door.doorTag == DOORTAG_WORLD_SHARE)
		{
			auto otherDoor = std::find_if(world.doors.begin(), world.doors.end(),
			        [door](const UnloadedDoor &d)
			        {
				    	return d.doorTag != DOORTAG_WORLD_SHARE &&
				    	d.worldShare == door.worldShare;
			        });

			if (otherDoor == world.doors.end())
			{
				Logger::logError(format("Door %1% has an unknown world share tag '%2%'",
				            _str(door.doorID), door.worldShare));
				continue;
			}

			// share world ID
			door.worldID = otherDoor->worldID;
		}

		// load world
		else if (door.doorTag == DOORTAG_WORLD_NAME)
		{
      		Logger::logDebuggiest(format("Door ID %1% loads world '%2%'", _str(door.doorID), door.worldName));
			LoadedWorld &loadedWorld = loadWorld(door.worldName, true);
			if (loadedWorld.failed())
			{
				Logger::logError(format("Cannot find building world '%1%', owner of door %2%",
				            door.worldName, _str(door.doorID)));
				continue;
			}

			door.worldID = loadedWorld.world->getID();
      		newWorld = &loadedWorld;
		}

		else if (door.doorTag == DOORTAG_UNKNOWN)
		{
			Logger::logError(format("Door %1% has no assigned door tag", _str(door.doorID)));
      		return;
		}

    	if (newWorld == nullptr)
      		newWorld = getLoadedWorld(door.worldID);

    	discoverAndLoadAllWorlds(*newWorld, visitedWorlds);
	}
}

WorldService::WorldLoader::UnloadedDoor *WorldService::WorldLoader::findPartnerDoor(
		LoadedWorld &world, int doorID)
{
	for (UnloadedDoor &door : world.doors)
		if (door.doorID == -doorID)
			return &door;

	return nullptr;
}

void WorldService::WorldLoader::connectDoors(WorldTreeNode &currentNode, LoadedWorld &world,
		WorldConnectionTable &connectionLookup, std::set<WorldID> &visitedWorlds)
{
	if (visitedWorlds.find(world.world->getID()) != visitedWorlds.end())
		return;
	visitedWorlds.insert(world.world->getID());

	for (UnloadedDoor &door : world.doors)
	{
		LoadedWorld *childWorld = getLoadedWorld(door.doorID > 0 ? door.worldID : currentNode.parent->value->getID());
		if (childWorld == nullptr)
		{
			Logger::logError(format("World %1% has not been loaded yet in connectDoors()", 
						_str(door.worldID)));
			return;
		}

		UnloadedDoor *targetDoor = findPartnerDoor(*childWorld, door.doorID);
		if (targetDoor == nullptr)
		{
			Logger::logError(format("Cannot find partner door in world %1% for door %2% in world %3%",
						_str(door.worldID), _str(door.doorID), _str(world.world->getID())));
			return;
		}

		// add connection to this world's lookup table
		connectionLookup.emplace(std::piecewise_construct,
				std::forward_as_tuple(world.world->getID(), door.tile),             // src
				std::forward_as_tuple(childWorld->world->getID(), targetDoor->tile) // dst
				);

		Logger::logDebuggiest(format("Added world connection %1% to %2% from %3% through door %4%",
					door.doorID < 0 ? "up" : "down", _str(world.world->getID()), 
					_str(childWorld->world->getID()), _str(door.doorID)));

		// add node
		if (door.doorID > 0)
		{
			currentNode.children.emplace_back();
			WorldTreeNode &childNode = currentNode.children.back();
			childNode.parent = &currentNode;
			childNode.value = childWorld->world;

			// recurse
			connectDoors(childNode, *childWorld, connectionLookup, visitedWorlds);
		}

	}
}

WorldService::WorldLoader::LoadedWorld &WorldService::WorldLoader::loadWorld(const std::string &name, 
    	bool isBuilding)
{
  	return loadWorld(name, isBuilding, generateWorldID());
}

WorldService::WorldLoader::LoadedWorld &WorldService::WorldLoader::loadWorld(const std::string &name, 
    	bool isBuilding, WorldID worldID)
{
  	// create new LoadedWorld in place
  	LoadedWorld &loadedWorld = loadedWorlds.emplace(worldID, LoadedWorld{}).first->second;

	// load tmx
	auto path = getWorldFilePath(name, isBuilding);
	loadedWorld.tmx.load(path);
	loadedWorld.world = new World(worldID, path);
	loadedWorld.world->loadFromFile(loadedWorld.tmx);

	// find buildings and doors
	auto buildingLayer = std::find_if(loadedWorld.tmx.layers.begin(), loadedWorld.tmx.layers.end(),
	        [](const TMX::Layer &layer)
	        {
		    return layer.name == "buildings";
	        });

	// no buildings layer
	if (buildingLayer == loadedWorld.tmx.layers.end())
  	{
    	Logger::logDebuggier("No \"buildings\" layer");
		return loadedWorld;
  	}


	for (const TMX::TileWrapper &tile : buildingLayer->items)
	{
		if (tile.type != TMX::TILE_PROPERTY_SHAPE)
			continue;

		const TMX::PropertyObject &propObj = tile.property;

		if (propObj.hasProperty(TMX::PROPERTY_BUILDING_WORLD))
		{
			// buildings
			sf::IntRect bounds(
					(int) (tile.tile.position.x / Constants::tilesetResolution),
					(int) (tile.tile.position.y / Constants::tilesetResolution),
					(int) (propObj.dimensions.x / Constants::tilesetResolution),
					(int) (propObj.dimensions.y / Constants::tilesetResolution)
					);

			UnloadedBuilding b;
			b.bounds = bounds;
			b.insideWorldName = propObj.getProperty(TMX::PROPERTY_BUILDING_WORLD);
      		b.insideWorldID = generateWorldID();
			loadedWorld.buildings.push_back(b);
		}

		else if (propObj.hasProperty(TMX::PROPERTY_DOOR_ID))
		{
			// doors
			UnloadedDoor d;
			d.tile.x = (tile.tile.position.x / Constants::tilesetResolution);
			d.tile.y = (tile.tile.position.y / Constants::tilesetResolution);
			d.doorID = boost::lexical_cast<int>(propObj.getProperty(TMX::PROPERTY_DOOR_ID));
			d.doorTag = DOORTAG_UNKNOWN;

			// preloaded
			if (propObj.hasProperty(TMX::PROPERTY_DOOR_WORLD_ID))
			{
				d.doorTag = DOORTAG_WORLD_ID;
				d.worldID = boost::lexical_cast<WorldID>(propObj.getProperty(TMX::PROPERTY_DOOR_WORLD_ID));
			}

			// unloaded
			if (propObj.hasProperty(TMX::PROPERTY_DOOR_WORLD))
			{
				d.doorTag = DOORTAG_WORLD_NAME;
				d.worldName = propObj.getProperty(TMX::PROPERTY_DOOR_WORLD);
			}

			// sharing
			else if (propObj.hasProperty(TMX::PROPERTY_DOOR_WORLD_SHARE_SPECIFIER))
			{
				d.doorTag = DOORTAG_WORLD_SHARE;
				d.worldShare = propObj.getProperty(TMX::PROPERTY_DOOR_WORLD_SHARE_SPECIFIER);
			}

			// share source
			if (propObj.hasProperty(TMX::PROPERTY_DOOR_WORLD_SHARE_SOURCE))
			{
				d.worldShare = propObj.getProperty(TMX::PROPERTY_DOOR_WORLD_SHARE_SOURCE);
			}

			loadedWorld.doors.push_back(d);
		}
	}

	return loadedWorld;
}

WorldID WorldService::WorldLoader::generateWorldID()
{
	return lastWorldID++;
}

WorldService::WorldLoader::LoadedWorld *WorldService::WorldLoader::getLoadedWorld(WorldID id)
{
	auto found = loadedWorlds.find(id);
	return found == loadedWorlds.end() ? nullptr : &found->second;
}


WorldService::WorldLoader::UnloadedBuilding *WorldService::WorldLoader::findDoorBuilding
	(LoadedWorld &world, UnloadedDoor &door)
{
	const sf::Vector2i &tile = door.tile;

	for (UnloadedBuilding &building : world.buildings)
	{
		const sf::IntRect &bounds = building.bounds;
		if (bounds.left <= tile.x && bounds.left + bounds.width >= tile.x &&
		    	bounds.top <= tile.y && bounds.top + bounds.height >= tile.y)
			return &building;
	}

	return nullptr;
}


std::string WorldService::WorldLoader::getWorldFilePath(const std::string &name, bool isBuilding)
{
	static const std::string extension = ".tmx";

  	const std::string &root = Config::getResource(isBuilding ? "world.buildings" : "world.root");
  	return Utils::joinPaths(
      		root,
      		name + extension
      		);
}
