#ifdef __cplusplus
# define LP_NS_BEGIN extern "C" {
# define LP_NS_END   }
#else
# define LP_NS_BEGIN
# define LP_NS_END
#endif

LP_NS_BEGIN


#define LUA_LIB
#include <lua.h>
#include <time.h>
#include <lauxlib.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#if LUA_VERSION_NUM >= 503
# define lua53_rawgetp lua_rawgetp
#elif LUA_VERSION_NUM == 502
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_rawgetp(L, idx, p); return lua_type(L, -1); }
#else
# define lua_rawlen          lua_objlen
# define luaL_newlib(L,libs) luaL_register(L, lua_tostring(L, 1), libs);
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_pushlightuserdata(L, (void*)p); lua_rawget(L, idx); return lua_type(L, -1); }
static int lua_rawsetp(lua_State *L, int idx, const void *p)
{ lua_pushlightuserdata(L, (void*)p); lua_insert(L, -2); lua_rawset(L, idx); }
#endif

#ifndef LUAMOD_API
# define LUAMOD_API LUALIB_API
#endif


#define LP_VERSION "path 0.2"

#if _WIN32
# define LP_DIRSEP   "\\"
# define LP_ALTSEP   "/"
# define LP_EXTSEP   "."
# define LP_CURDIR   "."
# define LP_PARDIR   ".."
# define LP_PATHSEP  ";"
# define LP_DEVNULL  "nul"
#else
# define LP_DIRSEP   "/"
# define LP_ALTSEP   "/"
# define LP_EXTSEP   "."
# define LP_CURDIR   "."
# define LP_PARDIR   ".."
# define LP_PATHSEP  ":"
# define LP_DEVNULL  "/dev/null"
#endif


typedef struct lp_State   lp_State;
typedef struct lp_DirData lp_DirData;

typedef int lp_WalkHandler(lp_State *S, void *ud, const char *s, int state);

#define LP_WALKIN   0
#define LP_WALKOUT  1
#define LP_WALKFILE 2

#define LP_MAX_COMPCOUNT  255
#define LP_MAX_TMPNUM     1000000
#define LP_BUFFERSIZE     4096
#define LP_MAX_SIZET      ((~(size_t)0)-100)

#define LP_STATE_KEY    ((void*)(ptrdiff_t)0x9A76B0FF)
#define LP_DIRDATA      "lpath.Dir Context"

#define lp_returnself(L) do { lua_settop((L), 1); return 1; } while (0)

struct lp_State {
    lua_State *L;
    /* path buffer data */
    char      *ptr;
    size_t     size;
    size_t     capacity;
    /* code page */
    int        current_cp;
};

static int lpL_delstate(lua_State *L) {
    lp_State *S = (lp_State*)lua_touserdata(L, 1);
    if (S != NULL) {
        free(S->ptr);
        memset(S, 0, sizeof(lp_State));
    }
    return 0;
}

static lp_State *lp_getstate(lua_State *L) {
    lp_State *S;
    if (lua53_rawgetp(L, LUA_REGISTRYINDEX, LP_STATE_KEY) == LUA_TUSERDATA)
        S = (lp_State*)lua_touserdata(L, -1);
    else {
        S = (lp_State*)lua_newuserdata(L, sizeof(lp_State));
        memset(S, 0, sizeof(lp_State));
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, lpL_delstate);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
        S->ptr = (char*)malloc(LP_BUFFERSIZE);
        S->capacity = LP_BUFFERSIZE;
        lua_rawsetp(L, LUA_REGISTRYINDEX, LP_STATE_KEY);
    }
    lua_pop(L, 1);
    S->L = L;
    S->size = 0;
    S->ptr[0] = 0;
    return S;
}


/* buffer routines */

#define lp_buffer(S)      ((S)->ptr)
#define lp_bufflen(S)    ((S)->size)
#define lp_setbuffer(S,n) ((S)->size = (n))
#define lp_addsize(S,sz)  ((S)->size += (sz))
#define lp_addstring(S,s) lp_addlstring((S),(s),strlen(s))

static char *lp_prepare(lp_State *S, size_t sz) {
    if (sz > S->capacity || S->capacity - sz < S->size) {
        char *ptr;
        size_t expect  = S->size + sz;
        size_t newsize = LP_BUFFERSIZE;
        while (newsize < expect && newsize < LP_MAX_SIZET)
            newsize += newsize >> 1;
        if (newsize < expect || (ptr = (char*)realloc(S->ptr, newsize)) == NULL)
            return NULL;
        S->ptr = ptr;
        S->capacity = newsize;
    }
    return &S->ptr[S->size];
}

static char *lp_prepbuffsize(lp_State *S, size_t sz) {
    char *ptr = lp_prepare(S, sz);
    if (ptr == NULL) luaL_error(S->L, "path buffer out of memory");
    return ptr;
}

static char *lp_prepbuffupdate(lp_State *S, size_t sz, void *ps) {
    char *p = ps != NULL ? *(char**)ps : NULL;
    char *ptr = S->ptr;
    char *newptr = lp_prepbuffsize(S, sz);
    if (ps != NULL && (p == NULL || (p >= S->ptr && p < S->ptr + S->size)))
        *(char**)ps = S->ptr + (p == NULL ? 0 : p - ptr);
    assert(newptr != NULL);
    return newptr;
}

static void lp_addchar(lp_State *S, int ch)
{ *(char*)lp_prepbuffsize(S, 1) = ch; ++S->size; }

static void lp_addlstring(lp_State *S, const void *s, size_t len)
{ memcpy(lp_prepbuffsize(S, len), s, len); S->size += len; }

static int lp_pushresult(lp_State *S)
{ lua_pushlstring(S->L, S->ptr, S->size); return 1; }


/* path algorithms */

#define LP_ALTSEPCHAR  (LP_ALTSEP[0])
#define LP_DIRSEPCHAR  (LP_DIRSEP[0])
#define LP_EXTSEPCHAR  (LP_EXTSEP[0])
#define LP_PATHSEPCHAR (LP_PATHSEP[0])
#define LP_CURDIRLEN   (sizeof(LP_CURDIR)-1)
#define LP_PARDIRLEN   (sizeof(LP_PARDIR)-1)

#define lp_isdirsep(ch) ((ch) == LP_DIRSEPCHAR || (ch) == LP_ALTSEPCHAR)
#define lp_isdirend(ch) (lp_isdirsep(ch) || ch == '\0')
#define lp_iscurdir(s)  (memcmp((s), LP_CURDIR, LP_CURDIRLEN) == 0 && lp_isdirend(s[LP_CURDIRLEN]))
#define lp_ispardir(s)  (memcmp((s), LP_PARDIR, LP_PARDIRLEN) == 0 && lp_isdirend(s[LP_PARDIRLEN]))
#define lp_charequal(ch1, ch2) (lp_normchar(ch1) == lp_normchar(ch2))

static const char *lp_nextsep(const char *p) {
    while (*p != '\0' && !lp_isdirsep(*p))
        ++p;
    return p;
}

static const char *lp_drivehead(const char *s) {
#ifdef _WIN32
    if (lp_isdirsep(s[0]) && lp_isdirsep(s[1])
            && s[2] == '?' && lp_isdirsep(s[3]))
        return s + 4;
#endif
    return s;
}

static const char *lp_splitdrive(const char *s) {
#ifdef _WIN32
    const char *os = s, *split = s, *mp;
retry:
    if (lp_isdirsep(s[0]) && lp_isdirsep(s[1]) && !lp_isdirsep(s[2])) {
        if (s[2] == '?' && lp_isdirsep(s[3]) && s == os) {
            /* \\?\, prefix for long path */
            s += 4, split += 4;
            goto retry;
        }
        /* vvvvvvvvvvvvvvvvvvvv drive letter or UNC path
         * \\machine\mountpoint\directory\etc\...
         *           directory ^^^^^^^^^^^^^^^
         * a UNC path can't have two slashes in a row
         * (after the initial two) */
        if (*(mp = lp_nextsep(s + 2)) != '\0' && !lp_isdirend(mp[1]))
            split = lp_nextsep(mp + 1);
    }
    else if (s[1] == ':')
        split = s + 2;
    return split;
#else
    return s;
#endif
}

static const char *lp_splitpath(const char *s) {
    const char *p;
    s = lp_splitdrive(s);
    if (*s == '\0')
        return s;
    p = s + strlen(s) - 1;
    while (s < p && !lp_isdirsep(*p))
        --p;
    return lp_isdirsep(*p) ? p + 1 : s;
}

static const char *lp_splitext(const char *s) {
    const char *e = s + strlen(s) - 1, *p = e;
    while (s < p && *p != LP_EXTSEPCHAR && !lp_isdirsep(*p))
        --p;
    return *p == LP_EXTSEPCHAR ? p : e + 1;
}

static int lp_isabs(lua_State *L, const char *s) {
    const char *p = lp_splitdrive(s);
    lua_pushboolean(L, lp_isdirsep(*p));
    return 1;
}

