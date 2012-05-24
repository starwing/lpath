#define LUA_LIB
#ifdef __cplusplus
#  include <lua.hpp>
#else
#  include <lua.h>
#  include <lauxlib.h>
#endif


#define PB_BUFFERSIZE LUAL_BUFFERSIZE
#if LUA_VERSION_NUM >= 502
#  define PathBuffer      luaL_Buffer
#  define pb_addsize      luaL_addsize
#  define pb_buffinit     luaL_buffinit
#  define pb_prepbuffer   luaL_prepbuffer
#  define pb_prepbuffsize luaL_prepbuffsize
#  define pb_pushresult   luaL_pushresult
#else
#  define LUA_OK 0

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
#else
#  define CUR_PATH "."
#  define PAR_PATH ".."
#  define PATH_SEP '/'
#  define EXT_SEP  '.'
#endif

#define DIR_DATA "Dir Context"

enum Errno {
    LPATH_EOTHER = -1,
    LPATH_ENOENT =  2,
    LPATH_EEXIST = 17,
    LPATH_EOK    =  0
};

typedef enum Errno Errno;
typedef struct DirData DirData;
typedef Errno WalkFunc(lua_State *L, const char *s, int isdir);

/* path algorithms */

#include <string.h>

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

static int relpath_impl(lua_State *L, const char *fn, const char *path, int pathsep) {
    PathBuffer b;
    int count_dot2 = 0;
    char *wp, *whead;
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
    pb_buffinit(L, &b);
    whead = wp = pb_prepbuffsize(&b,
            (sizeof(PAR_PATH)-1)*count_dot2 + strlen(pf));
    while (count_dot2--) {
        strcpy(wp, PAR_PATH);
        wp += sizeof(PAR_PATH)-1;
        *wp++ = pathsep;
    }
    strcpy(wp, pf == fn ? pf : pf + 1);
    pb_addsize(&b, wp-whead);
    pb_pushresult(&b);
    return 1;
}

/* system specfied routines */

#ifdef _WIN32
#include <ctype.h>
#include <Windows.h>

#define PLAT "win32"

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

