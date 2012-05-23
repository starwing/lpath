#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>


#include <assert.h>
#include <ctype.h>
#include <string.h>


#define PB_BUFFERSIZE LUAL_BUFFERSIZE
#if LUA_VERSION_NUM >= 502
#  define PathBuffer      luaL_Buffer
#  define pb_addsize      luaL_addsize
#  define pb_buffinit     luaL_buffinit
#  define pb_prepbuffer   luaL_prepbuffer
#  define pb_prepbuffsize luaL_prepbuffsize
#  define pb_pushresult   luaL_pushresult
#else
#include <stdlib.h>

typedef struct PathBuffer {
    size_t len;
    char *p;
    char buf[PB_BUFFERSIZE];
    lua_State *L;
} PathBuffer;

static void pb_buffinit(lua_State *L, PathBuffer *b) {
    b->p = b->buf;
    b->L = L;
    b->len = 0;
}

static char *pb_prepbuffer(PathBuffer *b) {
    return b->p;
}

static void pb_addsize(PathBuffer *b, size_t len) {
    b->len += len;
}

static void pb_pushresult(PathBuffer *b) {
    lua_pushlstring(b->L, b->p, b->len);
    if (b->p != b->buf) free(b->p);
}

static char *pb_prepbuffsize(PathBuffer *b, size_t len) {
    if (b->p != b->buf) free(b->p);
    if ((b->p = malloc(len)) == NULL)
        luaL_error(b->L, "no enough memory");
    return b->p;
}
#endif


#define ALT_SEP '/'

#ifdef _WIN32
#  define CUR_PATH "."
#  define PAR_PATH ".."
#  define PATH_SEP '\\'
#  define EXT_SEP  '.'
#endif

#define DIR_DATA "Dir Context"

typedef struct DirData DirData;


static size_t trimpath(char *s, int *isabs, int pathsep, int altsep) {
    char *wp = s, *rp = s;
    for (; *rp != '\0'; ++rp) {
        if (*rp == altsep || *rp == pathsep) {
            while (rp[1] == altsep || rp[1] == pathsep) ++rp;
            if (rp[1] == '.'
                    && (rp[2] == altsep || rp[2] == pathsep || rp[2] == '\0'))
                ++rp;
            else *wp++ = pathsep;
        }
        else *wp++ = *rp;
    }
    if (s < wp && wp[-1] == '.' && wp[-2] == '.' &&
            (wp - 2 == s || wp[-3] == pathsep))
        *wp++ = pathsep;
    *wp = '\0';
    if (isabs != NULL) *isabs = *s == pathsep;
    return wp - s;
}

static size_t normpath_inplace(char *s, int pathsep) {
    int isabs;
    char *wp = s, *rp = s;
    trimpath(s, &isabs, pathsep, ALT_SEP);
    for (; *rp != '\0'; ++rp) {
        /*while (*rp != '\0' && *rp != pathsep) *wp++ = *rp++;*/
        /*if (*rp == '\0') break;*/
        if (rp[0] == pathsep && rp[1] == '.' && rp[2] == '.'
                && (rp[3] == pathsep || rp[3] == '\0')) {
            char *lastwp = wp;
            while (s < lastwp && *--lastwp != pathsep)
                ;
            if (lastwp != wp && (wp[-1] != '.' || wp[-2] != '.' ||
                                 (s < wp - 3 && wp[-3] != pathsep))) {
                wp = lastwp;
                rp += 2;
                continue;
            }
            else if (lastwp == s && isabs) {
                rp += 2;
                continue;
            }
        }
        if (rp[0] != pathsep || wp != s || isabs)
            *wp++ = *rp;
    }
    if (wp == s)
        *wp++ = isabs ? pathsep : '.';
    if (wp == s + 1 && s[0] == '.')
        *wp++ = pathsep;
    *wp = '\0';
    return wp - s;
}

static int normpath_impl(lua_State *L, const char *s, int pathsep) {
    char *buff;
    PathBuffer b;
    pb_buffinit(L, &b);
    buff = pb_prepbuffsize(&b, strlen(s) + 1);
    strcpy(buff, s);
    pb_addsize(&b, normpath_inplace(buff, pathsep));
    pb_pushresult(&b);
    return 1;
}