static int lp_normchar(int ch) {
    if (lp_isdirsep(ch))
        return LP_DIRSEPCHAR;
#ifdef _WIN32
    if (ch >= 'a' && ch <='z')
        return ch + 'A' - 'a';
#endif
    return ch;
}

static int lp_driveequal(const char **pd1, const char **pd2) {
    const char *d1 = lp_drivehead(*pd1);
    const char *d2 = lp_drivehead(*pd2);
    const char *p1 = lp_splitdrive(d1);
    const char *p2 = lp_splitdrive(d2);
    size_t l1 = p1 - d1, l2 = p2 - d2;
    *pd1 = p1, *pd2 = p2;
    if (l2 == 0)       return 1;
    else if (l1 != l2) return 0;
    while (d1 < p1 && d2 < p2 && lp_charequal(*d1, *d2))
        ++d1, ++d2;
    return d1 == p1;
}

static int lp_normpath(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    const char *path = lp_splitdrive(s);
    int isabs = lp_isdirsep(*path);
    size_t top = 0, pos[LP_MAX_COMPCOUNT];
    while (s < path)
        lp_addchar(S, lp_normchar(*s++));
    if (isabs) lp_addchar(S, LP_DIRSEPCHAR);
    pos[top++] = lp_bufflen(S) + isabs;
    while (*s != '\0') {
        const char *e;
        while (lp_isdirsep(*s)) ++s;
        if (lp_iscurdir(s))
            s += LP_CURDIRLEN;
        else if (lp_ispardir(s)) {
            s += LP_PARDIRLEN;
            if (top > 1)
                lp_setbuffer(S, pos[--top]);
            else if (isabs)
                lp_setbuffer(S, pos[top++]);
            else {
                lp_addlstring(S, LP_PARDIR, LP_PARDIRLEN);
                lp_addchar(S, LP_DIRSEPCHAR);
            }
        }
        else if (*(e = lp_nextsep(s)) == '\0') {
            lp_addstring(S, s);
            break;
        }
        else if (top >= LP_MAX_COMPCOUNT) {
            lua_pushnil(S->L);
            lua_pushstring(S->L, "path too complicate");
            return -2;
        }
        else {
            pos[top++] = lp_bufflen(S);
            lp_addlstring(S, s, e-s);
            lp_addchar(S, LP_DIRSEPCHAR);
            s = e + 1;
        }
    }
    if (lp_bufflen(S) == 0)
        lua_pushstring(L, LP_CURDIR LP_DIRSEP);
    else
        lp_pushresult(S);
    return 1;
}

static int lp_relpath(lua_State *L, const char *fn, const char *path) {
    lp_State *S;
    int count_dot2 = 0;
    const char *f, *p, *d = fn;
    if (!lp_driveequal(&fn, &path)) {
        lua_pushstring(L, d);
        return 1;
    }
    f = fn, p = path;
    while (*f != '\0' && *p != '\0' && lp_charequal(*f, *p))
        ++f, ++p;
    if (*f == '\0' && *p == '\0') {
        lua_pushstring(L, LP_CURDIR LP_DIRSEP);
        return 1;
    }
    while (fn < f && !lp_isdirsep(*f))
        --f, --p;
    if (path == p && !lp_isdirsep(*p))
        ++count_dot2;
    for (; *p != '\0'; ++p)
        if (lp_isdirsep(*p))
            ++count_dot2;
    if (path < p && lp_isdirsep(p[-1]))
        --count_dot2;
    S = lp_getstate(L);
    lp_addlstring(S, d, lp_splitdrive(d) - d);
    while (count_dot2--)
        lp_addstring(S, LP_PARDIR LP_DIRSEP);
    lp_addstring(S, f == fn && !lp_isdirsep(*f) ? f : f + 1);
    return lp_pushresult(S);
}

static int lpL_joinpath(lua_State *L) {
    int i, top = lua_gettop(L);
    lp_State *S = lp_getstate(L);
    for (i = 1; i <= top; ++i) {
        const char *s = lp_buffer(S);
        const char *rd = lp_drivehead(luaL_checkstring(L, i));
        const char *d = rd;
        if (!lp_driveequal(&s, &d) || lp_isdirsep(d[0]))
            lp_setbuffer(S, 0);
        lp_addstring(S, rd);
        if (d[0] != '\0' && i != top)
            lp_addchar(S, LP_DIRSEPCHAR);
    }
    lua_settop(L, 0);
    return lp_pushresult(S);
}


/* system specfied utils */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define LP_PLATFORM      "windows"

#define lp_pusherror(L, t, f) lp_pusherrmsg((L), GetLastError(), (t), (f))

static int lp_pusherrmsg(lua_State *L, DWORD err, const char *title, const char *fn);

static char *lp_addmultibyte(lp_State *S, LPCWSTR ws) {
    int cp = S->current_cp;
    int wc = (int)wcslen(ws = ws ? ws : (WCHAR*)lp_buffer(S)), size = (wc+1)*3;
    char *s = lp_prepbuffupdate(S, size, (void*)&ws);
    int bc = WideCharToMultiByte(cp, 0, ws, wc+1, s, size, NULL, NULL);
    if (bc > size) {
        s = lp_prepbuffupdate(S, size = bc+1, (void*)ws);
        bc = WideCharToMultiByte(cp, 0, ws, wc+1, s, size, NULL, NULL);
    }
    if (bc == 0) {
        lp_pusherror(S->L, "multibyte", NULL);
        lua_error(S->L);
    }
    lp_addsize(S, bc);
    return s;
}

static WCHAR *lp_addlwidechar(lp_State *S, LPCSTR s, int bc) {
    int cp = S->current_cp;
    int size = (bc+1)*sizeof(WCHAR);
    WCHAR *ws = (WCHAR*)lp_prepbuffupdate(S, size, (void*)&s);
    int wc = MultiByteToWideChar(cp, 0, s, bc+1, ws, size);
    if (wc > bc) {
        ws = (WCHAR*)lp_prepbuffupdate(S, size = (wc+1)*sizeof(WCHAR), (void*)&s);
        wc = MultiByteToWideChar(cp, 0, s, bc+1, ws, size);
    }
    if (wc == 0) {
        lp_pusherror(S->L, "unicode", NULL);
        lua_error(S->L);
    }
    lp_addsize(S, wc*sizeof(WCHAR));
    return ws;
}

static WCHAR *lp_addwidechar(lp_State *S, LPCSTR s) {
    int bc = (int)strlen(s = (s ? s : lp_buffer(S)));
    return lp_addlwidechar(S, s, bc);
}

static WCHAR *lp_addwidepath(lp_State *S, LPCSTR s) {
    int top, bc = (int)strlen(s = (s ? s : lp_buffer(S)));
    if (bc < MAX_PATH) return lp_addlwidechar(S, s, bc);
    top = (int)lp_bufflen(S);
    lp_addlstring(S, L"\\\\?\\", 4*sizeof(WCHAR));
    lp_addwidechar(S, s);
    return (WCHAR*)((char*)lp_buffer(S) + top);
}

static const char *lp_win32error(lua_State *L, DWORD errnum) {
    lp_State *S = lp_getstate(L);
    const char *ret = NULL;
    WCHAR *msg = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        errnum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg,
        0, NULL);
    if (len == 0)
        ret = "get system error message error";
    if ((ret = lp_prepare(S, len*3)) == NULL)
        ret = "error message out of memory";
    else {
        int bc = WideCharToMultiByte(S->current_cp, 0, msg, len,
                (char*)ret, len*3, NULL, NULL);
        LocalFree(msg);
        if (bc > (int)len*3)
            ret = "error message too large";
        else if (bc == 0)
            ret = "mutibyte: format error message error";
        else {
            lp_addsize(S, bc);
            lp_addchar(S, '\0');
        }
    }
    return ret;
}

static int lp_pusherrmsg(lua_State *L, DWORD err, const char *title, const char *fn) {
    const char *msg = lp_win32error(L, err);
    lua_pushnil(L);
    if (title && fn)
        lua_pushfstring(L, "%s:%s:(errno=%d): %s", title, fn, err, msg);
    else if (title || fn)
        lua_pushfstring(L, "%s:(errno=%d): %s", title ? title : fn, err, msg);
    else
        lua_pushfstring(L, "lpath:(errno=%d): %s", err, msg);
    return -2;
}

static int lpL_ansi(lua_State *L) {
    lp_State *S = lp_getstate(L);
    int cp = S->current_cp;
    const char *utf8;
    switch (lua_type(L, 1)) {
    case LUA_TNONE:
    case LUA_TNIL:
    case LUA_TNUMBER:
        S->current_cp = (UINT)lua_tonumber(L, 1);
        return 0;
    case LUA_TSTRING:
        utf8 = lua_tostring(L, 1);
        S->current_cp = CP_UTF8;
        lp_addwidechar(S, utf8);
        S->current_cp = cp;
        lua_pushstring(L, lp_addmultibyte(S, NULL));
        return 1;
    default:
        lua_pushfstring(L, "number/string expected, got %s",
                luaL_typename(L, 1));
        return luaL_argerror(L, 1, lua_tostring(L, -1));
    }
}

