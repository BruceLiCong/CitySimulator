#include <config.hpp>
#include "maploader.hpp"
#include "world.hpp"
#include "logger.hpp"

bool isCollidable(BlockType blockType)
{
	static const std::set<BlockType> collidables(
			{WATER, TREE, BUILDING_WALL, BUILDING_EDGE, BUILDING_ROOF, BUILDING_ROOF_CORNER});
	return collidables.find(blockType) != collidables.end();
}

LayerType layerTypeFromString(const std::string &s)
{
	if (s == "underterrain")
		return LAYER_UNDERTERRAIN;
	if (s == "terrain")
		return LAYER_TERRAIN;
	if (s == "overterrain")
		return LAYER_OVERTERRAIN;
	if (s == "objects")
		return LAYER_OBJECTS;
	if (s == "collisions")
		return LAYER_COLLISIONS;

	Logger::logWarning("Unknown LayerType: " + s);
	return LAYER_COUNT;
}

bool isTileLayer(LayerType &layerType)
{
	return layerType == LAYER_UNDERTERRAIN || layerType == LAYER_TERRAIN || layerType == LAYER_OVERTERRAIN;
}

bool compareRectsHorizontally(const sf::FloatRect &a, const sf::FloatRect &b)
{
	if (a.top < b.top) return true;
	if (b.top < a.top) return false;

	if (a.left < b.left) return true;
	if (b.left < a.left) return false;

	return false;
}

bool compareRectsVertically(const sf::FloatRect &a, const sf::FloatRect &b)
{
	if (a.left < b.left) return true;
	if (b.left < a.left) return false;

	if (a.top < b.top) return true;
	if (b.top < a.top) return false;

	return false;
}

void CollisionMap::findCollidableTiles(std::vector<sf::FloatRect> &rectsRet) const
{
	sf::Vector2i worldTileSize = container->getTileSize();

	// find collidable tiles
	for (auto y = 0; y < worldTileSize.y; ++y)
	{
		for (auto x = 0; x < worldTileSize.x; ++x)
		{
			BlockType bt = container->getBlockAt({x, y}, LAYER_TERRAIN); // the only collidable tile layer
			if (!isCollidable(bt))
				continue;

			sf::Vector2f pos(static_cast<float>(x) * Constants::tileSizef,
			                 static_cast<float>(y) * Constants::tileSizef);
			sf::Vector2f size(Constants::tileSizef, Constants::tileSizef); // todo: assuming all tiles are the same size

			sf::FloatRect rect(pos, size);
			rectsRet.push_back(rect);

			// todo
			// fill every tile with a single tile sized rectangle
			// attempt to merge every rectangle with its adjacent rectangles (smallest/largest area first?)
			// vertically, then horizontally, then choose the set with the least rectangles
		}
	}

	// todo repeat for object layer
}

void CollisionMap::mergeAdjacentTiles(std::vector<sf::Rect<float>> &rects)
{
	// join individual rects
	sort(rects.begin(), rects.end(), compareRectsHorizontally);
	mergeHelper(rects, true);

	// join rows together
	sort(rects.begin(), rects.end(), compareRectsVertically);
	mergeHelper(rects, false);
}

void CollisionMap::mergeHelper(std::vector<sf::FloatRect> &rects, bool moveOnIfFar)
{
	bool (*nextRowFunc)(const sf::FloatRect *last, const sf::FloatRect *current);
	if (moveOnIfFar)
	{
		nextRowFunc = [](const sf::FloatRect *lastRect, const sf::FloatRect *rect)
		{
			return powf(rect->left - lastRect->left, 2.f) + powf(rect->top - lastRect->top, 2.f) >
			       Constants::tileSizef * Constants::tileSizef;
		};
	}
	else
	{
		nextRowFunc = [](const sf::FloatRect *lastRect, const sf::FloatRect *rect)
		{
			// adjacent and same dimensions
			return !(lastRect->left <= rect->left + rect->width &&
			         rect->left <= lastRect->left + lastRect->width &&
			         lastRect->top <= rect->top + rect->height &&
			         rect->top <= lastRect->top + lastRect->height &&
			         lastRect->width == rect->width && lastRect->height == rect->height);
		};
	}


	std::vector<sf::FloatRect> rectsCopy(rects.begin(), rects.end());
	rects.clear();

	sf::FloatRect *current = nullptr;
	sf::FloatRect *lastRect = nullptr;

	rectsCopy.push_back(sf::FloatRect(-100.f, -100.f, 0.f, 0.f)); // to ensure the last rect is included

	for (size_t i = 0; i < rectsCopy.size(); ++i)
	{
		sf::FloatRect *rect = &rectsCopy[i];

		// no current rect expanding
		if (current == nullptr)
		{
			current = lastRect = rect;
			continue;
		}

		if ((nextRowFunc)(lastRect, rect))
		{
			rects.push_back(*current);
			current = lastRect = rect;
			continue;
		}

		// stretch current
		current->left = std::min(current->left, rect->left);
		current->top = std::min(current->top, rect->top);
		current->width = std::max(current->left + current->width, rect->left + rect->width) - current->left;
		current->height = std::max(current->top + current->height, rect->top + rect->height) - current->top;

		lastRect = rect;
	}
}

