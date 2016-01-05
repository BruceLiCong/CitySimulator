#include "ai.hpp"

EntityBrain::EntityBrain(EntityID e) :
		entity(e),
		controller(e, Config::getFloat("debug.movement.force"),
		           Config::getFloat("debug.movement.max-speed.walk"),
		           Config::getFloat("debug.movement.max-speed.run"))

{
	EntityService *es = Locator::locate<EntityService>();
	if (!es->hasComponent(entity, COMPONENT_PHYSICS))
		error("Could not create brain for entity %1% as it doesn't have a physics component", std::to_string(entity));

	phys = es->getComponent<PhysicsComponent>(entity, COMPONENT_PHYSICS);
}


EntityBrain::~EntityBrain()
{
}


void EntityBrain::tick(float delta)
{
	controller.tick(phys, delta);
}

void EntityBrain::setMoving(bool moving, DirectionType direction)
{
	// todo
}
