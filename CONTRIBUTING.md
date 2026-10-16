# Contributing

Follow the repository's [coding instructions](AGENTS.md). Keep changes scoped,
use C++23 named modules outside Qt/platform boundaries, and preserve read-only
bank access, strict origin policies, credential handling, and opt-in sample data.

Do not commit real exports, credentials, tokens, databases, generated packages,
or ignored `tasks/` files.

## Verification

Use [Building](docs/building.md) for the platform's build, test, QML lint, and
plugin-host syntax commands. Run `git diff --check` before committing. Parser,
security-policy, limit, and lifecycle changes need regression coverage.

Documentation describes code that exists and commands provided by the
repository. Keep setup details in the connection guide, runtime structure in
architecture, and controls in security design. Update affected guides together
when behavior changes, and distinguish automated tests from live-bank checks.

## Commits

Keep each commit focused. Use `feat(<module>):`, `fix(<module>):`, or
`refactor(<module>):` with a subject under 72 characters and a body explaining
why the change is needed. Git history is the change record.
