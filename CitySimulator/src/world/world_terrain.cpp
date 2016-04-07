#include "world.hpp"
#include "service/logging_service.hpp"

WorldTerrain::WorldTerrain(World *container) : BaseWorld(container), tileset(tileset)
{
	tileVertices.setPrimitiveType(sf::Quads);
	overLayerVertices.setPrimitiveType(sf::Quads);
}

int WorldTerrain::getBlockIndex(const sf::Vector2i &pos, LayerType layerType)
{
	int index = (pos.x + pos.y * container->tileSize.x);
	index += layers.at(layerType).depth * container->tileSize.x * container->tileSize.y;
	index *= 4;

	return index;
}


int WorldTerrain::getVertexIndex(const sf::Vector2i &pos, LayerType layerType)
{
	int index = (pos.x + pos.y * container->tileSize.x);
	int depth = layers.at(layerType).depth;
	if (isOverLayer(layerType))
	{
		int diff = tileLayerCount - overLayerCount;
		depth -= diff;
	}

	index += depth * container->tileSize.x * container->tileSize.y;
	index *= 4;

	return index;
}


void WorldTerrain::rotateObject(sf::Vertex *quad, float degrees, const sf::Vector2f &pos)
{
	sf::Vector2f origin(pos.x, pos.y + 1);

	float radians(degrees * Math::degToRad);
	const float c(cos(radians));
	const float s(sin(radians));

	for (int i = 0; i < 4; ++i)
	{
		sf::Vector2f vPos(quad[i].position);
		vPos -= origin;

		sf::Vector2f rotated(vPos);
		rotated.x = vPos.x * c - vPos.y * s;
		rotated.y = vPos.x * s + vPos.y * c;

		rotated += origin;

		quad[i].position = rotated;
	}
}

void WorldTerrain::positionVertices(sf::Vertex *quad, const sf::Vector2f &pos, int delta)
{
	quad[0].position = sf::Vector2f(pos.x, pos.y);
	quad[1].position = sf::Vector2f(pos.x + delta, pos.y);
	quad[2].position = sf::Vector2f(pos.x + delta, pos.y + delta);
	quad[3].position = sf::Vector2f(pos.x, pos.y + delta);
}


sf::VertexArray &WorldTerrain::getVertices(const LayerType &layerType)
{
	return isOverLayer(layerType) ? overLayerVertices : tileVertices;
}

void WorldTerrain::resizeVertices()
{
	sf::Vector2i tilesetResolution(container->getTileSize());
	const int sizeMultiplier = tilesetResolution.x * tilesetResolution.y * 4;

	blockTypes.resize(tileLayerCount * sizeMultiplier);

	tileVertices.resize((tileLayerCount - overLayerCount) * sizeMultiplier);
	overLayerVertices.resize(overLayerCount * sizeMultiplier);
}

void WorldTerrain::registerLayer(LayerType layerType, int depth)
{
	layers.emplace_back(layerType, depth);
	Logger::logDebuggier(format("Found %3%layer type %1% at depth %2%", _str(layerType), _str(depth),
	                            isOverLayer(layerType) ? "overterrain " : ""));
}

void WorldTerrain::setBlockType(const sf::Vector2i &pos, BlockType blockType, LayerType layer, int rotationAngle,
                                int flipGID)
{
	int vertexIndex = getVertexIndex(pos, layer);
	sf::VertexArray &vertices = getVertices(layer);
	auto size = vertices.getVertexCount();
	sf::Vertex *quad = &vertices[vertexIndex];

	positionVertices(quad, static_cast<sf::Vector2f>(pos), 1);
	tileset->textureQuad(quad, blockType, rotationAngle, flipGID);

	blockTypes[getBlockIndex(pos, layer)] = blockType;
}

void WorldTerrain::addObject(const sf::Vector2f &pos, BlockType blockType, float rotationAngle, int flipGID)
{
	// TODO: simply append object vertices to world vertices; remember order of objects so vertices can be referenced in the future

	std::vector<sf::Vertex> quad(4);
	sf::Vector2f adjustedPos = sf::Vector2f(pos.x / Constants::tilesetResolution,
	                                        (pos.y - Constants::tilesetResolution) / Constants::tilesetResolution);

	positionVertices(&quad[0], adjustedPos, 1);
	tileset->textureQuad(&quad[0], blockType, 0, flipGID);

	if (rotationAngle != 0)
		rotateObject(&quad[0], rotationAngle, adjustedPos);

	sf::VertexArray &vertices = getVertices(LAYER_OBJECTS);
	for (int i = 0; i < 4; ++i)
		vertices.append(quad[i]);

	objects.emplace_back(blockType, rotationAngle, Utils::toTile(pos));
}

