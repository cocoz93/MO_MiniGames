//
#include "GameInstance.h"
#include <iostream>

int main()
{
    CGameInstance game;

    if (!game.Initialize())
    {
        std::cerr << "Failed to initialize game." << std::endl;
        return 1;
    }

    game.Run();
    game.Shutdown();

    std::cout << "Client terminated." << std::endl;
    return 0;
}