Audio
=====

A Loom project showing sound. Press Play, with headphones or speakers on.

    Scenes/Main.loomscene     a humming orb circling between two bells
    Scripts/Orbit.hpp         keeps the orb circling
    Scripts/Chime.hpp         rings a bell with Loom::Audio::PlayAt
    Scripts/Shape.hpp         draws the cubes and the sphere
    Assets/                   the sounds and the shader

The Camera carries an AudioListener, so everything is heard from where it
looks on. The orb carries a looping AudioSource: its hum pans from side to
side and swells as it comes round in front. Each bell plays a one-shot from
where it stands every few seconds, left then right, and lights up as it rings.

Pause holds every sound where it is, and Stop silences them.
