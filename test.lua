local unit = require 'luaunit'
local eq       = unit.assertEquals
local table_eq = unit.assertItemsEquals
local is_true  = unit.assertTrue
local fail     = unit.assertErrorMsgMatches

local path, loc = require "path"
local info = require "path.info"
local fs   = require "path.fs"
local env  = require "path.env"

print(loc)
print("uname:", assert(env.uname()))
print("binpath:", assert(fs.binpath()))
print("getcwd:", assert(fs.getcwd()))

local dir_table = {
   test = {
      test1 = { "file1"; "file2"; "file3"; },
      test2 = { "file1"; "file2"; "file3"; },
      test3 = { "file1"; "file2"; "file3"; },
      "test4";
   },
}

local glob_tree = {
   "top.txt",
   test_glob = {
      "test.txt",
      case_1 = {
         "case1.txt",
         a = { a = { a = { a = { c = { a = { "a", "b" } } } } } },
      },
      case_2 = {
         "case2.txt",
         a = { a = { a = { a = { a = { a = { "b" } } } } } },
      }
   }
}

local function in_tmpdir(name)
   local f = _G[name]
   local t = {}
   function t:setup()
      self.cwd = fs.getcwd()
      assert(fs.chdir(assert(fs.tmpdir())))
   end
   function t:teardown()
      assert(fs.chdir(self.cwd))
   end
   t.test = f
   _G[name] = t
end

local function maketree(t)
   for k, v in pairs(t) do
      if type(k) == "string" then
         assert(fs.mkdir(k))
         assert(fs.chdir(k))
         maketree(v)
         assert(fs.chdir "..")
      else
         assert(fs.touch(v))
      end
   end
end

