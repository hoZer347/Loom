RenderTargets
=============

A Loom project showing RenderTarget, as fog of war. Press Play.

    Scenes/Main.loomscene     three scouts on a dark field, and two panels
    Scripts/Scouts.hpp        moves the scouts and draws what they see
    Assets/Sight.shader       a scout's sight, a disc fading at its rim
    Assets/Fog.shader         the field, lit only where it has been seen
    Assets/Panel.shader       a panel, showing a target as it is
    Assets/Lit.shader         the scouts

Visible and Explored are GameObjects with a RenderTarget each. Every frame
Scouts draws the Sight disc where each scout is into both, seen from above
with RenderTarget::Overhead and blended by Max. It clears Visible first, so
Visible holds what the scouts see now. It never clears Explored, so Explored
keeps everywhere they have seen.

The field's material names both in its sampler variables, and the panels'
materials one each: the left panel shows Visible and the right one Explored.
The field is lit where the scouts see, dim where they have been, and black
where they have never been.

Select Visible or Explored in the Hierarchy to see the target in the
Inspector as it is drawn.
