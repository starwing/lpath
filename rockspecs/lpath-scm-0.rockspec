-- vim: ft=lua
package = "lpath"
version = "scm-0"

source = {
   url = "git://github.com/starwing/lpath.git",
}

description = {
   summary = "a OS specified path manipulation module for Lua",
   detailed = [[
lpath is a lfs-like Lua module to handle path, file system and file informations.

This module is designed to be easy extend to new system. Now it implements windows using Win32 API (lfs use legacy POSIX APIs on Windows), and POSIX systems.

This module is inspired by Python's os.path module. It split into 3 parts: path.info, path.fs and path itself.
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
