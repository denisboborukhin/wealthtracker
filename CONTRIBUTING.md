# Contributing

Contributions should preserve the local-first and read-only security model.

## Requirements

- C++23-capable compiler
- CMake
- Conan 2
- Ninja

## Commit subjects

Use exactly one of these forms:

- `feat(<module>): short description`
- `fix(<module>): short description`
- `refactor(<module>): short description`

Examples of module names are `core`, `storage`, `connectors`, `ui`, `build`,
`docs`, and `ci`.

Do not commit real bank exports, credentials, access tokens, local databases,
or files from the ignored `tasks/` directory.

