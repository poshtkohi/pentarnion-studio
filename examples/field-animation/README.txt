SAVED FIELD PLAYBACK AND SELECTED PNG FRAME EXPORT

heat_field_playback.p solves a real 2D heat equation, then shows a surface player.
pentarnion_field_playback.p displays an image player with all five original
coefficients available in the plot's coefficient selector. Playback reads saved
samples; no simulation/callback compilation runs on the GUI thread. Source data
is immutable and bounded to eight million retained real coordinates per request
and across open players. Each player uses at most 240 selected frames, at 1..60
frames per second. Nonuniform physical sample times are shown; playback FPS is
a display rate, not an assertion of real-time physical integration.

animatefield(field,options(component=0,mode="surface",fps=15.0,loop=0)) supports
native 1D/2D real/Pentarnion PDE result records. 1D fields display curves; 2D fields
support mode="surface" or "image". Components are zero based, 0..4; a real field
only accepts 0. Use first/last/stride/max_frames to select original sample indices.
Every selected coefficient uses one fixed range across the whole selection,
preventing misleading per-frame color or vertical auto-rescaling. All original
coefficients remain available; the component selector never substitutes norms.

The per-plot toolbar provides Play/Pause, Previous/Next, a frame slider, current
time and coefficient selection. New players start paused. Language equivalents:
animationframe(3); playanimation("on"); playanimation("off"). A custom title and
mathematical labels survive frame changes; context text records time/component.
Zoom/camera state remains stable during playback. Closing/replacing the plot
stops its timer; replacing an animation clears its derived display bounds.

export_selected_frames.p runs without a GUI. exportframes(field,directory,
options(...)) writes only explicitly selected PNGs, one frame at a time, and a
CSV manifest with original indices, exact sample times, coefficient and filenames.
Options include prefix, first/last/stride/max_frames, width/height/dpi, max_bytes,
create_directory and overwrite. Existing outputs require explicit overwrite=1;
missing directories require create_directory=1. A conservative disk estimate is
checked before writing anything. Limits are 240 images and at most 1 GiB per
sequence; each PNG has the separate native pixel/raster work budget. Cancellation
preserves complete images already written and their manifest, never a partial PNG.
Headless bitmap labels have the documented ASCII typography; full UTF-8 provenance
is retained in PNG metadata. Studio SVG/PDF remains the mathematical vector path.

exportgraphics can save the currently selected animation frame in Studio or in
native headless PNG mode. Playback controls do not synthesize unsolved times.
export_native_video.p adds a native, codec-free AVI path. exportvideo(field,
"movie.avi",options(fps=15.0,first=1,last=11,stride=1,max_frames=11,width=640,
height=480,max_bytes=64000000,max_seconds=60,overwrite=1)) streams one rendered
frame at a time as uncompressed BI_RGB24 with standard bottom-up BGR rows,
4-byte scanline alignment and a frame index. No FFmpeg/GPL codec is required.
AVI files are large: a strict <1 GiB file limit, explicit disk/frame/pixel/wall
budgets and atomic commit protect existing outputs. Source indices and exact
physical times are stored in RIFF provenance and rendered captions. Fixed movie
FPS describes display timing, not interpolation or real-time simulation speed.
Cancellation leaves the previous destination intact. Spatial slices/probes,
compressed video codecs and full figure-template storage remain follow-up work.
