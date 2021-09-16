lpath - Path utils for Lua
==========================
[![CI](https://github.com/starwing/lpath/actions/workflows/test.yml/badge.svg)](https://github.com/starwing/lpath/actions/workflows/test.yml)[![Coverage Status](https://coveralls.io/repos/github/starwing/lpath/badge.svg?branch=v0.3)](https://coveralls.io/github/starwing/lpath?branch=v0.3)

`lpath` is a [lfs](http://keplerproject.github.io/luafilesystem/)-like Lua module to handle path, file system and file informations.

This module is inspired by Python's `os.path` and `pathlib` module. It split into 4 parts:

- `path`: main module, pathlib style path operations.
- `path.fs`: fs specific operations, folder walking, file operations, etc.
- `path.info`: some constants about path literals.
- `path.env` set/get environment variables, and expand env vars in path.

All routines in this module which accept `...` for parameters means you could pass any count of `string` as arguments. All `string` will joined into a single path, just as pass all arguments to `path(...)`, and pass the resulting path string to the routine.

All routines may returns `nil, error` for error case. if you want raise error to Lua, use `assert(...)` for routines.

## Usage

### `path`

| routine                         | return value | description                                                  |
| ------------------------------- | ------------ | ------------------------------------------------------------ |
| `path(...)`                     | `string`     | return joined normalized path string.                        |
| `path.ansi()`                   | `none`       | set path string encoding to local code page.                 |
| `path.ansi(number)`             | `none`       | set the code page number for path string encoding.           |
| `path.ansi(string)`             | `string`     | convert UTF-8 `string` to current code page encoding.        |
| `path.utf8()`                   | `none`       | set path string encoding to UTF-8.                           |
| `path.utf8(string)`             | `string`     | convert current code page encoding `string` to UTF-8.        |
| `path.abs(...)`                 | `string`     | returns the absolute path for joined parts.                  |
| `path.rel(path[, dir])`         | `string`     | returns  the relation path for dir (default for current work directory). |
| `path.fnmatch(string, pattern)` | `boolean`    | returns whether the `pattern` matchs the `string`.           |
| `path.match(path, pattern)`     | `boolean`    | returns as `path.fnmatch`, but using Python path matching rules. |
| `path.drive(...)`               | `string`     | returns  the drive part of path.                             |
| `path.root(...)`                | `string`     | returns the root part of path. (`\` on Windows, `/` or `//` on POSIX systems.) |
| `path.anchor(...)`              | `string`     | same as `path.drive(...) .. path.root(...)`                  |
| `path.parent(...)`              | `string`     | returns the parent path for path.                            |
| `path.name(...)`                | `string`     | returns the file name part of the path.                      |
| `path.stem(...)`                | `string`     | returns the file name part without suffix name of the path.  |
| `path.suffix(...)`              | `string`     | returns  the suffix name of the path.                        |
| `path.suffixes(...)`            | `iteraotr`   | returns  a `idx`, `suffix` iterator to get suffix names of the path. |
| `path.parts(...)`               | `iterator`   | returns  a `idx`, `part` iterator to get parts in the path.  |
| `path.exists(...)`              | `boolean`    | returns whether the path is exists in file system (same as `fs.exists()`) |
| `path.resolve(...)`             | `string`     | returns the path itself, or the target path if path is a symlink. |
| `path.cwd()`                    | `string`     | fetch the current working directory path.                    |
| `path.bin()`                    | `string`     | fetch the current executable file path.                      |
| `path.isdir(...)`               | `boolean`    | returns whether the path is a directory.                     |
| `path.islink(...)`              | `boolean`    | returns whether the path is a symlink.                       |
| `path.isfile(...)`              | `boolean`    | returns whether the path is a regular file.                  |
| `path.ismount(...)`             | `boolean`    | returns whether the path is a mount point.                   |

### `path.fs`

| routine                               | return value | description                                                  |
| ------------------------------------- | ------------ | ------------------------------------------------------------ |
| `fs.dir(...)`                         | `iterator`   | returns a iterator `filename, type` to list all child items in path. |
| `fs.scandir(...[, depth])`            | `iterator`   | same as `fs.dir`, but  walk into sub directories recursively. |
| `fs.glob(...[, depth])`               | `iterator`   | same as `fs.scandir`, but accepts a pattern for filter the items in directory. |
| `fs.chdir(...)`                       | `string`     | change current working directory and returns the path, or `nil` for error. |
| `fs.mkdir(...)`                       | `string`     | create directory.                                            |
| `fs.rmdir(...)`                       | `string`     | remove empty directory.                                      |
| `fs.makedirs(...)`                    | `string`     | create directory recursively.                                |
| `fs.remvoedirs(...)`                  | `string`     | remove all items in a directory recursively.                 |
| `fs.unlockdirs(...)`                  | `string`     | add write perimission for all files in a directory recursively. |
| `fs.tmpdir(prefix)`                   | `string`     | create a tmpdir and returns it's path                        |
| `fs.ctime(...)`                       | `integer`    | returns the creation time for the path.                      |
| `fs.mtime(...)`                       | `integer`    | returns the modify time for the path.                        |
| `fs.atime(...)`                       | `integer`    | returns the access time for the path.                        |
| `fs.size(...)`                        | `integer`    | returns the file size for the path.                          |
| `fs.touch(...[, atime[, mtime]])`     | `string`     | update the access/modify time for the path file, if file is not exists, create it. |
| `fs.remove(...)`                      | `string`     | delete file.                                                 |
| `fs.copy(source, target)`             | `boolean`    | copy file from the source path to the target path.           |
| `fs.rename(source, target)`           | `boolean`    | move file from the source path to the target path.           |
| `fs.symlink(source, target[, isdir])` | `boolean`    | create a symbolic link from the source path to the target path. |
| `fs.exists(...)`                      | `boolean`    | same as `path.exists`                                        |
| `fs.getcwd()`                         | `string`     | same as `path.cwd()`                                         |
| `fs.binpath()`                        | `string`     | same as `path.bin()`                                         |
| `fs.is{dir/link/file/mount}`          | `string`     | same as correspond routines in `path` module.                |

### `path.env`

| routine               | return value  | description                                                  |
| --------------------- | ------------- | ------------------------------------------------------------ |
| `env.get(key)`        | `string`      | fetch a environment variable value.                          |
| `env.set(key, value)` | `string`      | set the environment variable value and returns the new value. |
| `env.expand(...)`     | `string`      | return a path that all environment variables replaced.       |
| `env.uname()`         | `string`, ... | returns the informations for the current operation system.   |

### `path.info`

`path.info` has several constants about current system:

  - `platform`:
      - `"windows"`
      - `"linux"`
      - `"macosx"`
      - `"android"`
      - `"posix"`
  - `sep`: separator of directory on current system. It's `"\\"` on Windows, `"/"` otherwise.
  - `altsep`: the alternative directory separator, always `"/"`.
  - `curdir`: the current directory, usually `"."`.
  - `pardir`: the parent directory, usually `".."`.
  - `devnull`: the null device file, `"nul"` on Windows, `"dev/null"` otherwise
  - `extsep`: extension separator, usually `"."`.
  - `pathsep`: the separator for $PATH, `";"` on Windows, otherwise `":"`.

## License

Same as Lua's License.

## Build

See here: http://lua-users.org/wiki/BuildingModules