static int lpL_utf8(lua_State *L) {
    lp_State *S = lp_getstate(L);
    int cp = S->current_cp;
    const char *ansi;
    switch (lua_type(L, 1)) {
    case LUA_TNONE:
    case LUA_TNIL:
    case LUA_TNUMBER:
        S->current_cp = (UINT)lua_tonumber(L, 1);
        return 0;
    case LUA_TSTRING:
        ansi = lua_tostring(L, 1);
        lp_addwidechar(S, ansi);
        S->current_cp = CP_UTF8;
        lua_pushstring(L, lp_addmultibyte(S, NULL));
        S->current_cp = cp;
        return 1;
    default:
        lua_pushfstring(L, "number/string expected, got %s",
                luaL_typename(L, 1));
        return luaL_argerror(L, 1, lua_tostring(L, -1));
    }
    return 0;
}


/* misc utils */

static void lp_pushuint64(lua_State *L, ULONGLONG ull) {
    if (sizeof(lua_Integer) >= 8)
        lua_pushinteger(L, (lua_Integer)ull);
    else
        lua_pushnumber(L, (lua_Number)ull);
}

static int lp_optftime(lua_State *L, int idx, PFILETIME pft) {
    ULARGE_INTEGER ln;
    if (lua_isnoneornil(L, idx))
        return 0;
    if (sizeof(lua_Integer) >= 8)
        ln.QuadPart = (ULONGLONG)luaL_checkinteger(L, idx);
    else
        ln.QuadPart = (ULONGLONG)luaL_checknumber(L, idx);
    pft->dwLowDateTime = ln.LowPart;
    pft->dwHighDateTime = ln.HighPart;
    return 1;
}

static void lp_pushftime(lua_State *L, PFILETIME pft) {
    ULARGE_INTEGER ln;
    ln.LowPart = pft->dwLowDateTime;
    ln.HighPart = pft->dwHighDateTime;
    lp_pushuint64(L, ln.QuadPart);
}

static int lpL_setenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    lp_State *S = lp_getstate(L);
    WCHAR *wvalue;
    lp_addwidechar(S, name);
    wvalue = value ? lp_addwidechar(S, value) : NULL;
    if (!SetEnvironmentVariableW((WCHAR*)lp_buffer(S), wvalue))
        return -lp_pusherror(L, "setenv", NULL);
    lua_settop(L, 2);
    return 1;
}

static int lpL_getenv(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WCHAR *ret, *ws = lp_addwidechar(S, luaL_checkstring(L, 1));
    DWORD wc;
    ret = (WCHAR*)lp_prepbuffupdate(S, MAX_PATH*sizeof(WCHAR), (void*)&ws);
    wc = GetEnvironmentVariableW(ws, ret, MAX_PATH);
    if (wc >= MAX_PATH) {
        ret = (LPWSTR)lp_prepbuffupdate(S, wc*sizeof(WCHAR), (void*)&ws);
        wc = GetEnvironmentVariableW(ws, ret, wc);
    }
    if (wc == 0)
        return -lp_pusherror(S->L, "getenv", NULL);
    lp_addsize(S, (wc+1)*sizeof(WCHAR));
    lua_pushstring(L, lp_addmultibyte(S, ret));
    return 1;
}

static int lp_expandvars(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    WCHAR *ret, *ws = lp_addwidechar(S, s);
    DWORD wc;
    ret = (WCHAR*)lp_prepbuffupdate(S, MAX_PATH*sizeof(WCHAR), (void*)&ws);
    wc = ExpandEnvironmentStringsW(ws, ret, MAX_PATH);
    if (wc >= MAX_PATH) {
        ret = (LPWSTR)lp_prepbuffupdate(S, wc*sizeof(WCHAR), (void*)&ws);
        wc = ExpandEnvironmentStringsW(ws, ret, wc);
    }
    if (wc == 0) return lp_pusherror(L, "expandvars", s);
    lp_addsize(S, wc*sizeof(WCHAR));
    lua_pushstring(L, lp_addmultibyte(S, ret));
    return 1;
}

static int lp_readreg(lp_State *S, HKEY hkey, LPCWSTR key) {
    DWORD size = MAX_PATH * sizeof(WCHAR), ret;
    WCHAR *wc = (lp_setbuffer(S, 0), (WCHAR*)lp_prepbuffsize(S, size));
    while ((ret = RegQueryValueExW(hkey, key, NULL, NULL, (LPBYTE)wc, &size))
            != ERROR_SUCCESS) {
        if (ret != ERROR_MORE_DATA) {
            lua_pushstring(S->L, lp_addmultibyte(S, key));
            return lp_pusherrmsg(S->L, ret, "platform", lua_tostring(S->L, -1));
        }
        wc = (WCHAR*)lp_prepbuffsize(S, size);
    }
    lp_addsize(S, size);
    lp_addlstring(S, L"", sizeof(WCHAR));
    return 0;
}

static int lpL_platform(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WCHAR *dot, *root = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    DWORD major = 0, minor = 0, build, size = sizeof(DWORD);
    HKEY hkey;
    int ret;
    if ((ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    root, 0, KEY_QUERY_VALUE, &hkey)) != ERROR_SUCCESS)
        return -lp_pusherrmsg(L, ret, "platform", NULL);
    if ((ret = lp_readreg(S, hkey, L"CurrentBuildNumber")) < 0)
    { ret = -ret; goto out; }
    build = wcstoul((WCHAR*)lp_buffer(S), NULL, 10);
    if (RegQueryValueExW(hkey, L"CurrentMajorVersionNumber",
                NULL, NULL, (LPBYTE)&major, &size) != ERROR_SUCCESS
            || RegQueryValueExW(hkey, L"CurrentMinorVersionNumber",
                NULL, NULL, (LPBYTE)&minor, &size) != ERROR_SUCCESS) {
        if ((ret = lp_readreg(S, hkey, L"CurrentVersion")) < 0)
        { ret = -ret; goto out; }
        major = wcstoul((WCHAR*)lp_buffer(S), &dot, 10);
        if (*dot == L'.') minor = wcstoul(dot + 1, NULL, 10);
    }
    lua_pushfstring(L, "Windows %d.%d Build %d", major, minor, build);
    lua_pushinteger(L, major);
    lua_pushinteger(L, minor);
    lua_pushinteger(L, build);
    ret = 4;
out:RegCloseKey(hkey);
    return ret;
}


/* path utils */

typedef DWORD WINAPI LPGETFINALPATHNAMEBYHANDLEW(
        HANDLE hFile,
        LPWSTR lpszFilePath,
        DWORD cchFilePath,
        DWORD dwFlags);

static LPGETFINALPATHNAMEBYHANDLEW *pGetFinalPathNameByHandleW;

static int lp_abs(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    WCHAR *ws = lp_addwidepath(S, s);
    WCHAR *ret = (WCHAR*)lp_prepbuffupdate(S,
            MAX_PATH*sizeof(WCHAR), (void*)&ws);
    DWORD wc = GetFullPathNameW(ws, MAX_PATH, ret, NULL);
    if (wc >= MAX_PATH) {
        ret = (WCHAR*)lp_prepbuffupdate(S, wc*sizeof(WCHAR), (void*)&ws);
        wc = GetFullPathNameW(ws, wc, ret, NULL);
    }
    if (wc == 0) return lp_pusherror(L, "abs", s);
    lp_addsize(S, (wc+1)*sizeof(WCHAR));
    lua_pushstring(L, lp_addmultibyte(S, ret));
    return 1;
}

static int lp_solvelink(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    WCHAR *ret, *ws = lp_addwidepath(S, s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            0,                     /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            0,                     /* no file attributes   */
            NULL);                 /* no attr. template    */
    DWORD wc;
    if(hFile == INVALID_HANDLE_VALUE)
        return lp_pusherror(L, "open", s);
    lp_setbuffer(S, 0);
    ret = (LPWSTR)lp_prepbuffsize(S, MAX_PATH*sizeof(WCHAR));
    wc = pGetFinalPathNameByHandleW(hFile, ret, MAX_PATH, 0);
    if (wc >= MAX_PATH) {
        ret = (LPWSTR)lp_prepbuffsize(S, wc*sizeof(WCHAR));
        wc = pGetFinalPathNameByHandleW(hFile, ret, wc, 0);
    }
    CloseHandle(hFile);
    if (wc == 0) return lp_pusherror(L, "realpath", s);
    lp_addsize(S, (wc + 1) * sizeof(WCHAR));
    if (wc > MAX_PATH + 4)
        lua_pushstring(L, lp_addmultibyte(S, ret));
    else
        lua_pushstring(L, lp_addmultibyte(S, ret+4));
    return 1;
}

static int lp_realpath(lua_State *L, const char *s) {
    if (!pGetFinalPathNameByHandleW) {
        HMODULE hModule = GetModuleHandleA("KERNEL32.dll");
        if (hModule != NULL)
            pGetFinalPathNameByHandleW = (LPGETFINALPATHNAMEBYHANDLEW*)
                GetProcAddress(hModule, "GetFinalPathNameByHandleW");
    }
    if (pGetFinalPathNameByHandleW == NULL)
        return lp_abs(L, s);
    return lp_solvelink(L, s);
}

