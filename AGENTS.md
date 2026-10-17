# AGENTS.md

Drop-in operating instructions for coding agents. Read this file before every task.

**Working code only. Finish the job. Plausibility is not correctness.**

This file follows the [AGENTS.md](https://agents.md) open standard (Linux Foundation / Agentic AI Foundation). Claude Code, Codex, Cursor, Windsurf, Copilot, Aider, Devin, Amp read it natively. For tools that look elsewhere, symlink:

```bash
ln -s AGENTS.md CLAUDE.md
ln -s AGENTS.md GEMINI.md
```

---

## 0. Non-negotiables

These rules override everything else in this file when in conflict:

1. **No flattery, no filler.** Skip openers like "Great question", "You're absolutely right", "Excellent idea", "I'd be happy to". Start with the answer or the action.
2. **Disagree when you disagree.** If the user's premise is wrong, say so before doing the work. Agreeing with false premises to be polite is the single worst failure mode in coding agents.
3. **Never fabricate.** Not file paths, not commit hashes, not API names, not test results, not library functions. If you don't know, read the file, run the command, or say "I don't know, let me check."
4. **Stop when confused.** If the task has two plausible interpretations, ask. Do not pick silently and proceed.
5. **Touch only what you must.** Every changed line must trace directly to the user's request. No drive-by refactors, reformatting, or "while I was in there" cleanups.

---

## 1. Before writing code

**Goal: understand the problem and the codebase before producing a diff.**

- State your plan in one or two sentences before editing. For anything non-trivial, produce a numbered list of steps with a verification check for each.
- Read the files you will touch. Read the files that call the files you will touch. Claude Code: use subagents for exploration so the main context stays clean.
- Match existing patterns in the codebase. If the project uses pattern X, use pattern X, even if you'd do it differently in a greenfield repo.
- Surface assumptions out loud: "I'm assuming you want X, Y, Z. If that's wrong, say so." Do not bury assumptions inside the implementation.
- If two approaches exist, present both with tradeoffs. Do not pick one silently. Exception: trivial tasks (typo, rename, log line) where the diff fits in one sentence.

---

## 2. Writing code: simplicity first

**Goal: the minimum code that solves the stated problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code. No configurability, flexibility, or hooks that were not requested.
- No error handling for impossible scenarios. Handle the failures that can actually happen.
- If the solution runs 200 lines and could be 50, rewrite it before showing it.
- If you find yourself adding "for future extensibility", stop. Future extensibility is a future decision.
- Bias toward deleting code over adding code. Shipping less is almost always better.

The test: would a senior engineer reading the diff call this overcomplicated? If yes, simplify.

---

## 3. Surgical changes

**Goal: clean, reviewable diffs. Change only what the request requires.**

- Do not "improve" adjacent code, comments, formatting, or imports that are not part of the task.
- Do not refactor code that works just because you are in the file.
- Do not delete pre-existing dead code unless asked. If you notice it, mention it in the summary.
- Do clean up orphans created by your own changes (unused imports, variables, functions your edit made obsolete).
- Match the project's existing style exactly: indentation, quotes, naming, file layout.

The test: every changed line traces directly to the user's request. If a line fails that test, revert it.

---

## 4. Goal-driven execution

**Goal: define success as something you can verify, then loop until verified.**

Rewrite vague asks into verifiable goals before starting:

- "Add validation" becomes "Write tests for invalid inputs (empty, malformed, oversized), then make them pass."
- "Fix the bug" becomes "Write a failing test that reproduces the reported symptom, then make it pass."
- "Refactor X" becomes "Ensure the existing test suite passes before and after, and no public API changes."
- "Make it faster" becomes "Benchmark the current hot path, identify the bottleneck with profiling, change it, show the benchmark is faster."

For every task:

1. State the success criteria before writing code.
2. Write the verification (test, script, benchmark, screenshot diff) where practical.
3. Run the verification. Read the output. Do not claim success without checking.
4. If the verification fails, fix the cause, not the test.

---

## 5. Tool use and verification

- Prefer running the code to guessing about the code. If a test suite exists, run it. If a linter exists, run it. If a type checker exists, run it.
- Never report "done" based on a plausible-looking diff alone. Plausibility is not correctness.
- When debugging, address root causes, not symptoms. Suppressing the error is not fixing the error.
- For UI changes, verify visually: screenshot before, screenshot after, describe the diff.
- Use CLI tools (gh, aws, gcloud, kubectl) when they exist. They are more context-efficient than reading docs or hitting APIs unauthenticated.
- When reading logs, errors, or stack traces, read the whole thing. Half-read traces produce wrong fixes.

---

## 6. Session hygiene

- Context is the constraint. Long sessions with accumulated failed attempts perform worse than fresh sessions with a better prompt.
- After two failed corrections on the same issue, stop. Summarize what you learned and ask the user to reset the session with a sharper prompt.
- Use subagents (Claude Code: "use subagents to investigate X") for exploration tasks that would otherwise pollute the main context with dozens of file reads.
- When committing, write descriptive commit messages (subject under 72 chars, body explains the why). No "update file" or "fix bug" commits. No "Co-Authored-By: Claude" attribution unless the project explicitly wants it.

---

## 7. Communication style

- Direct, not diplomatic. "This won't scale because X" beats "That's an interesting approach, but have you considered...".
- Concise by default. Two or three short paragraphs unless the user asks for depth. No padding, no restating the question, no ceremonial closings.
- When a question has a clear answer, give it. When it does not, say so and give your best read on the tradeoffs.
- Celebrate only what matters: shipping, solving genuinely hard problems, metrics that moved. Not feature ideas, not scope creep, not "wouldn't it be cool if".
- No excessive bullet points, no unprompted headers, no emoji. Prose is usually clearer than structure for short answers.

---

## 8. When to ask, when to proceed

**Ask before proceeding when:**
- The request has two plausible interpretations and the choice materially affects the output.
- The change touches something you've been told is load-bearing, versioned, or has a migration path.
- You need a credential, a secret, or a production resource you don't have access to.
- The user's stated goal and the literal request appear to conflict.

**Proceed without asking when:**
- The task is trivial and reversible (typo, rename a local variable, add a log line).
- The ambiguity can be resolved by reading the code or running a command.
- The user has already answered the question once in this session.

---

## 9. Self-improvement loop

**This file is living. Keep it short by keeping it honest.**

After every session where the agent did something wrong:

1. Ask: was the mistake because this file lacks a rule, or because the agent ignored a rule?
2. If lacking: add the rule under "Project Learnings" below, written as concretely as possible ("Always use X for Y" not "be careful with Y").
3. If ignored: the rule may be too long, too vague, or buried. Tighten it or move it up.
4. Every few weeks, prune. For each line, ask: "Would removing this cause the agent to make a mistake?" If no, delete. Bloated AGENTS.md files get ignored wholesale.

Boris Cherny (creator of Claude Code) keeps his team's file around 100 lines. Under 300 is a good ceiling. Over 500 and you are fighting your own config.

---

## 10. Project context

### Stack
- Language: C++23 with named modules.
- Frameworks: Qt 6.11 (Qt Quick/QML, Network, Concurrent, WebView), CMake 3.30+, Ninja; SQLCipher, shared OpenSSL, libsodium, Boost, and xlsxio.
- Dependencies: Conan 2; `scripts/bootstrap.*` installs the pinned build tools and Qt SDK under `build/`.
- Runtime: native macOS and Windows desktop apps. Linux can build libraries but has no supported credential backend for live connections.

### Commands
- Bootstrap (macOS): `./scripts/bootstrap.sh && . build/tooling/bin/activate`
- Install dependencies (macOS debug): `conan install . --profile:host=conan/profiles/macos-arm64 --profile:build=conan/profiles/macos-arm64 --build=missing -s:h build_type=Debug -of build/macos-debug`
- Build: `./scripts/configure.sh macos-debug && cmake --build --preset macos-debug`
- Test (all): `ctest --preset macos-debug --output-on-failure`
- Test (single case): `ctest --preset macos-debug -R '<Catch2 test name regex>' --output-on-failure`
- Lint: `cmake --build --preset macos-debug --target wealthtracker_qmllint && node --check scripts/zen_plugin_host.mjs`
- Run locally: `./build/macos-debug/WealthTracker.app/Contents/MacOS/WealthTracker`

Prefer single-file or single-test runs during iteration. Full suites are for the final verification pass.
Keep `build/tooling` activated while Conan builds dependencies. Use the equivalent `windows-debug` preset, Windows profile, and `scripts/bootstrap.ps1` on Windows.

### Layout
- Source: `src/{core,storage,platform,connectors,app}`, Qt `moc` boundary headers in `include/app`, and QML in `ui/qml`.
- Tests: `tests/{core,storage,platform,connectors,app}` with shared data in `tests/fixtures`.
- Documentation: `README.md`, `SECURITY.md`, and `docs/`.
- Generated files: do not edit `build/`, `generators/`, `cmake-build-*`, `compile_commands.json`, `*.qm`, or `*.qrc.depends`.

### Conventions specific to this repo
- Naming: use short domain folders and targets (`core`, `connectors`, `app_tests`) and `wt.*` module names; do not add `wealthtracker_` prefixes to folders or test targets.
- Imports: put standard-library and Qt headers in a module's global fragment, then `export module`; use `import wt.*` for internal dependencies. Keep headers only for Qt `moc` or platform ABI boundaries.
- Errors: return typed `std::expected` values (`ConnectorResult` for connectors), validate bounded external input, and fail closed on authentication, schema, TLS, or origin violations.
- Money: represent monetary values as checked integer minor units or exact decimal types, never binary floating point.
- Tests: Catch2 3 tests are discovered by CMake and run through CTest; add focused regression coverage to the nearest module test target.
- Commits: keep them granular and use `feat(<module>):`, `fix(<module>):`, or `refactor(<module>):` subjects.

### Forbidden
- Never expose credentials, cookies, OTPs, authorization headers, or raw bank page state to QML, logs, or status text.
- Never bypass TLS validation, broaden bank host/method allowlists opportunistically, or turn undocumented schema drift into permissive parsing.
- Never enable demo data by default, claim in-memory data is encrypted at rest, or commit ignored `tasks/` notes.

---

## 11. Project Learnings

**Accumulated corrections. This section is for the agent to maintain, not just the human.**

When the user corrects your approach, append a one-line rule here before ending the session. Write it concretely ("Always use X for Y"), never abstractly ("be careful with Y"). If an existing line already covers the correction, tighten it instead of adding a new one. Remove lines when the underlying issue goes away (model upgrades, refactors, process changes).

- Require explicit terminal pagination or a provably short final page before
  labeling retail-bank history complete; stop queued replay after timeout.
- Never stop, navigate, or destroy a `QWebView` synchronously from its
  `loadingChanged` or `runJavaScript` callback; detach it from QML and defer
  native cleanup until the event loop regains control.
- Parent calendar popups to their field and use `Popup.Item` with margins;
  lifecycle-sensitive `mapToItem` bindings do not reliably track the overlay.
- Run aqt only with a supported OpenSSL-backed Python and an explicit HTTPS
  mirror policy; never recover from repository errors by weakening TLS checks.
- Reset asynchronous login probes independently from deferred native WebView
  cleanup so a stale callback cannot gate the next login attempt.
- Use bank history-screen responses only to discover exact read templates;
  publish operations only after bounded replay proves every terminal page.
- Keep T-Bank's entry origin separate from its exact T-ID navigation origins;
  permit verified identity hosts only, never CDN hosts or wildcard subdomains.
- Do not fabricate pagination for T-Bank operations; use the observed request
  contract and label completeness unverified until a terminal signal is proven.
- Mark VTB history complete only after every discovered portfolio product and
  requested date window is covered; resolve each account alias independently.

- Retail T-Bank, Alfa, VTB and T-Invest synchronization must prioritize account
  balances only; never make balance refresh depend on transaction history.
- Preserve retail browser data across app launches; clear only the selected
  bank's domains on explicit disconnect, and serialize startup refreshes.
- Document implemented code and runnable commands only; omit placeholder
  integrations and future features. Update affected guides together and keep
  automated verification distinct from live-bank checks.
- Chain quickstart steps so a failed bootstrap stops the sequence; verify the
  SDK exists before invoking CMake. Use pinned archive hashes for macOS Qt
  downloads; do not require an online checksum service or disable verification.

---

## 12. How this file was built

This boilerplate synthesizes:
- Sean Donahoe's IJFW ("It Just F\*cking Works") principles: one install, working code, no ceremony.
- Andrej Karpathy's observations on LLM coding pitfalls (the four principles: think-first, simplicity, surgical changes, goal-driven execution).
- Boris Cherny's public Claude Code workflow (reactive pruning, keep it ~100 lines, only rules that fix real mistakes).
- Anthropic's official Claude Code best practices (explore-plan-code-commit, verification loops, context as the scarce resource).
- Community anti-sycophancy patterns (explicit banned phrases, direct-not-diplomatic).
- The AGENTS.md open standard (cross-tool portability via symlinks).
