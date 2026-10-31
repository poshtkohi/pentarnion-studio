PUBLICATION PLOTTING

Open project.pprj and run main.p. A reusable options record controls RGB palette,
line width/style, sans/serif/mono font family, font size and legend placement.
Supported line styles: solid, dash, dot, dashdot. Styles apply to the active plot.
hold("on") appends compatible full-data vector/curve series to the selected
subplot; hold("off") restores replacement on the next plot. It does not delete
existing held series. Held plots require matching axes/dimensions/algebra.
Multiple field meshes belong in separate subplots.

legend(labels...[,options(location="northwest",box=1)]) controls the key.
legend("off") hides it; legend("on") restores it. Locations: auto, northeast,
northwest, southeast, southwest, outside. xlim/ylim/zlim use [lower,upper] or
"auto". axis([xmin,xmax,ymin,ymax,(zmin,zmax)]) applies several bounds;
axis("equal") uses equal coordinate units, axis("auto") restores automatic
bounds/scaling. Original samples and all five Pentarnion coefficients survive
style changes, clipping and exports.

linkaxes([1,2],"x"/"y"/"xy") links allocated 2D subplots within the current
figure. Initial common limits cover their data; later limit edits and native
zoom/pan follow the selected coordinate(s). linkaxes([],"off") unlinks them.
Linked 3D camera navigation is not implemented in this increment.

exportgraphics(path,options(width_cm=16.0,height_cm=12.0,dpi=150,overwrite=1))
exports the current figure; subplot=2 selects one panel. Omit physical dimensions
to use the current GUI canvas. Existing files require explicit overwrite=1 in
the language; GUI file dialogs retain their normal overwrite confirmation.
Studio supports PNG/JPEG, SVG and vector PDF. PNG/JPEG physical export resizes
the current rendered frame; vector formats keep curve/axis/mesh projection
geometry and mathematical glyph paths. 3D vector surfaces use painter depth
ordering, with the same intersection/transparency limitations as existing SVG.
Fonts depend on native platform availability; mathematical labels use MathText.

The native headless figure mode supports PNG without Qt/OpenGL, Python or an
external renderer. It stores UTF-8 labels/provenance in PNG metadata and displays
ASCII bitmap annotations; it does not claim LaTeX/Unicode GUI typography. It uses
a deterministic orthographic 3D CPU projection with a depth buffer for meshes.
Pixel, point, raster-work and overwrite/cancellation budgets bound each export.
The example writes one PNG explicitly. Native headless SVG/PDF, figure-template
serialization remains separate work; saved field playback and native AVI/PNG
sequences are demonstrated in examples/field-animation.