static int lp_type(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    DWORD attr = GetFileAttributesW(ws);
    int isdir, islink;
    if (attr == INVALID_FILE_ATTRIBUTES)
        return lp_pusherror(L, "type", s);
    isdir = attr & FILE_ATTRIBUTE_DIRECTORY;
    islink = attr & FILE_ATTRIBUTE_REPARSE_POINT;
    lua_pushstring(L, islink ? "link" :
                      isdir ? "dir" : "file");
    return 1;
}


/* dir utils */

struct lp_DirData {
    lp_State *S;
    HANDLE hFile;
    WIN32_FIND_DATAW wfd;
    DWORD err;
};

static int lpL_deldir(lua_State *L) {
    lp_DirData *d = (lp_DirData*)luaL_checkudata(L, 1, LP_DIRDATA);
    if (d->hFile != INVALID_HANDLE_VALUE) {
        FindClose(d->hFile);
        d->hFile = INVALID_HANDLE_VALUE;
    }
    return 0;
}

static int lpL_diriter(lua_State *L) {
    lp_DirData *d = (lp_DirData*)lua_touserdata(L, 1);
    const char *fn;
retry:
    if (d == NULL || d->err == ERROR_NO_MORE_FILES)
        return 0;
    else if (d->err != NO_ERROR) {
        lp_pusherrmsg(L, d->err, "dir.iter", NULL);
        return lua_error(L);
    }
    fn = lp_addmultibyte(d->S, d->wfd.cFileName);
    while (lp_iscurdir(fn) || lp_ispardir(fn)) {
        if (!FindNextFileW(d->hFile, &d->wfd))
        { d->err = GetLastError(); goto retry; }
        fn = lp_addmultibyte(d->S, d->wfd.cFileName);
    }
    lua_pushstring(L, fn);
    lua_pushstring(L, d->wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY?"dir":"file");
    lp_pushuint64(L, ((ULONGLONG)d->wfd.nFileSizeHigh<<32)|d->wfd.nFileSizeLow);
    lp_pushftime(L, &d->wfd.ftCreationTime);
    lp_pushftime(L, &d->wfd.ftLastWriteTime);
    lp_pushftime(L, &d->wfd.ftLastAccessTime);
    if (!FindNextFileW(d->hFile, &d->wfd))
        d->err = GetLastError();
    return 6;
}

static int lp_dir(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    lp_DirData *d;
    lp_addwidepath(S, s);
    lp_setbuffer(S, lp_bufflen(S)-sizeof(WCHAR));
    lp_addlstring(S, L"\\*", 3*sizeof(WCHAR));
    d = (lp_DirData*)lua_newuserdata(L, sizeof(lp_DirData));
    d->hFile = FindFirstFileW((WCHAR*)lp_buffer(S), &d->wfd);
    if (d->hFile == INVALID_HANDLE_VALUE) {
        lp_pusherror(L, "dir", s);
        lua_error(L);
    }
    if (luaL_newmetatable(L, LP_DIRDATA)) {
        lua_pushcfunction(L, lpL_deldir);
        lua_setfield(L, -2, "__gc");
    }
    d->S   = S;
    d->err = NO_ERROR;
    lua_setmetatable(L, -2);
    lua_pushcfunction(L, lpL_diriter);
    lua_insert(L, -2);
    return 2;
}

static int lp_walkpath(lua_State *L, const char *s, lp_WalkHandler *h, void *ud) {
    lp_State *S = lp_getstate(L);
    DWORD err, attr = GetFileAttributesW(lp_addwidechar(S, s));
    WIN32_FIND_DATAW wfd;
    int ret = 0, top = 0;
    size_t pos[LP_MAX_COMPCOUNT];
    HANDLE hFile[LP_MAX_COMPCOUNT];
    if (*s != '\0' && attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (*s != '\0' && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return h(S, ud, s, LP_WALKFILE);
    wfd.cFileName[0] = 0;
    wfd.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    pos[0] = lp_bufflen(S) - sizeof(WCHAR);
    do {
        size_t len = wcslen(wfd.cFileName)*sizeof(WCHAR);
        lp_setbuffer(S, pos[top]);
        lp_addlstring(S, wfd.cFileName, len + sizeof(WCHAR));
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            ret = h(S, ud, lp_addmultibyte(S, NULL), LP_WALKFILE);
            if (ret < 0) break;
        }
        else if (top < LP_MAX_COMPCOUNT-1
                    && wcscmp(wfd.cFileName, L".")
                    && wcscmp(wfd.cFileName, L".."))
        {
            ret = h(S, ud, lp_addmultibyte(S, NULL), LP_WALKIN);
            if (ret < 0) break;
            if (ret != 0) {
                lp_setbuffer(S, pos[top] + len);
                lp_addlstring(S, L"\\*", 3*sizeof(WCHAR));
                hFile[++top] = FindFirstFileW((WCHAR*)lp_buffer(S), &wfd);
                if (hFile[top] == INVALID_HANDLE_VALUE) {
                    err = GetLastError();
                    lua_pushstring(L, lp_addmultibyte(S, NULL));
                    ret = lp_pusherrmsg(L, err, "findfile1", lua_tostring(L, -1));
                    break;
                }
                pos[top] = lp_bufflen(S) - 2*sizeof(WCHAR);
                continue;
            }
        }
        for (; top != 0 && !FindNextFileW(hFile[top], &wfd); --top) {
            if ((err = GetLastError()) != ERROR_NO_MORE_FILES) {
                lua_pushstring(L, lp_addmultibyte(S, NULL));
                ret = lp_pusherrmsg(L, err, "findfile2", lua_tostring(L, -1));
                goto out;
            }
            FindClose(hFile[top]);
            lp_setbuffer(S, pos[top]);
            lp_addlstring(S, L"", sizeof(WCHAR));
            ret = h(S, ud, lp_addmultibyte(S, NULL), LP_WALKOUT);
            if (ret < 0) goto out;
        }
    } while (top);
out:while (top) FindClose(hFile[top--]);
    return ret;
}

static int lpL_getcwd(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WCHAR *ret = (WCHAR*)lp_prepbuffsize(S, MAX_PATH*sizeof(WCHAR));
    DWORD wc = GetCurrentDirectoryW(MAX_PATH, ret);
    if (wc >= MAX_PATH) {
        ret = (WCHAR*)lp_prepbuffsize(S, wc*sizeof(WCHAR));
        wc = GetCurrentDirectoryW(wc, ret);
    }
    if (wc == 0) return lp_pusherror(L, "getcwd", NULL);
    lp_addsize(S, (wc + 1) * sizeof(WCHAR));
    lua_pushstring(L, lp_addmultibyte(S, ret));
    return 1;
}

static int lpL_binpath(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WCHAR *ret = (WCHAR*)lp_prepbuffsize(S, MAX_PATH*sizeof(WCHAR));
    DWORD wc = GetModuleFileNameW(NULL, ret, MAX_PATH);
    if (wc > MAX_PATH) {
        ret = (WCHAR*)lp_prepbuffsize(S, wc*sizeof(WCHAR));
        wc = GetModuleFileNameW(NULL, ret, wc);
    }
    if (wc == 0) return lp_pusherror(L, "binpath", NULL);
    lp_addsize(S, (wc + 1) * sizeof(WCHAR));
    lua_pushstring(L, lp_addmultibyte(S, ret));
    return 1;
}

static int lpL_tmpdir(lua_State* L) {
    size_t len;
    const char *s, *prefix = luaL_optstring(L, 1, "lua_");
    lp_State *S = lp_getstate(L);
    WCHAR tmpdir[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, tmpdir) == 0)
        return -lp_pusherror(L, "tmpdir", NULL);
    lua_pushstring(L, lp_addmultibyte(S, tmpdir));
    s = lua_tolstring(L, -1, &len);
    srand(((int)(ptrdiff_t)&L) ^ clock());
    do {
        int magic = ((unsigned)rand()<<16|rand()) % LP_MAX_TMPNUM;
        const char *fmt = lp_isdirsep(s[len-1]) ?
            "%s%s%d" LP_DIRSEP : "%s" LP_DIRSEP "%s%d" LP_DIRSEP;
        lua_settop(L, 3);
        lua_pushfstring(L, fmt, s, prefix, magic);
        lp_setbuffer(S, 0);
    } while (GetFileAttributesW(lp_addwidepath(S, lua_tostring(L, -1))) !=
            INVALID_FILE_ATTRIBUTES);
    if (!CreateDirectoryW(lp_addwidepath(S, lua_tostring(L, -1)), NULL))
        return -lp_pusherror(L, "tmpdir", lp_addmultibyte(S, NULL));
    return 1;
}

static int lp_chdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    if (!SetCurrentDirectoryW(ws))
        return lp_pusherror(L, "chdir", s);
    return 0;
}

