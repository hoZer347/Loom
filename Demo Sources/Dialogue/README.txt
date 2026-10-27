Dialogue
========

A Loom project showing a Dialogue Box streaming a .dialogue file. Press Play.

    Scenes/Main.loomscene     a floor under a Dialogue Box
    Assets/Intro.dialogue     what the box says, with its commands in brackets
    Assets/Fonts/             the font the text is baked from, and its licence
    Assets/Lit.shader         what the floor draws with
    Scripts/Narrator.hpp      the Dialogue Box's manager and its Shout command

The Dialogue Box came from the Hierarchy's right-click menu, under Cutscene,
with its DialogueManager swapped for Narrator, this project's own. It
streams the file it names into the UiText under it, which draws
[Color, ...] and [Size, ...] as they say. [Pause] waits for a click or a
key, and [Clear] starts a new page. Double-click Intro.dialogue in
the Scripts tab to edit it in the Dialogues panel.

Narrator.hpp's DialogueBinding members are commands too: [Shout, hey] writes
HEY! into the box. The Inspector's New Binding adds another to it and opens
it in Visual Studio. Add Component's Logic > DialogueManager makes a new
manager script for the object instead, and once it has compiled it is filed
under Custom > Dialogues.
