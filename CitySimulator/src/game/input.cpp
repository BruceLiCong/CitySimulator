#include <SFML/Window/Keyboard.hpp>
#include "world.hpp"
#include "ai.hpp"
#include "service/locator.hpp"

void InputService::onEnable()
{
	bindings.clear();

	bindKey(KEY_UP, sf::Keyboard::W);
	bindKey(KEY_LEFT, sf::Keyboard::A);
	bindKey(KEY_DOWN, sf::Keyboard::S);
	bindKey(KEY_RIGHT, sf::Keyboard::D);
	bindKey(KEY_YIELD_CONTROL, sf::Keyboard::Tab);
	bindKey(KEY_SPRINT, sf::Keyboard::Space);
	bindKey(KEY_EXIT, sf::Keyboard::Escape);
	// todo load from config

	// check all keys have been registered
	if (bindings.left.size() != KEY_UNKNOWN)
	{
		Logger::logError(format("Expected %1% key bindings, received %2% instead",
								_str(KEY_UNKNOWN), _str(bindings.left.size())));

		error("Invalid number of key bindings");
	}

	// listen to input events
	auto events = Locator::locate<EventService>();
	events->registerListener(this, EVENT_RAW_INPUT_KEY);
	events->registerListener(this, EVENT_RAW_INPUT_CLICK);
}

void InputService::onDisable()
{
	Locator::locate<EventService>()->unregisterListener(this);
}


void InputService::bindKey(InputKey binding, sf::Keyboard::Key key)
{
	std::string verb("Set");

	// remove existing
	auto existing = bindings.left.find(binding);
	if (existing != bindings.left.end())
	{
		verb = "Replaced existing";
		bindings.left.replace_data(existing, key);
	}

	bindings.left.insert({binding, key});
	Logger::logDebuggier(format("%1% binding for key %2%: %3%", verb, _str(key), _str(binding)));
}

void InputService::onEvent(const Event &event)
{
	if (event.type == EVENT_RAW_INPUT_CLICK)
		handleMouseEvent(event);
	else
		handleKeyEvent(event);
}

void InputService::setPlayerEntity(EntityID entity)
{
	auto es = Locator::locate<EntityService>();
	if (!es->hasComponent(entity, COMPONENT_INPUT))
		error("Cannot set player entity to %1% as it doesn't have an input component", _str(entity));

	// switch out brain
	auto head = es->getComponent<InputComponent>(entity, COMPONENT_INPUT);
	playersOldBrain = boost::dynamic_pointer_cast<EntityBrain>(head->brain);

	if (!inputBrain)
		inputBrain.reset(new InputBrain(entity)); // lazy init
	else
		inputBrain->setEntity(entity);

	head->brain = inputBrain;

	playerEntity = entity;
	Locator::locate<CameraService>()->setTrackedEntity(entity);
}

void InputService::clearPlayerEntity()
{
	// replace brain
	auto es = Locator::locate<EntityService>();
	auto head = es->getComponent<InputComponent>(*playerEntity, COMPONENT_INPUT);
	head->brain = playersOldBrain;
	playersOldBrain.reset();

	playerEntity.reset();
	Locator::locate<CameraService>()->clearPlayerEntity();
}


struct ClickCallback : public b2QueryCallback
{
	b2Body *clickedBody = nullptr;

	virtual bool ReportFixture(b2Fixture *fixture) override
	{
		clickedBody = fixture->GetBody();
		return false; // stop after single
	}
};

boost::optional<EntityIdentifier *> InputService::getClickedEntity(const sf::Vector2i &screenPos, float radius)
{
	// translate to world tile coordinates
	sf::Vector2f pos(Utils::toTile(Locator::locate<RenderService>()->mapScreenToWorld(screenPos)));

	// find body
	b2AABB aabb;
	ClickCallback callback;
	aabb.lowerBound.Set(pos.x - radius, pos.y - radius);
	aabb.upperBound.Set(pos.x + radius, pos.y + radius);
	Locator::locate<WorldService>()->getWorld().getBox2DWorld()->QueryAABB(&callback, aabb);

	if (callback.clickedBody == nullptr)
		return boost::optional<EntityIdentifier *>();

	return Locator::locate<EntityService>()->getEntityIDFromBody(*callback.clickedBody);
}

