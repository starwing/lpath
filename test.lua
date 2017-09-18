local unit = require 'luaunit'
local eq       = unit.assertEquals
local table_eq = unit.assertItemsEquals
local is_true  = unit.assertTrue
local fail     = unit.assertErrorMsgMatches

local path = require "path"
local info = require "path.info"
local fs   = require "path.fs"

print("platform:", assert(fs.platform()))
print("binpath:", assert(fs.binpath()))
print("getcwd:", assert(fs.getcwd()))

local dir_table = {
   name = "test";
   { name = "test1";
     "file1";
     "file2";
     "file3";
   },
   { name = "test2";
     "file1";
     "file2";
     "file3";
   },
   { name = "test3";
     "file1";
     "file2";
     "file3";
   },
   "test4";
}

local function maketree(t)
   if t.name then
      assert(fs.mkdir(assert(t.name)))
      assert(fs.chdir(assert(t.name)))
   end
   for k, v in ipairs(t) do
      if type(v) == "string" then
         assert(fs.touch(v))
      else
         maketree(v)
      end
   end
   if t.name then
      assert(fs.chdir "..")
   end
end

local function collect_tree(t, r, p)
   local rt = r or {}
   local prefix = path(t.name, "")
   if p then prefix = path(p, prefix) end
   rt[#rt+1] = prefix
   for k, v in ipairs(t) do
      if type(v) == "string" then
         rt[#rt+1] = path(prefix, v)
      else
         collect_tree(v, rt, prefix)
      end
   end
   return rt
end

local function test(name)
   return function(t)
      local plat = info.platform
      local f = t[info.platform]
      if not f and plat ~= "windows" then
         f = t.posix
      end
      if f then _G["test_"..name] = f end
   end
end

test "codepage" {
   windows = function()
      path.ansi()
      path.utf8()
      eq(path.ansi"abc", "abc")
      eq(path.utf8"abc", "abc")
      path.ansi()
      fail(".-number/string expected, got boolean.*",
           path.ansi, false)
      fail(".-number/string expected, got boolean.*",
           path.utf8, false)
   end;
}

test "buffer" {
   windows = function()
      local name = (("a"):rep(256).."/"):rep(32)
      local result = (("a"):rep(256).."\\"):rep(32)
      eq(path(name), result)
      local name = ("a".."/"):rep(256)
      fail("path too complicate", assert, path(name))
   end;

   posix = function()
      local name = (("a"):rep(256).."/"):rep(32)
      eq(path(name), name)
      local name = ("a".."/"):rep(256)
      fail("path too complicate", assert, path(name))
   end;
}

test "normpath" {
   windows = function()
      fail(".-string expected.*", path)
      eq(path(""), [[.\]])
      eq(path("a"), [[a]])
      eq(path("a/."), [[a\]])
      eq(path("a/./b"), [[a\b]])
      eq(path("a/../b"), [[b]])
      eq(path("a/../../b"), [[..\b]])
      eq(path("/a/../../b"), [[\b]])
      eq(path("a/"), [[a\]])
      eq(path("a/b"), [[a\b]])
      eq(path(".."), [[..\]])
      eq(path("../.."), [[..\..\]])
      eq(path("//a/b/c"), [[\\A\B\c]])
      eq(path("a:b/c"), [[A:b\c]])
      eq(path("c:/b/c"), [[C:\b\c]])
   end;

   posix = function()
      fail(".-string expected.*", path)
      eq(path(""), [[./]])
      eq(path("a"), [[a]])
      eq(path("a/"), [[a/]])
      eq(path("a/."), [[a/]])
      eq(path("a/./b"), [[a/b]])
      eq(path("a/../b"), [[b]])
      eq(path("a/../../b"), [[../b]])
      eq(path("/a/../../b"), [[/b]])
      eq(path("a/b"), [[a/b]])
      eq(path(".."), [[../]])
      eq(path("../.."), [[../../]])
      eq(path("//a/b/c"), [[/a/b/c]])
      eq(path("a:b/c"), [[a:b/c]])
      eq(path("c:/b/c"), [[c:/b/c]])
   end;
}

test "join" {
   windows = function()
      path.ansi()
      eq(path("a", ""), [[a\]])
      eq(path("a", "b"), [[a\b]])
      eq(path("a", "b"), path.join("a", "b"))
      eq(path("", "a/", "b/"), [[a\b\]])
      eq(path("", "a/", "/b"), [[\b]])
      eq(path("c:", "a/", "d:/b"), [[D:\b]])
   end;

   posix = function()
      path.ansi()
      eq(path("a", ""), [[a/]])
      eq(path("a", "b"), [[a/b]])
      eq(path("a", "b"), path.join("a", "b"))
      eq(path("", "a/", "b/"), [[a/b/]])
      eq(path("", "a/", "/b"), [[/b]])
      eq(path("c:", "a/", "d:/b"), [[c:/a/d:/b]])
   end;
}

test "abs" {
   windows = function()
      local cwd = fs.getcwd()
      local dir = assert(fs.tmpdir())
      fs.chdir(dir)
      assert(fs.touch "test")
      eq(path(fs.getcwd(), "test"), path.abs "test")
      eq(path(fs.getcwd(), "test"), fs.realpath "test")
      fs.chdir(cwd)
      is_true(path.isabs "/")
      is_true(path.isabs "c:/")
      is_true(path.isabs "//aaa/bb/")
      is_true(not path.isabs "aa/bb/")
      is_true(not path.isabs "c:")
      is_true(not path.isabs "c:aa/bb")
      is_true(not path.isabs "//aaa/bb")
   end;

   posix = function()
      local cwd = fs.getcwd()
      local dir = assert(fs.tmpdir())
      fs.chdir(dir)
      assert(fs.touch "test")
      eq(path(fs.getcwd(), "test"), path.abs "test")
      fs.chdir(cwd)
      is_true(path.isabs "/")
      is_true(not path.isabs "c:/")
      is_true(path.isabs "//aaa/bb/")
      is_true(not path.isabs "aa/bb/")
      is_true(not path.isabs "c:")
      is_true(not path.isabs "c:aa/bb")
      is_true(path.isabs "//aaa/bb")
   end;
}

test "rel" {
   windows = function()
      eq(path.rel("", ""), [[.\]])
      eq(path.rel("a", "a"), [[.\]])
      eq(path.rel("a", "b"), [[..\a]])
      eq(path.rel("a/b/ccc", "a/b/cccc/"), [[..\ccc]])
      -- eq(path.rel("a", path.abs"b"), [[..\a]])
      eq(path.rel("a/b/c/d", "a/b/e/f"), [[..\..\c\d]])
      eq(path.rel("c:a", "c:b"), [[C:..\a]])
      eq(path.rel("c:a", "d:b"), [[C:a]])
      eq(path.rel("c:/a", "d:b"), [[C:\a]])
      assert(fs.chdir "c:/")
      eq(path.rel("c:/a", path.abs"c:b"), [[C:..\a]])
   end;

   posix = function()
      eq(path.rel("a", "b"), [[../a]])
      --eq(path.rel("a", path.abs"b"), [[..\a]])
      eq(path.rel("a/b/c/d", "a/b/e/f"), [[../../c/d]])
      eq(path.rel("c:a", "c:b"), [[../c:a]])
      eq(path.rel("c:a", "d:b"), [[../c:a]])
      eq(path.rel("c:/a", "d:b"), [[../c:/a]])
      --eq(path.rel("c:/a", path.abs"c:b"), [[../c:/a]])
   end;
}

test "split" {
   windows = function()
      local a, b = path.split ""
      eq(a, ""); eq(b, "")
      local a, b = path.split "a/b"
      eq(a, "a/"); eq(b, "b")
      local a, b = path.split "aaa/bbb"
      eq(a, "aaa/"); eq(b, "bbb")
      local a, b = path.split "/"
      eq(a, "/"); eq(b, "")
      local a, b = path.split "/a"
      eq(a, "/"); eq(b, "a")
      local a, b = path.split "a"
      eq(a, ""); eq(b, "a")
      local a, b = path.split "aaa"
      eq(a, ""); eq(b, "aaa")
      local a, b = path.split "a/"
      eq(a, "a/"); eq(b, "")
      local a, b = path.split "//a/b/c"
      eq(a, "//a/b/"); eq(b, "c")
      local a, b = path.split "//a/b/"
      eq(a, "//a/b/"); eq(b, "")
      local a, b = path.split "c:"
      eq(a, "c:"); eq(b, "")
      local a, b = path.split "c:/"
      eq(a, "c:/"); eq(b, "")
      local a, b = path.split "c:/a"
      eq(a, "c:/"); eq(b, "a")
   end;

   posix = function()
      local a, b = path.split ""
      eq(a, ""); eq(b, "")
      local a, b = path.split "a/b"
      eq(a, "a/"); eq(b, "b")
      local a, b = path.split "aaa/bbb"
      eq(a, "aaa/"); eq(b, "bbb")
      local a, b = path.split "/"
      eq(a, "/"); eq(b, "")
      local a, b = path.split "/a"
      eq(a, "/"); eq(b, "a")
      local a, b = path.split "a"
      eq(a, ""); eq(b, "a")
      local a, b = path.split "aaa"
      eq(a, ""); eq(b, "aaa")
      local a, b = path.split "a/"
      eq(a, "a/"); eq(b, "")
      local a, b = path.split "//a/b/c"
      eq(a, "//a/b/"); eq(b, "c")
      local a, b = path.split "//a/b/"
      eq(a, "//a/b/"); eq(b, "")
      local a, b = path.split "c:"
      eq(a, ""); eq(b, "c:")
      local a, b = path.split "c:/"
      eq(a, "c:/"); eq(b, "")
      local a, b = path.split "c:/a"
      eq(a, "c:/"); eq(b, "a")
   end;
}

test "splitdrive" {
   windows = function()
      local a, b = path.splitdrive "a/b"
      eq(a, ""); eq(b, "a/b")
      local a, b = path.splitdrive "c:a/b"
      eq(a, "c:"); eq(b, "a/b")
      local a, b = path.splitdrive "c:/a/b"
      eq(a, "c:"); eq(b, "/a/b")
      local a, b = path.splitdrive "cd:/a/b"
      eq(a, ""); eq(b, "cd:/a/b")
      local a, b = path.splitdrive "//a/"
      eq(a, ""); eq(b, "//a/")
      local a, b = path.splitdrive "//a/b"
      eq(a, "//a/b"); eq(b, "")
      local a, b = path.splitdrive "//a/b/a/b"
      eq(a, "//a/b"); eq(b, "/a/b")
      local a, b = path.splitdrive "//?/a"
      eq(a, "//?/"); eq(b, "a")
      local a, b = path.splitdrive "//?//a"
      eq(a, "//?/"); eq(b, "/a")
      local a, b = path.splitdrive "//?///a/"
      eq(a, "//?/"); eq(b, "//a/")
      local a, b = path.splitdrive "//?///a/b"
      eq(a, "//?///a/b"); eq(b, "")
      local a, b = path.splitdrive "//?///a/b/"
      eq(a, "//?///a/b"); eq(b, "/")
      local a, b = path.splitdrive "//?/c:"
      eq(a, "//?/c:"); eq(b, "")
      local a, b = path.splitdrive "//?/c:/"
      eq(a, "//?/c:"); eq(b, "/")
      local a, b = path.splitdrive "//?/cd:/"
      eq(a, "//?/"); eq(b, "cd:/")
   end;

   posix = function()
      local a, b = path.splitdrive "a/b"
      eq(a, ""); eq(b, "a/b")
      local a, b = path.splitdrive "c:a/b"
      eq(a, ""); eq(b, "c:a/b")
      local a, b = path.splitdrive "c:/a/b"
      eq(a, ""); eq(b, "c:/a/b")
      local a, b = path.splitdrive "cd:/a/b"
      eq(a, ""); eq(b, "cd:/a/b")
      local a, b = path.splitdrive "//a/"
      eq(a, ""); eq(b, "//a/")
      local a, b = path.splitdrive "//a/b"
      eq(a, ""); eq(b, "//a/b")
      local a, b = path.splitdrive "//a/b/a/b"
      eq(a, ""); eq(b, "//a/b/a/b")
      local a, b = path.splitdrive "//?/a"
      eq(a, ""); eq(b, "//?/a")
      local a, b = path.splitdrive "//?//a"
      eq(a, ""); eq(b, "//?//a")
      local a, b = path.splitdrive "//?///a/"
      eq(a, ""); eq(b, "//?///a/")
      local a, b = path.splitdrive "//?///a/b"
      eq(a, ""); eq(b, "//?///a/b")
      local a, b = path.splitdrive "//?///a/b/"
      eq(a, ""); eq(b, "//?///a/b/")
      local a, b = path.splitdrive "//?/c:"
      eq(a, ""); eq(b, "//?/c:")
      local a, b = path.splitdrive "//?/c:/"
      eq(a, ""); eq(b, "//?/c:/")
      local a, b = path.splitdrive "//?/cd:/"
      eq(a, ""); eq(b, "//?/cd:/")
   end;
}

test "expandvars" {
   windows = function()
      if fs.getenv "FOO" then
         fs.setenv("FOO", nil)
      end
      eq(fs.getenv "FOO", nil)
      eq(fs.setenv("FOO", "BAR"), "BAR")
      eq(fs.getenv "FOO", "BAR")
      eq(fs.expandvars "abc%FOO%abc", "abcBARabc")
   end;

   posix = function()
      if fs.getenv "FOO" then
         fs.setenv("FOO", nil)
      end
      eq(fs.getenv "FOO", nil)
      eq(fs.setenv("FOO", "BAR"), "BAR")
      eq(fs.getenv "FOO", "BAR")
      eq(fs.expandvars "abc${FOO}abc", "abcBARabc")
   end;
}

test "itercomp" {
   windows = function()
      local function collect(s)
         local t = {}
         for i, v in path.itercomp(s) do
            t[#t+1] = v
         end
         return t
      end
      table_eq(collect "aa/bb/cc/dd",
               {"aa", "bb", "cc", "dd"})
      table_eq(collect "/aa/bb/cc/dd",
               {"\\", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:/aa/bb/cc/dd",
               {"C:", "\\", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:aa/bb/cc/dd",
               {"C:", "aa", "bb", "cc", "dd"})
      table_eq(collect "//aa/bb/aa/bb/cc/dd",
               {[[\\AA\BB]],
               "\\", "aa", "bb", "cc", "dd"})
   end;

   posix = function()
      local function collect(s)
         local t = {}
         for i, v in path.itercomp(s) do
            t[#t+1] = v
         end
         return t
      end
      table_eq(collect "aa/bb/cc/dd",
               {"aa", "bb", "cc", "dd"})
      table_eq(collect "/aa/bb/cc/dd",
               {"/", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:/aa/bb/cc/dd",
               {"c:", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:aa/bb/cc/dd",
               { "c:aa", "bb", "cc", "dd"})
      table_eq(collect "//aa/bb/aa/bb/cc/dd",
               { "/", "aa", "bb", "aa", "bb", "cc", "dd"})
   end;
}

function test_splitext()
   local a, b = path.splitext "a/b"
   eq(a, "a/b"); eq(b, "")
   local a, b = path.splitext "a/b.c"
   eq(a, "a/b"); eq(b, ".c")
   local a, b = path.splitext "a/b.c.d"
   eq(a, "a/b.c"); eq(b, ".d")
   local a, b = path.splitext "a.b/c"
   eq(a, "a.b/c"); eq(b, "")
   local a, b = path.splitext "a.b/.c"
   eq(a, "a.b/"); eq(b, ".c")
   local a, b = path.splitext "a.b/.c.d"
   eq(a, "a.b/.c"); eq(b, ".d")
end

function test_dir()
   local cwd = fs.getcwd()
   local dir = assert(fs.tmpdir())
   assert(fs.chdir(dir))
   local old = { "a", "b", "c", "d", "e" }
   maketree(old)
   local t = {}
   for fn in fs.dir() do
      t[#t+1] = fn
   end
   table_eq(t, old)
   maketree(dir_table)
   local function check_dir(d)
      assert(fs.chdir(assert(d.name)))
      local files = {}
      local dirs = {}
      for fn, ft in fs.dir '.' do
         assert(fn ~= '.' and fn ~= '..')
         if ft == 'dir' or (ft == nil and fs.type(fn) == "dir") then
            assert(not dirs[fn])
            dirs[fn] = true
         else
            assert(not files[fn])
            files[fn] = true
         end
      end
      for k, v in ipairs(d) do
         if type(v) == "string" then
            assert(files[v], v)
            files[v] = nil
         else
            assert(dirs[v.name], v.name)
            dirs[v.name] = nil
            check_dir(v)
         end
      end
      assert(not next(files), next(files))
      assert(not next(dirs), next(dirs))
      assert(fs.chdir "..")
   end
   check_dir(dir_table)
   fs.chdir(cwd)
end

function test_makedirs()
   local cwd = fs.getcwd()
   local dir = assert(fs.tmpdir())
   assert(fs.chdir(dir))
   assert(fs.makedirs "a/b/c/d/e/f/g/h/i/j/k/l/m/n")
   assert(fs.mkdir "a")
   fail(".-rmdir:a.*", assert, fs.rmdir("a"))
   assert(fs.mkdir "b")
   eq(fs.rmdir "b", "b")
   eq(fs.type "a", "dir")
   assert(fs.makedirs("c", "a"))
   assert(fs.touch "c")
   assert(fs.touch "c/a/b")
   assert(fs.ftime "c")
   assert(fs.cmpftime("a", "c"))
   assert(fs.makedirs("c", "a", "b"))
   assert(fs.removedirs "a")
   fail(".-makedirs.*", assert, fs.makedirs("c/a/b/c"))
   assert(fs.remove "c/a/b")
   assert(fs.makedirs "c/a/b/c")
   io.output "txtfile"
   io.write "helloworld"
   io.close()
   eq(fs.fsize "txtfile", 10)
   assert(fs.copy("txtfile", "txtfile2"))
   assert(fs.rename("txtfile2", "txtfile3"))
   fs.chdir(cwd)
end

function test_fnmatch()
   is_true(fs.fnmatch("abc", "a[b]c"))
   is_true(fs.fnmatch("abc", "*a*b*c*"))
   is_true(not fs.fnmatch("abc", "a[^b]c"))
end

function test_walk()
   local cwd = fs.getcwd()
   local dir = assert(fs.tmpdir())
   assert(fs.removedirs "test")
   maketree(dir_table)
   local map = {}
   local inlist = {}
   for _, v in ipairs(collect_tree(dir_table)) do
      map[v] = true
   end
   map['test'..info.sep] = nil
   for path, state in fs.walk "test" do
      if state == 'out' then
         assert(inlist[path])
         inlist[path] = nil
      else
         eq(map[path], true)
         map[path] = nil
         if state == 'in' then
            inlist[path] = true
         end
      end
   end
   eq(next(map), nil)
   eq(next(inlist), nil)
   collectgarbage "collect"
   eq(#assert(fs.glob("*f[i]le*", "test")), 9)
   assert(fs.removedirs "test")
   assert(not fs.exists "test")
   fs.chdir(cwd)
end

os.exit(unit.LuaUnit.run(), true)