static int lp_mkdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    if (!CreateDirectoryW(ws, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            lp_returnself(L);
        return lp_pusherrmsg(L, err, "mkdir", s);
    }
    return 1;
}

static int lp_rmdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    if (!RemoveDirectoryW(ws))
        return lp_pusherror(L, "rmdir", s);
    return 0;
}

static int lp_makedirs(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    WCHAR *ws, *cur, old;
    DWORD err;
    int rets;
    if ((rets = lp_normpath(L, s)) < 0) return rets;
    lp_setbuffer(S, 0);
    ws = lp_addwidepath(S, s = lua_tostring(L, -1));
    cur = ws + (lp_splitdrive(s) - s);
    if (lp_isdirsep(*cur)) ++cur;
    for (old = *cur; old != 0; *cur = old, ++cur) {
        while (*cur != 0 && !lp_isdirsep(*cur))
            cur++;
        old = *cur, *cur = 0;
        if (CreateDirectoryW(ws, NULL)
                || (err = GetLastError()) == ERROR_ALREADY_EXISTS)
            continue;
        lua_pushstring(L, lp_addmultibyte(S, ws));
        return lp_pusherrmsg(L, err, "makedirs", lua_tostring(L, -1));
    }
    return 0;
}

static int rmdir_walker(lp_State *S, void *ud, const char *s, int state) {
    LPCWSTR ws = lp_addwidechar(S, s);
    (void)ud;
    if (state == LP_WALKIN)
        return 1;
    if (state == LP_WALKFILE && !DeleteFileW(ws))
        return lp_pusherror(S->L, "removedirs", s);
    else if (state == LP_WALKOUT && !RemoveDirectoryW(ws))
        return lp_pusherror(S->L, "removedirs", s);
    return 0;
}

static int unlock_walker(lp_State *S, void *ud, const char *s, int state) {
    LPCWSTR ws = lp_addwidechar(S, s);
    (void)ud;
    if (state == LP_WALKIN)
        return 1;
    if (state == LP_WALKFILE
            && !SetFileAttributesW(ws,
                GetFileAttributesW(ws) & ~FILE_ATTRIBUTE_READONLY))
        return lp_pusherror(S->L, "unlock", s);
    return 0;
}


/* file utils */

static int lp_exists(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            FILE_WRITE_ATTRIBUTES, /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            FILE_FLAG_BACKUP_SEMANTICS, /* open directory also */
            NULL);                 /* no attr. template    */
    CloseHandle(hFile);
    lua_pushboolean(L, hFile != INVALID_HANDLE_VALUE);
    return 1;
}

static int lp_remove(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    return DeleteFileW(ws) ? 1 : lp_pusherror(L, "remove", s);
}

static int lp_fsize(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULARGE_INTEGER ul;
    if (!GetFileAttributesExW(ws, GetFileExInfoStandard, &fad))
        return lp_pusherror(L, "fsize", s);
    ul.LowPart = fad.nFileSizeLow;
    ul.HighPart = fad.nFileSizeHigh;
    lp_pushuint64(L, ul.QuadPart);
    return 1;
}

static int lp_ftime(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(ws, GetFileExInfoStandard, &fad))
        return lp_pusherror(L, "ftime", s);
    lp_pushftime(L, &fad.ftCreationTime);
    lp_pushftime(L, &fad.ftLastWriteTime);
    lp_pushftime(L, &fad.ftLastAccessTime);
    return 3;
}

static int lpL_touch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    LPCWSTR ws = lp_addwidepath(lp_getstate(L), s);
    FILETIME at, mt;
    SYSTEMTIME st;
    HANDLE hFile = CreateFileW(ws, /* file to open       */
            FILE_WRITE_ATTRIBUTES, /* open for write attributes   */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security   */
            OPEN_ALWAYS,           /* existing file only */
            FILE_FLAG_BACKUP_SEMANTICS, /* open directory also */
            NULL);                 /* no attr. template  */
    if (hFile == INVALID_HANDLE_VALUE)
        return -lp_pusherror(L, "open", s);
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &mt);
    at = mt;
    lp_optftime(L, 2, &mt);
    lp_optftime(L, 3, &at);
    if (!SetFileTime(hFile, NULL, &at, &mt))
        return -lp_pusherror(L, "touch", s);
    if (!CloseHandle(hFile))
        return -lp_pusherror(L, "close", s);
    lp_returnself(L);
}

static int lpL_rename(lua_State *L) {
    lp_State *S = lp_getstate(L);
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    WCHAR *wto;
    lp_addwidepath(S, from);
    wto = lp_addwidepath(S, to);
    if (!MoveFileW((WCHAR*)lp_buffer(S), wto))
        return -lp_pusherror(L, "rename", to);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_copy(lua_State *L) {
    lp_State *S = lp_getstate(L);
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int failIfExists = lua_toboolean(L, 3);
    WCHAR *wto;
    lp_addwidepath(S, from);
    wto = lp_addwidepath(S, to);
    if (!CopyFileW((WCHAR*)lp_buffer(S), wto, failIfExists))
        return -lp_pusherror(L, "copy", to);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_cmpftime(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WIN32_FILE_ATTRIBUTE_DATA fad1, fad2;
    int use_atime = lua_toboolean(L, 3);
    LONG cmp_c, cmp_m, cmp_a;
    WCHAR *wf2;
    lp_addwidepath(S, luaL_checkstring(L, 1));
    wf2 = lp_addwidepath(S, luaL_checkstring(L, 2));
    if (!GetFileAttributesExW((WCHAR*)lp_buffer(S), GetFileExInfoStandard, &fad1))
        lua_pushinteger(L, -1);
    else if (!GetFileAttributesExW(wf2, GetFileExInfoStandard, &fad2))
        lua_pushinteger(L, 1);
    else {
        cmp_c = CompareFileTime(&fad1.ftCreationTime,   &fad2.ftCreationTime);
        cmp_m = CompareFileTime(&fad1.ftLastWriteTime,  &fad2.ftLastWriteTime);
        cmp_a = CompareFileTime(&fad1.ftLastAccessTime, &fad2.ftLastAccessTime);
        if (use_atime)
            lua_pushinteger(L, cmp_c == 0 ? cmp_m == 0 ? cmp_a : cmp_m : cmp_c);
        else
            lua_pushinteger(L, cmp_c == 0 ? cmp_m : cmp_c);
    }
    return 1;
}


#else /* POSIX systems */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utime.h>
#ifndef __ANDROID__
# include <wordexp.h>
#endif
#ifdef __APPLE__
# include <libproc.h>
#endif

#if defined(__linux__)
# define LP_PLATFORM "linux"
#elif defined(__APPLE__) && defined(__MACH__)
# define LP_PLATFORM "macosx"
#elif defined(__ANDROID__)
# define LP_PLATFORM "android"
#else
# define LP_PLATFORM "posix"
#endif

static int lp_pusherror(lua_State *L, const char *title, const char *fn) {
    int err = errno;
    const char *msg = strerror(errno);
    (void)lp_prepbuffupdate;
    lua_pushnil(L);
    if (title && fn)
        lua_pushfstring(L, "%s:%s:(errno=%d): %s", title, fn, err, msg);
    else if (title || fn)
        lua_pushfstring(L, "%s:(errno=%d): %s", title ? title : fn, err, msg);
    else
        lua_pushfstring(L, "lpath:(errno=%d): %s", err, msg);
    return -2;
}

static int lpL_ansi(lua_State *L) {
    if (lua_isstring(L, 1))
        lp_returnself(L);
    return 0;
}

static int lpL_utf8(lua_State *L) {
    if (lua_isstring(L, 1))
        lp_returnself(L);
    return 0;
}


/* misc utils */

static void lp_pushuint64(lua_State *L, unsigned long long ull) {
    if (sizeof(lua_Integer) >= 8)
        lua_pushinteger(L, (lua_Integer)ull);
    else
        lua_pushnumber(L, (lua_Number)ull);
}

static unsigned long long lp_optuint64(lua_State *L, int idx, unsigned long long def) {
    if (sizeof(lua_Integer) >= 8)
        return (unsigned long long)luaL_optinteger(L, idx, (lua_Integer)def);
    else
        return (unsigned long long)luaL_optnumber(L, idx, (lua_Number)def);
}

static int lpL_setenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    if (setenv(name, value, 1) < 0)
        return -lp_pusherror(L, "setenv", NULL);
    lua_settop(L, 2);
    return 1;
}

static int lpL_getenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = getenv(name);
    lua_pushstring(L, value);
    return 1;
}

static int lp_expandvars(lua_State *L, const char *s) {
#ifdef __ANDROID__
    lua_pushnil(L);
    lua_pushstring(L, "expandvars not support on Android");
    return -2;
#else
    wordexp_t p;
    int i, res = wordexp(s, &p, 0);
    const char *errmsg = NULL;
    switch (res) {
    case WRDE_BADCHAR: errmsg = "invalid char"; break;
    case WRDE_NOSPACE: errmsg = "out of memory"; break;
    case WRDE_SYNTAX: errmsg = "syntax error"; break;
    }
    if (errmsg) {
        lua_pushnil(L);
        lua_pushstring(L, errmsg);
        wordfree(&p);
        return 2;
    }
    luaL_checkstack(L, p.we_wordc, "too many results");
    for (i = 0; i < p.we_wordc; ++i)
        lua_pushstring(L, p.we_wordv[i]);
    wordfree(&p);
    return i;
#endif
}

