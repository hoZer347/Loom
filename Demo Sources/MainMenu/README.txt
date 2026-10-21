MainMenu
========

A Loom project that starts on a menu and changes scene from it. Press Play.

    Scenes/Menu.loomscene     the menu: Start Game and Quit
    Scenes/World.loomscene    what Start Game opens
    Scripts/Menu.hpp          answers the menu's buttons
    Scripts/World.hpp         shows the seed the world started on
    Assets/Fonts/             the font the text is baked from, and its licence
    Assets/Lit.shader         what the floor, the ground and the tower draw with

Start Game picks a new seed and loads the world with Scene::Load. The scenes
are swapped once the frame that asked is over, so the click is finished with
before the menu goes. The world shows the seed it was handed through a static,
which outlives the scene that set it. Back to Menu loads the menu again.

Quit ends the game with Engine::Quit. In the editor that ends the run, and
Stop does the same at any point: the scenes go back to the ones Play started
on, whichever scene the game had moved to.
