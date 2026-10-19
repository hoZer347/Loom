UI
==

A Loom project showing UiRect, UiText and UiButton over a scene. Press Play.

    Scenes/Main.loomscene     a card on a floor, under a menu
    Scripts/Menu.hpp          answers the menu's buttons
    Assets/Fonts/             the font the text is baked from, and its licence
    Assets/Lit.shader         what the floor and the card draw with

The menu on the left is a UiRect with a UiRect, a UiText and a UiButton on
each of its children, laid out from the panel's top. Red, Green and Blue tint
the card, and the count at the top goes up with every click a button takes.

More opens a popup drawn over part of the menu. Where the two overlap only the
popup's Close hears a click: the cursor is on the rect drawn last under it,
whatever is beneath. Close shuts the popup again.

The UI is drawn in the Game view, over every scene in it, and not in the
Scene view.