local function collect_tree(t, r, prefix)
   r = r or {}
   prefix = prefix or ""
   for k, v in pairs(t) do
      if type(k) == "string" then
         r[#r+1] = path(prefix, k)
         collect_tree(v, r, r[#r])
      else
         r[#r+1] = path(prefix, v)
      end
   end
   return r
end

function _G.test_codepage()
   if info.platform == "windows" then
      path.ansi()
      path.utf8()
      eq(path.ansi"abc", "abc")
      eq(path.utf8"abc", "abc")
      path.ansi()
      fail(".-number/string expected, got boolean.*",
           path.ansi, false)
      fail(".-number/string expected, got boolean.*",
           path.utf8, false)
   end
end

function _G.test_path()
   if info.platform == "windows" then
      eq(path 'a/b/c',     'a\\b\\c')
      eq(path 'c:/a/b/c',  'C:\\a\\b\\c')
      eq(path '//a/b',     '\\\\A\\B\\')
      eq(path '//a/b/c',   '\\\\A\\B\\c')
      eq(path '//a/b/c/d', '\\\\A\\B\\c\\d')
      eq(path "a/",        "a\\")
      eq(path "c:",        "C:")

      local name = (("a"):rep(256).."/"):rep(32)
      local result = (("a"):rep(256).."\\"):rep(32)
      eq(path(name), result)
      -- name = ("a".."/"):rep(256)
      -- fail("path too complicate", assert, path(name))

      eq(path('C:/a/b', 'x/y'), 'C:\\a\\b\\x\\y')
      eq(path('C:/a/b', '/x/y'), 'C:\\x\\y')
      eq(path('C:/a/b', 'D:x/y'), 'D:x\\y')
      eq(path('C:/a/b', 'D:/x/y'), 'D:\\x\\y')
      eq(path('C:/a/b', '//host/share/x/y'), '\\\\HOST\\SHARE\\x\\y')
      eq(path('C:/a/b', 'c:x/y'), 'C:\\a\\b\\x\\y')
      eq(path('C:/a/b', 'c:/x/y'), 'C:\\x\\y')

      eq(path("a", ""), "a\\")
      eq(path("/a/../../b"), "\\b")
      eq(path("a/../../../b"), "..\\..\\b")
      eq(path("a", "b", "c"), 'a\\b\\c')
      eq(path("foo/../bar", "123", "."), 'bar\\123\\')
      eq(path("c:a", "d:b", "c"), 'D:b\\c')
      eq(path("c:a", "d:", "c"), 'D:c')
      eq(path("c:a", "/", "c"), 'C:\\c')
      eq(path("a", "/b", "c"), '\\b\\c')
      eq(path("a", "//foo/bar", "c"), '\\\\FOO\\BAR\\c')
      eq(path("//./foo", "//./bar"), "\\\\.\\BAR\\")
      eq(path("//?/foo", "//?/bar"), "\\\\?\\foo\\bar")

      path.utf8()
      path.ansi()
      eq(path("a", ""),            "a\\")
      eq(path("a", "b"),           "a\\b")
      eq(path("a", "b"),           "a\\b")
      eq(path("", "a/", "b/"),     "a\\b\\")
      eq(path("", "a/", "/b"),     "\\b")
      eq(path("c:", "a/", "d:/b"), "D:\\b")

      eq(path(""), [[.]])
      eq(path("a"), [[a]])
      eq(path("a/."), [[a\]])
      eq(path("a/./b"), [[a\b]])
      eq(path("a/../b"), [[b]])
      eq(path("a/../../b"), [[..\b]])
      eq(path("/a/../../b"), [[\b]])
      eq(path("a/"), [[a\]])
      eq(path("a/b"), [[a\b]])
      eq(path(".."), [[..]])
      eq(path("../.."), [[..\..]])
      eq(path("//a/b/c"), [[\\A\B\c]])
      eq(path("a:b/c"), [[A:b\c]])
      eq(path("c:/b/c"), [[C:\b\c]])
   else
      eq(path("a/"), "a/")
      eq(path("/a"), "/a")
      eq(path("//a"), "//a")
      eq(path("///a"), "/a")
      eq(path.anchor("/a"), "/")
      eq(path.anchor("//a"), "//")
      eq(path.anchor("///a"), "/")
      eq(path.parent(".."), "../..")
      eq(path.parent("../"), "../..")
      eq(path.parent("../foo/bar"), "../foo")

      local name = (("a"):rep(256).."/"):rep(32)
      eq(path(name), name)
      -- name = ("a".."/"):rep(256)
      -- fail("path too complicate", assert, path(name))

      eq(path("a", ""), "a/")
      eq(path("/a/../../b"), "/b")

      path.utf8()
      path.ansi()
      eq(path("a", ""),            "a/")
      eq(path("a", "b"),           "a/b")
      eq(path("", "a/", "b/"),     "a/b/")
      eq(path("", "a/", "/b"),     "/b")
      eq(path("c:", "a/", "d:/b"), "c:/a/d:/b")

      eq(path(), '.')
      eq(path(""), [[.]])
      eq(path("a"), [[a]])
      eq(path("a/"), [[a/]])
      eq(path("a/."), [[a/]])
      eq(path("a/./b"), [[a/b]])
      eq(path("a/../b"), [[b]])
      eq(path("a/../../b"), [[../b]])
      eq(path("/a/../../b"), [[/b]])
      eq(path("/a/../../../../../../../../../..//b"), [[/b]])
      eq(path("a/b"), [[a/b]])
      eq(path(".."), [[..]])
      eq(path("../.."), [[../..]])
      eq(path("//a/b/c"), [[//a/b/c]])
      eq(path("a:b/c"), [[a:b/c]])
      eq(path("c:/b/c"), [[c:/b/c]])
   end
   eq(path.alt(), '.')
   eq(path.alt(""), [[.]])
   eq(path.alt("a"), [[a]])
   eq(path.alt("a/"), [[a/]])
   eq(path.alt("a/."), [[a/]])
   eq(path.alt("a/./b"), [[a/b]])
   eq(path.alt("a/../b"), [[b]])
   eq(path.alt("a/../../b"), [[../b]])
   eq(path.alt("/a/../../b"), [[/b]])
   eq(path.alt("/a/../../../../../../../../../..//b"), [[/b]])
   eq(path.alt("a/b"), [[a/b]])
   eq(path.alt(".."), [[..]])
   eq(path.alt("../.."), [[../..]])
   if info.platform == "windows" then
      eq(path.alt("//a/b/c"), [[//A/B/c]])
      eq(path.alt("a:b/c"), [[A:b/c]])
      eq(path.alt("c:/b/c"), [[C:/b/c]])
   else
      eq(path.alt("//a/b/c"), [[//a/b/c]])
      eq(path.alt("a:b/c"), [[a:b/c]])
      eq(path.alt("c:/b/c"), [[c:/b/c]])
   end
end

function _G.test_driveroot()
   if info.platform == "windows" then
      eq(path.drive 'c:',        'C:')
      eq(path.drive 'c:a/b',     'C:')
      eq(path.drive 'c:/',       'C:')
      eq(path.drive 'c:/a/b/',   'C:')
      eq(path.drive '//a/b',     '\\\\A\\B')
      eq(path.drive '//a/b/',    '\\\\A\\B')
      eq(path.drive '//a/b/c/d', '\\\\A\\B')
      eq(path.root 'c:',        '')
      eq(path.root 'c:a/b',     '')
      eq(path.root 'c:/',       '\\')
      eq(path.root 'c:/a/b/',   '\\')
      eq(path.root '//a/b',     '\\')
      eq(path.root '//a/b/',    '\\')
      eq(path.root '//a/b/c/d', '\\')

      eq(path.anchor "/a", "\\")
      eq(path.anchor "//a", "\\")
      eq(path.anchor "///a", "\\")
      eq(path.anchor 'c:', 'C:')
      eq(path.anchor 'c:a/b', 'C:')
      eq(path.anchor 'c:/', 'C:\\')
      eq(path.anchor 'c:/a/b/', 'C:\\')
      eq(path.anchor '//a/b', '\\\\A\\B\\')
      eq(path.anchor '//a/b/', '\\\\A\\B\\')
      eq(path.anchor '//a/b/c/d', '\\\\A\\B\\')

      eq(path.drive("//."), "")
      eq(path.root("//."), "\\")
      eq(path.drive("//./"), "\\\\.\\")
      eq(path.root("//./"), "\\")
      eq(path.drive("//.//"), "")
      eq(path.root("//.//"), "\\")
      eq(path.drive("//?"), "")
      eq(path.root("//?"), "\\")
      eq(path.drive("//?/"), "\\\\?\\")
      eq(path.root("//?/"), "")
      eq(path.drive("//?/c:"), "\\\\?\\C:")
      eq(path.root("//?/c:") ,"")
      eq(path.drive("//?/c:/"), "\\\\?\\C:")
      eq(path.root("//?/c:/"), "\\")
      eq(path.root("/a"), "\\")
      eq(path.root("//a"), "\\")
      eq(path.root("///a"), "\\")

      eq(path.drive "a/b",        "")
      eq(path.drive "c:a/b",      "C:")
      eq(path.drive "c:/a/b",     "C:")
      eq(path.drive "cd:/a/b",    "")
      eq(path.drive "//a/",       "\\\\A\\")
      eq(path.drive "//a/b",      "\\\\A\\B")
      eq(path.drive "//a/b/a/b",  "\\\\A\\B")
      eq(path.drive "//?/a",      "\\\\?\\")
      eq(path.drive "//?//a",     "\\\\?\\")
      eq(path.drive "//?///a/",   "\\\\?\\")
      eq(path.drive "//?///a/b",  "\\\\?\\")
      eq(path.drive "//?///a/b/", "\\\\?\\")
      eq(path.drive "//?/c:",     "\\\\?\\C:")
      eq(path.drive "//?/c:/",    "\\\\?\\C:")
      eq(path.drive "//?/cd:/",   "\\\\?\\")

      eq(path.root "/",         "\\")
      eq(path.root "c:/",       "\\")
      eq(path.root "//aaa/bb/", "\\")
      eq(path.root "aa/bb/",    "")
      eq(path.root "c:",        "")
      eq(path.root "c:aa/bb",   "")
      eq(path.root "///aaa/bb", "\\")
   else
      eq("", path.drive "a/b")
      eq("", path.drive "c:a/b")
      eq("", path.drive "c:/a/b")
      eq("", path.drive "cd:/a/b")
      eq("", path.drive "//a/")
      eq("", path.drive "//a/b")
      eq("", path.drive "//a/b/a/b")
      eq("", path.drive "//?/a")
      eq("", path.drive "//?//a")
      eq("", path.drive "//?///a/")
      eq("", path.drive "//?///a/b")
      eq("", path.drive "//?///a/b/")
      eq("", path.drive "//?/c:")
      eq("", path.drive "//?/c:/")
      eq("", path.drive "//?/cd:/")

      eq(path.root("/a"), "/")
      eq(path.root("//a"), "//")
      eq(path.root("///a"), "/")

      eq(path.root "/",         "/")
      eq(path.root "c:/",       "")
      eq(path.root "//aaa/bb/", "//")
      eq(path.root "aa/bb/",    "")
      eq(path.root "c:",        "")
      eq(path.root "c:aa/bb",   "")
      eq(path.root "///aaa/bb", "/")
   end
end

function _G.test_parts()
   local function collect(s)
      local t = {}
      for _, v in path.parts(s) do
         t[#t+1] = v
      end
      return t
   end

   eq(path.parts("a", "b", "c", "d", 1), "a")
   eq(path.parts("a/b", "c", "d", 1), "a")
   eq(path.parts("a/../b", "c", "d", 1), "b")
   eq(path.parts("a/b/c/d", -1), "d")
   eq(path.parts("../../a/b/c/d", 1), "..")
   eq(path.parts("../../a/b/c/d", 2), "..")
   eq(path.parts("../../a/b/c/d", 3), "a")
   eq(path.parts("../../a/b/c/d", 6), "d")
   eq(path.parts("../../a/b/c/d", 7), nil)
   eq(path.parts("../../a/b/c/d", -7), nil)
   eq(path.parts("../../a/b/c/d", -6), "..")
   eq(path.parts("../../a/b/c/d", -5), "..")
   eq(path.parts("../../a/b/c/d", -4), "a")
   eq(path.parts("../../a/b/c/d", -1), "d")

   table_eq(collect("a/b/c/d"), {"a", "b", "c", "d"})

   if info.platform == "windows" then
      table_eq(collect "a/b/c/d",
               {"a", "b", "c", "d"})
      table_eq(collect "a/b/c/d/",
               {"a", "b", "c", "d"})
      table_eq(collect "a/b/c/d",
               {"a", "b", "c", "d"})
      table_eq(collect "aa/bb/cc/dd",
               {"aa", "bb", "cc", "dd"})
      table_eq(collect "/aa/bb/cc/dd",
               {"\\", "aa", "bb", "cc", "dd"})
      table_eq(collect "/a/b/c/d",
               {"\\", "a", "b", "c", "d"})
      table_eq(collect "/a/b/c/d/",
               {"\\", "a", "b", "c", "d"})
      table_eq(collect "c:/aa/bb/cc/dd",
               {"C:\\", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:/a/b/c/d",
               {"C:\\", "a", "b", "c", "d"})
      table_eq(collect "c:/a/b/c/d/",
               {"C:\\", "a", "b", "c", "d"})
      table_eq(collect "c:aa/bb/cc/dd",
               {"C:", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:a/b/c/d",
               {"C:", "a", "b", "c", "d"})
      table_eq(collect "c:a/b/c/d/",
               {"C:", "a", "b", "c", "d"})
      table_eq(collect "//aa/bb/aa/bb/cc/dd",
               {[[\\AA\BB\]], "aa", "bb", "cc", "dd"})
      table_eq(collect "//a/b/a/b/c/d",
               {[[\\A\B\]], "a", "b", "c", "d"})
      table_eq(collect "//a/b/a/b/c/d/",
               {[[\\A\B\]], "a", "b", "c", "d"})
      table_eq(collect "c:/",
               {[[C:\]]})
      table_eq(collect "c:",
               {[[C:]]})
      table_eq(collect "//a/b/",
               {[[\\A\B\]]})
      table_eq(collect "//a/b",
               {[[\\A\B\]]})
   else
      table_eq(collect("/a/b/c/d"), {"/", "a", "b", "c", "d"})
      table_eq(collect "aa/bb/cc/dd",
               {"aa", "bb", "cc", "dd"})
      table_eq(collect "/aa/bb/cc/dd",
               {"/", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:/aa/bb/cc/dd",
               {"c:", "aa", "bb", "cc", "dd"})
      table_eq(collect "c:aa/bb/cc/dd",
               { "c:aa", "bb", "cc", "dd"})
      table_eq(collect "//aa/bb/aa/bb/cc/dd",
               { "//", "aa", "bb", "aa", "bb", "cc", "dd"})
   end
end

function _G.test_parentname()
   local function t(s, p, n)
      eq(path.parent(s), p)
      eq(path.name(s), n)
   end
   if info.platform == "windows" then
      local p = path.parent
      local s = 'z:a/b/c'
      eq(p(s),          'Z:a\\b')
      eq(p(p(s)),       'Z:a')
      eq(p(p(p(s))),    'Z:')
      eq(p(p(p(p(s)))), 'Z:..')
      s = 'z:/a/b/c'
      eq(p(s),          'Z:\\a\\b')
      eq(p(p(s)),       'Z:\\a')
      eq(p(p(p(s))),    'Z:\\')
      eq(p(p(p(p(s)))), 'Z:\\')
      s = '//a/b/c/d'
      eq(p(s),          '\\\\A\\B\\c')
      eq(p(p(s)),       '\\\\A\\B\\')
      eq(p(p(p(s))),    '\\\\A\\B\\')

      eq(path.parent(".."), "..\\..")
      eq(path.parent("../"), "..\\..")
      eq(path.parent("../foo/bar"), "..\\foo")

      eq(path.name 'c:', '')
      eq(path.name 'c:/', '')
      eq(path.name 'c:a/b', 'b')
      eq(path.name 'c:/a/b', 'b')
      eq(path.name 'c:a/b.py', 'b.py')
      eq(path.name 'c:/a/b.py', 'b.py')
      eq(path.name '//My.py/Share.php', '')
      eq(path.name '//My.py/Share.php/a/b', 'b')

      t("",        "..",         "")
      t("a/b",     "a",          "b")
      t("aaa/bbb", "aaa",        "bbb")
      t("/",       "\\",         "")
      t("/a",      "\\",         "a")
      t("a",       ".",          "a")
      t("aaa",     ".",          "aaa")
      t("a/",      ".",          "a")
      t("//a/b/c", "\\\\A\\B\\", "c")
      t("//a/b/",  "\\\\A\\B\\", "")
      t("c:",      "C:..",       "")
      t("c:/",     "C:\\",       "")
      t("c:/a",    "C:\\",       "a")
   else
      t("",        "..",    "")
      t("a/b",     "a",     "b")
      t("a",       ".",     "a")
      t("a/",      ".",     "a")
      t("aaa",     ".",     "aaa")
      t("aaa/",    ".",     "aaa")
      t("aaa/bbb", "aaa",   "bbb")
      t("/",       "/",     "")
      t("/a",      "/",     "a")
      t("/a/",     "/",     "a")
      t("//a/b/c", "//a/b", "c")
      t("//a/b",   "//a",   "b")
      t("c:",      ".",     "c:")
      t("c:/",     ".",     "c:")
      t("c:/a",    "c:",    "a")
      t("c:/a/",   "c:",    "a")
   end

   eq(path.parent("abc/"), ".")
   eq(path.parent(""), "..")
   eq(path.parent("."), "..")
   eq(path.parent("./"), "..")
   eq(path.parent("../foo"), "..")
   eq(path.name(".."), "..")
   eq(path.name(""), "")
   eq(path.name("/"), "")
   eq(path.name("/abc"), "abc")
   eq(path.name("/abc/"), "abc")
   eq(path.name("abc/"), "abc")
end

function _G.test_stem()
   if info.platform == "windows" then
      eq(path.stem'c:', '')
      eq(path.stem'c:.', '')
      --eq(path.stem'c:..', '..')
      eq(path.stem'c:/', '')
      eq(path.stem'c:a/b', 'b')
      eq(path.stem'c:a/b.py', 'b')
      eq(path.stem'c:a/.hgrc', '.hgrc')
      eq(path.stem'c:a/.hg.rc', '.hg')
      eq(path.stem'c:a/b.tar.gz', 'b.tar')
      eq(path.stem'c:a/Some name. Ending with a dot.',
         'Some name. Ending with a dot.')
   end
   eq(path.stem("abc"), "abc")
   eq(path.stem(".abc"), ".abc")
   eq(path.stem("abc.exe"), "abc")
   eq(path.stem(".abc.exe"), ".abc")
   eq(path.stem("abc.tar.gz"), "abc.tar")
   eq(path.stem(".abc.tar.gz"), ".abc.tar")
end

function _G.test_suffix()
   local function collect(name)
      local t = {}
      for _, ext in path.suffixes(name) do
         t[#t+1] = ext
      end
      return t
   end

   if info.platform == "windows" then
      eq(path.suffix'c:', '')
      eq(path.suffix'c:/', '')
      eq(path.suffix'c:a/b', '')
      eq(path.suffix'c:/a/b', '')
      eq(path.suffix'c:a/b.py', '.py')
      eq(path.suffix'c:/a/b.py', '.py')
      eq(path.suffix'c:a/.hgrc', '')
      eq(path.suffix'c:/a/.hgrc', '')
      eq(path.suffix'c:a/.hg.rc', '.rc')
      eq(path.suffix'c:/a/.hg.rc', '.rc')
      eq(path.suffix'c:a/b.tar.gz', '.gz')
      eq(path.suffix'c:/a/b.tar.gz', '.gz')
      eq(path.suffix'c:a/Some name. Ending with a dot.', '')
      eq(path.suffix'c:/a/Some name. Ending with a dot.', '')
      eq(path.suffix'//My.py/Share.php', '')
      eq(path.suffix'//My.py/Share.php/a/b', '')

      table_eq(collect 'c:', {})
      table_eq(collect 'c:/', {})
      table_eq(collect 'c:a/b', {})
      table_eq(collect 'c:/a/b', {})
      table_eq(collect 'c:a/b.py', {'.py'})
      table_eq(collect 'c:/a/b.py', {'.py'})
      table_eq(collect 'c:a/.hgrc', {})
      table_eq(collect 'c:/a/.hgrc', {})
      table_eq(collect 'c:a/.hg.rc', {'.rc'})
      table_eq(collect 'c:/a/.hg.rc', {'.rc'})
      table_eq(collect 'c:a/b.tar.gz', {'.tar', '.gz'})
      table_eq(collect 'c:/a/b.tar.gz', {'.tar', '.gz'})
      table_eq(collect '//My.py/Share.php', {})
      table_eq(collect '//My.py/Share.php/a/b', {})
      table_eq(collect 'c:a/Some name. Ending with a dot.', {})
      table_eq(collect 'c:/a/Some name. Ending with a dot.', {})
   end
   eq(path.suffix "a/b", "")
   eq(path.suffix "a/b.c", ".c")
   eq(path.suffix "a/b.c.d", ".d")
   eq(path.suffix "a.b/c", "")
   eq(path.suffix "a.b/.c", "")
   eq(path.suffix "a.b/c.d", ".d")
   eq(path.suffix "a.b/", ".b")

   eq(path.suffix("abc"), "")
   eq(path.suffix(".abc"), "")
   eq(path.suffix("abc.exe"), ".exe")
   eq(path.suffix(".abc.exe"), ".exe")
   eq(path.suffix("abc.tar.gz"), ".gz")
   eq(path.suffix(".abc.tar.gz"), ".gz")

   table_eq(collect "", {})
   table_eq(collect "abc", {})
   table_eq(collect ".abc", {})
   table_eq(collect "abc.def.zzz", {".def", ".zzz"})
   table_eq(collect "abc....zzz", {".", ".", ".", ".zzz"})
end

function _G.test_fnmatch()
   is_true(path.fnmatch("abc", "a[b]c"))
   is_true(path.fnmatch("abc", "*a*b*c*"))
   is_true(not path.fnmatch("abc", "a[!b]c"))

   local function check(a, b, r, f)
      local ok = (r == nil and true or r)
      eq(not not (f or path.fnmatch)(a, b), ok)
   end

   check('abc', 'abc')
   check('abc', '?*?')
   check('abc', '???*')
   check('abc', '*???')
   check('abc', '???')
   check('abc', '*')
   check('abc', 'ab[cd]')
   check('abc', 'ab[!de]')
   check('abc', 'ab[de]', false)
   check('a',   '??',     false)
   check('a',   'b',      false)

   check('\\', '[\\]')
   check('a',  '[!\\]')
   check('\\', '[!\\]', false)

   check('foo\nbar',   'foo*')
   check('foo\nbar\n', 'foo*')
   check('\nfoo',      'foo*', false)
   check('\n',         '*')

   -- test slow
   check(('a'):rep(50), '*a*a*a*a*a*a*a*a*a*a')
   -- The next "takes forever" if the regexp translation is
   -- straightforward.  See bpo-40480.
   check(('a'):rep(50)..'b', '*a*a*a*a*a*a*a*a*a*a', false)

   -- test edge case
   check('[', '[[]')
   check('&', '[a&&b]')
   check('|', '[a||b]')
   check('~', '[a~~b]')
   check(',', '[a-z+--A-Z]')
   check('.', '[a-z--/A-Z]')

   -- test case
   if info.platform == "windows" then
      check('abc', 'abc', true)
      check('AbC', 'abc', true)
      check('abc', 'AbC', true)
      check('AbC', 'AbC', true)
      check('usr/bin',  'usr/bin',  true)
      check('usr\\bin', 'usr/bin',  true)
      check('usr/bin',  'usr\\bin', true)
      check('usr\\bin', 'usr\\bin', true)
   else
      check('abc', 'abc', true)
      check('AbC', 'abc', false)
      check('abc', 'AbC', false)
      check('AbC', 'AbC', true)
      check('usr/bin',  'usr/bin',  true)
      check('usr\\bin', 'usr/bin',  false)
      check('usr/bin',  'usr\\bin', false)
      check('usr\\bin', 'usr\\bin', true)
   end
end

function _G.test_match()
   local function assert_true(cond)
      assert(not not cond, cond)
   end
   local function assert_false(cond)
      assert(not cond, cond)
   end
   fail(".*empty pattern.*", function()
      path.match("a", "")
   end)
   assert_false(path.match("a", "."))
   assert_true(path.match(".", "."))
   assert_false(path.match('../../a', '../a'))

   -- Simple relative pattern.
   assert_true(path.match('b.lua', 'b.lua'))
   assert_true(path.match('a/b.lua', 'b.lua'))
   assert_true(path.match('/a/b.lua', 'b.lua'))
   assert_false(path.match('a.lua', 'b.lua'))
   assert_false(path.match('b/lua', 'b.lua'))
   assert_false(path.match('/a.lua', 'b.lua'))
   assert_false(path.match('b.lua/c', 'b.lua'))

   -- Wilcard relative pattern.
   assert_true(path.match('b.lua', 'b.lua'))
   assert_true(path.match('b.lua', '*.lua'))
   assert_true(path.match('a/b.lua', '*.lua'))
   assert_true(path.match('/a/b.lua', '*.lua'))
   assert_false(path.match('b.pyc', '*.lua'))
   assert_false(path.match('b./lua', '*.lua'))
   assert_false(path.match('b.lua/c', '*.lua'))

   -- Multi-part relative pattern.
   assert_true(path.match('ab/c.lua', 'a*/*.lua'))
   assert_true(path.match('/d/ab/c.lua', 'a*/*.lua'))
   assert_false(path.match('a.lua', 'a*/*.lua'))
   assert_false(path.match('/dab/c.lua', 'a*/*.lua'))
   assert_false(path.match('ab/c.lua/d', 'a*/*.lua'))

   -- Absolute pattern.
   assert_true(path.match('/b.lua', '/*.lua'))
   assert_false(path.match('b.lua', '/*.lua'))
   assert_false(path.match('a/b.lua', '/*.lua'))
   assert_false(path.match('/a/b.lua', '/*.lua'))

   -- Multi-part absolute pattern.
   assert_true(path.match('/a/b.lua', '/a/*.lua'))
   assert_false(path.match('/ab.lua', '/a/*.lua'))
   assert_false(path.match('/a/b/c.lua', '/a/*.lua'))

   -- Multi-part glob-style pattern.
   assert_false(path.match('/a/b/c.lua', '/**/*.lua'))
   assert_true(path.match('/a/b/c.lua', '/a/**/*.lua'))

   if info.platform == "windows" then
      assert_false(path.match("C:/foo", "D:/foo"))
   end
end

function _G.test_env()
   if info.platform == "windows" then
      local long = ("a"):rep(1024)
      env.set("long", long)
      eq(env.get "long", long)
      eq(env.expand "<%long%>", "<"..long..">")
      if env.get "FOO" then
         env.set("FOO", nil)
      end
      eq(env.get "FOO", nil)
      eq(env.set("FOO", "BAR"), "BAR")
      eq(env.get "FOO", "BAR")
      eq(env.expand "abc%FOO%abc", "abcBARabc")
   else
      eq(env.get "FOO", nil)
      eq(env.set("FOO", "BAR"), "BAR")
      eq(env.get "FOO", "BAR")
      eq(env.expand "abc${FOO}abc", "abcBARabc")
      fail(".-syntax error.*",
         function() assert(env.expand("$(ls /")) end)
      if env.get "FOO" then
         env.set("FOO", nil)
      end
   end
end

function _G.test_abs()
   if info.platform == "windows" then
      assert(fs.touch "test")
      assert(fs.touch("test", nil, nil))
      assert(fs.touch("test", os.time()))
      assert(fs.chdir "C:/")
      eq(path(fs.getcwd(), "Windows"), path.abs "Windows")
      eq(path(fs.getcwd(), "Windows"), path.resolve "Windows")
   else
      assert(fs.touch "test")
      assert(fs.touch("test", nil, nil))
      eq(path(fs.getcwd(), "test"), path.abs "test")
   end
end

function _G.test_rel()
   if info.platform == "windows" then
      eq(path.rel("/aaa/bbb", "/aaa/bbb/ccc"), '..')
      eq(path.rel("/aaa/bbb/", "/aaa/bbb/ccc"), '..')
      eq(path.rel("/aaa/bbb/ccc", "/aaaabbb"), '..\\aaa\\bbb\\ccc')
      eq(path.rel("/aaaabbb/ccc", "/aaa/bbb"), '..\\..\\aaaabbb\\ccc')
      eq(path.rel("a"), 'a')
      eq(path.rel(path.abs("a/")), 'a\\')
      eq(path.rel(path.abs("a")), 'a')
      eq(path.rel("a/b"), 'a\\b')
      eq(path.rel("../a/b"), '..\\a\\b')
      eq(path.rel("a", "b"), '..\\a')
      eq(path.rel("a", "b/"), '..\\a')
      eq(path.rel("a/", "b/"), '..\\a\\')
      eq(path.rel("a/", "b/"), '..\\a\\')
      eq(path.rel("a", "b/c"), '..\\..\\a')
      eq(path.rel("c:/foo/bar/bat", "c:/x/y"), '..\\..\\foo\\bar\\bat')
      eq(path.rel("//conky/mountpoint/a", "//conky/mountpoint/b/c"), '..\\..\\a')
      eq(path.rel("a", "a"), '.')
      eq(path.rel("/foo/bar/bat", "/x/y/z"), '..\\..\\..\\foo\\bar\\bat')
      eq(path.rel("/foo/bar/bat", "/foo/bar"), 'bat')
      eq(path.rel("/foo/bar/bat", "/"), 'foo\\bar\\bat')
      eq(path.rel("/", "/foo/bar/bat"), '..\\..\\..')
      eq(path.rel("/foo/bar/bat", "/x"), '..\\foo\\bar\\bat')
      eq(path.rel("/x", "/foo/bar/bat"), '..\\..\\..\\x')
      eq(path.rel("/", "/"), '.')
      eq(path.rel("/a", "/a"), '.')
      eq(path.rel("/a/b", "/a/b"), '.')
      eq(path.rel("c:/foo", "C:/FOO"), '.')
      eq(path.rel("a/b/ccc", "a/b/cccc/"), [[..\ccc]])
      eq(path.rel("a", path.abs"b"), [[..\a]])
      eq(path.rel("a/b/c/d", "a/b/e/f"), [[..\..\c\d]])
      eq(path.rel("c:a", "c:b"), [[..\a]])
      eq(path.rel("c:a", "d:b"), [[C:a]])
      eq(path.rel("c:/a", "d:b"), [[C:\a]])
      local cwd = fs.getcwd()
      assert(fs.chdir "c:/")
      eq(path.rel("c:/a", path.abs"c:b"), [[..\a]])
      assert(fs.chdir(cwd))

      eq(path.rel("a"), 'a')
      eq(path.rel(path.abs("a")), 'a')
      eq(path.rel("a/b"), 'a\\b')
      eq(path.rel("../a/b"), '..\\a\\b')
      eq(path.rel("a", "b"), '..\\a')
      eq(path.rel("a", "b/c"), '..\\..\\a')
      eq(path.rel("c:/foo/bar/bat", "c:/x/y"), '..\\..\\foo\\bar\\bat')
      eq(path.rel("//conky/mountpoint/a", "//conky/mountpoint/b/c"), '..\\..\\a')
      eq(path.rel("a", "a"), '.')
      eq(path.rel("/foo/bar/bat", "/x/y/z"), '..\\..\\..\\foo\\bar\\bat')
      eq(path.rel("/foo/bar/bat", "/foo/bar"), 'bat')
      eq(path.rel("/foo/bar/bat", "/"), 'foo\\bar\\bat')
      eq(path.rel("/", "/foo/bar/bat"), '..\\..\\..')
      eq(path.rel("/foo/bar/bat", "/x"), '..\\foo\\bar\\bat')
      eq(path.rel("/x", "/foo/bar/bat"), '..\\..\\..\\x')
      eq(path.rel("/", "/"), '.')
      eq(path.rel("/a", "/a"), '.')
      eq(path.rel("/a/b", "/a/b"), '.')
      eq(path.rel("c:/foo", "C:/FOO"), '.')
      eq(path.rel("a/b/ccc", "a/b/cccc/"), [[..\ccc]])
      -- eq(path.rel("a", path.abs"b"), [[..\a]])
      eq(path.rel("a/b/c/d", "a/b/e/f"), [[..\..\c\d]])
      eq(path.rel("c:a", "c:b"), [[..\a]])
      eq(path.rel("c:a", "d:b"), [[C:a]])
      eq(path.rel("c:/a", "d:b"), [[C:\a]])
      assert(fs.chdir "c:/")
      eq(path.rel("c:/a", path.abs"c:b"), [[..\a]])
   else
      eq(path.rel("/aaa/bbb", "/aaa/bbb/ccc"), '..')
      eq(path.rel("/aaa/bbb/", "/aaa/bbb/ccc"), '..')
      eq(path.rel("/aaa/bbb/ccc", "/aaaabbb"), '../aaa/bbb/ccc')
      eq(path.rel("/aaaabbb/ccc", "/aaa/bbb"), '../../aaaabbb/ccc')
      eq(path.rel("a"), 'a')
      eq(path.rel(path.abs("a/")), 'a/')
      eq(path.rel(path.abs("a")), 'a')
      eq(path.rel("a/b"), 'a/b')
      eq(path.rel("../a/b"), '../a/b')
      eq(path.rel("a", "b"), '../a')
      eq(path.rel("a", "b/"), '../a')
      eq(path.rel("a/", "b/"), '../a/')
      eq(path.rel("a/", "b/"), '../a/')
      eq(path.rel("a", "b/c"), '../../a')
      eq(path.rel("c:/foo/bar/bat", "c:/x/y"), '../../foo/bar/bat')
      eq(path.rel("//conky/mountpoint/a", "//conky/mountpoint/b/c"), '../../a')
      eq(path.rel("a", "a"), '.')
      eq(path.rel("/foo/bar/bat", "/x/y/z"), '../../../foo/bar/bat')
      eq(path.rel("/foo/bar/bat", "/foo/bar"), 'bat')
      eq(path.rel("/foo/bar/bat", "/"), 'foo/bar/bat')
      eq(path.rel("/", "/foo/bar/bat"), '../../..')
      eq(path.rel("/foo/bar/bat", "/x"), '../foo/bar/bat')
      eq(path.rel("/x", "/foo/bar/bat"), '../../../x')
      eq(path.rel("/", "/"), '.')
      eq(path.rel("/a", "/a"), '.')
      eq(path.rel("/a/b", "/a/b"), '.')
      eq(path.rel("a/b/ccc", "a/b/cccc/"), '../ccc')
      eq(path.rel("a", path.abs"b"), '../a')
      eq(path.rel("a/b/c/d", "a/b/e/f"), '../../c/d')
      local cwd = fs.getcwd()
      assert(fs.chdir "/")
      eq(path.rel("/a", path.abs"b"), '../a')
      assert(fs.chdir(cwd))

      eq(path.rel("a", "b"), [[../a]])
      --eq(path.rel("a", path.abs"b"), [[..\a]])
      eq(path.rel("a/b/c/d", "a/b/e/f"), [[../../c/d]])
      eq(path.rel("c:a", "c:b"), [[../c:a]])
      eq(path.rel("c:a", "d:b"), [[../c:a]])
      eq(path.rel("c:/a", "d:b"), [[../c:/a]])
      --eq(path.rel("c:/a", path.abs"c:b"), [[../c:/a]])
   end
end
in_tmpdir "test_rel"

function _G.test_makedirs()
   if info.platform == "windows" then
      local long = "//?/"..("a"):rep(1024)
      fail(".-chdir:.*:%(errno%=206%).*",
         function() assert(fs.chdir(long)) end)
      fail(".-makedirs:.*:%(errno%=2%).*",
         function() assert(fs.makedirs(long)) end)
      fail(".-mkdir:.*:%(errno%=2%).*",
         function() assert(fs.mkdir(long)) end)
      long = path(("//?/%s/%s/%s/%s"):format(
         path.cwd(), ("a"):rep(100), ("b"):rep(100), ("c"):rep(100)))
      assert(fs.makedirs(long))
      eq(path.abs(long), long)
      assert(#path.resolve(long) >= #long)
   end
   assert(fs.makedirs "a/b/c/d/e/f/g/h/i/j/k/l/m/n")
   assert(path.resolve "a")
   assert(fs.mkdir "a")
   fail(".-rmdir:a.*", assert, fs.rmdir("a"))
   assert(fs.mkdir "b")
   eq(fs.rmdir "b", "b")
   eq(fs.isdir "a", "a")
   assert(fs.removedirs "a")
   assert(fs.makedirs("c", "a"))
   assert(fs.touch "c")
   assert(fs.touch "c/a/b")
   assert(fs.unlockdirs "c")
   assert(fs.ctime "c")
   assert(fs.mtime "c")
   assert(fs.atime "c")
   assert(fs.makedirs("c", "a", "b"))
   fail(".-makedirs.*", assert, fs.makedirs("c/a/b/c"))
   assert(fs.remove "c/a/b")
   assert(fs.makedirs "c/a/b/c")
   io.output "txtfile"
   io.write "helloworld"
   io.close()
   eq(fs.size "txtfile", 10)
   assert(fs.copy("txtfile", "txtfile2"))
   assert(fs.rename("txtfile2", "txtfile3"))
   if info.platform ~= "windows" then
      assert(fs.symlink("txtfile", "txtfile4"))
   end
end
in_tmpdir "test_makedirs"

function _G.test_dir()
   maketree(dir_table)
   for f, s in fs.dir "test/test1/file1" do
      if info.platform == "windows" then
         eq(f, "test\\test1\\file1")
      else
         eq(f, "test/test1/file1")
      end
      eq(s, "file")
   end
   local function check_dir(d)
      local files = {}
      local dirs = {}
      for fn, ft in fs.dir() do
         assert(fn ~= '.' and fn ~= '..')
         if ft == 'dir' or (ft == nil and fs.type(fn) == "dir") then
            assert(not dirs[fn])
            dirs[fn] = true
         else
            assert(not files[fn])
            files[fn] = true
         end
      end
      for k, v in pairs(d) do
         if type(k) == "string" then
            assert(dirs[k], k)
            dirs[k] = nil
            assert(fs.chdir(assert(k)))
            check_dir(v)
            assert(fs.chdir "..")
         else
            assert(files[v], v)
            files[v] = nil
         end
      end
      assert(not next(files), next(files))
      assert(not next(dirs), next(dirs))
   end
   check_dir(dir_table)
   local function test_count(n, ...)
      local count = 0
      for _ in ... do
         count = count + 1
      end
      eq(count, n)
   end
   local function test_table(r, ...)
      local t = {}
      for n in ... do
         t[#t+1] = n
      end
      table.sort(t)
      table_eq(t, r)
   end
   maketree(glob_tree)
   assert(fs.chdir "test_glob")
   test_table({'case_1', 'case_2', 'test.txt'}, fs.dir())
   for _, state in fs.dir() do
      if state == "dir" then break end
   end
   if info.platform == 'windows' then
      test_table({'.\\case_1', '.\\case_2', '.\\test.txt'}, fs.dir'.')
   else
      test_table({'./case_1', './case_2', './test.txt'}, fs.dir'.')
   end

   test_count(36, fs.scandir())
   test_count(5, fs.scandir(1))
   test_count(11, fs.scandir(2))

   assert(fs.mkdir "../test_deep")
   assert(fs.chdir "../test_deep")
   local old = { "a", "b", "c", "d", "e" }
   maketree(old)
   local t = {}
   for fn in fs.dir() do
      t[#t+1] = fn
   end
   table_eq(t, old)
end
in_tmpdir "test_dir"

function _G.test_walk()
   assert(fs.removedirs "test")
   maketree(dir_table)
   local map = {}
   local inlist = {}
   for _, v in ipairs(collect_tree(dir_table)) do
      map[v] = true
   end
   map['test'..info.sep] = nil
   for p, state in fs.scandir "test" do
      if state == 'out' then
         assert(inlist[p])
         inlist[p] = nil
      else
         eq(map[p], true)
         map[p] = nil
         if state == 'in' then
            inlist[p] = true
         end
      end
   end
   eq(next(map), nil)
   eq(next(inlist), nil)
   collectgarbage "collect"
   -- for _, f in ipairs(fs.glob("*f[i]le*", "test")) do
   --    print(f)
   -- end
   -- print "====="
   -- for _, f in ipairs(fs.glob("*", "test")) do
   --    print(f)
   -- end
   local cnt = 0
   local gmap = {
      [path "test/test1/file1"] = true,
      [path "test/test1/file2"] = true,
      [path "test/test1/file3"] = true,
      [path "test/test2/file1"] = true,
      [path "test/test2/file2"] = true,
      [path "test/test2/file3"] = true,
      [path "test/test3/file1"] = true,
      [path "test/test3/file2"] = true,
      [path "test/test3/file3"] = true,
   }
   for f, s in fs.glob "test/**/*f[i]le*" do
      is_true(gmap[f]); gmap[f] = nil
      eq(s, "file")
      cnt = cnt + 1
   end
   eq(cnt, 9)
   assert(fs.removedirs "test")
   assert(not fs.exists "test")
end
in_tmpdir "test_walk"

function _G.test_attr()
   maketree(dir_table)
   assert(fs.isdir "test")
   assert(not fs.isdir "-not-exists-")
   assert(fs.isdir "test/test1")
   assert(fs.isdir("test", "test1"))
   assert(not fs.isdir("test", "test1", "file1"))
   assert(fs.isfile("test", "test1", "file1"))
   assert(not fs.isfile "-not-exists-")
   assert(not fs.ismount "test")
   assert(not fs.ismount "-not-exists-")
   assert(not fs.isfile "-not-exists-")
   assert(not fs.islink "test")
   assert(not fs.islink "-not-exists-")

   if info.platform == "windows" then
      fail(".-size:_not_exists_:%(errno%=2%).*",
         function() assert(fs.size "_not_exists_") end)
      fail(".-remove:_not_exists_:%(errno%=2%).*",
         function() assert(fs.remove "_not_exists_") end)
      fail(".-rename::%(errno%=2%).*",
         function() assert(fs.rename("_not_exists_", "")) end)
      fail(".-copy::%(errno%=2%).*",
         function() assert(fs.copy("_not_exists_", "")) end)
      fail(".-resolve:_not_exists_:%(errno%=%d+%).*",
         function() assert(path.resolve("_not_exists_")) end)
      assert(fs.ismount("//foo/bar"))
      assert(fs.ismount("//foo/bar/"))
      assert(fs.ismount("C:/"))
      assert(not fs.ismount("C:"))
      assert(not fs.ismount("//?/C:"))
      assert(fs.ismount("//?/C:/"))
      assert(fs.ismount("//./"))
   else
      fail(".-size:_not_exists_:%(errno%=%d+%).*",
         function() assert(fs.size "_not_exists_") end)
      fail(".-remove:_not_exists_:%(errno%=%d+%).*",
         function() assert(fs.remove "_not_exists_") end)
      fail(".-rename::%(errno%=%d+%).*",
         function() assert(fs.rename("_not_exists_", "")) end)
      fail(".-open:_not_exists_:%(errno%=%d+%).*",
         function() assert(fs.copy("_not_exists_", "")) end)
      fail(".-realpath:_not_exists_:%(errno%=%d+%).*",
         function() assert(path.resolve("_not_exists_")) end)
      fail(".-symlink:test:%(errno%=%d+%).*",
         function() assert(fs.symlink("_not_exists_", "test")) end)
   end
end
in_tmpdir "test_attr"

function _G.test_glob()
   local function check(path, r)
      local t = {}
      for fn in fs.glob(path) do
         t[#t+1] = fn
      end
      table.sort(t)
      table_eq(t, r)
   end
   local function check_count(path, r)
      local match = {}
      for fn, ty in fs.glob(path) do
         match[ty] = (match[ty] or 0) + 1
      end
      table_eq(match, r)
   end
   maketree(glob_tree)
   fail(".*Unacceptable pattern: ''.*", function()
      fs.glob()
   end)
   fail(".*Unacceptable pattern: ''.*", function()
      fs.glob ""
   end)
   check("*.txt", {"top.txt"})
   check("test_glob", {"test_glob"})
   if info.platform == 'windows' then
      check("test_glob/", {"test_glob\\"})
   else
      check("test_glob/", {"test_glob/"})
   end
   check("top.txt/", {})
   check_count("**", {["in"]=16, out=16})
   check_count("**/*.txt", {file=4})
   check_count("test_glob/*/a", {dir=2})
   check_count("test_glob/**/a/a/**/b", {file=2})
   check_count("test_glob/**/a/**/c/**/b", {file=1})
   check_count("test_glob/case_1/**/a/a/a/b", {})
   check_count("test_glob/case_2/**/a/a/a/b", {file=1})
   check_count("test_glob/**/a/b", {file=2})
   check_count("test_glob/**/a/a/**/a/b", {file=2})
   check_count("test_glob/case_1/**/a/", {["in"]=5, out=5})
   check_count("test_glob/case_1/**/a", {file=1, ["in"]=5, out=5})
   check_count("test_glob/*", {file=1, dir=2})

   -- print(("="):rep(78))
   -- for f, type in fs.glob "**/*" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
   -- for f, type in fs.glob "**/*.so" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
   -- for f, type in fs.glob "*.so.*/**/*.xx" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
   -- for f, type in fs.glob "**/Contents/**/*.so" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
   -- print("**/Contents/**/DWARF/*")
   -- for f, type in fs.glob "**/Contents/**/DWARF/*" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
   -- for f, type in fs.glob "*.so.*/Contents/**/DWARF/*" do
   --    print(">>", f, type)
   -- end
   -- print(("="):rep(78))
end
in_tmpdir "test_glob"

os.exit(unit.LuaUnit.run(), true)

