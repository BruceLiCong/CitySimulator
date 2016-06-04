#include <boost/filesystem.hpp>
#include <iostream>
#include "game.hpp"
#include "service/locator.hpp"

const std::string RESOURCE_DIR("res");

bool ensureCWD(int argc, char **argv)
{
	using namespace boost::filesystem;

	if (!exists(current_path() / RESOURCE_DIR))
	{
		// no args given
		if (argc != 2)
		{
			std::cerr << "Root directory not found. \nUsage: " << argv[0] << " <relative path to root dir>" << std::endl;
			return false;
		}

		// try supplied relative path
		path newPath = current_path() / argv[1];

		// doesn't exist
		if (!exists(newPath))
		{
			std::cerr << "Invalid path: " << newPath.string() << std::endl;
			return false;
		}

		// update path and try again
		current_path(newPath);
		return ensureCWD(1, argv);
	}

	return true;
}


void loadConfig(int &windowStyle)
{
	auto config = new ConfigService(RESOURCE_DIR, Constants::referenceConfigFileName, Constants::userConfigFileName);
	Locator::provide(SERVICE_CONFIG, config);

	// logging level
	Locator::locate<LoggingService>()->setLogLevel(Config::getString("debug.log-level"));

	int width, height;

	// borderless fullscreen
	if (Config::getBool("display.borderless-fullscreen", false))
	{
		windowStyle = sf::Style::None;

		auto screenSize(sf::VideoMode::getDesktopMode());
		width = screenSize.width;
		height = screenSize.height;
	}

		// standard window
	else
	{
		windowStyle = sf::Style::Default;
		width = Config::getInt("display.resolution.width");
		height = Config::getInt("display.resolution.height");
	}

	Constants::setWindowSize(width, height);
}

int main(int argc, char **argv)
{
	try
	{
		// logging before all
		Locator::provide(SERVICE_LOGGING, new LoggingService(std::cout, LOG_INFO));

		// ensure that the program root is in the project root
		if (!ensureCWD(argc, argv))
			return 1;

		// create essential services
		Locator::provide(SERVICE_EVENT, new EventService);

		// load window size/style
		int style;
		loadConfig(style);

		sf::RenderWindow window(sf::VideoMode(Constants::windowSize.x, Constants::windowSize.y), "Game", style);

		// create game
		Game game(window);
		game.beginGame();
		game.endGame();

		Logger::logInfo("Shutdown cleanly");
		return 0;
	}
	catch (std::exception &e)
	{
		Logger::logError(std::string("An error occurred: ") + e.what());
		return 1;
	}
	catch (...)
	{
		Logger::logError("An unknown error occurred");
		return 2;
	}
}