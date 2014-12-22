local P = require "path"
local fs = require "path.fs"
local info = require "path.info"

local keys = {}
for k,v in pairs(info) do
   keys[#keys+1] = k
end
table.sort(keys)
for _, k in ipairs(keys) do
   print(k, info[k])
end

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
end)
add_test("itercomp", function ()
end)
add_test("join", function ()
end)
add_test("normcase", function ()
end)
add_test("normpath", function ()
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
add_test("dir", function ()
    local cwd = assert(fs.getcwd())
    assert(fs.chdir "test/..")
    local function _check(d)
        assert(fs.chdir(assert(d.name)))
        local idx = 1
        for fn, ft in fs.dir '.' do
            if fn == '.' or fn == '..' then goto next end
            if ft == 'dir' or (ft == nil and fs.type(fn) == "dir") then
                assert(d[idx].name == fn, "idx = "..idx..", name = "..tostring(d[idx].name)..", fn = "..fn)
                _check(d[idx])
            else
                assert(d[idx] == fn, "idx = "..idx..", name = "..tostring(d[idx])..", fn = "..fn)
            end
            idx = idx + 1
            ::next::
        end
        assert(fs.chdir "..")
    end
    _check(test_dir)
    assert(fs.chdir(cwd))
end)
add_test("exists", function ()
end)
add_test("fsize", function ()
end)
add_test("ftime", function ()
end)
add_test("getcwd", function ()
end)
add_test("mkdir", function ()
    fs.removedirs "test"
    assert(fs.mkdir "test")
    assert(fs.exists "test")
    assert(fs.type "test" == "dir")
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
add_test("platform", function ()
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
add_test("remove", function ()
end)
add_test("removedirs", function ()
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
assert(fs.removedirs "test")