static int lpL_platform(lua_State *L) {
    struct utsname buf;
    if (uname(&buf) != 0) {
        lua_pushstring(L, LP_PLATFORM);
        return 1-lp_pusherror(L, "platform", NULL);
    }
    lua_pushstring(L, buf.sysname);
    lua_pushstring(L, buf.nodename);
    lua_pushstring(L, buf.release);
    lua_pushstring(L, buf.version);
    lua_pushstring(L, buf.machine);
#ifdef _GNU_SOURCE
    lua_pushstring(L, buf.domainname);
    return 6;
#endif
    return 5;
}


/* path utils */

static int lp_abs(lua_State *L, const char *s) {
    if (!lp_isdirsep(*s)) {
        lp_State *S = lp_getstate(L);
        char *ret = lp_prepbuffsize(S, PATH_MAX);
        if (getcwd(ret, PATH_MAX) == NULL)
            return lp_pusherror(L, "getcwd", NULL);
        lp_addsize(S, strlen(ret));
        lp_addchar(S, LP_DIRSEPCHAR);
        lp_addstring(S, s);
        return lp_pushresult(S);
    }
    return 1;
}

static int lp_realpath(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    char *ret = lp_prepbuffsize(S, PATH_MAX);
    if (realpath(s, ret) == NULL)
        return lp_pusherror(L, "realpath", NULL);
    lua_pushstring(L, ret);
    return 1;
}

static int lp_type(lua_State *L, const char *s) {
    struct stat buf;
    if (stat(s, &buf) != 0)
        return lp_pusherror(L, "stat", s);
    lua_pushstring(L,
            S_ISLNK(buf.st_mode) ? "link" :
            S_ISDIR(buf.st_mode) ? "dir" : "file");
    return 1;
}


/* dir utils */

static int lp_chdir(lua_State *L, const char *s)
{ return chdir(s) == 0 ? 0 : lp_pusherror(L, "chdir", s); }

static int lp_rmdir(lua_State *L, const char *s)
{ return rmdir(s) == 0 ? 0 : lp_pusherror(L, "rmdir", s); }

struct lp_DirData {
    int closed;
    DIR *dir;
};

static int lpL_deldir(lua_State *L) {
    lp_DirData *d = (lp_DirData*)luaL_checkudata(L, 1, LP_DIRDATA);
    if (!d->closed) {
        closedir(d->dir);
        d->closed = 1;
    }
    return 0;
}

static int lp_readdir(lua_State *L, DIR *dir, struct dirent **dirent, struct stat *buf) {
    int fd = dirfd(dir);
    struct dirent *data;
    do {
        errno = 0;
        if ((data = readdir(dir)) != NULL)
            continue;
        if (errno != 0)
            return lp_pusherror(L, "dir.iter", NULL);
        closedir(dir);
        return 0;
    } while (lp_iscurdir(data->d_name) || lp_ispardir(data->d_name));
    if (fd < 0 || fstatat(fd, data->d_name, buf, AT_SYMLINK_NOFOLLOW) != 0)
        return lp_pusherror(L, "dir.stat", data->d_name);
    *dirent = data;
    return 1;
}

static int lpL_diriter(lua_State *L) {
    lp_DirData *d = (lp_DirData*)lua_touserdata(L, 1);
    struct dirent *dir;
    struct stat buf;
    int ret;
    if (d == NULL || d->closed) return 0;
    if ((ret = lp_readdir(L, d->dir, &dir, &buf)) < 0)
        lua_error(L);
    if (ret == 0) {
        d->closed = 1;
        return 0;
    }
    lua_pushstring(L, dir->d_name);
    lua_pushstring(L, S_ISDIR(buf.st_mode) ? "dir" : "file");
    lp_pushuint64(L, buf.st_size);
    lp_pushuint64(L, buf.st_ctime);
    lp_pushuint64(L, buf.st_mtime);
    lp_pushuint64(L, buf.st_atime);
    return 6;
}

static int lp_dir(lua_State *L, const char *s) {
    lp_DirData *d = (lp_DirData*)lua_newuserdata(L, sizeof(lp_DirData));
    d->dir = opendir(s);
    d->closed = 0;
    if (d->dir == NULL) {
        lp_pusherror(L, "dir", s);
        lua_error(L);
    }
    if (luaL_newmetatable(L, LP_DIRDATA)) {
        lua_pushcfunction(L, lpL_deldir);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushcfunction(L, lpL_diriter);
    lua_insert(L, -2);
    return 2;
}

static int lp_walkpath(lua_State *L, const char *s, lp_WalkHandler *h, void *ud) {
    lp_State *S = lp_getstate(L);
    struct stat buf;
    struct dirent *data = NULL;
    int top = 0, ret = stat(s, &buf), isdir = 1;
    size_t pos[LP_MAX_COMPCOUNT];
    DIR *dir[LP_MAX_COMPCOUNT];
    if (*s != '\0' && ret != 0)
        return 0;
    if (*s != '\0' && !S_ISDIR(buf.st_mode))
        return h(S, ud, s, LP_WALKFILE);
    lp_addstring(S, s), pos[top] = strlen(s);
    do {
        if (top) {
            lp_setbuffer(S, pos[top]);
            lp_addstring(S, data->d_name);
        }
        if (isdir == 0) {
            *lp_prepbuffsize(S, 1) = 0;
            ret = h(S, ud, lp_buffer(S), LP_WALKFILE);
            if (ret < 0) goto out;
        }
        else if (top < LP_MAX_COMPCOUNT-1) {
            size_t len = lp_bufflen(S) + 1;
            lp_addlstring(S, "/", 2);
            ret = h(S, ud, lp_buffer(S), LP_WALKIN);
            if (ret < 0) goto out;
            if (ret != 0) {
                dir[++top] = opendir(lp_buffer(S));
                if (dir[top] == NULL) {
                    ret = lp_pusherror(L, "opendir", lp_buffer(S));
                    goto out;
                }
                pos[top] = len;
            }
        }
        while (top && (ret = lp_readdir(L, dir[top], &data, &buf)) <= 0) {
            if (ret < 0) goto out;
            lp_setbuffer(S, pos[top--]);
            *lp_prepbuffsize(S, 1) = 0;
            ret = h(S, ud, lp_buffer(S), LP_WALKOUT);
            if (ret < 0) goto out;
        }
        isdir = S_ISDIR(buf.st_mode);
    } while (top != 0);
out:while (top) closedir(dir[top--]);
    return ret;
}

static int lpL_getcwd(lua_State *L) {
    lp_State *S = lp_getstate(L);
    char *ret = lp_prepbuffsize(S, PATH_MAX);
    if (getcwd(ret, PATH_MAX) == NULL)
        return -lp_pusherror(L, "getcwd", NULL);
    lua_pushstring(L, ret);
    return 1;
}

static int lpL_binpath(lua_State *L) {
    lp_State *S = lp_getstate(L);
#ifdef __APPLE__
    char *ret = lp_prepbuffsize(S, PROC_PIDPATHINFO_MAXSIZE);
    if (proc_pidpath(getpid(), ret, PROC_PIDPATHINFO_MAXSIZE) < 0)
        return -lp_pusherror(L, "binpath", NULL);
#else
    char *ret = lp_prepbuffsize(S, PATH_MAX);
    if (readlink("/proc/self/exe", ret, PATH_MAX) < 0)
        return -lp_pusherror(L, "binpath", NULL);
#endif
    lua_pushstring(L, ret);
    return 1;
}

static int lpL_tmpdir(lua_State* L) {
    struct stat buf;
    const char *prefix = luaL_optstring(L, 1, "lua_");
    const char *s = "/tmp/", *dir;
    srand(((int)(ptrdiff_t)&L) ^ clock() );
    do {
        int magic = ((unsigned)rand()<<16|rand()) % LP_MAX_TMPNUM;
        lua_settop(L, 2);
        lua_pushfstring(L, "%s%s%d/", s, prefix, magic);
    } while (stat(dir = lua_tostring(L, -1), &buf) == 0);
    if (mkdir(dir, 0777) != 0)
        return -lp_pusherror(L, "tmpdir", dir);
    return 1;
}

static int lp_mkdir(lua_State *L, const char *s) {
    if (mkdir(s, 0777) != 0) {
        if (errno == EEXIST)
            return 1;
        return lp_pusherror(L, "mkdir", s);
    }
    return 1;
}

static int lp_makedirs(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    char *dir, *cur, old;
    int rets;
    if ((rets = lp_normpath(L, s)) < 0) return rets;
    lp_setbuffer(S, 0);
    lp_addstring(S, lua_tostring(L, -1));
    lp_addchar(S, '\0');
    dir = lp_buffer(S);
    cur = dir + (lp_splitdrive(s) - s);
    if (lp_isdirsep(*cur)) ++cur;
    for (old = *cur; old != 0; *cur = old, ++cur) {
        struct stat buf;
        cur = (char*)lp_nextsep(cur);
        old = *cur, *cur = 0;
        if ((stat(dir, &buf) == 0 && S_ISDIR(buf.st_mode))
                || mkdir(dir, 0777) == 0
                || errno == EEXIST)
            continue;
        return lp_pusherror(L, "makedirs", dir);
    }
    return 0;
}

static int rmdir_walker(lp_State *S, void *ud, const char *s, int state) {
    (void)ud;
    if (state == LP_WALKIN)
        return 1;
    if (state == LP_WALKFILE && remove(s) != 0)
        return lp_pusherror(S->L, "removedirs", s);
    else if (state == LP_WALKOUT && rmdir(s) != 0)
        return lp_pusherror(S->L, "removedirs", s);
    return 0;
}

static int unlock_walker(lp_State *S, void *ud, const char *s, int state) {
    struct stat buf;
    (void)ud;
    if (state == LP_WALKIN)
        return 1;
    if (state == LP_WALKFILE
            && stat(s, &buf) != 0
            && chmod(s, buf.st_mode & ~0200) != 0)
        return lp_pusherror(S->L, "unlock", s);
    return 0;
}


/* file utils */

static int lp_remove(lua_State *L, const char *s)
{ return remove(s) == 0 ? 1 : lp_pusherror(L, "remove", s); }

static int lp_cmptime(time_t t1, time_t t2)
{ return t1 < t2 ? -1 : t1 > t2 ? 1 : 0; }

static int lp_exists(lua_State *L, const char *s) {
    struct stat buf;
    lua_pushboolean(L, stat(s, &buf) == 0);
    return 1;
}

static int lp_fsize(lua_State *L, const char *s) {
    struct stat buf;
    if (stat(s, &buf) != 0)
        return lp_pusherror(L, "fsize", s);
    lp_pushuint64(L, buf.st_size);
    return 1;
}

static int lp_ftime(lua_State *L, const char *s) {
    struct stat buf;
    if (stat(s, &buf) != 0)
        return lp_pusherror(L, "ftime", s);
    lp_pushuint64(L, buf.st_ctime);
    lp_pushuint64(L, buf.st_mtime);
    lp_pushuint64(L, buf.st_atime);
    return 3;
}

static int lpL_touch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    struct utimbuf utb, *buf;
    int fh = open(s, O_WRONLY|O_CREAT, 0644);
    if (fh < 0 && errno != EISDIR)
        return -lp_pusherror(L, "touch", s);
    close(fh);
    if (lua_gettop(L) == 1) /* set to current date/time */
        buf = NULL;
    else {
        utb.modtime = (time_t)lp_optuint64(L, 2, time(NULL));
        utb.actime = (time_t)lp_optuint64(L, 3, utb.modtime);
        buf = &utb;
    }
    if (utime(s, buf) != 0)
        return -lp_pusherror(L, "utime", s);
    lp_returnself(L);
}