const std::vector<WorldObject> &WorldTerrain::getObjects()
{
	return objects;
}

const std::vector<WorldLayer> &WorldTerrain::getLayers()
{
	return layers;
}

void WorldTerrain::discoverLayers(std::vector<TMX::Layer> &layers, std::vector<LayerType> &layerTypes)
{
	auto layerIt = layers.begin();
	int depth(0);
	tileLayerCount = 0;
	overLayerCount = 0;
	while (layerIt != layers.end())
	{
		auto layer = *layerIt;

		// unknown layer type
		LayerType layerType = layerTypeFromString(layer.name);
		if (layerType == LAYER_UNKNOWN)
		{
			Logger::logError("Invalid layer name: " + layer.name);
			layerIt = layers.erase(layerIt);
			continue;
		}

		// invisible layer
		if (!layer.visible)
		{
			layerIt = layers.erase(layerIt);
			continue;
		}

		if (isTileLayer(layerType))
			++tileLayerCount;
		if (isOverLayer(layerType))
			++overLayerCount;

		// add layer
		registerLayer(layerType, depth);
		layerTypes.push_back(layerType);

		++depth;
		++layerIt;
	}
}

void WorldTerrain::discoverFlippedTiles(const std::vector<TMX::Layer> &layers, std::unordered_set<int> &flippedGIDs)
{
	for (const TMX::Layer &layer : layers)
	{
		for (const TMX::TileWrapper &tile : layer.items)
		{
			if (!tile.tile.isFlipped() || tile.tile.getGID() == BLOCK_BLANK)
				continue;

			int flipGID = tile.tile.getFlipGID();

			// already done
			if (flippedGIDs.find(flipGID) == flippedGIDs.end())
				flippedGIDs.insert(flipGID);
		}
	}
}

void WorldTerrain::loadLayers(const std::vector<TMX::Layer> &layers)
{
	int layerIndex(0);

	for (const TMX::Layer &layer : layers)
	{
		LayerType layerType = this->layers[layerIndex++].type;

		if (layerType == LAYER_OBJECTS)
		{
			// objects
			for (const TMX::TileWrapper &tile : layer.items)
			{
				BlockType blockType = static_cast<BlockType>(tile.tile.getGID());
				if (blockType == BLOCK_BLANK)
					continue;

				addObject(tile.tile.position, blockType, tile.objectRotation, tile.tile.getFlipGID());
			}
		}

		else if (isTileLayer(layerType))
		{
			sf::Vector2i tempPos;

			// tiles
			for (const TMX::TileWrapper &tile : layer.items)
			{
				BlockType blockType = static_cast<BlockType>(tile.tile.getGID());
				if (blockType == BLOCK_BLANK)
					continue;

				tempPos.x = (int) tile.tile.position.x;
				tempPos.y = (int) tile.tile.position.y;

				setBlockType(tempPos, blockType, layerType, tile.tile.getRotationAngle(), tile.tile.getFlipGID());
			}
		}
	}
}

void WorldTerrain::render(sf::RenderTarget &target, sf::RenderStates &states, bool overLayers) const
{
	states.texture = tileset->getTexture();
	target.draw(overLayers ? overLayerVertices : tileVertices, states);
}

void WorldTerrain::load(TMX::TileMap &tileMap, std::unordered_set<int> &flippedGIDs, Tileset *tileset)
{
	this->tileset = tileset;

	// find layer count and depths
	std::vector<LayerType> types;
	discoverLayers(tileMap.layers, types);

	Logger::logDebug(format("Discovered %1% tile layer(s), of which %2% is/are overlayer(s)",
	                        _str(tileLayerCount), _str(overLayerCount)));

	// resize vertex array to accommodate for layer count
	resizeVertices();

	// collect any gids that need flipping
	discoverFlippedTiles(tileMap.layers, flippedGIDs);
}
