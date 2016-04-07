#ifndef CITYSIMULATOR_WORLD_SERVICE_HPP
#define CITYSIMULATOR_WORLD_SERVICE_HPP

#include "base_service.hpp"
#include "world.hpp"
#include "building.hpp"
#include "bodydata.hpp"

typedef std::unordered_map<Location, Location> WorldConnectionTable;

class WorldService : public BaseService
{
public:
	WorldService(const std::string &mainWorldPath, const std::string &tilesetPath);

	virtual void onEnable() override;

	virtual void onDisable() override;

	World &getWorld();

private:
	typedef TreeNode<World> WorldTreeNode;

	Tileset tileset;
	std::string mainWorldName;

	World *mainWorld;
	WorldTreeNode worldTree;
	WorldConnectionTable connectionLookup;

	struct WorldLoader
	{
		struct UnloadedBuilding
		{
			WorldID insideWorldID;
			std::string insideWorldName;
			sf::IntRect bounds;
		};

		enum DoorTag
		{

			/**
			 * This door connects to a preloaded world
			 */
			DOORTAG_WORLD_ID,

			/**
			 * This door connects to the world of the
			 * source for this share tag
			 */
			DOORTAG_WORLD_SHARE,

			/**
			 * This door connects to a currently
			 * unloaded instance of this world name
			 */
			DOORTAG_WORLD_NAME,

			/**
			 * Error
			 */
			DOORTAG_UNKNOWN
		};

		struct UnloadedDoor
		{
			sf::Vector2i tile;
			int doorID;

			DoorTag doorTag;
			std::string worldName;
			std::string worldShare;
			WorldID worldID;
		};

		struct LoadedWorld
		{
			World *world;
			TMX::TileMap tmx;

			std::vector<UnloadedDoor> doors;
			std::vector<UnloadedBuilding> buildings;

			bool failed() const
			{
				return world == nullptr;
			}
		};

		WorldID lastWorldID;
		std::unordered_set<int> flippedTileGIDs;

		std::map<int, LoadedWorld> loadedWorlds;


		WorldLoader();

		/**
		 * Recursively loads all worlds into the WorldTree
		 * @param connectionLookup The connection lookup table to populate
		 * @param treeRoot The world tree to populate
		 * @return The main world
		 */
		World *loadWorlds(const std::string &mainWorldName,
				WorldConnectionTable &connectionLookup, WorldTreeNode &treeRoot);

		/**
		 * Loads the given world with the given ID
		 * @param name The world name, sans file extension
		 * @param isBuilding True if the world is a building, otherwise false
	 	 * @param worldID The world's allocated ID
		 * @return A reference to the newly loaded world
		 */

		LoadedWorld &loadWorld(const std::string &name, bool isBuilding, WorldID worldID);
		/**
		 * Loads the given world with a newly allocated ID
		 * @param name The world name, sans file extension
		 * @param isBuilding True if the world is a building, otherwise false
		 * @return A reference to the newly loaded world
		 */
		LoadedWorld &loadWorld(const std::string &name, bool isBuilding);

		/**
		 * Discovers all worlds by recursing door connections, and loads
		 * them
		 */
		void discoverAndLoadAllWorlds(LoadedWorld &world, std::set<WorldID> &visitedWorlds);

		/**
		 * Populates the WorldTree with connections between doors
		 */
		void connectDoors(WorldTreeNode &parent, LoadedWorld &world, 
				WorldConnectionTable &connectionLookup, std::set<WorldID> &visitedWorlds);

		/**
		 * @return The next world ID to use
		 */
		WorldID generateWorldID();

		/**
		 * @return The world with the given ID
		 */
		LoadedWorld *getLoadedWorld(WorldID id);

		/**
		 * @param name The world name, sans file extension
		 * @param isBuilding True if the world is a building, otherwise false
		 * @return The true file path of the world
		 */
		std::string getWorldFilePath(const std::string &name, bool isBuilding);

		/**
		 * @return The building that physically contains the given door, null if not found
		 */
		UnloadedBuilding *findDoorBuilding(LoadedWorld &world, UnloadedDoor &door);

		/**
		 * @return The partner door in the given world with the given door ID,
		 * null if not found
		 */
		UnloadedDoor *findPartnerDoor(LoadedWorld &world, int doorID);

	};

};

#endif
