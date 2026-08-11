# Contributing

Contributions must preserve local execution, least privilege, and read-only
bank access.

## Development rules

- Use C++23 named modules for new non-Qt boundaries. Keep headers for Qt `moc`,
  platform ABIs, or third-party C APIs.
- Do not add payment, transfer, trading, card-management, or credential-export
  behavior.
- Give every network connector an exact HTTPS host policy, explicit methods,
  bounded responses, deterministic cancellation, and parser tests.
- Do not expose credentials, session projections, bank response bodies, or
  imported descriptions to QML or logs.
- Treat external plugin bundles as arbitrary code. Do not weaken the UI warning
  or describe the child process as a sandbox.
- Keep sample data opt-in and clearly synthetic.

Do not commit real exports, credentials, tokens, databases, generated packages,
or files under the ignored `tasks/` directory.

## Verification

Before submitting a change, run:

```sh
cmake --build --preset macos-debug
ctest --preset macos-debug --output-on-failure
cmake --build --preset macos-debug --target wealthtracker_qmllint
node --check scripts/zen_plugin_host.mjs
git diff --check
```

Use the equivalent Windows preset when changing Windows-specific code. Add a
regression test for every security policy, parser, limit, or lifecycle change.

## Commit subjects

Each commit must use one of these forms:

- `feat(<module>): short description`
- `fix(<module>): short description`
- `refactor(<module>): short description`

Keep commits scoped to one reviewable concern. Common scopes are `core`,
`storage`, `connectors`, `tbank`, `app`, `ui`, `build`, `docs`, and `ci`.
