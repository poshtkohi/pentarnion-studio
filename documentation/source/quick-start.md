# Pentarnion Studio Quick Start

Pentarnion Studio 0.2
Windows edition and language 0.2
31 October 2026

This guide takes you from installation to a saved Pentarnion calculation and a figure. It uses a small example whose numerical output was checked with this release. You do not need MATLAB, Microsoft Excel or a separately installed compiler for the supported installed workflows.

@authors

@contents

## 1 Install and launch Studio

Run **Pentarnion Studio Installer.exe** as your normal Windows user. The default application directory is `%LocalAppData%\Programs\Pentarnion Studio`. The installer is intended for a per-user installation and does not require an administrator account. Keep the default folder unless you have a particular reason to use another location.

The package includes the native runtime and compiler tools needed by its supported workflows. It does not add tools to the global PATH or install fonts system-wide. The installer is unsigned; Windows may therefore show an unknown-publisher warning. Check the release checksum against the accompanying checksum file before deciding whether to proceed. A checksum establishes file identity, rather than publisher identity.

Open **Pentarnion Studio** from the Start menu. The same Start menu group contains Examples, Documentation, Quick Start, Theory and Programming Language, and Uninstall Pentarnion Studio. The documentation shortcuts open local files. The website link in the status bar opens `https://pentarnion.com` only when you activate it.

The writable example workspace is the Windows Documents known folder followed by `Pentarnion Studio\Examples`. Windows may place Documents under OneDrive or another redirected location. Use the Examples shortcut instead of assuming a literal user-profile path. Updates copy only missing example files and preserve existing files, including your edited copies.

On a fresh first launch, the launcher selects the small calculations project when no existing session or explicit command-line file takes precedence. You can always open it yourself: choose **Open Project**, then select `calculations\project.pprj` from the Examples shortcut's folder. The Project panel lists the project and its source files. Double-click `main.p` to open it in the editor.

**If Studio does not appear**, first check whether it is already open or has a dialog behind another window. Launch the installed shortcut rather than copying the executable alone: the adjacent runtime libraries are part of the application. For startup failures, record the exact message and consult the troubleshooting chapter in the full manual.

## 2 Meet the workspace

The editor is where you write `.p` scripts. The **Command Window** accepts commands and shows the results, warnings and errors in one scrollable surface. Ordinary output is black; diagnostics use red. A source diagnostic includes a location that you can activate to move to the corresponding editor line.

The **Project** panel groups files belonging to the current project. The **Figures** area holds plots, which can be tabbed, split, tiled or undocked. **Copilot** is a separate optional panel. Its greeting and example questions are local; selecting an example fills the question box without sending it. A configured provider and an explicit Send action are needed for a provider request. Source context defaults to None.

Choose Classic or Ribbon from the interface controls in the View area. Both modes use the same commands and shortcuts. Switching presentation does not run a script or replace your documents. If panels are misplaced, use **Restore Default View**. Closing or arranging figure views does not delete their source scripts or clear the numerical workspace.

The worker symbol at the lower right reports observed scientific worker activity. A requested thread count is a maximum, so a small calculation may use one worker. An idle symbol is normal when no calculation is running. The link immediately to its left is the Pentarnion website.

Use **Help for selection** or F1 for an editor term. **Language Reference** opens the formatted local Help browser. For command-line help, enter `help plot2` or `help("plot2");`. Pentarnion has a typed language with semicolon-terminated statements and braced functions; familiarity with MATLAB helps with the workspace, but MATLAB source is not automatically valid Pentarnion source.

## 3 Run your first calculation

Open `calculations\main.p` and run it with the Run File command or F5. Save the script before execution. For a new file without a path, Studio asks where to save it; cancelling that save also cancels the run. You can also run a saved script from the Command Window using `run("main.p");` when the file is in the active project directory.

The bundled calculation creates $p=i+j$. It prints the element, its square, its squared norm and its canonical reciprocal. The following complete script adds a figure and an assertion. Save it as `first-calculation.p` in a writable project folder, then run it.

@example First Pentarnion calculation
```p
use_algebra("A_gamma", gamma=1);
clc;
clear;
pentarnion p = i + j;
display("p =", p);
display("p squared =", p^2);
display("norm2(p) =", norm2(p));
display("inv(p) =", inv(p));
assert(norm2(p^2 + 2) < 1e-24);
figure(1, "Name", "First Pentarnion vector", "NumberTitle", "off");
plot2("i", "j", p);
title("A vector in the $(i,j)$ plane");
xlabel("$i$"); ylabel("$j$");
```

The verified numerical output is:

