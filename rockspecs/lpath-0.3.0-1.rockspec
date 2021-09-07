-- vim: ft=lua
package = "lpath"
version = "0.3.0-1"

source = {
   url = "git://github.com/starwing/lpath.git",
   tag = "0.3.0"
}

description = {
   summary = "a OS specific path manipulation module for Lua",
   detailed = [[
lpath is a lfs-like Lua module to handle path, file system and file informations.

This module is designed to be easy extend to new system. Now it implements windows using Win32 API (lfs use legacy POSIX APIs on Windows), and POSIX systems.

This module is inspired by Python's os.path and pathlib module. It split into 4 parts: path.info, path.fs, path.env and path itself.
]],
   homepage = "https://github.com/starwing/lpath",
   license = "MIT/X11",
}

dependencies = {
   "lua >= 5.1",
}

build = {
   copy_directories = {},

   type = "builtin",

   modules = {
      path = "lpath.c",
   }
}
