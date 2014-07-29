#define LUA_LIB
#ifdef __cplusplus
#  include <lua.hpp>
#else
#  include <lua.h>
#  include <lauxlib.h>
#endif

#define ALT_SEP '/'

#ifdef _WIN32
#  define CUR_PATH "."
#  define PAR_PATH ".."
#  define PATH_SEP '\\'
#  define EXT_SEP  '.'
#else
#  define CUR_PATH "."
#  define PAR_PATH ".."
#  define PATH_SEP '/'
#  define EXT_SEP  '.'
#endif

#define DIR_DATA "Dir Context"

typedef struct DirData DirData;
typedef struct WalkData WalkData;
typedef int WalkFunc(lua_State *L, const char *s, int isdir);

/* path algorithms */

#include <assert.h>
#include <string.h>

#define COMP_MAX 50

#define ispathsep(ch)   ((ch) == PATH_SEP || (ch) == ALT_SEP)
#define iseos(ch)       ((ch) == '\0')
#define ispathend(ch)   (ispathsep(ch) || iseos(ch))

size_t normpath(char *out, const char *in) {
    char *pos[COMP_MAX], **top = pos, *head = out;
    int isabs = ispathsep(*in);

    if (isabs) *out++ = '/';
    *top++ = out;

    while (!iseos(*in)) {
        while (ispathsep(*in)) ++in;

        if (iseos(*in))
            break;

        if (memcmp(in, ".", 1) == 0 && ispathend(in[1])) {
            ++in;
            continue;
        }

        if (memcmp(in, "..", 2) == 0 && ispathend(in[2])) {
            in += 2;
            if (top != pos + 1)
                out = *--top;
            else if (isabs)
                out = top[-1];
            else {
                strcpy(out, "../");
                out += 3;
            }
            continue;
        }

        if (top - pos >= COMP_MAX)
            return 0; /* path too complicate */

        *top++ = out;
        while (!ispathend(*in))
            *out++ = *in++;
        if (ispathsep(*in))
            *out++ = '/';
    }

    *out = '\0';
    if (*head == '\0')
        strcpy(head, "./");
    return out - head;
}

static int normpath_impl(lua_State *L, const char *s, size_t len) {
    char *buff;
    if (len < LUAL_BUFFERSIZE) {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        buff = luaL_prepbuffer(&b);
        luaL_addsize(&b, normpath(buff, s));
        luaL_pushresult(&b);
    }
    else {
        buff = (char*)lua_newuserdata(L, len+1);
        lua_pushlstring(L, buff, normpath(buff, s));
    }
    return 1;
}

static int relpath_impl(lua_State *L, const char *fn, const char *path, int pathsep) {
    luaL_Buffer b;
    int count_dot2 = 0;
    const char *pf = fn, *pp = path;
    while (*pf != '\0' && *pp != '\0' && *pf == *pp)
        ++pf, ++pp;
    if (*pf == '\0' && *pp == '\0') {
        lua_pushfstring(L, CUR_PATH "%c", pathsep);
        return 1;
    }
    if (pf != fn && *pf != pathsep) {
        while (fn < pf && *pf != pathsep)
            --pf, --pp;
    }
    while (*pp != '\0')
        if (*++pp == pathsep)
            ++count_dot2;
    if (path < pp && pp[-1] == pathsep)
        --count_dot2;
    luaL_buffinit(L, &b);
    while (count_dot2--) {
        luaL_addlstring(&b, PAR_PATH, sizeof(PAR_PATH) - 1);
        luaL_addchar(&b, pathsep);
    }
    luaL_addstring(&b, pf == fn ? pf : pf + 1);
    luaL_pushresult(&b);
    return 1;
}

static int Ljoin(lua_State *L) {
    luaL_Buffer b;
    int i, top = lua_gettop(L);
    luaL_buffinit(L, &b);
    for (i = 1; i <= top; ++i) {
        size_t len;
        const char *s = luaL_checklstring(L, i, &len);
        if (i != 1)
            luaL_addchar(&b, PATH_SEP);
        while (s[len - 1] == PATH_SEP)
            --len;
        luaL_addlstring(&b, s, len);
    }
    luaL_pushresult(&b);
    return 1;
}

static const char *get_single_pathname(lua_State *L, size_t *psz) {
    int top = lua_gettop(L);
    if (top == 0) {
        if (psz) *psz = 1;
        return ".";
    }
    if (top > 1) {
        Ljoin(L);
        lua_replace(L, 1);
        lua_settop(L, 1);
    }
    return luaL_checklstring(L, 1, psz);
}

/* system specfied routines */

#define first_arg(L) (lua_settop((L), 1), 1)

#ifdef _WIN32
#  include "lpath_win32.c"
#elif _POSIX_SOURCE
#  include "lpath_posix.c"
#else

#define PLAT "unknown"