CollisionMap::~CollisionMap()
{
	if (worldBody != nullptr)
		world.DestroyBody(worldBody);
}

void CollisionMap::getSurroundingTiles(const sf::Vector2i &tilePos, std::set<sf::Rect<float>> &ret)
{
	const static int edge = 1; // todo dependent on entity size

	// gather all (unique) rects in the given range
	sf::FloatRect rect;
	for (int y = -edge; y <= edge; ++y)
	{
		for (int x = -edge; x <= edge; ++x)
		{
			sf::Vector2i offsetTile(tilePos.x + x, tilePos.y + y);
			if (getRectAt(offsetTile, rect))
				ret.insert(rect);
		}
	}
}

bool CollisionMap::getRectAt(const sf::Vector2i &tilePos, sf::FloatRect &ret)
{
	auto result(cellGrid.find(Utils::toPixel(tilePos)));
	if (result == cellGrid.end())
		return false;

	ret = result->second;
	return true;
}

void CollisionMap::load()
{
	std::vector<sf::FloatRect> rects;

	// gather all collidable tiles
	findCollidableTiles(rects);

	// merge adjacents
	mergeAdjacentTiles(rects);

	// debug drawing
	world.SetDebugDraw(&b2Renderer);
	b2Renderer.SetFlags(b2Draw::e_shapeBit);

	// create world body
	b2BodyDef worldBodyDef;
	worldBodyDef.type = b2_staticBody;
	worldBody = world.CreateBody(&worldBodyDef);

	// world borders
	int borderThickness = Constants::tileSize;
	int padding = Constants::tileSize / 4;
	auto worldSize = container->pixelSize;
	rects.emplace_back(-borderThickness - padding, 0, borderThickness, worldSize.y);
	rects.emplace_back(0, -borderThickness - padding, worldSize.y, borderThickness);
	rects.emplace_back(worldSize.x + padding, 0, borderThickness, worldSize.y);
	rects.emplace_back(0, worldSize.y + padding, worldSize.y, borderThickness);

	// todo make big collision rectangles hollow to work better with box2d?

	// collision fixtures
	b2FixtureDef fixDef;
	b2PolygonShape box;
	fixDef.shape = &box;
	fixDef.friction = 0.f;
	for (auto &unscaledRect : rects)
	{
		auto rect = Utils::scaleToBox2D(unscaledRect);
		b2Vec2 rectCentre(rect.left + rect.width / 2,
		                  rect.top + rect.height / 2);
		box.SetAsBox(
				rect.width / 2, // half dimensions
				rect.height / 2,
				rectCentre,
				0.f
		);
		worldBody->CreateFixture(&fixDef);
	}
}

World::World() : terrain(this), collisionMap(this)
{
	transform.scale(Constants::tileSizef, Constants::tileSizef);
}

void World::loadFromFile(const std::string &filename, const std::string &tileset)
{
	Logger::logDebug(str(boost::format("Began loading world %1%") % filename));
	Logger::pushIndent();

	TMX::TileMap *tmx = TMX::TileMap::load(filename);

	// failure
	if (tmx == nullptr)
		throw std::runtime_error("Could not load world from null TileMap");

	sf::Vector2i size(tmx->width, tmx->height);
	resize(size);

	// terrain
	terrain.load(tmx, tileset);
	collisionMap.load();

	Logger::popIndent();
	Logger::logInfo(str(boost::format("Loaded world %1%") % filename));
	delete tmx;
}

void World::resize(sf::Vector2i size)
{
	tileSize = size;
	pixelSize = Utils::toPixel(size);
}

WorldTerrain &World::getTerrain()
{
	return terrain;
}

CollisionMap &World::getCollisionMap()
{
	return collisionMap;
}

b2World *World::getBox2DWorld()
{
	return &collisionMap.world;
}

sf::Vector2i World::getPixelSize() const
{
	return pixelSize;
}

sf::Vector2i World::getTileSize() const
{
	return tileSize;
}

sf::Transform World::getTransform() const
{
	return transform;
}

BlockType World::getBlockAt(const sf::Vector2i &tile, LayerType layer)
{
	int index = terrain.getBlockIndex(tile, layer);
	return terrain.blockTypes[index];
}

void World::getSurroundingTiles(const sf::Vector2i &tilePos, std::set<sf::FloatRect> &ret)
{
	return collisionMap.getSurroundingTiles(tilePos, ret);
}

void World::tick(float delta)
{
	// todo fixed time step
	getBox2DWorld()->Step(delta, 6, 2);
}

void World::draw(sf::RenderTarget &target, sf::RenderStates states) const
{
	states.transform *= transform;

	// terrain
	terrain.render(target, states);
}
