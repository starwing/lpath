local P = require 'path'

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
        assert(P[name], "required "..name.." in lpath")
        f()
        print("[ OK ] "..name)
    end
end

add_test("mkdir",     function ()
    P.rmdir_rec "test"
    assert(P.mkdir "test")
    assert(P.exists "test")
    assert(P.isdir "test")
end)

add_test("mkdir_rec", function ()
    local function _dfs(d)
        assert(P.mkdir(assert(d.name)))
        assert(P.chdir(assert(d.name)))
        for k, v in ipairs(d) do
            if type(v) == 'string' then
                assert(P.touch(v))
            else
                _dfs(v)
            end
        end
        assert(P.chdir "..")
    end
    local cwd = assert(P.getcwd())
    assert(P.chdir "test/..")
    _dfs(test_dir)
    assert(P.chdir(cwd))
end)

add_test("dir",       function ()
    local cwd = assert(P.getcwd())
    assert(P.chdir "test/..")
    local function _check(d)
        assert(P.chdir(assert(d.name)))
        local idx = 1
        for fn, ft in P.dir '.' do
            if fn == '.' or fn == '..' then goto next end
            if ft == 'dir' or (ft == nil and P.isdir(fn)) then
                assert(d[idx].name == fn, "idx = "..idx..", name = "..tostring(d[idx].name)..", fn = "..fn)
                _check(d[idx])
            else
                assert(d[idx] == fn, "idx = "..idx..", name = "..tostring(d[idx])..", fn = "..fn)
            end
            idx = idx + 1
            ::next::
        end
        assert(P.chdir "..")
    end
    _check(test_dir)
    assert(P.chdir(cwd))
end)

add_test("isdir",     function ()
end)
add_test("chdir",     function ()
end)
add_test("rmdir",     function ()
end)
add_test("rmdir_rec", function ()
end)
add_test("exists",    function ()
end)
add_test("getcwd",    function ()
end)
add_test("filetime",  function ()
end)
add_test("filesize",  function ()
end)
add_test("cmptime",   function ()
end)
add_test("touch",     function ()
end)
add_test("abspath",   function ()
end)
add_test("relpath",   function ()
end)
add_test("normpath",  function ()
end)
add_test("joinpath",  function ()
end)
add_test("splitpath", function ()
end)
add_test("splitext",  function ()
end)
add_test("iterpath",  function ()
end)
add_test("walkpath",  function ()
end)

for _,v in ipairs(tests) do v() end