```text
p = i + j
p squared = -2
norm2(p) = 2
inv(p) = -0.5*i - 0.5*j
```

The figure shows an arrow from the origin to the point with displayed coordinates `(1,1)`. The plot is a projection onto the `i` and `j` coefficients; the original element still has five coefficients. In particular, `norm2(p)` is the squared Euclidean coefficient norm, not its square root.

`clc;` clears the visible Command Window. `clear;` removes user variables. They have different purposes. Use `display(...)` to print a value in a script; an ordinary expression followed by a semicolon can be evaluated without printed output.

## 4 Inspect figures and save results

Select a figure or subplot to make it active. Its selection border changes colour. The plot tools provide zoom, pan, reset or fit, data tips and export. Data tips inspect retained numerical samples; you can keep several tips and remove them using the data-tip controls. They are annotations, not new solver samples.

Use **Export Image** for the complete selected figure. Use the Save icon on an individual subplot to export only that subplot. PNG and JPEG are raster formats. SVG preserves supported vector content and is useful when a figure must be resized for a paper. PDF export is available for supported publication/headless figure routes; the full manual explains why export capabilities depend on the plot type. Choose a fresh filename until you are satisfied with the result.

For a second exercise, open `paper1-bracketing\project.pprj` from the Examples folder and read its README and `main.p` before running. This project solves a bracketing-dependent ODE and draws five component time series plus trajectory projections. It is larger than the introductory calculation. The first solve may compile a native equation library; later runs reuse a valid structural cache when only runtime parameter values or solver settings change.

While a long calculation is running, Studio indicates Running rather than displaying an idle prompt. Press Ctrl+C in the Command Window, or use Stop, to request cancellation. Native work stops at its cancellation checkpoints. Keep the application open while it completes that request; do not start another copy to repeat the same calculation.

Use `figure(2);` and `subplot(2,2,1);` in source to select a figure and panel. `tilefigures("studio",1,2);` arranges whole figures side by side. The figure toolbar also offers a visual grid picker, split docking targets and desktop tiling. A subplot layout and a layout of complete figures are separate choices.

## 5 Save work and continue learning

Save source through Save or Save All. Studio also provides autosaving, but explicitly saving at a meaningful stopping point gives you a clear source file to archive or send. Save a new project under a name and location you control. The editable examples belong in Documents rather than the installed application directory.

For numerical variables, use a workspace file:

```p
save("first-workspace.pws", "p");
```

This saves data rather than executable function definitions. Keep the `.p` source as well. To recover the variable for later Command Window submissions, use `load("first-workspace.pws");`. A script that uses a loaded name later in the same checked file must first declare a compatible typed target. Relative data paths use the active project directory, or the source directory when no project is open.

You can exchange real numeric tables with CSV and XLSX using `readmatrix` and `writematrix`. XLSX access does not require Microsoft Excel. The numeric import API is not a general Excel formula engine; export Pentarnion coefficients explicitly instead of assuming a five-dimensional element fits into one spreadsheet number.

The full manual develops multiplication order, canonical reciprocals versus equation solving, native callbacks, arrays and tensors, ODEs, plotting, persistence and the research examples. A successful calculation is a useful check on that example, not proof that a chosen numerical method is accurate for every problem. Preserve settings, initial conditions and source with your results.

## 6 Solve common first run problems

@table Common problems and immediate checks
Problem | What to check
No output appears | Run the saved file explicitly and use display for values you want printed. Check whether the Command Window is visible.
Expected semicolon | End the statement with ; and activate the diagnostic's source location.
Unknown name | Declare the variable before use and check case and spelling. No implicit variable declarations or ans binding are created.
Unsupported source character | Use ASCII identifiers such as ell. Unicode remains suitable for comments and strings.
Multiplication grouping error | Parenthesize three or more nonassociative factors, for example (p*q)*r or p*(q*r).
Native compiler error | Use the installed launcher and keep the whole installation together. Save the compiler diagnostic; do not copy only the EXE.
Plot looks crowded | Enlarge or undock the figure, reduce the number of panels, then reset the view before exporting.
Old example remains after update | Existing example files are preserved intentionally. Compare your copy with the release's example inventory instead of overwriting edited work.

Close Studio after saving work before updating or uninstalling it. Uninstallation removes the owned installation and its internal files; it preserves the external Documents workspace and other user data. Keep your own backups of source, datasets and results. Windows 10 compatibility checks that have not yet been performed are recorded in the release notes and VM checklist.

For correspondence, contact Dr Alireza Poshtkohi at `a.poshtkohi@herts.ac.uk`. Product website: `https://pentarnion.com`.
