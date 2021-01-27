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
#ifndef lua_rawsetp
#define lua_rawsetp lua_rawsetp
static void lua_rawsetp(lua_State *L, int idx, const void *p)
{ lua_pushlightuserdata(L, (void*)p); lua_insert(L, -2); lua_rawset(L, idx); }
#endif
#endif

#ifndef LUAMOD_API
# define LUAMOD_API LUALIB_API
#endif

#define LP_VERSION "path 0.2"


/* array routines */

#define ARRAY_MIN_LEN (4)
#define ARRAY_MAX_LEN (~(unsigned)0 - 100)

#define array_init(A) ((A) = NULL)
#define array_free(A) array_resize_((void**)&(A), 0, sizeof(*(A)))

#define array_(A) ((ArrayInfo*)(A)-1)

#define array_info(A) ((A) ? array_(A) : NULL)
#define array_len(A)  ((A) ? array_(A)->len : 0u)
#define array_cap(A)  ((A) ? array_(A)->cap : 0u)
#define array_end(A)  ((A) ? &(A)[array_len(A)] : NULL)

#define array_setlen(A,n) ((A) ? array_(A)->len  = (unsigned)(n) : 0u)
#define array_addlen(A,n) ((A) ? array_(A)->len += (unsigned)(n) : 0u)
#define array_sublen(A,n) (assert(array_len(A) >= n), array_(A)->len-=(n))

#define array_reset(A)    array_setlen(A, 0)
#define array_resize(A,n) array_resize_((void**)&(A),(n),sizeof(*(A)))
#define array_grow(A,n) (array_grow_((void**)&(A),(n),sizeof(*(A))) ?\
                         array_end(A) : NULL)

typedef struct ArrayInfo { unsigned len, cap; } ArrayInfo;

static int array_resize_(void **pA, size_t cap, size_t objlen) {
    ArrayInfo *AI, *oldAI = (assert(pA), array_info(*pA));
    if (cap == 0) { free(oldAI); array_init(*pA); return 1; }
    AI = (ArrayInfo*)realloc(oldAI, sizeof(ArrayInfo) + cap*objlen);
    if (AI == NULL) return 0;
    if (!oldAI) AI->cap = AI->len = 0;
    if (AI->cap > cap) AI->cap = (unsigned)cap;
    AI->cap = (unsigned)cap;
    *pA = (void*)(AI + 1);
    return 1;
}
 
static int array_grow_(void **pA, size_t len, size_t objlen) {
    size_t cap = array_cap(*pA);
    size_t exp = array_len(*pA) + len;
    if (cap < exp) {
        size_t newcap = ARRAY_MIN_LEN;
        while (newcap < ARRAY_MAX_LEN/objlen && newcap < exp)
            newcap += newcap;
        return newcap < exp ? 0 : array_resize_(pA, newcap, objlen);
    }
    return 1;
}


/* state */

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
    char      *buf;
#ifdef _WIN32
    wchar_t   *wbuf;
#endif
    /* code page */
    int        current_cp;
};

static int lpL_delstate(lua_State *L) {
    lp_State *S = (lp_State*)lua_touserdata(L, 1);
    if (S != NULL) {
        array_free(S->buf);
#ifdef _WIN32
        array_free(S->wbuf);
#endif
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
        lua_rawsetp(L, LUA_REGISTRYINDEX, LP_STATE_KEY);
    }
    lua_pop(L, 1);
    S->L = L;
    array_reset(S->buf);
#ifdef _WIN32
    array_reset(S->wbuf);
#endif
    return S;
}

static char *lp_prepbuffsize(lp_State *S, size_t len) {
    char *buf = array_grow(S->buf, len);
    if (buf == NULL) luaL_error(S->L, "out of memory");
    return buf;
}

static int lp_addchar(lp_State *S, int ch)
{ return *lp_prepbuffsize(S, 1) = ch, array_addlen(S->buf, 1); }

static int lp_addlstring(lp_State *S, const char *s, size_t len)
{ return memcpy(lp_prepbuffsize(S, len), s, len), array_addlen(S->buf, len); }

static int lp_addstring(lp_State *S, const char *s)
{ return lp_addlstring(S, s, strlen(s)); }

static int lp_pushresult(lp_State *S)
{ return lua_pushlstring(S->L, S->buf, array_len(S->buf)), 1; }


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
    if (s && lp_isdirsep(s[0]) && lp_isdirsep(s[1])
            && s[2] == '?' && lp_isdirsep(s[3]))
        return s + 4;
#endif
    return s;
}

static const char *lp_splitdrive(const char *s) {
#ifdef _WIN32
    const char *os = s, *split = s, *mp;
retry:
    if (s && lp_isdirsep(s[0]) && lp_isdirsep(s[1]) && !lp_isdirsep(s[2])) {
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
    } else if (s && s[1] == ':')
        split = s + 2;
    return split;
#else
    return s;
#endif
}