struct DirData {
    int dummy;
};

static int LNYI(lua_State *L) {
    lua_pushnil(L);
    lua_pushstring(L, "not implement yet");
    return 2;
}

#define NYI_impl(n,arg) static int n##_impl arg { return LNYI(L); }
NYI_impl(dir,      (lua_State *L, DirData *d, const char *s))
NYI_impl(chdir,    (lua_State *L, const char *s))
NYI_impl(mkdir,    (lua_State *L, const char *s))
NYI_impl(rmdir,    (lua_State *L, const char *s))
NYI_impl(remove,   (lua_State *L, const char *s))
NYI_impl(abspath,  (lua_State *L, const char *s, size_t *plen))
NYI_impl(walkpath, (lua_State *L, const char *s, WalkFunc *walk))
#undef NYI_impl

#define Ldir_gc         LNYI
#define Lexists         LNYI
#define Lgetcwd         LNYI
#define Lsetenv         LNYI
#define Ltouch          LNYI
#define Lfiletime       LNYI
#define Lfilesize       LNYI
#define Lisdir          LNYI
#define Lcmptime        LNYI
#define Lnormpath       LNYI

#endif

/* common routines */

static int Labs(lua_State *L) {
    return abspath_impl(L, get_single_pathname(L, NULL), NULL);
}

static int Lrel(lua_State *L) {
    size_t lf, lp;
    const char *fn = luaL_checklstring(L, 1, &lf);
    const char *path = luaL_checklstring(L, 2, &lp);
    normpath_impl(L, fn, lf);
    normpath_impl(L, path, lp);
    fn = lua_tostring(L, -2);
    path = lua_tostring(L, -1);
    return relpath_impl(L, fn, path, PATH_SEP);
}

static int Lsplitext(lua_State *L) {
    size_t len;
    const char *fname = luaL_checklstring(L, 1, &len);
    const char *last = &fname[len];
    while (fname < last && *--last != EXT_SEP)
        ;
    if (fname < last) {
        lua_pushlstring(L, fname, last - fname);
        lua_pushstring(L, last);
    }
    else {
        lua_pushvalue(L, 1);
        lua_pushliteral(L, "");
    }
    return 2;
}

static int Lsplit(lua_State *L) {
    const char *fname = get_single_pathname(L, NULL);
    size_t pos = 0;
    int top = lua_gettop(L);
    abspath_impl(L, fname, &pos);
    if (lua_isnil(L, top + 1))
        return 2;
    lua_pushlstring(L, lua_tostring(L, -1), pos);
    lua_pushstring(L, lua_tostring(L, -2) + pos);
    return 2;
}

static int unary_func(lua_State *L, int (*f)(lua_State *, const char *)) {
    if (f(L, get_single_pathname(L, NULL)) != 0)
        return 2;
    return first_arg(L);
}

static int Lchdir(lua_State *L) { return unary_func(L, chdir_impl); }
static int Lmkdir(lua_State *L) { return unary_func(L, mkdir_impl); }
static int Lrmdir(lua_State *L) { return unary_func(L, rmdir_impl); }

static int Lmkdir_rec(lua_State *L) {
    size_t len;
    const char *p, *s = get_single_pathname(L, &len);
    int top = lua_gettop(L);
    if (Lgetcwd(L) != 1) return 2;
    if (normpath_impl(L, s, len) != 1) return 2;
    lua_replace(L, 1);
    s = p = lua_tostring(L, 1);
    if (*p == PATH_SEP) ++p;
    while (*p != '\0') {
        while (*p != '\0' && *p++ != PATH_SEP);
        lua_pushlstring(L, s, p - s);
        s = lua_tostring(L, -1);
        if (mkdir_impl(L, s) != 0) {
            lua_remove(L, -3);
            break;
        }
        lua_settop(L, top + 1);
        if (chdir_impl(L, s) != 0) break;
        s = p;
    }
    if (chdir_impl(L, lua_tostring(L, top + 1)) != 0) return 2;
    return lua_gettop(L) - top - 1;
}

static int rmdir_rec_walk(lua_State *L, const char *s, int isdir) {
    return isdir ? rmdir_impl(L, s) : remove_impl(L, s);
}

static int Lrmdir_rec(lua_State *L) {
    const char *s = get_single_pathname(L, NULL);
    int res;
    if (Lgetcwd(L) != 1) return 2;
    res = walkpath_impl(L, s, rmdir_rec_walk);
    if (chdir_impl(L, lua_tostring(L, -res-1)) != 0 || res != 0)
        return 2;
    return first_arg(L);
}