#if 0
static int splitpath_tostack(lua_State *L, const char *s, int pathsep) {
    int t = lua_gettop(L);
    const char *lasts = s;
    for (; *s != '\0'; ++s) {
        if ((*s == ALT_SEP || *s == pathsep) && s != lasts) {
            luaL_checkstack(L, 1, "no more room for path");
            lua_pushlstring(L, lasts, s - lasts);
            lasts = s + 1;
        }
    }
    luaL_checkstack(L, 1, "no more room for path");
    lua_pushlstring(L, lasts, s - lasts);
    return lua_gettop(L) - t;
}

static int normpath_impl(lua_State *L, const char *s, int pathsep) {
    int first = lua_gettop(L) + 1;
    int last = first + splitpath_tostack(L, s, pathsep) - 1;
    int i = first;
    int isabs = (*s == ALT_SEP || *s == pathsep);
    while (i <= last) {
        if (!strcmp(lua_tostring(L, i), CUR_PATH)) {
            lua_remove(L, i);
            --last;
        }
        else if (!strcmp(lua_tostring(L, i), PAR_PATH)) {
            if (i > first && strcmp(lua_tostring(L, i - 1), PAR_PATH)) {
                lua_remove(L, --i);
                lua_remove(L, i);
                last -= 2;
            }
            else if (i == first && isabs) {
                lua_remove(L, i);
                --last;
            }
            else ++i;
        }
        else ++i;
    }
    if (!isabs && first > last)
        lua_pushstring(L, CUR_PATH);
    else {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        if (isabs)
            luaL_addchar(&b, pathsep);
        for (i = first; i <= last; ++i) {
            if (i != first)
                luaL_addchar(&b, pathsep);
            lua_pushvalue(L, i);
            luaL_addvalue(&b);
        }
        luaL_pushresult(&b);
        lua_insert(L, first);
        lua_settop(L, first);
    }
    return 1;
}
#endif


#ifdef _WIN32
#include <Windows.h>

struct DirData {
    HANDLE hFile;
    WIN32_FIND_DATA wfd;
    DWORD lasterror;
};

static int push_win32error(lua_State *L, DWORD errnum) {
    LPSTR msg;
    lua_pushnil(L);
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        errnum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg,
        0, NULL);
    lua_pushstring(L, msg);
    LocalFree(msg);
    return 2;
}

static int push_lasterror(lua_State *L) {
    return push_win32error(L, GetLastError());
}

static int push_abspath(lua_State *L, const char *fname, size_t *ppos) {
    DWORD len;
    PathBuffer b;
    char *pbase, *ppath;
    pb_buffinit(L, &b);
    if ((len = GetFullPathNameA(fname, PB_BUFFERSIZE,
                                (ppath = pb_prepbuffer(&b)), &pbase)) == 0)
        return push_lasterror(L);
    if (len > PB_BUFFERSIZE && GetFullPathNameA(fname, PB_BUFFERSIZE,
            (ppath = pb_prepbuffsize(&b, len)),
            &pbase) != len) {
        pb_pushresult(&b);
        return 0;
    }
    if (ppos) *ppos = pbase - ppath;
    pb_addsize(&b, len);
    pb_pushresult(&b);
    return 1;
}

static void push_word64(lua_State *L, DWORD low, DWORD high) {
    lua_Number time = high;
    time = time*~(DWORD)0 + time + low;
    lua_pushnumber(L, time);
}

static void push_filetime(lua_State *L, PFILETIME pft) {
    push_word64(L, pft->dwLowDateTime, pft->dwHighDateTime);
}

static void dir_clsoe(DirData *d) {
    FindClose(d->hFile);
    d->hFile = INVALID_HANDLE_VALUE;
    d->lasterror = ERROR_NO_MORE_FILES;
}

static int Ldir_gc(lua_State *L) {
    DirData *d = (DirData*)lua_touserdata(L, 1);
    if (d->lasterror == NO_ERROR)
        dir_clsoe(d);
    return 0;
}

