local path = require "path"
local fs = require "path.fs"
local info = require "path.info"

local function print_table(name, t)
   print("table "..name..":")
   local keys = {}
   for k,v in pairs(t) do
      keys[#keys+1] = k
   end
   table.sort(keys)
   for _, k in ipairs(keys) do
      print((">  %-10s = %-10s"):format(k, tostring(t[k])))
   end
   print(("-"):rep(30))
end
print(fs.platform())
print_table("path", path)
print_table("path.fs", fs)
print_table("path.info", info)

local test_dir = {
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

local tests = {}
function add_test(name, f)
   tests[#tests+1] = function()
      print("[TEST] "..name)
      f()
      print("[ OK ] "..name)
   end
end

-- path

add_test("abs", function ()
   local cwd = fs.getcwd()
   assert(path.abs("foo") == path.join(cwd, "foo"))
end)
add_test("itercomp", function ()
   local function check(s, p)
      local i = 1
      local t = "abcdef"
      for comp in path.itercomp(s) do
         if p then
            assert(comp == p)
            p = p ~= info.sep and info.sep
         else
            assert(t:sub(i, i) == comp)
            i = i + 1
         end
      end
   end
   check("a/b/c/d/e/f")
   check("/a/b/c/d/e/f", info.sep)
   if info.platform == "windows" then
      check("c:/a/b/c/d/e/f", "c:")
      check([[\\server\mountpoint\a\b\c\d\e\f]], [[\\server\mountpoint]])
      check("//server/mountpoint/a/b/c/d/e/f", [[\\server\mountpoint]])
   end
end)
add_test("join", function ()
   local join = path.join
   local sep = info.sep
   assert(join("a", "b") == "a"..sep.."b")
   assert(join("a", "/b") == sep.."b")
   assert(join("/a", "/b") == sep.."b")
   assert(join("/a", "/b") == sep.."b")
   if info.platform == "windows" then
      assert(join("c:/a", "c:/b") == [[c:\b]])
      assert(join("//server/mp/a", "//server/mp/b") == [[\\server\mp\b]])
   end
end)
add_test("normcase", function ()
end)
add_test("realpath", function ()
end)
add_test("rel", function ()
end)
add_test("split", function ()
end)
add_test("splitdrive", function ()
end)
add_test("splitext", function ()
end)
add_test("type", function ()
end)

-- path.fs

add_test("chdir", function ()
end)
add_test("cmpftime", function ()
end)
add_test("copy", function ()
end)
add_test("mkdir", function ()
   fs.removedirs "test"
   assert(fs.mkdir "test")
   assert(fs.exists "test")
   assert(path.type "test" == "dir")
end)
add_test("makedirs", function ()
   local function _dfs(d)
      assert(fs.mkdir(assert(d.name)))
      assert(fs.chdir(assert(d.name)))
      for k, v in ipairs(d) do
         if type(v) == 'string' then
            assert(fs.touch(v))
         else
            _dfs(v)
         end
      end
      assert(fs.chdir "..")
   end
   local cwd = assert(fs.getcwd())
   assert(fs.chdir "test/..")
   _dfs(test_dir)
   assert(fs.chdir(cwd))
end)
add_test("dir", function ()
   local cwd = assert(fs.getcwd())
   assert(fs.chdir "test/..")
   local function _check(d)
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
            assert(files[v])
            files[v] = nil
         else
            assert(dirs[v.name])
            dirs[v.name] = nil
            _check(v)
         end
      end
      assert(not next(files), next(files))
      assert(not next(dirs), next(dirs))
      assert(fs.chdir "..")
   end
   _check(test_dir)
   assert(fs.chdir(cwd))
end)
add_test("glob", function ()
   assert(#fs.glob("*file*", "test") == 9)
end)
add_test("removedirs", function ()
   assert(fs.removedirs("test"))
end)
add_test("exists", function ()
end)
add_test("fsize", function ()
end)
add_test("ftime", function ()
end)
add_test("getcwd", function ()
end)
add_test("remove", function ()
end)
add_test("rmdir", function ()
end)
add_test("setenv", function ()
end)
add_test("touch", function ()
end)
add_test("walk", function ()
end)

for _,v in ipairs(tests) do v() end
fs.removedirs "test"