static DirData *dirdata_new(lua_State *L) {
    DirData *d = (DirData*)lua_newuserdata(L, sizeof(DirData));
    if (luaL_newmetatable(L, DIR_DATA)) {
        lua_pushcfunction(L, Ldir_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return d;
}

static int Ldir(lua_State *L) {
    const char *s = get_single_pathname(L, NULL);
    return dir_impl(L, dirdata_new(L), s);
}

static int iterpath_iter(lua_State *L) {
    const char *s = lua_tostring(L, 1);
    int p = lua_tointeger(L, lua_upvalueindex(1));
    assert(s != NULL);
    if (p == 0 && s[0] == PATH_SEP) {
        char root[] = { PATH_SEP, '\0' };
        lua_pushinteger(L, p + 1);
        lua_replace(L, lua_upvalueindex(1));
        lua_pushstring(L, root);
    }
    else {
        int pend = p;
        while (s[pend] != '\0' && s[pend] != PATH_SEP)
            ++pend;
        if (pend == p) return 0;
        lua_pushinteger(L, pend + 1);
        lua_replace(L, lua_upvalueindex(1));
        lua_pushlstring(L, &s[p], pend - p);
    }
    return 1;
}

static int Literpath(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, iterpath_iter, 1);
    normpath_impl(L, s, len);
    return 2;
}

static int walkpath_iter(lua_State *L) {
    const char *path;
    int nrets, stacktop = lua_rawlen(L, 1);
redo:
    lua_settop(L, 1);
    if (stacktop == 0) return 0;
    lua_rawgeti(L, 1, stacktop-1); /* 2 */
    lua_rawgeti(L, 1, stacktop); /* 3 */
    lua_call(L, 1, LUA_MULTRET); /* 2,3->? */
    if ((nrets = lua_gettop(L) - 1) == 0) {
        lua_settop(L, 6); /* 4 */
        lua_rawseti(L, 1, stacktop-2); /* 4->1 */
        lua_rawseti(L, 1, stacktop-1); /* 3->1 */
        lua_rawseti(L, 1, stacktop); /* 2->1 */
        stacktop -= 3;
        goto redo; /* tail return walkpath_iter(L); */
    }
    path = lua_tostring(L, 2);
    if (!strcmp(path, CUR_PATH) || !strcmp(path, PAR_PATH))
        goto redo; /* tail return walkpath_iter(L); */
    lua_rawgeti(L, 1, stacktop-2);
    lua_pushvalue(L, 2);
    lua_concat(L, 2);
    lua_replace(L, 2);
    /* the second return value is "file" or "dir" */
    if (*lua_tostring(L, 3) == 'd') {
        lua_pushfstring(L, "%s%c", lua_tostring(L, 2), PATH_SEP);
        path = lua_tostring(L, -1);
        lua_rawseti(L, 1, stacktop+1);
        lua_pushnil(L); lua_rawseti(L, 1, stacktop+2); /* place holder, avoid hash part */
        dir_impl(L, dirdata_new(L), path);
        lua_rawseti(L, 1, stacktop+3);
        lua_rawseti(L, 1, stacktop+2);
    }
    return nrets;
}

static int Lwalk(lua_State *L) {
    const char *s = get_single_pathname(L, NULL);
    lua_pushcfunction(L, walkpath_iter);
    lua_createtable(L, 3, 0);
    lua_pushfstring(L, "%s%c", s, PATH_SEP);
    dir_impl(L, dirdata_new(L), s);
    lua_rawseti(L, -4, 3);
    lua_rawseti(L, -3, 2);
    lua_rawseti(L, -2, 1);
    return 2;
}

/* register functions */

static luaL_Reg libs[] = {
#define ENTRY(n) { #n, L##n }
    ENTRY(dir),       ENTRY(filesize),
    ENTRY(isdir),     ENTRY(cmptime),
    ENTRY(chdir),     ENTRY(touch),
    ENTRY(mkdir),     ENTRY(abs),
    ENTRY(rmdir),     ENTRY(rel),
    ENTRY(mkdir_rec), ENTRY(normpath),
    ENTRY(rmdir_rec), ENTRY(join),
    ENTRY(exists),    ENTRY(split),
    ENTRY(getcwd),    ENTRY(splitext),
    ENTRY(setenv),    ENTRY(iterpath),
    ENTRY(filetime),  ENTRY(walk),
#undef  ENTRY
    { NULL, NULL }
};

LUALIB_API int luaopen_path(lua_State *L) {
#if LUA_VERSION_NUM >= 502
    luaL_newlib(L, libs);
#else
    luaL_register(L, lua_tostring(L, 1), libs);
#endif
    lua_pushliteral(L, PLAT);
    lua_setfield(L, -2, "platform");
    return 1;
}

/*
 * linuxcc: flags+='-O2 -shared'
 * linuxcc: output='path.so' run='lua test.lua'
 * cc: lua='lua52' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-ggdb -pedantic -mdll -DLUA_BUILD_AS_DLL -Id:/$lua/include'
 * cc: output='path.dll' run='lua test.lua'
 */