static int dir_helper(lua_State *L) {
    DirData *d = lua_touserdata(L, lua_upvalueindex(1));
    if (d->lasterror != NO_ERROR)
        return push_win32error(L, d->lasterror);
    lua_pushstring(L, d->wfd.cFileName);
    push_word64(L, d->wfd.nFileSizeLow, d->wfd.nFileSizeHigh);
    push_filetime(L, &d->wfd.ftCreationTime);
    push_filetime(L, &d->wfd.ftLastAccessTime);
    push_filetime(L, &d->wfd.ftLastWriteTime);
    if (!FindNextFile(d->hFile, &d->wfd)) {
        d->lasterror = GetLastError();
        dir_clsoe(d);
    }
    return 1;
}

static int dir_open(lua_State *L, DirData *d, const char *s) {
    const char *pattern = lua_pushfstring(L, "%s\\*", s);
    if ((d->hFile = FindFirstFile(pattern, &d->wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    d->lasterror = NO_ERROR;
    lua_remove(L, -1);
    lua_pushcclosure(L, dir_helper, 1);
    return 1;
}

static int Lchdir(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    if (!SetCurrentDirectoryA(s))
        return push_lasterror(L);
    lua_settop(L, 1);
    return 1;
}

static int Lgetcwd(lua_State *L) {
    DWORD len;
    PathBuffer b;
    pb_buffinit(L, &b);
    if ((len = GetCurrentDirectoryA(PB_BUFFERSIZE, pb_prepbuffer(&b))) == 0)
        return push_lasterror(L);
    if (len > PB_BUFFERSIZE && GetCurrentDirectoryA(len,
            pb_prepbuffsize(&b, len)) != len) {
        pb_pushresult(&b);
        return push_lasterror(L);
    }
    pb_addsize(&b, len);
    pb_pushresult(&b);
    return 1;
}

static int Lisdir(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    DWORD attrs = GetFileAttributes(s);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return push_lasterror(L);
    lua_pushboolean(L, attrs & FILE_ATTRIBUTE_DIRECTORY);
    return 1;
}

static int Lexists(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = luaL_checkstring(L, 1);
    HANDLE hFile;
    if ((hFile = FindFirstFile(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    lua_pushstring(L, wfd.cFileName);
    return 1;
}

static int Lfiletime(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = luaL_checkstring(L, 1);
    HANDLE hFile;
    if ((hFile = FindFirstFile(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    push_filetime(L, &wfd.ftCreationTime);
    push_filetime(L, &wfd.ftLastAccessTime);
    push_filetime(L, &wfd.ftLastWriteTime);
    return 3;
}

static int Lfilesize(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = luaL_checkstring(L, 1);
    HANDLE hFile;
    if ((hFile = FindFirstFile(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    push_word64(L, wfd.nFileSizeLow, wfd.nFileSizeHigh);
    return 1;
}

static int Lcmptime(lua_State *L) {
    WIN32_FIND_DATA wfd1, wfd2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 1);
    HANDLE hFile1, hFile2;
    if ((hFile1 = FindFirstFile(f1, &wfd1)) == INVALID_HANDLE_VALUE ||
            (hFile2 = FindFirstFile(f2, &wfd2)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    lua_pushinteger(L, CompareFileTime(&wfd1.ftCreationTime,
                                       &wfd2.ftCreationTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastAccessTime,
                                       &wfd2.ftLastAccessTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastWriteTime,
                                       &wfd2.ftLastWriteTime));
    return 3;
}

static int Lnormpath(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    while (isspace(*s)) ++s;
    if (!strncmp(s, "\\\\.\\", 4) || !strncmp(s, "\\\\?\\", 4)) {
        lua_settop(L, 1);
        return 1;
    }
    if (!isalpha(*s) || s[1] != ':') {
        int backslash = 0;
        while (s[backslash] == '\\') ++backslash;
        if (backslash == 0)
            return normpath_impl(L, s, '\\');
        lua_pushlstring(L, s, backslash - 1);
        normpath_impl(L, &s[backslash - 1], '\\');
        lua_concat(L, 2);
        return 1;
    }
    lua_pushfstring(L, "%c:", toupper(*s));
    normpath_impl(L, s + 2, '\\');
    lua_concat(L, 2);
    return 1;
}
#endif


static int Labspath(lua_State *L) {
    const char *fname = luaL_checkstring(L, 1);
    if (!push_abspath(L, fname, NULL))
        return push_lasterror(L);
    return 1;
}

static int Lrelpath(lua_State *L) {
    size_t pathlen;
    const char *fn = luaL_checkstring(L, 1);
    const char *path = luaL_checkstring(L, 2);
    if (!push_abspath(L, fn, NULL) || !push_abspath(L, path, NULL))
        return push_lasterror(L);
    if (strncmp(fn, path, (pathlen = strlen(path))) == 0)
        lua_pushstring(L, fn + pathlen);
    else
        lua_pushstring(L, fn);
    return 1;
}

static int Ljoinpath(lua_State *L) {
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

static int iterpath_helper(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    int p = lua_tointeger(L, lua_upvalueindex(1));
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
    lua_pushcclosure(L, iterpath_helper, 1);
    normpath_impl(L, s, PATH_SEP);
    return 2;
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

#if 0
static int Lrelpath(lua_State *L) {
    int i, top_fn, top_path, len_fn, len_path;
    const char *fn = luaL_checkstring(L, 1);
    const char *path = luaL_checkstring(L, 2);
    if (!push_abspath(L, fn, NULL) || !push_abspath(L, path, NULL))
        return push_lasterror(L);
    fn = lua_tostring(L, -2);
    path = lua_tostring(L, -1);
    top_fn = lua_gettop(L)+1; len_fn = splitpath_tostack(L, fn, PATH_SEP);
    top_path = lua_gettop(L)+1; len_path = splitpath_tostack(L, path, PATH_SEP);
    for (i = 0; i < len_fn && i < len_path; ++i) {
        if (stricmp(lua_tostring(L, top_fn+i), lua_tostring(L, top_path+i)) != 0)
            break;
    }
    if (i == len_path && i == len_fn) {
        lua_settop(L, top_fn-1);
        lua_pushstring(L, fn);
    }
    else {
        int count = i;
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        for (i = 0; i < len_path - count; ++i)
            luaL_addstring(&b, PAR_PATH);
        for (i = count; i < len_fn; ++i) {
            lua_pushvalue(L, top_fn + i);
            luaL_addvalue(&b);
        }
        luaL_pushresult(&b);
        lua_insert(L, top_fn);
        lua_settop(L, top_fn);
    }
    return 1;
}
#endif

static int Lsplitpath(lua_State *L) {
    const char *fname = luaL_checkstring(L, 1);
    size_t pos;
    if (!push_abspath(L, fname, &pos))
        return push_lasterror(L);
    lua_pushlstring(L, lua_tostring(L, -1), pos);
    lua_pushstring(L, lua_tostring(L, -2) + pos);
    return 2;
}

static int Ldir(lua_State *L) {
    const char *s = luaL_optstring(L, 1, ".");
    DirData *d;
    d = (DirData*)lua_newuserdata(L, sizeof(DirData));
    if (luaL_newmetatable(L, DIR_DATA)) {
        lua_pushcfunction(L, Ldir_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return dir_open(L, d, s);
}

static luaL_Reg libs[] = {
#define ENTRY(n) { #n, L##n }
    ENTRY(exists),
    ENTRY(getcwd),
    ENTRY(chdir),
 /* ENTRY(mkdir), */
 /* ENTRY(rmdir), */
    ENTRY(filetime),
    ENTRY(filesize),
    ENTRY(isdir),
    ENTRY(cmptime),
    ENTRY(normpath),
    ENTRY(joinpath),
    ENTRY(abspath),
    ENTRY(relpath),
    ENTRY(splitpath),
    ENTRY(splitext),
    ENTRY(iterpath),
    ENTRY(dir),
#undef  ENTRY
    { NULL, NULL }
};

LUALIB_API int luaopen_path(lua_State *L) {
#if LUA_VERSION_NUM >= 502
    luaL_newlib(L, libs);
#else
    luaL_register(L, lua_tostring(L, 1), libs);
#endif
#ifdef _WIN32
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "win32");
#endif
    return 1;
}

/*
 * cc: flags+='-mdll -DLUA_BUILD_AS_DLL' libs+='d:/lua52/lua52.dll'
 * cc: output='path.dll'
 */
