#pragma once
#include "state.hpp"
#include "world.hpp"

class GameState : public State
{
public:
	explicit GameState(BaseGame *game_);
	~GameState();


	virtual void tick(float delta) override;
	virtual void render(sf::RenderWindow &window) override;
	virtual void handleInput(const sf::Event &event) override;

private:
	BaseWorld *world;
	sf::View view;

};
