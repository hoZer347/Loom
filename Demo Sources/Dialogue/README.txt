Dialogue
========

A Loom project showing a Dialogue Box streaming a .dialogue file. Press Play.

    Scenes/Main.loomscene     a floor under a Dialogue Box
    Assets/Intro.dialogue     what the box says, with its commands in brackets
    Assets/Fonts/             the font the text is baked from, and its licence
    Assets/Lit.shader         what the floor draws with

The Dialogue Box came from the Hierarchy's right-click menu, under Cutscene.
Its DialogueManager streams the file it names into the UiText under it,
which draws [Color, ...] and [Size, ...] as they say. [Pause] waits for a
click or a key, and [Clear] starts a new page. Double-click Intro.dialogue in
the Scripts tab to edit it in the Dialogues panel.
