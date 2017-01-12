lpath - Path utils for Lua
==========================
[![Build Status](https://travis-ci.org/starwing/lpath.svg?branch=master)](https://travis-ci.org/starwing/lpath)

`lpath` is a
[lfs](http://keplerproject.github.io/luafilesystem/)-like Lua module
to handle path, file system and
file informations.

This module is inspired by Python's os.path module. It split into 3
parts: `path.info`, `path.fs` and `path` itself.

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

`path.fs` has functions that implemented by `lfs`, such as folder
listing, folder tree walker, etc.

`path` has functions that maintains the Python os.path module. Such as
normalize path, split path into directory and filename
(`path.split()`), basename and extension name (`path.splitext()`) or
drive volume and paths (`path.splitdrive()`).

All functions expect iterators return 2 values on error: a `nil`, and
a error message.  Error message is encoded by ANSI code page by
default.

using `path.utf8()` and `path.ansi()` change the default code page
used by module. and beside the default code page, if you wanna UTF-8
string, call `path.utf8()` on ANSI string; if you wanna ANSI string,
call `path.ansi()` on UTF-8 string.

Some of functions accepts only one type of argument: `[comp]`.
`[comp]` is a list of strings, can be empty. If `[comp]` is empty, the
argument passed to function is `"./"`, i.e. current directory.
Otherwise these function will call `path.join()` on the list of
strings, and pass the result of `path.join()` to functions.

Functions that accept `[comp]`:
  - `path.abs()`
  - `path.isabs()`
  - `path.itercomp()`
  - `path.join()`
  - `path.fs.chdir()`
  - `path.fs.dir()`
  - `path.fs.exists()`
  - `path.fs.expandvars()`
  - `path.fs.fsize()`
  - `path.fs.ftime()`
  - `path.fs.makedirs()`
  - `path.fs.mkdir()`
  - `path.fs.realpath()`
  - `path.fs.remove()`
  - `path.fs.removedirs()`
  - `path.fs.rmdir()`
  - `path.fs.type()`
  - `path.fs.walk()`


Functions:
----------

- `path.fs.platform() -> string`
> return a system name from `uname()` if use POSIX systems. return
> `"Windows m.n Build bbbb"` on Windows systems, `m` is the major
> version, `n` is the minor version, and `bbbb` is the build number.

- `path.fs.binpath() -> string`
> get the file path of current execuable file.

- `path.fs.getcwd() -> string`
> get the current working directory.

- `path.fs.realpath([comp]) -> string`
> return the real path after all symbolic link resolved. On Windows, on
> systems before Vista this function is same as path.abs(), but after
> Vista it also resolved the NTFS symbolic link.

- `path.fs.chdir([comp]) -> string`
> change current working directory to `[comp]`.

- `path.fs.mkdir(path, mode) -> string`
> create directory for `path`, mode is ignored on Windows.

- `path.fs.makedirs([comp]) -> string`
> create directory for `[comp]`, automatic create mediately folders.

- `path.fs.rmdir([comp]) -> string`
> remove directory, directory should empty.

- `path.fs.removedirs([comp]) -> string`
> remove directory and files in directory.

- `path.fs.dir([comp]) -> iterator`
> return a iterator to list all files/folders in directory `[comp]`.
> Iterator will return `name`, `type`, `size`, `[CMA]` file times in
> for:
> 
> ```lua
> for fname, type, size, ctime, mtime, atime in path.fs.dir(".") do
>    -- ...
> end
> ```
> 
> Sometimes you only want `name` or `type` or `size`, just ignore remain
> return values:
> 
> ```lua
> for fname, type in path.fs.dir(".") do
>    print(fname, "is a", type)
> end
> ```

- `path.fs.walk([comp]) -> iterator`
> same as `path.fs.dir()`, but iterator a folder tree, recursively.
> notice that a directory will occurs in loop, one have type "in", and
> one have type "out", call `walk()` on a path:
> ```
> + a
>   | b
>   | c
> ```
> will result:
> ```
> "a/", "in"
> "a/b", "file"
> "a/c", "file"
> "a/", "out"
> ```

- `path.fs.type([comp]) -> string`
> get the file type (file, dir or link) of file.

- `path.fs.exists([comp]) -> boolean`
> judge a path real have a file

- `path.fs.ftime([comp]) -> ctime, mtime, atime`
> return the create time, modify time and access time of file.

- `path.cmpftime(file1, file2, use_atime) `
> return a integer, `0` for equal, `1` for `file1` is newer than
> `file2`, `-1` for `file1` is older than `file2`.
> if use_atime is true, the compare use access time, otherwise only
> use create time and modify time only.

- `path.fs.fsize([comp]) -> number`
> return the size of file.

- `path.fs.touch(path[, mtime[, atime]]) -> path`
> if `path` is not exists, create a empty file at `path`, otherwise
> update file's time to current. If given `atime` or `mtime`,
> update file's time to these values.

- `path.fs.copy(f1, f2[, fail_if_exists]) -> true`
> copy `f1` to `f2`. If `fail_if_exists` is true and `f2` exists, this
> function fails.

- `path.fs.rename(f1, f2)`
> rename/move `f1` to `f2`, if `f2` exists, this function fails.

- `path.fs.remove([comp]) -> string`
> remove file.

- `path.fs.getenv(name)`
> get value of a environment variable.

- `path.fs.setenv(name, value)`
> set a environment variable.

- `path.fs.expandvars([comp]) -> string`
> expandvars environment variables in string.

- `path.fs.glob(pattern[, dir[, table[, limit]]]) -> table`
> glob with pattern in `dir`, using table if passed.
> e.g. `fs.glob "*.txt"` return a table contains all txt file in
> current directory.
> use `limit` control the recursive level, pass 1 for only one level
> of directory, and pass negative number (-1) to traver all sub
> directory in `dir`.

- `path.fs.fnmatch(path, pattern) -> boolean`
> the fnmatch algorithm used by `path.fs.glob()`, return a boolean
> value for whether the path matches the pattern.


- `path.ansi(string) -> string`
- `path.utf8(string) -> string`
> these functions convert string between ANSI code pages and UTF-8 on
> Windows.  On other System (especially POSIX systems) these functions
> does nothing.

- `path.type([comp]) -> string`
> get the type of file, return `"file"`, `"dir"` or `"link"`.

- `path.isabs(string) -> boolean`
> return whether the string is a absolute path.

- `path.abs([comp]) -> string`
> return the absolute path for [comp]. Same as Python's
> `os.path.abspath()`

- `path.rel(filepath, path) -> string`
> return relative path for filepath based on path. Same as Python's
> `os.path.relpath()`

- `path.itercomp([comp]) -> iterator`
> return a iterator that iterate the component of path. e.g.
> ```lua
> for comp in path.itercomp("a/b/c/d") do
>    print(comp)
> end
> -- print:
> -- a
> -- b
> -- c
> -- d
> ```

- `path([comp]) -> string`
- `path.join([comp]) -> string`
> join all arguments with path sep (`"\\"` on Windows, `"/"` otherwise).
> these routines will normalize the path, e.g. "a/../b" become "b"

- `path.split([comp]) -> dirname, basename`
> split file path into directory name and base name.

- `path.splitdrive([comp]) -> drive/UNC, path`
> split file path into UNC part/drive volume and base name.
> e.g. D:\foo\bar into D: and \foo\bar,
>      \\server\mountpoint\foo\bar into
>      \\server\mountpoint and \foo\bar

- `path.splitext([comp]) -> base-with-dirname, ext`
> split file path into base name and extension name.
> e.g. `\foo\bar\baz.exe` into `\foo\bar\baz` and `.exe`.


License
=======
Same as Lua's License.

Build
=====
See here: http://lua-users.org/wiki/BuildingModules

To do
=====
Complete test suite