static int abspath_impl(lua_State *L, const char *s, size_t *ppos) {
    DWORD len;
    PathBuffer b;
    char *pbase, *ppath;
    pb_buffinit(L, &b);
    if ((len = GetFullPathNameA(s, PB_BUFFERSIZE,
                                (ppath = pb_prepbuffer(&b)), &pbase)) == 0)
        return push_lasterror(L);
    if (len > PB_BUFFERSIZE && GetFullPathNameA(s, PB_BUFFERSIZE,
            (ppath = pb_prepbuffsize(&b, len)),
            &pbase) != len) {
        pb_pushresult(&b);
        return push_lasterror(L);
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

static int dir_impl(lua_State *L, DirData *d, const char *s) {
    const char *pattern = lua_pushfstring(L, "%s\\*", s);
    if ((d->hFile = FindFirstFile(pattern, &d->wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    d->lasterror = NO_ERROR;
    lua_remove(L, -1);
    lua_pushcclosure(L, dir_helper, 1);
    return 1;
}

static int mkdir_impl(lua_State *L, const char *s, Errno* perrno) {
    if (!CreateDirectory(s, NULL)) {
        DWORD errnum = GetLastError();
        if (perrno)
            *perrno = errnum == ERROR_FILE_EXISTS    ? LPATH_EEXIST :
                      errnum == ERROR_FILE_NOT_FOUND ? LPATH_ENOENT : LPATH_EOTHER;
        return push_win32error(L, errnum);
    }
    return 0;
}

static int rmdir_impl(lua_State *L, const char *s) {
    if (!RemoveDirectory(s))
        return push_lasterror(L);
    return 0;
}

static int remove_impl(lua_State *L, const char *s) {
    if (!DeleteFile(s))
        return push_lasterror(L);
    return 0;
}

static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk) {
    WIN32_FIND_DATA wfd;
    HANDLE hFile;
    int err = LPATH_EOK;
    lua_pushfstring(L, "%s\\*", s);
    s = lua_tostring(L, -1);
    if ((hFile = FindFirstFile(s, &wfd)) == INVALID_HANDLE_VALUE) {
        return push_lasterror(L);
    }
    do {
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            if ((err = walk(L, wfd.cFileName, 0)) != LPATH_EOK)
                goto ret;
            continue;
        }
        if ((err = walk(L, wfd.cFileName, 1)) != LPATH_EOK)
            goto ret;
        if ((err = walkpath_impl(L, wfd.cFileName, walk)) != LPATH_EOK)
            goto ret;
    } while (FindNextFile(hFile, &wfd));
ret:
    FindClose(hFile);
    return err != LPATH_EOK ? 2 : 0;
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

#elif _POSIX
#include <unistd.h>

#define PLAT "posix"

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

static int dir_impl       (lua_State *L, DirData *d, const char *s) { return LNYI(L); }
static int rmdir_impl     (lua_State *L, const char *s) { return LNYI(L); }
static int remove_impl    (lua_State *L, const char *s) { return LNYI(L); }
static int mkdir_impl     (lua_State *L, const char *s, Errno *perrno) { return LNYI(L); }
static int abspath_impl   (lua_State *L, const char *s, size_t *plen) { return LNYI(L); }
static int walkpath_impl  (lua_State *L, const char *s, WalkFunc *walk) { return LNYI(L); }

#define Ldir_gc         LNYI
#define Lexists         LNYI
#define Lgetcwd         LNYI
#define Lchdir          LNYI
#define Lfiletime       LNYI
#define Lfilesize       LNYI
#define Lisdir          LNYI
#define Lcmptime        LNYI
#define Lnormpath       LNYI

#endif

/* common routines */

static int Labspath(lua_State *L) {
    const char *fname = luaL_checkstring(L, 1);
    return abspath_impl(L, fname, NULL);
}

static int Lrelpath(lua_State *L) {
    const char *fn = luaL_checkstring(L, 1);
    const char *path = luaL_checkstring(L, 2);
    normpath_impl(L, fn, PATH_SEP);
    normpath_impl(L, path, PATH_SEP);
    fn = lua_tostring(L, -2);
    path = lua_tostring(L, -1);
    return relpath_impl(L, fn, path, PATH_SEP);
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

static int Lsplitpath(lua_State *L) {
    const char *fname = luaL_checkstring(L, 1);
    size_t pos;
    int top = lua_gettop(L);
    abspath_impl(L, fname, &pos);
    if (lua_isnil(L, top + 1))
        return 2;
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
    return dir_impl(L, d, s);
}

static int Lmkdir(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    int recurse = lua_toboolean(L, 2);
    Errno errnum;
    if (recurse) {
        int has_error = 0;
        const char *p;
        normpath_impl(L, s, PATH_SEP);
        s = p = lua_tostring(L, -1);
        if (*p == PATH_SEP) ++p;
        while (*p != '\0') {
            while (*p != '\0' && *p != PATH_SEP) ++p;
            lua_pushlstring(L, s, p-s);
            if (mkdir_impl(L, lua_tostring(L, -1), &errnum) != 0)
                switch (errnum) {
                case LPATH_EEXIST:
                    lua_pop(L, 2);
                    continue;
                case LPATH_EOK: break;  /* nothing on stack */
                case LPATH_ENOENT: 
                case LPATH_EOTHER:
                    if (has_error)
                        lua_pop(L, 2);
                    has_error = 1;
                break;
                }
            if (!has_error) lua_settop(L, 1);
        }
        return has_error ? 2 : 1;
    }
    if (mkdir_impl(L, s, NULL) != 0)
        return 2;
    lua_settop(L, 1);
    return 1;
}

static Errno rmdir_rec_walk(lua_State *L, const char *s, int isdir) {
    int nret = isdir ? rmdir_impl(L, s)
                     : remove_impl(L, s);
    return nret == 2 ? LPATH_EOTHER : LPATH_EOK;
}

static int Lrmdir(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    int recurse = lua_toboolean(L, 2);
    if (!recurse) {
        if (rmdir_impl(L, s) != 0)
            return 2;
    }
    else if (walkpath_impl(L, s, rmdir_rec_walk) != 0)
        return 2;
    lua_settop(L, 1);
    return 1;
}

static Errno walkpath_cb(lua_State *L, const char *s, int isdir) {
    lua_pushvalue(L, 2);
    lua_pushstring(L, s);
    lua_pushstring(L, isdir ? "dir" : "file");
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return LPATH_EOTHER;
    }
    return LPATH_EOK;
}

static int Lwalkpath(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (walkpath_impl(L, s, walkpath_cb) != 0)
        return 2;
    lua_settop(L, 1);
    return 1;
}

/* register functions */

static luaL_Reg libs[] = {
#define ENTRY(n) { #n, L##n }
    ENTRY(exists),
    ENTRY(getcwd),
    ENTRY(chdir),
    ENTRY(mkdir),
    ENTRY(rmdir),
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
    ENTRY(walkpath),
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
    lua_pushliteral(L, PLAT);
    lua_setfield(L, -2, "platform");
    return 1;
}

/*
 * cc: flags+='-pedantic -mdll -DLUA_BUILD_AS_DLL' libs+='d:/lua52/lua52.dll'
 * cc: output='path.dll'
 */
