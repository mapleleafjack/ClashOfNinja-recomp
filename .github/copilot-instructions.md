## Tests

Add a test wherever behaviour can be checked offline, without prompting: a bug you fixed, a
rule you encoded, a round trip someone would expect to work. The tests are ctest cases in
`launcher/CMakeLists.txt` — a `*_test.cpp` with a `check()` harness and a `add_test(NAME
<area>-logic ...)` — and they run on Linux, macOS and Windows. A new test executable must also
join the `ninja -C launcher/build-ci NarutoLauncher update_test ...` lines in
`.github/workflows/build.yml`, or CI never builds it. Runtime code with no project of its own
is tested the same way when the logic can be kept in a header (see `boot_test`).

## Working style

Be direct: make the edit, build it, run the existing test suite once, then report and stop.

Do not add verification theatre. Specifically, do not:

- write throwaway probe programs to re-check something the code or an existing test already says,
- convert renders to images or take screenshots to eyeball a layout you can reason about,
- re-run the whole suite after each small edit, or re-check a fix you have already verified,
- chase a stack trace before considering the obvious cause (a concurrent build, stale objects, a
  file locked by a running process).

Verification is for the end of a task, in one pass, using the project's own tests. If a real
failure needs investigation, say what you suspect first and ask before a long detour.

## Tests

Every behaviour change comes with a test, added as part of the change. Put it where it
belongs: logic that runs offline goes in `launcher/tests/*_test.cpp`, and anything about the
window gets a `--render` view in `launcher/CMakeLists.txt` — the render harness can assert
state and exit non-zero, as `cover-click` and `mods-from-game` do. When a change is not
covered, say which behaviour is untested rather than leaving it silent.

## graphify

For any question about this repo's architecture, structure, components, or how to add/modify/find
code, your first action should be `graphify query "<question>"` when `graphify-out/graph.json`
exists. Use `graphify path "<A>" "<B>"` for relationship questions and `graphify explain "<concept>"`
for focused-concept questions. These return a scoped subgraph, usually much smaller than the full
report or raw grep output.

Triggers: "how do I…", "where is…", "what does … do", "add/modify a <component>",
"explain the architecture", or anything that depends on how files or classes relate.

If `graphify-out/wiki/index.md` exists, use it for broad navigation. Read `graphify-out/GRAPH_REPORT.md`
only for broad architecture review or when query/path/explain do not surface enough context. Only read
source files when (a) modifying/debugging specific code, (b) the graph lacks the needed detail, or
(c) the graph is missing or stale.

Type `/graphify` in Copilot Chat to build or update the graph.