static int lpL_rename(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    if (rename(from, to) != 0)
        return -lp_pusherror(L, "rename", NULL);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_copy(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int failIfExists = lua_toboolean(L, 3);
    lua_Integer mode = luaL_optinteger(L, 4, 0644);
    char *buf = lp_prepbuffsize(lp_getstate(L), BUFSIZ);
    size_t size;
    int source, dest;
    if ((source = open(from, O_RDONLY, 0)) < 0)
        return -lp_pusherror(L, "open", from);
    if (failIfExists)
        dest = open(to, O_WRONLY|O_CREAT|O_EXCL, mode);
    else
        dest = open(to, O_WRONLY|O_CREAT|O_TRUNC, mode);
    if (dest < 0) return -lp_pusherror(L, "open", to);

    while ((size = read(source, buf, BUFSIZ)) > 0) {
        if (write(dest, buf, size) < 0) {
            close(source);
            close(dest);
            return -lp_pusherror(L, "write", to);
        }
    }
    close(source);
    close(dest);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_cmpftime(lua_State *L) {
    struct stat buf1, buf2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 2);
    int use_atime = lua_toboolean(L, 3);
    int cmp_c, cmp_m, cmp_a;
    if (stat(f1, &buf1) != 0) {
        lua_pushinteger(L, -1);
        return 1;
    }
    if (stat(f2, &buf2) != 0) {
        lua_pushinteger(L, 1);
        return 1;
    }
    cmp_c = lp_cmptime(buf1.st_ctime, buf2.st_ctime);
    cmp_m = lp_cmptime(buf1.st_mtime, buf2.st_mtime);
    cmp_a = lp_cmptime(buf1.st_atime, buf2.st_atime);
    if (use_atime)
        lua_pushinteger(L, cmp_c == 0 ? cmp_m == 0 ? cmp_a : cmp_m : cmp_c);
    else
        lua_pushinteger(L, cmp_c == 0 ? cmp_m : cmp_c);
    return 1;
}

#endif


/* a simple glob implement */

static const char *glob_classend(const char *p) {
    if (*p == '^') ++p;
    while (*++p != '\0' && *p != ']')
        ;
    return *p == ']' ? p+1 : NULL;
}

static int glob_matchclass(int c, const char *p, const char *ec) {
    int sig = 1;
    if (*(p+1) == '^') { sig = 0; p++; }  /* skip the '^' */
    while (++p < ec) {
        int b = *p, e = p[1] != '-' && p+2<ec ? *p : *(p += 2);
        if (b <= c && c <= e)
            return sig;
    }
    return !sig;
}

static int fnmatch(const char *pattern, const char *s, size_t len) {
    const char *s_end = s+len;
    while (*pattern != '\0') {
        const char *ec;
        int i, min = 0, hasmax = 0;
        switch (*pattern) {
        case '*': case '?':
            while (*pattern == '*' || *pattern == '?')
                if (*pattern++ == '?') ++min;
                else                   hasmax = 1;
            if (s_end - s < min)  return 0;
            if (!hasmax) s += min;
            else if (*pattern == '\0') return 1;
            len = (s_end-s) - min;
            for (i = 0; i <= (int)len; ++i)
                if (fnmatch(pattern, s_end-i, i)) return 1;
            return 0;
        case '[':
            if ((ec = glob_classend(pattern + 1)) != NULL) {
                if (!glob_matchclass(*s, pattern, ec))
                    return 0;
                pattern = ec;
                ++s;
                break;
            }
            /* FALLTHOUGH */
        default:
            if (!lp_charequal(*pattern, *s))
                return 0;
            ++pattern, ++s;
        }
    }
    return s == s_end;
}

typedef struct GlobState {
    lua_State  *L;
    const char *pattern;
    size_t      idx;
    size_t      limit;
} GlobState;

static int glob_walker(lp_State *S, void *ud, const char *s, int state) {
    GlobState *gs = (GlobState*)ud;
    (void)S;
    if (lp_iscurdir(s)) {
        s += LP_CURDIRLEN;
        while (lp_isdirsep(*s)) ++s;
    }
    if (state == LP_WALKIN) {
        if (gs->limit == 0) return 0;
        --gs->limit;
    }
    else if (state == LP_WALKOUT)
        ++gs->limit;
    if (state != LP_WALKOUT && fnmatch(gs->pattern, s, strlen(s))) {
        lua_pushstring(gs->L, s);
        lua_rawseti(gs->L, 3, gs->idx++);
    }
    return 1;
}

static int lpL_glob(lua_State *L) {
    GlobState gs;
    const char *p = luaL_checkstring(L, 1);
    lua_Integer l = luaL_optinteger(L, 4, -1);
    int rets;
    lua_settop(L, 3);
    if ((rets = lp_normpath(L, luaL_optstring(L, 2, ""))) < 0)
        return -rets;
    lua_replace(L, 2);
    if (lua_istable(L, 3))
        gs.idx = (size_t)lua_rawlen(L, 3) + 1;
    else {
        lua_newtable(L);
        lua_replace(L, 3);
        gs.idx = 1;
    }
    gs.L = L;
    gs.pattern = p;
    gs.limit   = (int)l;
    rets = lp_walkpath(L, lua_tostring(L, 2), glob_walker, &gs);
    return rets > 0 ? 1 : -rets;
}

static int lpL_fnmatch(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    const char *pattern = luaL_checkstring(L, 2);
    lua_pushboolean(L, fnmatch(pattern, s, len));
    return 1;
}


/* dir utils */

static int lpL_compiter(lua_State *L) {
    size_t len, idx = (size_t)lua_tointeger(L, 2);
    const char *d = lua_tolstring(L, 1, &len);
    const char *p = lp_splitdrive(d), *next;
    int isabs = lp_isdirsep(*p);
    if (idx == 0) {
        if (d == p && !isabs)
            p = lp_nextsep(p);
        lua_pushinteger(L, 1);
        lua_pushlstring(L, d, p == d ? 1 : p-d);
        return 2;
    } else if (idx == 1 && d != p && isabs) {
        lua_pushinteger(L, p-d + 1);
        lua_pushstring(L, LP_DIRSEP);
        return 2;
    } else if (idx <= len && ((next = d + (idx-1)) < p
                || lp_isdirsep(*next)
                || *(next = lp_nextsep(next)) != '\0')) {
        next = next < p ? p : next + 1;
        lua_pushinteger(L, next - d + 1);
        lua_pushlstring(L, next, idx = lp_nextsep(next) - next);
        return idx == 0 ? 0 : 2;
    }
    return 0;
}

static int lp_itercomp(lua_State *L, const char *s) {
    lua_pushcfunction(L, lpL_compiter);
    if (lp_normpath(L, s) < 0)
        lua_error(L);
    return 2;
}

static int lpL_walkiter(lua_State *L) {
    /* stack: stack_table: { path, iter, dirdata } */
    int nrets, top = (int)lua_rawlen(L, 1);
    if (top == 0) return 0;
    lua_settop(L, 1);
    lua_rawgeti(L, 1, top-2); /* 2 */
    lua_rawgeti(L, 1, top-1); /* 3 */
    lua_rawgeti(L, 1, top); /* 4 */
    lua_call(L, 1, LUA_MULTRET); /* 3,4->? */
    if ((nrets = lua_gettop(L) - 2) == 0) {
        lua_pushstring(L, "out"); /* 3 */
        lua_settop(L, 6); /* 4,5,6 */
        lua_rawseti(L, 1, top-2); /* 6->1 */
        lua_rawseti(L, 1, top-1); /* 5->1 */
        lua_rawseti(L, 1, top); /* 4->1 */
        return top == 3 ? 0 : 2; /* out from dir */
    }
    /* the second return value is "file" or "dir" */
    if (*lua_tostring(L, 4) == 'f') {
        lua_pushfstring(L, "%s%s", lua_tostring(L, 2), lua_tostring(L, 3));
        lua_replace(L, 3);
        return nrets;
    }
    lua_pushfstring(L, "%s%s" LP_DIRSEP,
            lua_tostring(L, 2),
            lua_tostring(L, 3));
    lua_replace(L, 3);
    lua_pushstring(L, "in");
    lua_replace(L, 4);
    lp_dir(L, lua_tostring(L, 3));
    lua_insert(L, -2);
    lua_pushvalue(L, 3);
    lua_rawseti(L, 1, top+1);
    lua_rawseti(L, 1, top+2);
    lua_rawseti(L, 1, top+3);
    return nrets;
}

static int lp_walk(lua_State *L, const char *s) {
    size_t len;
    lp_normpath(L, s); /* 1 */
    lua_pushcfunction(L, lpL_walkiter); /* 2 */
    lua_createtable(L, 3, 0); /* 3 */
    s = lua_tolstring(L, -3, &len);
    if (lp_iscurdir(s)) {
        s   += LP_CURDIRLEN + 1;
        len -= LP_CURDIRLEN + 1;
    }
    if (*s == '\0')
        lua_pushstring(L, ""); /* 4 */
    else if (s[len-1] == LP_DIRSEPCHAR)
        lua_pushvalue(L, -3); /* 1->4 */
    else
        lua_pushfstring(L, "%s" LP_DIRSEP, s); /* 4 */
    lp_dir(L, lua_tostring(L, -4)); /* ... path iter table path iter dirdata */
    lua_rawseti(L, -4, 3);
    lua_rawseti(L, -3, 2);
    lua_rawseti(L, -2, 1);
    return 2;
}

static int lpL_rel(lua_State *L) {
    const char *fn = luaL_checkstring(L, 1);
    const char *path = luaL_checkstring(L, 2);
    int rets;
    if ((rets = lp_normpath(L, fn)) < 0 || (rets = lp_normpath(L, path)) < 0)
        return -rets;
    return lp_relpath(L, lua_tostring(L, -2), lua_tostring(L, -1));
}


/* routines has signature: function(path...) -> ... */

static int lp_join(lua_State *L, const char *s)
{ return lp_normpath(L, s); }

static int lp_removedirs(lua_State *L, const char *s)
{ return lp_walkpath(L, s, rmdir_walker, NULL); }

static int lp_unlock(lua_State *L, const char *s)
{ return lp_walkpath(L, s, unlock_walker, NULL); }

#define LP_PATH_ROUTINES(ENTRY)\
    ENTRY(abs)                 \
    ENTRY(isabs)               \
    ENTRY(itercomp)            \
    ENTRY(join)                \

#define LP_PATH_FS_ROUTINES(ENTRY)\
    ENTRY(chdir)               \
    ENTRY(dir)                 \
    ENTRY(exists)              \
    ENTRY(expandvars)          \
    ENTRY(fsize)               \
    ENTRY(ftime)               \
    ENTRY(unlock)              \
    ENTRY(makedirs)            \
    ENTRY(mkdir)               \
    ENTRY(realpath)            \
    ENTRY(remove)              \
    ENTRY(removedirs)          \
    ENTRY(rmdir)               \
    ENTRY(type)                \
    ENTRY(walk)                \

static const char *lp_checkpathargs(lua_State *L) {
    int top = lua_gettop(L);
    if (top == 0) {
        lua_pushstring(L, "." LP_DIRSEP);
        return "." LP_DIRSEP;
    }
    else if (top == 1)
        return luaL_checkstring(L, 1);
    lpL_joinpath(L);
    return lua_tostring(L, 1);
}

#define ENTRY(name)                                 \
    static int lpL_##name(lua_State *L) {           \
        const char *s = lp_checkpathargs(L);        \
        int ret = lp_##name(L, s);                  \
        if (ret == 0) lp_returnself(L);             \
        return ret < 0 ? -ret : ret;                }