void InputService::handleMouseEvent(const Event &event)
{
	// only left clicks for now
	if (event.rawInputClick.button != sf::Mouse::Left || !event.rawInputClick.pressed)
		return;

	// only control if not currently controlling
	if (hasPlayerEntity())
		return;

	sf::Vector2i windowPos(event.rawInputClick.x, event.rawInputClick.y);

	auto clicked(getClickedEntity(windowPos, 0));
	if (clicked.is_initialized())
		setPlayerEntity(clicked.get()->id);

}

void InputService::handleKeyEvent(const Event &event)
{
	InputKey binding(getBinding(event.rawInputKey.key));

	// unrecognized key
	if (binding == KEY_UNKNOWN)
		return;

	// quit
	if (binding == KEY_EXIT)
	{
		Logger::logDebug("Exit key pressed, quitting");
		Locator::locate<RenderService>()->getWindow()->close();
		return;
	}

	EventService *es = Locator::locate<EventService>();
	bool hasEntity = hasPlayerEntity();

	Event e;

	// assign entity id
	e.entityID = hasEntity ? *playerEntity : CAMERA_ENTITY;

	// yield entity control
	if (binding == KEY_YIELD_CONTROL)
	{
		if (hasEntity)
		{
			e.type = EVENT_INPUT_YIELD_CONTROL;
			clearPlayerEntity();
			es->callEvent(e);
		}
		return;
	}

	// sprint
	if (binding == KEY_SPRINT)
	{
		e.type = EVENT_INPUT_SPRINT;
		e.sprintToggle.start = event.rawInputKey.pressed;
		es->callEvent(e);
		return;
	}

	// movement
	if (event.rawInputKey.pressed)
	{

		e.type = EVENT_INPUT_MOVE;
		e.move.halt = false;

		DirectionType direction;

		switch (binding)
		{
			case KEY_UP:
				direction = DIRECTION_NORTH;
				break;
			case KEY_LEFT:
				direction = DIRECTION_WEST;
				break;
			case KEY_DOWN:
				direction = DIRECTION_SOUTH;
				break;
			case KEY_RIGHT:
				direction = DIRECTION_EAST;
				break;
			default:
				error("An invalid movement key slipped through InputService's onEvent: %1%",
					  _str(binding));
				return;
		}

		float x, y;
		Direction::toVector(direction, x, y);
		e.move.x = x;
		e.move.y = y;

		es->callEvent(e);
		return;
	}
}

sf::Keyboard::Key InputService::getKey(InputKey binding)
{
	auto result = bindings.left.find(binding);
	return result == bindings.left.end() ? sf::Keyboard::Unknown : result->second;
}

InputKey InputService::getBinding(sf::Keyboard::Key key)
{

	auto result = bindings.right.find(key);
	return result == bindings.right.end() ? InputKey::KEY_UNKNOWN : result->second;
}

void MovementController::reset(EntityID entity, float movementForce, float maxWalkSpeed, float maxSprintSpeed)
{
	this->entity = entity;
	this->movementForce = movementForce;
	this->maxSpeed = maxWalkSpeed;
	this->maxSprintSpeed = maxSprintSpeed;
	this->steering.SetZero();
	running = false;
}


void MovementController::registerListeners()
{
	Locator::locate<EventService>()->registerListener(this, EVENT_INPUT_MOVE);
	Locator::locate<EventService>()->registerListener(this, EVENT_INPUT_SPRINT);
}

void MovementController::unregisterListeners()
{
	Locator::locate<EventService>()->unregisterListener(this);
}

b2Vec2 MovementController::tick(float delta, float &newMaxSpeed)
{
	newMaxSpeed = running ? maxSprintSpeed : maxSpeed;
	b2Vec2 ret = steering;
	ret *= movementForce;
	steering.SetZero();
	return ret;
}


void MovementController::tick(PhysicsComponent *phys, float delta)
{
	float maxSpeed;
	b2Vec2 steering(tick(delta, maxSpeed));
	phys->steering.Set(steering.x, steering.y);
	phys->maxSpeed = maxSpeed;

}

void MovementController::onEvent(const Event &event)
{
	// todo remove check and unregister listener instead
	if (event.entityID != entity)
		return;

	// sprinting
	if (event.type == EVENT_INPUT_SPRINT)
	{
		running = event.sprintToggle.start;
		return;
	}

//	steering += b2Vec2(event.move.x, event.move.y);
	move({event.move.x, event.move.y});
}


void MovementController::move(const sf::Vector2f &vector)
{
	b2Vec2 vec = toB2Vec(vector);
//	vec.Normalize();
	steering += vec;
}


void MovementController::halt()
{
	steering.SetZero();
}
