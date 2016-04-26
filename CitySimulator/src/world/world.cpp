#include "world.hpp"
#include "service/locator.hpp"

WorldService::WorldService(const std::string &mainWorldPath, const std::string &tilesetPath)
		: tileset(tilesetPath), mainWorldName(mainWorldPath), entityTransferListener(this)
{
}

void WorldService::onEnable()
{
	Logger::logDebug("Starting to load worlds");
	Logger::pushIndent();
	
	// load and connect all worlds
	WorldLoader loader(connectionLookup, terrainCache);
	loader.loadWorlds(mainWorldName);

	// generate tileset
	tileset.load();
	tileset.convertToTexture(loader.flippedTileGIDs);

	// load terrain
	for (auto &pair : terrainCache)
		pair.second.applyTiles(tileset);

	// transfer loaded worlds
	for (auto &lwPair : loader.loadedWorlds)
	{
		World *world = lwPair.second.world;
		worlds[world->getID()] = world;
	}

	// transfer buildings
	BuildingMap &bm = getMainWorld()->getBuildingMap();
	for (WorldLoader::LoadedBuilding &building : loader.buildings)
	{
		Building &b = bm.addBuilding(building.bounds, building.insideWorldID);
		for (WorldLoader::LoadedDoor &door : building.doors)
			b.addDoor(Location(b.getOutsideWorld()->getID(), door.tile), door.doorID);
	}

	// load collisions
	for (auto &pair : terrainCache)
		pair.second.loadBlockData();

	Logger::popIndent();

	// register listener
	Locator::locate<EventService>()->registerListener(
			&entityTransferListener, EVENT_HUMAN_SWITCH_WORLD);
}

void WorldService::onDisable()
{
	Logger::logDebug("Deleting all loaded worlds");
	for (auto &pair : worlds)
		delete pair.second;
}


World *WorldService::getMainWorld()
{
	return getWorld(0);
}

World *WorldService::getWorld(WorldID id)
{
	auto world = worlds.find(id);
	return world == worlds.end() ? nullptr : world->second;
}

bool WorldService::getConnectionDestination(const Location &src, Location &out)
{
	auto dst = connectionLookup.find(src);
	if (dst == connectionLookup.end())
		return false;

	out = dst->second;
	return true;
}

WorldService::EntityTransferListener::EntityTransferListener(WorldService *ws) : ws(ws)
{
}

void WorldService::EntityTransferListener::onEvent(const Event &event)
{
	World *newWorld = ws->getWorld(event.switchWorld.newWorld);
	b2World *newBWorld = newWorld->getBox2DWorld();
	// todo nullptr should never be returned, throw exception instead

	EntityService *es = Locator::locate<EntityService>();
	PhysicsComponent *phys = es->getComponent<PhysicsComponent>(event.entityID, COMPONENT_PHYSICS); // todo never return null
	b2World *oldBWorld = phys->bWorld;

	// clone body and add to new world
	b2Body *oldBody = phys->body;
	b2Body *newBody = es->createBody(newBWorld, oldBody);

	// remove from old world
	oldBWorld->DestroyBody(oldBody);

	// update component
	phys->body = newBody;
	phys->bWorld = newBWorld;
}

World::World(WorldID id, const std::string &name, bool outside) 
: id(id), name(name), outside(outside)
{
	transform.scale(Constants::tileSizef, Constants::tileSizef);

	if (outside)
		buildingMap.reset(this);
}

void World::setTerrain(WorldTerrain &terrain)
{
	this->terrain = &terrain;
}

WorldTerrain *World::getTerrain()
{
	return terrain;
}

CollisionMap *World::getCollisionMap()
{
	return terrain == nullptr ? nullptr : terrain->getCollisionMap();
}

BuildingMap &World::getBuildingMap()
{
	return *buildingMap;
}

b2World *World::getBox2DWorld()
{
	return &getCollisionMap()->world;
}

sf::Vector2i World::getPixelSize() const
{
	return Utils::toPixel(getTileSize());
}

sf::Vector2i World::getTileSize() const
{
	return terrain->size;
}

sf::Transform World::getTransform() const
{
	return transform;
}

WorldID World::getID() const
{
	return id;
}

std::string World::getName() const
{
	return name;
}

bool World::isOutside() const
{
	return outside;
}

void World::tick(float delta)
{
	// todo fixed time step
	getBox2DWorld()->Step(delta, 6, 2);
}

// todo move to world_rendering
void World::draw(sf::RenderTarget &target, sf::RenderStates states) const
{
	states.transform *= transform;

	// terrain
	terrain->render(target, states, false);

	// entities
	Locator::locate<EntityService>()->renderSystems();

	// overterrain
	terrain->render(target, states, true);

}