LP_PATH_ROUTINES(ENTRY)
LP_PATH_FS_ROUTINES(ENTRY)
#undef ENTRY

static int lpL_split(lua_State *L) {
    const char *s = lp_checkpathargs(L);
    const char *b = lp_splitpath(s);
    lua_pushlstring(L, s, b-s);
    lua_pushstring(L, b);
    return 2;
}

static int lpL_splitdrive(lua_State *L) {
    const char *d = lp_checkpathargs(L);
    const char *p = lp_splitdrive(d);
    lua_pushlstring(L, d, p-d);
    lua_pushstring(L, p);
    return 2;
}

static int lpL_splitext(lua_State *L) {
    const char *s = lp_checkpathargs(L);
    const char *e = lp_splitext(s);
    lua_pushlstring(L, s, e-s);
    lua_pushstring(L, e);
    return 2;
}


/* register functions */

static int lpL_libcall(lua_State *L) {
    int ret;
    lua_remove(L, 1);
    if (lua_gettop(L) == 0)
        luaL_argerror(L, 1, "string expected");
    lpL_joinpath(L);
    return (ret = lp_normpath(L, lua_tostring(L, 1))) < 0 ? -ret : ret;
}

LUAMOD_API int luaopen_path(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(n) { #n, lpL_##n },
        LP_PATH_ROUTINES(ENTRY)
        ENTRY(ansi)
        ENTRY(rel)
        ENTRY(split)
        ENTRY(splitdrive)
        ENTRY(splitext)
        ENTRY(utf8)
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, lpL_libcall);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);
    return 1;
}

LUAMOD_API int luaopen_path_fs(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(n) { #n, lpL_##n },
        LP_PATH_FS_ROUTINES(ENTRY)
        ENTRY(binpath)
        ENTRY(cmpftime)
        ENTRY(copy)
        ENTRY(fnmatch)
        ENTRY(getcwd)
        ENTRY(getenv)
        ENTRY(glob)
        ENTRY(platform)
        ENTRY(rename)
        ENTRY(setenv)
        ENTRY(tmpdir)
        ENTRY(touch)
#undef  ENTRY
        { NULL, NULL }
    };
    luaL_newlib(L, libs);
    return 1;
}

LUAMOD_API int luaopen_path_info(lua_State *L) {
    struct {
        const char *name;
        const char *value;
    } values[] = {
        { "altsep",   LP_ALTSEP   },
        { "curdir",   LP_CURDIR   },
        { "devnull",  LP_DEVNULL  },
        { "extsep",   LP_EXTSEP   },
        { "pardir",   LP_PARDIR   },
        { "pathsep",  LP_PATHSEP  },
        { "platform", LP_PLATFORM },
        { "sep",      LP_DIRSEP   },
        { "version",  LP_VERSION  },
        { NULL, NULL }
    }, *p = values;
    lua_createtable(L, 0, sizeof(values)/sizeof(values[0])-1);
    for (; p->name != NULL; ++p) {
        lua_pushstring(L, p->name);
        lua_pushstring(L, p->value);
        lua_rawset(L, -3);
    }
    return 1;
}


LP_NS_END

/* cc: flags+='-Wextra --coverage' run='lua test.lua'
 * unixcc: flags+='-s -O3 -shared -fPIC' output='path.so'
 * win32cc: flags+='-s -O3 -mdll -DLUA_BUILD_AS_DLL'
 * win32cc: libs+='-llua53' output='path.dll' */