static const char *lp_splitpath(const char *s) {
    const char *p;
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
    unsigned top = 0, pos[LP_MAX_COMPCOUNT];
    while (s < path) lp_addchar(S, lp_normchar(*s++));
    if (isabs) lp_addchar(S, lp_normchar(*s++));
    pos[top++] = array_len(S->buf) + isabs;
    while (*s != '\0') {
        const char *e;
        while (lp_isdirsep(*s)) ++s;
        if (lp_iscurdir(s))
            s += LP_CURDIRLEN;
        else if (lp_ispardir(s)) {
            s += LP_PARDIRLEN;
            if (top > 1)
                array_setlen(S->buf, pos[--top]);
            else if (isabs)
                array_setlen(S->buf, pos[top++]);
            else {
                lp_addlstring(S, LP_PARDIR, LP_PARDIRLEN);
                lp_addchar(S, LP_DIRSEPCHAR);
            }
        } else if (*(e = lp_nextsep(s)) == '\0') {
            lp_addstring(S, s);
            break;
        } else if (top >= LP_MAX_COMPCOUNT) {
            lua_pushnil(S->L);
            lua_pushstring(S->L, "path too complicate");
            return -2;
        } else {
            pos[top++] = array_len(S->buf);
            lp_addlstring(S, s, e-s);
            lp_addchar(S, LP_DIRSEPCHAR);
            s = e + 1;
        }
    }
    if (array_len(S->buf)) return lp_pushresult(S);
    lua_pushstring(L, LP_CURDIR LP_DIRSEP);
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
        const char *s = S->buf;
        const char *rd = lp_drivehead(luaL_checkstring(L, i));
        const char *d = rd;
        if (!lp_driveequal(&s, &d) || lp_isdirsep(d[0]))
            array_reset(S->buf);
        lp_addstring(S, rd);
        if (d[0] != '\0' && i != top)
            lp_addchar(S, LP_DIRSEPCHAR);
    }
    lua_settop(L, 0);
    return lp_pushresult(S);
}


/* system specfied utils */

#ifdef _WIN32

# define WIN32_LEAN_AND_MEAN
# include <Windows.h>

#define LP_PLATFORM      "windows"

#define lp_pusherror(L, t, f) lp_pusherrmsg((L), GetLastError(), (t), (f))

static int lp_pusherrmsg(lua_State *L, DWORD err, const char *title, const char *fn);

static LPWSTR lp_prepwbuffsize(lp_State *S, size_t len) {
    LPWSTR buf = array_grow(S->wbuf, len);
    if (buf == NULL) luaL_error(S->L, "out of memory");
    return buf;
}

static int lp_addlwstring(lp_State *S, LPCWSTR s, size_t len) {
    memcpy(lp_prepwbuffsize(S, len), s, len*sizeof(wchar_t));
    return array_addlen(S->wbuf, len);
}

static LPWSTR lp_addl2wstring(lp_State *S, LPCSTR s, size_t bc) {
    int cp = S->current_cp;
    int size = ((int)bc+1);
    LPWSTR ws = lp_prepwbuffsize(S, size);
    int wc = MultiByteToWideChar(cp, 0, s, (int)bc+1, ws, size);
    if (wc > (int)bc) {
        ws = lp_prepwbuffsize(S, size = (wc+1));
        wc = MultiByteToWideChar(cp, 0, s, (int)bc+1, ws, size);
    }
    if (wc == 0) {
        lp_pusherror(S->L, "unicode", NULL);
        lua_error(S->L);
    }
    array_addlen(S->wbuf, wc-1);
    return ws;
}

static LPWSTR lp_add2wstring(lp_State *S, LPCSTR s) {
    int bc = (int)strlen(s = (s ? s : S->buf));
    return lp_addl2wstring(S, s, bc);
}

static LPWSTR lp_addlwpath(lp_State *S, LPCSTR s, size_t bc) {
    int top;
    if (bc < MAX_PATH) return lp_addl2wstring(S, s, bc);
    top = (int)array_len(S->buf);
    lp_addlwstring(S, L"\\\\?\\", 4);
    lp_addl2wstring(S, s, bc);
    return &S->wbuf[top];
}

static LPWSTR lp_addwpath(lp_State *S, LPCSTR s) {
    int bc = (int)strlen(s = (s ? s : S->buf));
    return lp_addlwpath(S, s, bc);
}

