Run main.p to inspect the native module catalogue and a simple typed calculation.
Use Check, then Ctrl+Space for completion, hover for signatures/types,
Ctrl+Shift+I for the docked Symbol Inspector and F12 for a declaration.
Help > Feature Rules and Native Modules shows reference hashes and loaded modules.
It remains open beside the source. It does not execute documentation.

To inspect a call frame, enter these two commands in the same Command Window:
  function real failing(real x){return sqrt(-x);};
  failing(2)
The second command deliberately reports a domain error. Click its retained
function frame to recall the defining command, without executing it.
See specs/editor_assistance_v0_2.txt for the exact contracts and limits.