static char *lp_addlw2string(lp_State *S, LPCWSTR ws, int wc) {
    int cp = S->current_cp, size = (wc + 1) * 3;
    char *s = lp_prepbuffsize(S, size);
    int bc = WideCharToMultiByte(cp, 0, ws, wc+1, s, size, NULL, NULL);
    if (bc > size) {
        s = lp_prepbuffsize(S, size = bc+1);
        bc = WideCharToMultiByte(cp, 0, ws, wc+1, s, size, NULL, NULL);
    }
    if (bc == 0) {
        lp_pusherror(S->L, "multibyte", NULL);
        lua_error(S->L);
    }
    array_addlen(S->buf, bc-1);
    return s;
}

static char *lp_addw2string(lp_State *S, LPCWSTR ws) {
    int wc = (int)wcslen(ws = (ws ? ws : S->wbuf));
    return lp_addlw2string(S, ws, wc);
}

static const char *lp_win32error(lua_State *L, DWORD errnum) {
    lp_State *S = lp_getstate(L);
    const char *ret = NULL;
    LPWSTR msg = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        errnum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg,
        0, NULL);
    if (len == 0)
        ret = "get system error message error";
    if ((ret = array_grow(S->buf, (size_t)len*3)) == NULL)
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
            array_addlen(S->buf, bc);
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
    size_t len;
    const char *utf8;
    switch (lua_type(L, 1)) {
    case LUA_TNONE:
    case LUA_TNIL:
    case LUA_TNUMBER:
        S->current_cp = (UINT)lua_tonumber(L, 1);
        return 0;
    case LUA_TSTRING:
        utf8 = lua_tolstring(L, 1, &len);
        S->current_cp = CP_UTF8;
        lp_addl2wstring(S, utf8, len);
        S->current_cp = cp;
        lua_pushstring(L, lp_addw2string(S, NULL));
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
        lp_add2wstring(S, ansi);
        S->current_cp = CP_UTF8;
        lua_pushstring(L, lp_addw2string(S, NULL));
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
    size_t klen, vlen;
    const char *name = luaL_checklstring(L, 1, &klen);
    const char *value = luaL_optlstring(L, 2, NULL, &vlen);
    lp_State *S = lp_getstate(L);
    LPWSTR wvalue;
    lp_addl2wstring(S, name, klen + 1);
    wvalue = value ? lp_addl2wstring(S, value, vlen) : NULL;
    if (!SetEnvironmentVariableW(S->wbuf, wvalue))
        return -lp_pusherror(L, "setenv", NULL);
    lua_settop(L, 2);
    return 1;
}

static int lpL_getenv(lua_State *L) {
    lp_State *S = lp_getstate(L);
    LPWSTR ws  = lp_add2wstring(S, luaL_checkstring(L, 1));
    LPWSTR ret = lp_prepwbuffsize(S, MAX_PATH);
    DWORD  wc  = GetEnvironmentVariableW(ws, ret, MAX_PATH);
    if (wc >= MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = GetEnvironmentVariableW(ws, ret, wc);
    }
    if (wc == 0)
        return -lp_pusherror(S->L, "getenv", NULL);
    array_addlen(S->wbuf, wc+1);
    lua_pushstring(L, lp_addw2string(S, ret));
    return 1;
}

static int lp_expandvars(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    LPWSTR ws  = lp_add2wstring(S, s);
    LPWSTR ret = lp_prepwbuffsize(S, MAX_PATH);
    DWORD  wc  = ExpandEnvironmentStringsW(ws, ret, MAX_PATH);
    if (wc >= MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = ExpandEnvironmentStringsW(ws, ret, wc);
    }
    if (wc == 0) return lp_pusherror(L, "expandvars", s);
    array_addlen(S->wbuf, wc);
    lua_pushstring(L, lp_addw2string(S, ret));
    return 1;
}

static int lp_readreg(lp_State *S, HKEY hkey, LPCWSTR key) {
    DWORD size = MAX_PATH, ret;
    LPWSTR wc = (array_reset(S->wbuf), lp_prepwbuffsize(S, size));
    while ((ret = RegQueryValueExW(hkey, key, NULL, NULL, (LPBYTE)wc, &size))
            != ERROR_SUCCESS) {
        if (ret != ERROR_MORE_DATA) {
            lua_pushstring(S->L, lp_addw2string(S, key));
            return lp_pusherrmsg(S->L, ret, "platform", lua_tostring(S->L, -1));
        }
        wc = lp_prepwbuffsize(S, size);
    }
    array_addlen(S->wbuf, size);
    lp_addlwstring(S, L"", 1);
    return 0;
}

static int lpL_platform(lua_State *L) {
    lp_State *S = lp_getstate(L);
    LPWSTR dot, root = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    DWORD major = 0, minor = 0, build, size = sizeof(DWORD);
    HKEY hkey;
    int ret;
    if ((ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    root, 0, KEY_QUERY_VALUE, &hkey)) != ERROR_SUCCESS)
        return -lp_pusherrmsg(L, ret, "platform", NULL);
    if ((ret = lp_readreg(S, hkey, L"CurrentBuildNumber")) < 0)
    { ret = -ret; goto out; }
    build = wcstoul(S->wbuf, NULL, 10);
    if (RegQueryValueExW(hkey, L"CurrentMajorVersionNumber",
                NULL, NULL, (LPBYTE)&major, &size) != ERROR_SUCCESS
            || RegQueryValueExW(hkey, L"CurrentMinorVersionNumber",
                NULL, NULL, (LPBYTE)&minor, &size) != ERROR_SUCCESS) {
        if ((ret = lp_readreg(S, hkey, L"CurrentVersion")) < 0)
        { ret = -ret; goto out; }
        major = wcstoul(S->wbuf, &dot, 10);
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

typedef DWORD (WINAPI *LPGETFINALPATHNAMEBYHANDLEW)(
        HANDLE hFile,
        LPWSTR lpszFilePath,
        DWORD cchFilePath,
        DWORD dwFlags);

static LPGETFINALPATHNAMEBYHANDLEW pGetFinalPathNameByHandleW;

static int lp_abs(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    LPWSTR ws  = lp_addwpath(S, s);
    LPWSTR ret = lp_prepwbuffsize(S, MAX_PATH);
    DWORD  wc  = GetFullPathNameW(ws, MAX_PATH, ret, NULL);
    if (wc >= MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = GetFullPathNameW(ws, wc, ret, NULL);
    }
    if (wc == 0) return lp_pusherror(L, "abs", s);
    array_setlen(S, wc+1);
    lua_pushstring(L, lp_addw2string(S, ret));
    return 1;
}

static int lp_solvelink(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    LPWSTR ret, ws = lp_addwpath(S, s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            0,                     /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            FILE_FLAG_BACKUP_SEMANTICS, /* no file attributes   */
            NULL);                 /* no attr. template    */
    DWORD wc;
    if(hFile == INVALID_HANDLE_VALUE)
        return lp_pusherror(L, "open", s);
    ret = lp_prepwbuffsize(S, MAX_PATH);
    wc = pGetFinalPathNameByHandleW(hFile, ret, MAX_PATH, 0);
    if (wc >= MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = pGetFinalPathNameByHandleW(hFile, ret, wc, 0);
    }
    CloseHandle(hFile);
    if (wc == 0) return lp_pusherror(L, "realpath", s);
    array_addlen(S->wbuf, wc + 1);
    if (wc > MAX_PATH + 4)
        lua_pushstring(L, lp_addw2string(S, ret));
    else
        lua_pushstring(L, lp_addw2string(S, ret+4));
    return 1;
}

static int lp_realpath(lua_State *L, const char *s) {
    if (!pGetFinalPathNameByHandleW) {
        HMODULE hModule = GetModuleHandleA("KERNEL32.dll");
        if (hModule != NULL) {
            union { LPGETFINALPATHNAMEBYHANDLEW f;
                    FARPROC                     v; } u;
            u.v = GetProcAddress(hModule, "GetFinalPathNameByHandleW");
            pGetFinalPathNameByHandleW = u.f;
        }
    }
    if (pGetFinalPathNameByHandleW == NULL)
        return lp_abs(L, s);
    return lp_solvelink(L, s);
}

static int lp_type(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
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
    fn = lp_addw2string(d->S, d->wfd.cFileName);
    while (lp_iscurdir(fn) || lp_ispardir(fn)) {
        if (!FindNextFileW(d->hFile, &d->wfd))
        { d->err = GetLastError(); goto retry; }
        fn = lp_addw2string(d->S, d->wfd.cFileName);
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
    lp_addwpath(S, s);
    lp_addlwstring(S, L"\\*", 3);
    d = (lp_DirData*)lua_newuserdata(L, sizeof(lp_DirData));
    d->hFile = FindFirstFileW(S->wbuf, &d->wfd);
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
    DWORD err, attr = GetFileAttributesW(lp_add2wstring(S, s));
    WIN32_FIND_DATAW wfd;
    int ret = 0, top = 0;
    size_t pos[LP_MAX_COMPCOUNT];
    HANDLE hFile[LP_MAX_COMPCOUNT];
    LPWSTR path;
    if (*s != '\0' && attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (*s != '\0' && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return h(S, ud, s, LP_WALKFILE);
    wfd.cFileName[0] = 0;
    wfd.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    if (array_len(S->wbuf) > 0) {
        path = array_end(S->wbuf);
        while (--path, lp_isdirsep(*path)) array_sublen(S->wbuf, 1);
    }
    pos[0] = array_len(S->wbuf);
    do {
        size_t len = wcslen(wfd.cFileName);
        array_setlen(S->wbuf, pos[top]);
        lp_addlwstring(S, wfd.cFileName, len + 1);
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            ret = h(S, ud, lp_addw2string(S, NULL), LP_WALKFILE);
            if (ret < 0) break;
        } else if (top < LP_MAX_COMPCOUNT-1
                    && wcscmp(wfd.cFileName, L".")
                    && wcscmp(wfd.cFileName, L".."))
        {
            ret = h(S, ud, lp_addw2string(S, NULL), LP_WALKIN);
            if (ret < 0) break;
            if (ret != 0) {
                array_setlen(S->wbuf, pos[top] + len);
                lp_addlwstring(S, L"\\*", 3);
                hFile[++top] = S->wbuf ?
                    FindFirstFileW(S->wbuf, &wfd) : INVALID_HANDLE_VALUE;
                if (hFile[top] == INVALID_HANDLE_VALUE) {
                    err = GetLastError();
                    lua_pushstring(L, lp_addw2string(S, NULL));
                    ret = lp_pusherrmsg(L, err, "findfile1", lua_tostring(L, -1));
                    break;
                }
                pos[top] = array_len(S->wbuf) - 2;
                continue;
            }
        }
        for (; top != 0 && !FindNextFileW(hFile[top], &wfd); --top) {
            if ((err = GetLastError()) != ERROR_NO_MORE_FILES) {
                lua_pushstring(L, lp_addw2string(S, NULL));
                ret = lp_pusherrmsg(L, err, "findfile2", lua_tostring(L, -1));
                goto out;
            }
            FindClose(hFile[top]);
            array_setlen(S->wbuf, pos[top]);
            lp_addlwstring(S, L"", 1);
            ret = h(S, ud, lp_addw2string(S, NULL), LP_WALKOUT);
            if (ret < 0) goto out;
        }
    } while (top);
out:while (top) FindClose(hFile[top--]);
    return ret;
}

static int lpL_getcwd(lua_State *L) {
    lp_State *S = lp_getstate(L);
    LPWSTR ret = lp_prepwbuffsize(S, MAX_PATH);
    DWORD  wc  = GetCurrentDirectoryW(MAX_PATH, ret);
    if (wc >= MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = GetCurrentDirectoryW(wc, ret);
    }
    if (wc == 0) return lp_pusherror(L, "getcwd", NULL);
    array_setlen(S->wbuf, wc + 1);
    lua_pushstring(L, lp_addw2string(S, ret));
    return 1;
}

static int lpL_binpath(lua_State *L) {
    lp_State *S = lp_getstate(L);
    LPWSTR ret = lp_prepwbuffsize(S, MAX_PATH);
    DWORD  wc  = GetModuleFileNameW(NULL, ret, MAX_PATH);
    if (wc > MAX_PATH) {
        ret = lp_prepwbuffsize(S, wc);
        wc = GetModuleFileNameW(NULL, ret, wc);
    }
    if (wc == 0) return lp_pusherror(L, "binpath", NULL);
    array_setlen(S->wbuf, wc + 1);
    lua_pushstring(L, lp_addw2string(S, ret));
    return 1;
}

static int lpL_tmpdir(lua_State* L) {
    size_t len;
    const char *s, *prefix = luaL_optstring(L, 1, "lua_");
    lp_State *S = lp_getstate(L);
    DWORD wc = GetTempPathW(MAX_PATH, lp_prepwbuffsize(S, MAX_PATH));
    if (wc > MAX_PATH) wc = GetTempPathW(wc, lp_prepwbuffsize(S, wc));
    if (wc == 0) return -lp_pusherror(L, "tmpdir", NULL);
    array_addlen(S->wbuf, wc);
    lua_pushstring(L, lp_addw2string(S, NULL));
    s = lua_tolstring(L, -1, &len);
    srand(((int)(ptrdiff_t)&L) ^ clock());
    do {
        int magic = ((unsigned)rand()<<16|rand()) % LP_MAX_TMPNUM;
        const char *fmt = lp_isdirsep(s[len-1]) ?
            "%s%s%d" LP_DIRSEP : "%s" LP_DIRSEP "%s%d" LP_DIRSEP;
        lua_settop(L, 3);
        lua_pushfstring(L, fmt, s, prefix, magic);
        array_reset(S->wbuf);
    } while (GetFileAttributesW(lp_addwpath(S, lua_tostring(L, -1))) !=
            INVALID_FILE_ATTRIBUTES);
    assert(S->wbuf != NULL);
    if (S->wbuf == NULL || !CreateDirectoryW(S->wbuf, NULL))
        return -lp_pusherror(L, "tmpdir", lua_tostring(L, -1));
    return 1;
}

static int lp_chdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
    if (!SetCurrentDirectoryW(ws))
        return lp_pusherror(L, "chdir", s);
    return 0;
}

static int lp_mkdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
    if (!CreateDirectoryW(ws, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            lp_returnself(L);
        return lp_pusherrmsg(L, err, "mkdir", s);
    }
    return 1;
}

static int lp_rmdir(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
    if (!RemoveDirectoryW(ws))
        return lp_pusherror(L, "rmdir", s);
    return 0;
}

static int lp_makedirs(lua_State *L, const char *s) {
    lp_State *S = lp_getstate(L);
    wchar_t *ws, *cur, old;
    DWORD err;
    int rets;
    if ((rets = lp_normpath(L, s)) < 0) return rets;
    array_reset(S->buf);
    ws = lp_addwpath(S, s = lua_tostring(L, -1));
    cur = ws + (lp_splitdrive(s) - s);
    if (lp_isdirsep(*cur)) ++cur;
    for (old = *cur; old != 0; *cur = old, ++cur) {
        while (*cur != 0 && !lp_isdirsep(*cur))
            cur++;
        old = *cur, *cur = 0;
        if (CreateDirectoryW(ws, NULL)
                || (err = GetLastError()) == ERROR_ALREADY_EXISTS)
            continue;
        lua_pushstring(L, lp_addw2string(S, ws));
        return lp_pusherrmsg(L, err, "makedirs", lua_tostring(L, -1));
    }
    return 0;
}

static int rmdir_walker(lp_State *S, void *ud, const char *s, int state) {
    LPCWSTR ws;
    (void)ud;
    array_reset(S->wbuf);
    ws = lp_add2wstring(S, s);
    if (state == LP_WALKIN)
        return 1;
    if (state == LP_WALKFILE && !DeleteFileW(ws))
        return lp_pusherror(S->L, "removedirs", s);
    else if (state == LP_WALKOUT && !RemoveDirectoryW(ws))
        return lp_pusherror(S->L, "removedirs", s);
    return 0;
}

static int unlock_walker(lp_State *S, void *ud, const char *s, int state) {
    LPCWSTR ws = lp_add2wstring(S, s);
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
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            FILE_WRITE_ATTRIBUTES, /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            FILE_FLAG_BACKUP_SEMANTICS, /* open directory also */
            NULL);                 /* no attr. template    */
    lua_pushboolean(L, hFile != INVALID_HANDLE_VALUE);
    CloseHandle(hFile);
    return 1;
}

static int lp_remove(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
    return DeleteFileW(ws) ? 1 : lp_pusherror(L, "remove", s);
}

static int lp_fsize(lua_State *L, const char *s) {
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
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
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
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
    LPCWSTR ws = lp_addwpath(lp_getstate(L), s);
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
    size_t flen, tlen;
    const char *from = luaL_checklstring(L, 1, &flen);
    const char *to = luaL_checklstring(L, 2, &tlen);
    LPWSTR wto = (lp_addlwpath(S, from, flen+1), lp_addlwpath(S, to, tlen));
    if (!MoveFileW(S->wbuf, wto))
        return -lp_pusherror(L, "rename", to);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_copy(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t flen, tlen;
    const char *from = luaL_checklstring(L, 1, &flen);
    const char *to = luaL_checklstring(L, 2, &tlen);
    int failIfExists = lua_toboolean(L, 3);
    LPWSTR wto = (lp_addlwpath(S, from, flen+1), lp_addlwpath(S, to, tlen));
    if (!CopyFileW(S->wbuf, wto, failIfExists))
        return -lp_pusherror(L, "copy", to);
    lua_pushboolean(L, 1);
    return 1;
}

static int lpL_cmpftime(lua_State *L) {
    lp_State *S = lp_getstate(L);
    WIN32_FILE_ATTRIBUTE_DATA fad1, fad2;
    int use_atime = lua_toboolean(L, 3);
    size_t len1, len2;
    const char *f1 = luaL_checklstring(L, 1, &len1);
    const char *f2 = luaL_checklstring(L, 2, &len2);
    LONG cmp_c, cmp_m, cmp_a;
    LPWSTR wf2 = (lp_addlwpath(S, f1, len1 + 1), lp_addlwpath(S, f2, len2));
    if (!GetFileAttributesExW(S->wbuf, GetFileExInfoStandard, &fad1))
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

static char *lp_truncbuffer(lp_State *S)
{ *lp_prepbuffsize(S, 1) = 0; return S->buf; }

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
    for (i = 0; i < (int)p.we_wordc; ++i)
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
        array_addlen(S->buf, strlen(ret));
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
    size_t pos[LP_MAX_COMPCOUNT], len;
    DIR *dir[LP_MAX_COMPCOUNT];
    if (*s != '\0' && ret != 0)
        return 0;
    if (*s != '\0' && !S_ISDIR(buf.st_mode))
        return h(S, ud, s, LP_WALKFILE);
    len = strlen(s);
    while (lp_isdirsep(s[len-1])) --len;
    lp_addlstring(S, s, len), pos[top] = len;
    do {
        if (top) {
            array_setlen(S->buf, pos[top]);
            lp_addstring(S, data->d_name);
        }
        if (isdir == 0) {
            ret = h(S, ud, lp_truncbuffer(S), LP_WALKFILE);
            if (ret < 0) goto out;
        } else if (top < LP_MAX_COMPCOUNT-1) {
            size_t len = array_len(S->buf) + 1;
            lp_addlstring(S, "/", 2);
            ret = h(S, ud, S->buf, LP_WALKIN);
            if (ret < 0) goto out;
            if (ret != 0) {
                dir[++top] = opendir(S->buf);
                if (dir[top] == NULL) {
                    ret = lp_pusherror(L, "opendir", S->buf);
                    goto out;
                }
                pos[top] = len;
            }
        }
        while (top && (ret = lp_readdir(L, dir[top], &data, &buf)) <= 0) {
            if (ret < 0) goto out;
            array_setlen(S->buf, pos[top--]);
            ret = h(S, ud, lp_truncbuffer(S), LP_WALKOUT);
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
    array_reset(S->buf);
    lp_addstring(S, lua_tostring(L, -1));
    dir = lp_truncbuffer(S);
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

typedef struct lp_Slice {
    const char *s, *e;
} lp_Slice;

static int lp_fnmatch(lp_Slice s, lp_Slice p);

static int lp_matchone(int ch, lp_Slice *p) {
    const char *ps = p->s;
    int inv = 0, res = 0;
    if (*p->s == '?' || (*p->s != '[' && *p->s == ch))
        return (++p->s, 1);
    if (*p->s != '[') return 0;
    if (*++p->s == '!') inv = 1, ++p->s;
    if (*p->s == ']') res = (ch == ']'), ++p->s;
    for (; !res && p->s < p->e && *p->s != ']'; ++p->s) {
        int range = p->s+1 < p->e && p->s[1] == '-'
                 && p->s+2 < p->e && p->s[2] != ']';
        res = range ? *p->s <= ch && ch <= p->s[2] : ch == *p->s;
        if (range) p->s += 2;
    }
    while (p->s < p->e && *p->s != ']')
        ++p->s;
    if (p->s == p->e) {
        p->s = ps;
        res = (ch == '['), inv = 0;
    }
    return res != inv ? (++p->s, 1) : (p->s = ps, 0);
}

static int lp_fnmatch(lp_Slice s, lp_Slice p) {
    const char *start = NULL, *match = NULL;
    if (p.s >= p.e) return s.s >= s.e;
    while (s.s < s.e) {
        if (p.s < p.e && lp_matchone(*s.s, &p))
            ++s.s;
        else if (p.s < p.e && *p.s == '*')
            match = s.s, start = ++p.s;
        else if (start != NULL)
            s.s = ++match, p.s = start;
        else return 0;
    }
    while (p.s < p.e && *p.s == '*') ++p.s;
    return p.s == p.e;
}

static int lp_makecomps(const char *s, lp_Slice parts[LP_MAX_COMPCOUNT]) {
    int i;
    for (i = 0; *s != '\0' && i < LP_MAX_COMPCOUNT; ++i) {
        while (lp_isdirsep(*s)) ++s;
        parts[i].s = s;
        while (*s != '\0' && !lp_isdirsep(*s)) ++s;
        parts[i].e = s;
    }
    return i;
}

typedef struct GlobState {
    lua_State  *L;
    const char *patt;
    lp_Slice    pparts[LP_MAX_COMPCOUNT];
    int         pnparts;
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
    } else if (state == LP_WALKOUT)
        ++gs->limit;
    if (state != LP_WALKOUT) {
        lp_Slice sparts[LP_MAX_COMPCOUNT];
        int i, j, snparts;
        s = lp_splitdrive(lp_drivehead(s));
        snparts = lp_makecomps(s, sparts);
        if (lp_isdirsep(*gs->patt) && snparts != gs->pnparts)
            return 1;
        if (snparts < gs->pnparts)
            return 1;
        for (i = snparts, j = gs->pnparts; j > 0; --i, --j)
            if (!lp_fnmatch(sparts[i-1], gs->pparts[j-1]))
                return 1;
        lua_pushstring(gs->L, s);
        lua_rawseti(gs->L, 3, (int)gs->idx++);
    }
    return 1;
}

static int lpL_glob(lua_State *L) {
    GlobState gs;
    size_t plen;
    const char *ts, *s, *p = luaL_checklstring(L, 1, &plen);
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
    ts = s = lua_tostring(L, 2);
    if (!lp_driveequal(&ts, &p) || *p == '\0')
        return 1;
    gs.L     = L;
    gs.patt  = p;
    gs.pnparts = lp_makecomps(p, gs.pparts);
    gs.limit = (int)l;
    rets = lp_walkpath(L, s, glob_walker, &gs);
    return rets > 0 ? 1 : -rets;
}

static int lpL_fnmatch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    const char *p = luaL_checkstring(L, 2);
    lp_Slice sparts[LP_MAX_COMPCOUNT], pparts[LP_MAX_COMPCOUNT];
    int i, j, snparts, pnparts;
    if (!lp_driveequal(&s, &p) || *p == '\0')
        return 0;
    snparts = lp_makecomps(s, sparts);
    pnparts = lp_makecomps(p, pparts);
    if (lp_isdirsep(*p) && snparts != pnparts)
        return 0;
    if (snparts > pnparts)
        return 0;
    for (i = snparts, j = pnparts; j > 0; --i, --j)
        if (!lp_fnmatch(sparts[i-1], pparts[j-1]))
            return 0;
    lua_pushboolean(L, 1);
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
    lua_rawgeti(L, 1, (lua_Integer)top-2); /* 2 */
    lua_rawgeti(L, 1, (lua_Integer)top-1); /* 3 */
    lua_rawgeti(L, 1, (lua_Integer)top); /* 4 */
    lua_call(L, 1, LUA_MULTRET); /* 3,4->? */
    if ((nrets = lua_gettop(L) - 2) == 0) {
        lua_pushstring(L, "out"); /* 3 */
        lua_settop(L, 6); /* 4,5,6 */
        lua_rawseti(L, 1, (lua_Integer)top-2); /* 6->1 */
        lua_rawseti(L, 1, (lua_Integer)top-1); /* 5->1 */
        lua_rawseti(L, 1, (lua_Integer)top); /* 4->1 */
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
    lua_rawseti(L, 1, (lua_Integer)top+1);
    lua_rawseti(L, 1, (lua_Integer)top+2);
    lua_rawseti(L, 1, (lua_Integer)top+3);
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

static const char *lp_checkpathargs(lua_State *L, size_t *plen) {
    int top = lua_gettop(L);
    if (top == 0) {
        const char r[] = "." LP_DIRSEP;
        lua_pushstring(L, r);
        if (plen) *plen = sizeof(r) - 1;
        return "." LP_DIRSEP;
    } else if (top == 1)
        return luaL_checklstring(L, 1, plen);
    lpL_joinpath(L);
    return lua_tolstring(L, 1, plen);
}

#define ENTRY(name)                                 \
    static int lpL_##name(lua_State *L) {           \
        const char *s = lp_checkpathargs(L,NULL);   \
        int ret = lp_##name(L, s);                  \
        if (ret == 0) lp_returnself(L);             \
        return ret < 0 ? -ret : ret;                }
LP_PATH_ROUTINES(ENTRY)
LP_PATH_FS_ROUTINES(ENTRY)
#undef ENTRY

static int lpL_split(lua_State *L) {
    const char *s = lp_checkpathargs(L, NULL);
    const char *b = lp_splitpath(lp_splitdrive(s));
    lua_pushlstring(L, s, b-s);
    lua_pushstring(L, b);
    return 2;
}

static int lpL_trim(lua_State *L) {
    size_t len;
    const char *p, *s = lp_checkpathargs(L, &len);
    while (*s == ' ') ++s, --len;
    p = lp_drivehead(lp_splitdrive(s));
    len -= (p - s);
    while (p[len-1] == ' ') --len;
    while (len > 1 && lp_isdirsep(p[len-1]))
        --len;
    lua_pushlstring(L, s, len + (p - s));
    return 1;
}

static int lpL_splitdrive(lua_State *L) {
    const char *d = lp_checkpathargs(L, NULL);
    const char *p = lp_splitdrive(d);
    lua_pushlstring(L, d, p-d);
    lua_pushstring(L, p);
    return 2;
}

static int lpL_splitext(lua_State *L) {
    const char *s = lp_checkpathargs(L, NULL);
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
        ENTRY(trim)
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

/* cc: flags+='-ggdb -Wextra -Wno-cast-function-type --coverage' run='lua test.lua'
 * unixcc: flags+='-s -O3 -shared -fPIC' output='path.so'
 * maccc: flags+='-O3 -shared -undefined dynamic_lookup' output='path.so'
 * win32cc: flags+='-s -O3 -mdll -DLUA_BUILD_AS_DLL'
 * win32cc: libs+='-llua54' output='path.dll' */

