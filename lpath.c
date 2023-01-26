#define LUA_LIB
#include <lua.h>
#include <time.h>
#include <lauxlib.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* compatible */

#if LUA_VERSION_NUM < 502
# define LUA_OK              0
# define lua_rawlen          lua_objlen
# define lua_getuservalue    lua_getfenv
# define lua_setuservalue    lua_setfenv
# ifndef LUA_GCISRUNNING /* not LuaJIT 2.1 */
#   define luaL_newlib(L,l)    (lua_newtable(L), luaL_register(L,NULL,l))

static lua_Integer lua_tointegerx(lua_State *L, int idx, int *pisint) {
    *pisint = lua_type(L, idx) == LUA_TNUMBER;
    return *pisint ? lua_tointeger(L, idx) : 0;
}

#   ifndef luaL_testudata
#   define luaL_testudata luaL_testudata
static void *luaL_testudata(lua_State *L, int ud, const char *tname) {
    void *p = lua_touserdata(L, ud);
    if (p != NULL) {
        if (lua_getmetatable(L, ud)) {
            luaL_getmetatable(L, tname);
            if (!lua_rawequal(L, -1, -2)) p = NULL;
            lua_pop(L, 2);
            return p;
        }
    }
    return NULL;
}
#   endif /* luaL_testudata */
# endif /* not LuaJIT 2.1 */
#endif /* Lua 5.1 */

#if LUA_VERSION_NUM >= 502
# define lua52_pushstring lua_pushstring
#else
static const char *lua52_pushstring(lua_State *L, const char *s)
{ lua_pushstring(L, s); return lua_tostring(L, -1); }
#endif

#if LUA_VERSION_NUM >= 503
# define lua53_rawgetp lua_rawgetp
#elif LUA_VERSION_NUM == 502
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_rawgetp(L, idx, p); return lua_type(L, -1); }
#else
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

#define LP_VERSION "path 0.3"

/* vector routines */

typedef struct VecHeader { unsigned len, cap; } VecHeader;

#define VEC_MIN_LEN (4)
#define VEC_MAX_LEN (~(unsigned)0 - 100)

#define vec_init(A)     ((A) = NULL)
#define vec_free(A)     (vec_resize(NULL,A,0), vec_init(A))
#define vec_reset(A)    vec_setlen(A,0)
#define vec_setlen(A,N) ((A) ? vec_rawlen(A) = (unsigned)(N) : 0u)

#define vec_(A)       ((VecHeader*)(A)-1)
#define vec_sz(A)     (sizeof(*(A)))
#define vec_rawlen(A) (vec_(A)->len)
#define vec_rawcap(A) (vec_(A)->cap)
#define vec_rawend(A) (&(A)[vec_rawlen(A)])

#define vec_hdr(A) ((A) ? vec_(A)       : NULL)
#define vec_len(A) ((A) ? vec_rawlen(A) : 0u)
#define vec_cap(A) ((A) ? vec_rawcap(A) : 0u)
#define vec_end(A) ((A) ? vec_rawend(A) : NULL)

#define vec_rawgrow(L,A,N) (vec_grow_((L),(void**)&(A),(unsigned)(N),vec_sz(A)))
#define vec_resize(L,A,N)  (vec_resize_((L),(void**)&(A),(N),vec_sz(A)))
#define vec_grow(L,A,N)    (assert(L), vec_rawgrow(L,A,N), vec_rawend(A))

#define vec_push(L,A,V)     (*vec_grow(L,A,1)=(V), vec_rawlen(A) += 1)
#define vec_pop(A)          (vec_len(A) ? vec_rawlen(A) -= 1 : 0)
#define vec_fill(L,A,V,S)   (memcpy(vec_grow(L,A,(S)+1),(V),(S)*vec_sz(A)))
#define vec_extend(L,A,V,S) (vec_fill(L,A,V,S), vec_rawlen(A)+=(unsigned)(S))
#define vec_concat(L,A,V)   vec_extend(L,A,V,strlen(V))

static int vec_resize_(lua_State *L, void **pA, unsigned cap, size_t objlen) {
    VecHeader *AI, *oldAI = (assert(pA), vec_hdr(*pA));
    if (cap == 0) { free(oldAI); vec_init(*pA); return 1; }
    AI = (VecHeader*)realloc(oldAI, sizeof(VecHeader) + cap*objlen);
    if (AI == NULL) return L ? luaL_error(L, "out of memory") : 0;
    if (!oldAI) AI->cap = AI->len = 0;
    if (AI->cap > cap) AI->cap = cap;
    AI->cap = cap;
    *pA = (void*)(AI + 1);
    return 1;
}

static int vec_grow_(lua_State *L, void **pA, unsigned len, size_t objlen) {
    unsigned cap = vec_cap(*pA);
    unsigned exp = vec_len(*pA) + len;
    if (cap < exp) {
        unsigned newcap = VEC_MIN_LEN;
        while (newcap < VEC_MAX_LEN/objlen && newcap < exp)
            newcap += newcap>>1;
        if (newcap < exp) return L ? luaL_error(L, "out of memory") : 0;
        return vec_resize_(L, pA, newcap, objlen);
    }
    return 1;
}

/* state */

#define LP_STATE_KEY    ((void*)(ptrdiff_t)0x9A76B0FF)
#define LP_WALKER_TYPE  "lpath.Walker"
#define LP_GLOB_TYPE    "lpath.Glob"
#define LP_PARTS_ITER   "lpath.PartsIter"

typedef struct lp_Part   lp_Part;
typedef struct lp_State  lp_State;
typedef struct lp_Walker lp_Walker;

#define LP_WALKER_PUBLIC \
    char        *buf;    \
    int          limit;  \
    lp_WalkState state;  \
    lp_WalkLevel *levels /* 'pos' is readonly, others are undefined */

typedef enum lp_WalkState {
    LP_WALKINIT,
    LP_WALKIN,
    LP_WALKFILE,
    LP_WALKOUT,
    LP_WALKDIR,
    LP_WALKSYS, /* '.' or '..', used internal */
} lp_WalkState;

struct lp_Part {
    const char *s, *e;
};

typedef struct lp_Path {
    lp_Part    *parts;  /* drive & parts list */
    int         dots;   /* count of '..', -1 for '/', -2 for "//" */
} lp_Path;

struct lp_State {
    lua_State     *L;
    char          *buf;
    lp_Path        p, pp; /* path, pattern path */
#ifdef _WIN32
    wchar_t       *wbuf;
    int            cp;
#endif /* _WIN32 */
};

static size_t lp_len(lp_Part s) { return s.e - s.s; }

static lp_Part lp_part(const char *s, size_t len)
{ lp_Part p; return (p.s = s, p.e = s+len, p); }

static int lp_pushresult(lp_State *S)
{ return lua_pushlstring(S->L, S->buf, vec_len(S->buf)), 1; }

static void lp_resetpath(lp_Path *p)
{ vec_reset(p->parts), p->dots = 0; }

static void lp_freepath(lp_Path *p)
{ if (p) vec_free(p->parts), p->dots = 0; }

static int lpL_delstate(lua_State *L) {
    lp_State *S = (lp_State*)lua_touserdata(L, 1);
    if (S != NULL) {
        lp_freepath(&S->p);
        lp_freepath(&S->pp);
        vec_free(S->buf);
#ifdef _WIN32
        vec_free(S->wbuf);
#endif
        memset(S, 0, sizeof(lp_State));
    }
    return 0;
}

static lp_State *lp_resetstate(lp_State *S) {
    lp_resetpath(&S->p);
    lp_resetpath(&S->pp);
    vec_reset(S->buf);
#ifdef _WIN32
    vec_reset(S->wbuf);
#endif
    return S;
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
    return lp_resetstate(S);
}

/* path algorithm */

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

#define LP_LEN(NAME)    (sizeof(LP_##NAME)-1)
#define lp_isdirsep(ch) ((ch) == LP_DIRSEP[0] || (ch) == LP_ALTSEP[0])
#define lp_isdirend(ch) (lp_isdirsep(ch) || ch == '\0')
#define lp_iscurdir(s)  (memcmp((s),LP_CURDIR,LP_LEN(CURDIR)) == 0 && lp_isdirend(s[LP_LEN(CURDIR)]))
#define lp_ispardir(s)  (memcmp((s),LP_PARDIR,LP_LEN(PARDIR)) == 0 && lp_isdirend(s[LP_LEN(PARDIR)]))
#define lp_charequal(ch1,ch2) (lp_normchar(ch1) == lp_normchar(ch2))

static const char *lp_nextsep(const char *p) {
    while (*p != '\0' && !lp_isdirsep(*p))
        ++p;
    return p;
}

static int lp_splitdrive(const char *s, lp_Part *p) {
    if (!(p->s = p->e = s)) return 0;
#ifdef _WIN32
    if (lp_isdirsep(s[0]) && lp_isdirsep(s[1]) && !lp_isdirsep(s[2])) {
        if (lp_isdirsep(s[3])) {
            if (s[2] == '?') { /* raw path? */
                p->e += s[4] && s[5] == ':' ? 6 : 4;
                return lp_isdirsep(*p->e);
            }
            if (s[2] == '.') { /* device path? */
                p->e = lp_isdirsep(s[4]) ? (p->s = s + 4) :
                    (s[4] ? lp_nextsep(s + 4) : s + 4);
                return 1;
            }
        }
        if (*(p->e = lp_nextsep(s + 2)) == '\0') /* normal path? */
            return p->e = p->s, 1;
        return p->e = lp_nextsep(p->e + 1), 1; /* UNC path */
    }
    p->e += s[0] && s[1] == ':' ? 2 : 0;
    return lp_isdirsep(*p->e);
#else
    return !lp_isdirsep(s[0]) ? 0 :
        (lp_isdirsep(s[1]) && !lp_isdirsep(s[2])) ? 2 : 1;
#endif
}

static int lp_normchar(int ch) {
#ifdef _WIN32
    if (lp_isdirsep(ch))
        return LP_DIRSEP[0];
    if (ch >= 'a' && ch <='z')
        return ch + 'A' - 'a';
#endif
    return ch;
}

static int lp_driveequal(lp_Part d1, lp_Part d2) {
    size_t l = lp_len(d2);
    if (l == 0) return 1;
    if (lp_len(d1) != l) return 0;
    while (d1.s < d1.e && d2.s < d2.e && lp_charequal(*d1.s, *d2.s))
        ++d1.s, ++d2.s;
    return d1.s == d1.e;
}

static void lp_joinraw(lua_State *L, const char *s, lp_Path *p) {
    while (*s != '\0') {
        lp_Part *cur = vec_grow(L, p->parts, 1);
        while (lp_isdirsep(*s)) ++s;
        cur->s = s;
        cur->e = s = lp_nextsep(s);
        if (lp_iscurdir(cur->s)) {
            if (*cur->e == '\0') vec_rawlen(p->parts) += 1;
            cur->e = cur->s;
        } else if (!lp_ispardir(cur->s))
            vec_rawlen(p->parts) += 1;
        else if (vec_rawlen(p->parts) > 1)
            vec_rawlen(p->parts) -= 1;
        else if (p->dots >= 0)
            p->dots += 1;
    }
}

static int lp_joinparts(lua_State *L, const char *s, lp_Path *p) {
    lp_Part drive;
    int root = lp_splitdrive(s, &drive);
    if (vec_len(p->parts) == 0)
        vec_push(L, p->parts, drive);
    else if (!lp_driveequal(p->parts[0], drive))
        vec_rawlen(p->parts) = 1, p->parts[0] = drive, p->dots = -root;
    if (root) /* is absolute path? */
        vec_rawlen(p->parts) = 1, p->dots = -root;
    if (vec_rawlen(p->parts) > 1 && lp_len(vec_rawend(p->parts)[-1]) == 0)
        vec_rawlen(p->parts) -= 1;        /* remove trailing '/' */
    if (vec_rawlen(p->parts) > 1 && *drive.e == '\0')
        vec_push(L, p->parts, lp_part(drive.e, 0));  /* empty? add '/' */
    else  /* join path to current parts */
        lp_joinraw(L, drive.e, p);
    return p->dots;
}

static char *lp_applydrive(lua_State *L, int sep, char **pp, lp_Part drive) {
    const char *s;
    for (s = drive.s; s < drive.e; ++s) {
        int ch = *s;
        if (lp_isdirsep(ch))
            vec_push(L, *pp, sep);
#ifdef _WIN32
        else if (ch >= 'a' && ch <='z')
            vec_push(L, *pp, ch + 'A' - 'a');
#endif
        else
            vec_push(L, *pp, ch);
    }
    return *pp;
}

static char *lp_applysepparts(lua_State *L, const char *sep, char **pp, lp_Path *p) {
    int i, len = vec_len(p->parts);
    if (len) {
        lp_applydrive(L, sep[0], pp, p->parts[0]);
        if (p->dots <= -2) vec_concat(L, *pp, sep);
        if (p->dots <= -1) vec_concat(L, *pp, sep);
        if (p->dots > 0) {
            for (i = 0; i < p->dots; ++i) {
                if (i > 0) vec_concat(L, *pp, sep);
                vec_concat(L, *pp, LP_PARDIR);
            }
            if (len > 1) vec_concat(L, *pp, sep);
        }
        for (i = 1; i < len; ++i) {
            if (i > 1) vec_concat(L, *pp, sep);
            vec_extend(L, *pp, p->parts[i].s, lp_len(p->parts[i]));
        }
    }
    if (vec_len(*pp) == 0) vec_push(L, *pp, LP_CURDIR[0]);
    *vec_grow(L, *pp, 1) = 0;
    return *pp;
}

static char *lp_applyparts(lua_State *L, char **pp, lp_Path *p)
{ return lp_applysepparts(L, LP_DIRSEP, pp, p); }

static char *lp_applyaltparts(lua_State *L, char **pp, lp_Path *p)
{ return lp_applysepparts(L, LP_ALTSEP, pp, p); }

static lp_Part lp_name(lp_Path *p) {
    unsigned len = vec_len(p->parts);
    lp_Part *name;
    if (len == 1 || (len == 2 && lp_len(p->parts[1]) == 0))
        return p->dots > 0 ? lp_part(LP_PARDIR, sizeof(LP_PARDIR)-1)
            : lp_part(NULL, 0);
    name = vec_rawend(p->parts);
    return lp_len(name[-1]) ? name[-1] : name[-2];
}

static const char *lp_splitext(lp_Part name) {
    const char *p = name.e;
    while (name.s < p && *p != LP_EXTSEP[0])
        --p;
    return name.s < p && p != name.e - 1 ? p : name.e;
}

static int lp_indexparts(lua_State *L, int idx, lp_Path *p) {
    int len = (int)vec_len(p->parts);
    int extra = p->dots < 0 || (len && lp_len(p->parts[0]));
    len += (p->dots > 0 ? p->dots : 0) + extra - 1 -
        (len && lp_len(p->parts[len-1]) == 0);
    if (idx < 0) idx += len + 1;
    if (!(idx >= 1 && idx <= len)) return 0;
    if (extra && idx == 1) {
        luaL_Buffer B;
        const char *s;
        luaL_buffinit(L, &B);
        for (s = p->parts[0].s; s < p->parts[0].e; ++s)
            luaL_addchar(&B, lp_normchar(*s));
        luaL_addstring(&B, p->dots == -1 ?
                LP_DIRSEP : p->dots == -2 ? LP_DIRSEP LP_DIRSEP : "");
        luaL_pushresult(&B);
    } else if (idx <= p->dots)
        lua_pushstring(L, LP_PARDIR);
    else {
        idx -= (p->dots < 0 ? 0 : p->dots) + extra;
        if (idx < 0 || idx >= (int)vec_len(p->parts))
            return lua_pushliteral(L, ""), 1;
        lua_pushlstring(L, p->parts[idx].s, lp_len(p->parts[idx]));
    }
    return 1;
}

static lp_State *lp_joinargs(lua_State *L, int start, int count) {
    lp_State *S = lp_getstate(L);
    int i;
    for (i = start; i <= count; ++i)
        lp_joinparts(L, luaL_checkstring(L, i), &S->p);
    return S;
}

/* system specfied utils */

#define LP_MAX_TMPNUM     1000000
#define LP_MAX_TMPCNT     6 /* 10 ** LP_MAX_TMPCNT */

#define lp_bool(L,b) (lua_pushboolean((L), (b)), 1)

#ifdef _WIN32

# define WIN32_LEAN_AND_MEAN
# include <Windows.h>

#define LP_PLATFORM      "windows"

#define lp_pusherror(L,t,f) lpP_pusherrmsg((L), GetLastError(), (t), (f))

static const char *lpP_win32error(lua_State *L, DWORD errnum) {
    lp_State *S = lp_getstate(L);
    const char *ret = NULL;
    LPWSTR msg = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, errnum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, NULL);
    if (len == 0) return "get system error message error";
    ret = vec_grow(L, S->buf, ((size_t)len*3 + 1));
    int bc = WideCharToMultiByte(S->cp, 0, msg, len + 1,
            (char*)ret, len*3 + 1, NULL, NULL);
    LocalFree(msg);
    if (bc > (int)len*3 + 1) return "error message too large";
    if (bc == 0) return "mutibyte: format error message error";
    vec_rawlen(S->buf) += bc-1;
    return ret;
}

static int lpP_pusherrmsg(lua_State *L, DWORD err, const char *title, const char *fn) {
    const char *lfn = lua52_pushstring(L, fn), *msg = lpP_win32error(L, err);
    lua_pushnil(L);
    if (title && lfn)
        lua_pushfstring(L, "%s:%s:(errno=%d): %s", title, lfn, err, msg);
    else
        lua_pushfstring(L, "%s:(errno=%d): %s", title ? title : lfn, err, msg);
    lua_remove(L, -3);
    return -2;
}

static LPWSTR lpP_addl2wstring(lua_State *L, LPWSTR *ws, LPCSTR s, int bc, int cp) {
    int size = (bc = bc < 0 ? (int)strlen(s) : bc) + 1, wc;
    if (bc == 0) return *vec_grow(L, *ws, 1) = 0, vec_rawend(*ws);
    wc = MultiByteToWideChar(cp, 0, s, bc,
            vec_grow(L, *ws, size), size);
    if (wc > bc) size = wc + 1,
        wc = MultiByteToWideChar(cp, 0, s, bc,
                vec_grow(L, *ws, size), size);
    if (wc == 0) lp_pusherror(L, "unicode", NULL), lua_error(L);
    vec_rawlen(*ws) += wc, *vec_grow(L, *ws, 1) = 0;
    return vec_rawend(*ws) - (ptrdiff_t)wc;
}

static LPSTR lpP_addlw2string(lua_State *L, LPSTR *s, LPCWSTR ws, int wc, int cp) {
    int size = ((wc = wc < 0 ? (int)wcslen(ws) : wc) + 1) * 3, bc;
    if (wc == 0) return *vec_grow(L, *s, 1) = 0, vec_rawend(*s);
    bc = WideCharToMultiByte(cp, 0, ws, wc,
            vec_grow(L, *s, size), size, NULL, NULL);
    if (bc > size) size = bc + 1,
        bc = WideCharToMultiByte(cp, 0, ws, wc,
                vec_grow(L, *s, size), size, NULL, NULL);
    if (bc == 0) lp_pusherror(L, "multibyte", NULL), lua_error(L);
    vec_rawlen(*s) += bc, *vec_grow(L, *s, 1) = 0;
    return vec_rawend(*s) - (ptrdiff_t)bc;
}

static int lpL_ansi(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t len;
    const char *utf8;
    switch (lua_type(L, 1)) {
    case LUA_TNONE:
    case LUA_TNIL:    return S->cp = CP_ACP, 0;
    case LUA_TNUMBER: return S->cp = (UINT)lua_tonumber(L, 1), 0;
    case LUA_TSTRING:
        utf8 = lua_tolstring(L, 1, &len);
        lpP_addl2wstring(L, &S->wbuf, utf8, (int)len, CP_UTF8);
        lpP_addlw2string(L, &S->buf, S->wbuf, -1, S->cp);
        return lp_pushresult(S);
    default:
        lua_pushfstring(L, "number/string expected, got %s",
                luaL_typename(L, 1));
        return luaL_argerror(L, 1, lua_tostring(L, -1));
    }
}

static int lpL_utf8(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t len;
    const char *ansi;
    switch (lua_type(L, 1)) {
    case LUA_TNONE:
    case LUA_TNIL:    return S->cp = CP_UTF8, 0;
    case LUA_TNUMBER: return S->cp = (UINT)lua_tonumber(L, 1), 0;
    case LUA_TSTRING:
        ansi = lua_tolstring(L, 1, &len);
        lpP_addl2wstring(L, &S->wbuf, ansi, (int)len, S->cp);
        lpP_addlw2string(L, &S->buf, S->wbuf, -1, CP_UTF8);
        return lp_pushresult(S);
    default:
        lua_pushfstring(L, "number/string expected, got %s",
                luaL_typename(L, 1));
        return luaL_argerror(L, 1, lua_tostring(L, -1));
    }
    return 0;
}

/* scandir */

typedef struct lp_WalkLevel {
    HANDLE   *hFile;
    unsigned  pos;
} lp_WalkLevel;

struct lp_Walker {
    LP_WALKER_PUBLIC;

    /* private */
    int              cp;
    WCHAR           *wbuf;
    WIN32_FIND_DATAW wfd;
    DWORD            err;
};

static void lp_initwalker(lp_State *S, lp_Walker *w, char *s, int limit) {
    memset(w, 0, sizeof(*w));
    w->cp    = S->cp;
    w->buf   = s;
    w->limit = limit;
    w->state = LP_WALKINIT;
}

static void lp_freewalker(lp_Walker *w) {
    int i, len;
    for (i = 0, len = vec_len(w->levels); i < len; ++i)
        FindClose(w->levels[i].hFile);
    vec_free(w->buf);
    vec_free(w->wbuf);
    vec_free(w->levels);
}

static int lpW_init(lua_State *L, lp_Walker *w) {
    DWORD attr = GetFileAttributesW(lpP_addl2wstring(L,
                &w->wbuf, w->buf, -1, w->cp));
    if (w->buf[0] != '\0' && attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (w->buf[0] != '\0' && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return w->state = LP_WALKFILE;
    return w->state = (w->limit ? LP_WALKIN : LP_WALKDIR);
}

static int lpW_in(lua_State *L, lp_Walker *w) {
    lp_WalkLevel *top = vec_grow(L, w->levels, 1);
    WCHAR *pathend = vec_grow(L, w->wbuf, 3);
    if (w->limit == 0) return 0;
    if (vec_rawlen(w->wbuf) && !lp_isdirsep(pathend[-1])) {
        vec_push(L, w->wbuf, LP_DIRSEP[0]);
        vec_push(L, w->buf, LP_DIRSEP[0]);
    }
    memcpy(vec_rawend(w->wbuf), L"*", 2 * sizeof(WCHAR));
    top->hFile = FindFirstFileW(w->wbuf, &w->wfd);
    if (top->hFile == INVALID_HANDLE_VALUE)
        return lp_pusherror(L, "walkin", w->buf);
    w->err = ERROR_ALREADY_ASSIGNED;
    top->pos = vec_rawlen(w->wbuf);
    vec_rawlen(w->levels) += 1;
    return --w->limit, 0;
}

static int lpW_out(lua_State *L, lp_Walker *w) {
    lp_WalkLevel *top = vec_rawend(w->levels) - 1;
    DWORD err = GetLastError();
    if (err != ERROR_NO_MORE_FILES)
        return lpP_pusherrmsg(L, err, "walkout", w->buf);
    FindClose(top->hFile);
    vec_rawlen(w->wbuf) = vec_rawlen(w->buf) = top->pos ? top->pos - 1 : 0;
    *vec_rawend(w->buf) = (char)(*vec_rawend(w->wbuf) = 0);
    vec_rawlen(w->levels) -= 1;
    return ++w->limit, 0;
}

static int lpW_file(lua_State *L, lp_Walker *w) {
    lp_WalkLevel* top;
    size_t len;
    if (vec_len(w->levels) == 0) return 0;
    top = vec_rawend(w->levels) - 1;
    if (w->err == ERROR_ALREADY_ASSIGNED)
        w->err = ERROR_SUCCESS;
    else if (!FindNextFileW(vec_rawend(w->levels)[-1].hFile, &w->wfd)) {
        w->err = GetLastError();
        if (w->err == ERROR_NO_MORE_FILES)
            return w->err = ERROR_SUCCESS, LP_WALKOUT;
    }
    if (w->err != ERROR_SUCCESS) return lp_pusherror(L, "walknext", w->buf);
    if (wcscmp(w->wfd.cFileName, L"" LP_CURDIR) == 0
            || wcscmp(w->wfd.cFileName, L"" LP_PARDIR) == 0)
        return LP_WALKSYS;
    vec_rawlen(w->buf) = vec_rawlen(w->wbuf) = top->pos;
    len = wcslen(w->wfd.cFileName);
    lpP_addlw2string(L, &w->buf, w->wfd.cFileName, (int)len, w->cp);
    vec_extend(L, w->wbuf, w->wfd.cFileName, len);
    *vec_grow(L, w->wbuf, 1) = 0;
    return w->wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ?
        (w->limit ? LP_WALKIN : LP_WALKDIR) : LP_WALKFILE;
}

/* dir operations */

static LPWSTR lpP_addwstring(lp_State *S, const char *s) {
    return lpP_addl2wstring(S->L, &S->wbuf, s,
            (s == S->buf ?  (int)vec_len(s) : -1), S->cp);
}

static int lpL_getcwd(lua_State *L) {
    lp_State *S = lp_getstate(L);
    vec_reset(S->buf), vec_reset(S->wbuf);
    DWORD wc = GetCurrentDirectoryW(MAX_PATH, vec_grow(L, S->wbuf, MAX_PATH));
    if (wc >= MAX_PATH)
        wc = GetCurrentDirectoryW(wc + 1, vec_grow(L, S->wbuf, wc + 1));
    if (wc == 0) return lp_pusherror(L, "getcwd", NULL);
    return lpP_addlw2string(L, &S->buf, S->wbuf, wc, S->cp), lp_pushresult(S);
}

static int lpL_binpath(lua_State *L) {
    lp_State *S = lp_getstate(L);
    DWORD wc = MAX_PATH, r, err;
    while (!(r = GetModuleFileNameW(NULL, vec_grow(S->L, S->wbuf, wc), wc))
            || r == wc) {
        if ((err = GetLastError()) == ERROR_SUCCESS)
            break;
        if (r == 0 || err != ERROR_INSUFFICIENT_BUFFER)
            return -lpP_pusherrmsg(L, err, "binpath", NULL);
        wc += wc << 1;
        if (wc >= (~(DWORD)0 >> 1)) return luaL_error(L, "out of memory");
    }
    return lpP_addlw2string(L, &S->buf, S->wbuf, r, S->cp), lp_pushresult(S);
}

static int lp_chdir(lp_State *S, const char *s) {
    return !SetCurrentDirectoryW(lpP_addwstring(S, s)) ?
        lp_pusherror(S->L, "chdir", s) : 0;
}

static int lp_mkdir(lp_State *S, const char *s) {
    if (!CreateDirectoryW(lpP_addwstring(S, s), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
            return lpP_pusherrmsg(S->L, err, "mkdir", s);
    }
    return 0;
}

static int lp_rmdir(lp_State *S, const char *s) {
    return !RemoveDirectoryW(lpP_addwstring(S, s)) ?
        lp_pusherror(S->L, "rmdir", s) : 0;
}

static int lp_makedirs(lp_State *S, const char *s) {
    size_t i, len = strlen(s);
    LPWSTR ws = lpP_addl2wstring(S->L, &S->wbuf, s, (int)len, S->cp);
    lp_Part drive;
    i = (lp_splitdrive(s, &drive), drive.e - s) + 1;
    for (; i <= len; ++i) {
        while (i < len && !lp_isdirsep(ws[i])) ++i;
        ws[i] = 0;
        if (!CreateDirectoryW(S->wbuf, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS)
                return lpP_pusherrmsg(S->L, err, "makedirs", s);
        }
        if (i != len) ws[i] = LP_DIRSEP[0];
    }
    return 0;
}

static int lp_removedirs(lp_State *S, lp_Walker *w, int *pcount, void *ud) {
    (void)ud;
    if (w->state == LP_WALKFILE)
        return ++*pcount, DeleteFileW(w->wbuf) ?
            0 : lp_pusherror(S->L, "remove", w->buf);
    if (w->state == LP_WALKDIR || w->state == LP_WALKOUT)
        return ++*pcount, RemoveDirectoryW(w->wbuf) ?
            0 : lp_pusherror(S->L, "rmdir", w->buf);
    return 0;
}

static int lp_unlockdirs(lp_State *S, lp_Walker *w, int *pcount, void *ud) {
    (void)ud;
    if (w->state == LP_WALKFILE)
        return ++*pcount, SetFileAttributesW(w->wbuf,
                GetFileAttributesW(w->wbuf) & ~FILE_ATTRIBUTE_READONLY) ?
            0 : lp_pusherror(S->L, "unlock", w->buf);
    return 0;
}

static int lpL_tmpdir(lua_State* L) {
    size_t postfixlen = LP_MAX_TMPCNT + 6;
    const char *prefix = luaL_optstring(L, 1, "lua_");
    lp_State *S = lp_getstate(L);
    DWORD wc = GetTempPathW(MAX_PATH, vec_grow(L, S->wbuf, MAX_PATH));
    WCHAR *wbuf;
    if (wc > MAX_PATH) wc = GetTempPathW(wc, vec_grow(L, S->wbuf, wc));
    if (wc == 0) return -lp_pusherror(L, "tmpdir", NULL);
    vec_rawlen(S->wbuf) += wc;
    wbuf = vec_grow(L, S->wbuf, postfixlen);
    srand(((int)(ptrdiff_t)&L) ^ clock());
    if (!lp_isdirsep(wbuf[-1])) vec_push(L, S->wbuf, LP_DIRSEP[0]);
    do {
        int magic = ((unsigned)rand()<<16|rand()) % LP_MAX_TMPNUM;
        swprintf(wbuf, postfixlen, L"%hs%d", prefix, magic);
    } while (GetFileAttributesW(S->wbuf) != INVALID_FILE_ATTRIBUTES);
    assert(S->wbuf != NULL);
    if (!CreateDirectoryW(S->wbuf, NULL))
        return -lp_pusherror(L, "tmpdir", lua_tostring(L, -1));
    return lpP_addlw2string(L, &S->buf, S->wbuf, -1, S->cp), lp_pushresult(S);
}

/* file operations */

static int lpP_optftime(lua_State *L, int idx, PFILETIME pft) {
    ULARGE_INTEGER ln;
    if (lua_isnoneornil(L, idx))
        return 0;
    ln.QuadPart = (ULONGLONG)luaL_checkinteger(L, idx);
    pft->dwLowDateTime = ln.LowPart;
    pft->dwHighDateTime = ln.HighPart;
    return 1;
}

static HANDLE lpP_open(LPCWSTR ws, DWORD dwDesiredAccess, DWORD dwCreationDisposition) {
    return CreateFileW(ws,         /* file to open       */
            dwDesiredAccess,       /* open for write attributes   */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security   */
            dwCreationDisposition, /* existing file only */
            FILE_FLAG_BACKUP_SEMANTICS, /* open directory also */
            NULL);                 /* no attr. template  */
}

static int lp_exists(lp_State *S, const char *s) {
    HANDLE hFile = lpP_open(lpP_addwstring(S, s), 0, OPEN_EXISTING);
    int r = hFile != INVALID_HANDLE_VALUE;
    return CloseHandle(hFile), lp_bool(S->L, r);
}

static int lp_size(lp_State *S, const char *s) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULARGE_INTEGER ul;
    if (!GetFileAttributesExW(lpP_addwstring(S, s), GetFileExInfoStandard, &fad))
        return lp_pusherror(S->L, "size", s);
    ul.LowPart = fad.nFileSizeLow;
    ul.HighPart = fad.nFileSizeHigh;
    return lua_pushinteger(S->L, ul.QuadPart), 1;
}

static int lpP_touch(lua_State *L) {
    FILETIME at, mt;
    SYSTEMTIME st;
    HANDLE *phFile = (HANDLE*)lua_touserdata(L, 1);
    const char *s = lua_tostring(L, 2);
    lpP_optftime(L, 3, &at);
    lpP_optftime(L, 4, &mt);
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &mt), at = mt;
    return SetFileTime(*phFile, NULL, &at, &mt) ? 0 :
        (lp_pusherror(L, "touch", s), lua_error(L));
}

static int lpL_touch(lua_State *L) {
    lp_State *S = lp_getstate(L);
    int ret;
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    LPCWSTR ws = lpP_addl2wstring(L, &S->wbuf, s, (int)len, S->cp);
    HANDLE hFile = lpP_open(ws, FILE_WRITE_ATTRIBUTES, OPEN_ALWAYS);
    if (hFile == INVALID_HANDLE_VALUE)
        return -lp_pusherror(S->L, "open", s);
    lua_settop(L, 3);
    lua_pushcfunction(L, lpP_touch);
    lua_pushlightuserdata(L, &hFile);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    ret = lua_pcall(L, 4, 0, 0);
    CloseHandle(hFile);
    return ret == LUA_OK ? lp_bool(L, 1) :
        (lua_pushnil(L), lua_insert(L, -2), 2);
}

static int lp_remove(lp_State *S, const char *s) {
    return DeleteFileW(lpP_addwstring(S, s)) ? 0 :
        lp_pusherror(S->L, "remove", s);
}

static int lpL_rename(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t flen, tlen;
    const char *from = luaL_checklstring(L, 1, &flen);
    const char *to = luaL_checklstring(L, 2, &tlen);
    LPWSTR wto = (lpP_addl2wstring(L, &S->wbuf, from, (int)flen+1, S->cp),
            lpP_addl2wstring(L, &S->wbuf, to, (int)tlen, S->cp));
    return MoveFileW(S->wbuf, wto) ? lp_bool(L, 1) :
        -lp_pusherror(L, "rename", to);
}

static int lpL_copy(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t flen, tlen;
    const char *from = luaL_checklstring(L, 1, &flen);
    const char *to = luaL_checklstring(L, 2, &tlen);
    int failIfExists = lua_toboolean(L, 3);
    LPWSTR wto = (lpP_addl2wstring(L, &S->wbuf, from, (int)flen+1, S->cp),
            lpP_addl2wstring(L, &S->wbuf, to, (int)tlen, S->cp));
    return CopyFileW(S->wbuf, wto, failIfExists) ? lp_bool(L, 1) :
        -lp_pusherror(L, "copy", to);
}

static DWORD lp_CreateSymbolicLinkW(lua_State *L, LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName, DWORD dwFlags) {
    typedef BOOLEAN APIENTRY F(LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName, DWORD dwFlags);
    static F* f;
    if (!f) {
        HMODULE hModule = GetModuleHandleA("KERNEL32.dll");
        if (hModule != NULL) {
            union { F *f; FARPROC v; } u;
            u.v = GetProcAddress(hModule, "CreateSymbolicLinkW");
            f = u.f;
        }
        if (!f) return luaL_error(L, "CreateSymbolicLinkW not implemented");
    }
    return f(lpSymlinkFileName, lpTargetFileName, dwFlags);
}

static int lpL_symlink(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t flen, tlen;
    const char *from = luaL_checklstring(L, 1, &flen);
    const char *to = luaL_checklstring(L, 2, &tlen);
    int dir = lua_toboolean(L, 3) ? /*SYMBOLIC_LINK_FLAG_DIRECTORY=*/1 : 0;
    LPWSTR wto = (lpP_addl2wstring(L, &S->wbuf, from, (int)flen+1, S->cp),
            lpP_addl2wstring(L, &S->wbuf, to, (int)tlen, S->cp));
    return lp_CreateSymbolicLinkW(L, wto, S->wbuf, dir) ? lp_bool(L, 1) :
        -lp_pusherror(L, "copy", to);
}

/* path informations */

static int lp_abs(lp_State *S, const char *s) {
    LPWSTR ws = (lpP_addwstring(S, s),
            vec_push(S->L, S->wbuf, 0),
            vec_grow(S->L, S->wbuf, MAX_PATH));
    DWORD wc = GetFullPathNameW(S->wbuf, MAX_PATH, ws, NULL);
    if (wc >= MAX_PATH) {
        ws = vec_grow(S->L, S->wbuf, wc + 1);
        wc = GetFullPathNameW(S->wbuf, wc, ws, NULL);
    }
    if (wc == 0) return lp_pusherror(S->L, "abs", s);
    vec_reset(S->buf), lpP_addlw2string(S->L, &S->buf, ws, wc, S->cp);
    return lp_pushresult(S);
}

#define lpP_isattr(N,ATTR)                                    do { \
    DWORD attr = GetFileAttributesW(lpP_addwstring(S, s));         \
    return (attr != INVALID_FILE_ATTRIBUTES                        \
            && N(attr&FILE_ATTRIBUTE_##ATTR))?0:lp_bool(S->L,0); } while (0)

static int lp_isdir(lp_State *S, const char *s)  {lpP_isattr(,DIRECTORY);    }
static int lp_islink(lp_State *S, const char *s) {lpP_isattr(,REPARSE_POINT);}
static int lp_isfile(lp_State *S, const char *s) {lpP_isattr(!,DIRECTORY);   }

static int lpP_ismount(lp_State *S, const char *s) {
    int wc, len = (int)vec_len(S->p.parts);
    int noparts = (len == 1 || (len == 2 && lp_len(S->p.parts[1]) == 0));
    LPWSTR ws;
    if (len < 1) return 0;
    if (lp_len(S->p.parts[0]) && lp_isdirsep(*S->p.parts[0].s))
        return (S->p.dots < 0 && noparts);
    if (S->p.dots < 0 && noparts) return 1;
    ws = (lpP_addwstring(S, s),
            wc = vec_len(S->wbuf),
            vec_push(S->L, S->wbuf, 0),
            vec_grow(S->L, S->wbuf, MAX_PATH));
    if (!GetVolumePathNameW(S->wbuf, ws, MAX_PATH)) return 0;
    if (lp_isdirsep(S->wbuf[wc-1])) S->wbuf[wc-1] = 0;
    if (wc = (int)wcslen(ws), lp_isdirsep(ws[wc-1])) ws[wc-1] = 0;
    return wcscmp(S->wbuf, ws) == 0;
}

static int lp_ismount(lp_State *S, const char *s) {
    int ret;
    if ((ret = lp_abs(S, s)) != 1) return ret;
    lp_joinparts(S->L, s = lua_tostring(S->L, -1), &lp_resetstate(S)->p);
    return lpP_ismount(S, s) ? 0 : lp_bool(S->L, 0);
}

#define lp_time(ty, field)                                  do { \
    LPWSTR ws = lpP_addwstring(S, s);                            \
    WIN32_FILE_ATTRIBUTE_DATA fad;                               \
    if (!GetFileAttributesExW(ws, GetFileExInfoStandard, &fad))  \
        return lp_pusherror(S->L, #ty "time", s);                \
    return lpP_pushtime(S->L, &fad.ft##field);                 } while (0)

static int lpP_pushtime(lua_State *L, const FILETIME *pft) {
    ULARGE_INTEGER ln;
    ln.LowPart = pft->dwLowDateTime;
    ln.HighPart = pft->dwHighDateTime;
    return lua_pushinteger(L, (lua_Integer)ln.QuadPart), 1;
}

static int lp_ctime(lp_State *S, const char *s) {lp_time(c, CreationTime);  }
static int lp_mtime(lp_State *S, const char *s) {lp_time(m, LastWriteTime); }
static int lp_atime(lp_State *S, const char *s) {lp_time(a, LastAccessTime);}

static DWORD lp_GetFinalPathNameByHandleW(lua_State *L, HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags) {
    typedef DWORD WINAPI F(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
    static F* f;
    if (!f) {
        HMODULE hModule = GetModuleHandleA("KERNEL32.dll");
        if (hModule != NULL) {
            union { F *f; FARPROC v; } u;
            u.v = GetProcAddress(hModule, "GetFinalPathNameByHandleW");
            f = u.f;
        }
        if (!f) return luaL_error(L, "GetFinalPathNameByHandleW not implemented");
    }
    return f(hFile, lpszFilePath, cchFilePath, dwFlags);
}

static int lpP_realpath(lua_State *L) {
    lp_State *S = (lp_State*)lua_touserdata(L, 1);
    HANDLE *phFile = (HANDLE*)lua_touserdata(L, 2);
    LPWSTR ret = vec_grow(S->L, S->wbuf, MAX_PATH);
    DWORD wc = lp_GetFinalPathNameByHandleW(S->L, *phFile, ret, MAX_PATH, 0);
    if (wc >= MAX_PATH) {
        ret = vec_grow(S->L, S->wbuf, wc);
        wc = lp_GetFinalPathNameByHandleW(S->L, *phFile, ret, wc, 0);
    }
    if (wc == 0) return lp_pusherror(S->L, "resolve", S->buf), lua_error(L);
    if (wc <= MAX_PATH + 4) ret += 4, wc -= 4;
    vec_reset(S->buf), lpP_addlw2string(S->L, &S->buf, ret, wc, S->cp);
    return lp_pushresult(S);
}

static int lp_realpath(lp_State *S, const char *s) {
    int ret;
    HANDLE hFile = lpP_open(lpP_addwstring(S, s), 0, OPEN_EXISTING);
    lua_pushcfunction(S->L, lpP_realpath);
    lua_pushlightuserdata(S->L, S);
    lua_pushlightuserdata(S->L, &hFile);
    ret = lua_pcall(S->L, 2, 1, 0);
    CloseHandle(hFile);
    return ret == LUA_OK ? 1 : (lua_pushnil(S->L), lua_insert(S->L, -2), 2);
}

/* utils */

static int lp_readreg(lp_State *S, HKEY hkey, LPCWSTR key) {
    DWORD wc = MAX_PATH, ret;
    LPWSTR ws = (vec_reset(S->wbuf), vec_grow(S->L, S->wbuf, wc));
    while ((ret = RegQueryValueExW(hkey, key, NULL, NULL, (LPBYTE)ws, &wc))
            != ERROR_SUCCESS) {
        if (ret != ERROR_MORE_DATA) {
            lua_pushstring(S->L,
                    lpP_addlw2string(S->L, &S->buf, key, -1, S->cp));
            return lpP_pusherrmsg(S->L, ret, "uname", lua_tostring(S->L, -1));
        }
        ws = vec_grow(S->L, S->wbuf, wc);
    }
    vec_rawlen(S->wbuf) = wc;
    *vec_grow(S->L, S->wbuf, 1) = 0;
    return 0;
}

static int lpP_uname(lua_State *L) {
    lp_State *S = (lp_State*)lua_touserdata(L, 1);
    HKEY *hKey = (HKEY*)lua_touserdata(L, 2);
    DWORD major = 0, minor = 0, build, size = sizeof(DWORD);
    LPWSTR dot;
    int ret;
    if ((ret = lp_readreg(S, *hKey, L"CurrentBuildNumber")) < 0)
        return lua_error(L);
    build = wcstoul(S->wbuf, NULL, 10);
    if (RegQueryValueExW(*hKey, L"CurrentMajorVersionNumber",
                NULL, NULL, (LPBYTE)&major, &size) != ERROR_SUCCESS
            || RegQueryValueExW(*hKey, L"CurrentMinorVersionNumber",
                NULL, NULL, (LPBYTE)&minor, &size) != ERROR_SUCCESS) {
        if ((ret = lp_readreg(S, *hKey, L"CurrentVersion")) < 0)
            return lua_error(L);
        major = wcstoul(S->wbuf, &dot, 10);
        if (*dot == L'.') minor = wcstoul(dot + 1, NULL, 10);
    }
    lua_pushfstring(L, "Windows %d.%d Build %d", major, minor, build);
    lua_pushinteger(L, major);
    lua_pushinteger(L, minor);
    lua_pushinteger(L, build);
    return 4;
}

static int lpL_uname(lua_State *L) {
    lp_State *S = lp_getstate(L);
    LPCWSTR root = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    HKEY hKey;
    int ret;
    if ((ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    root, 0, KEY_QUERY_VALUE, &hKey)) != ERROR_SUCCESS)
        return -lpP_pusherrmsg(L, ret, "uname", NULL);
    lua_pushcfunction(L, lpP_uname);
    lua_pushlightuserdata(L, S);
    lua_pushlightuserdata(L, &hKey);
    ret = lua_pcall(L, 2, 4, 0);
    RegCloseKey(hKey);
    return ret == LUA_OK ? 4 : (lua_pushnil(L), lua_insert(L, -2), 2);
}

static int lpL_getenv(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    LPWSTR ret = (lpP_addwstring(S, s),
            vec_push(L, S->wbuf, 0),
            vec_grow(L, S->wbuf, MAX_PATH));
    DWORD  wc  = GetEnvironmentVariableW(S->wbuf, ret, MAX_PATH);
    if (wc >= MAX_PATH) {
        ret = vec_grow(L, S->wbuf, wc);
        wc = GetEnvironmentVariableW(S->wbuf, ret, wc);
    }
    if (wc == 0) return -lp_pusherror(S->L, "getenv", NULL);
    return lpP_addlw2string(L, &S->buf, ret, wc, S->cp), lp_pushresult(S);
}

static int lpL_setenv(lua_State *L) {
    lp_State *S = lp_getstate(L);
    size_t klen, vlen;
    const char *name = luaL_checklstring(L, 1, &klen);
    const char *value = luaL_optlstring(L, 2, NULL, &vlen);
    LPWSTR wvalue = (lpP_addl2wstring(S->L, &S->wbuf, name, (int)klen, S->cp),
            vec_push(L, S->wbuf, 0),
            value ? lpP_addl2wstring(S->L, &S->wbuf, value, (int)vlen, S->cp) :
            NULL);
    if (!SetEnvironmentVariableW(S->wbuf, wvalue))
        return -lp_pusherror(L, "setenv", NULL);
    return lua_settop(L, 2), 1;
}

static int lpL_expandvars(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    LPWSTR ret = (lpP_addwstring(S, lp_applyparts(L, &S->buf, &S->p)),
            vec_push(L, S->wbuf, 0),
            vec_grow(L, S->wbuf, MAX_PATH));
    DWORD  wc  = ExpandEnvironmentStringsW(S->wbuf, ret, MAX_PATH);
    if (wc > MAX_PATH) {
        ret = vec_grow(L, S->wbuf, wc);
        wc = ExpandEnvironmentStringsW(S->wbuf, ret, wc);
    }
    if (wc == 0) return lp_pusherror(L, "expandvars", S->buf);
    vec_reset(S->buf);
    return lpP_addlw2string(L, &S->buf, ret, wc-1, S->cp), lp_pushresult(S);
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
#include <TargetConditionals.h>
# if TARGET_OS_OSX
#   include <libproc.h>
# endif
#endif

#if defined(__linux__)
# define LP_PLATFORM "linux"
#elif defined(__APPLE__)
# if TARGET_OS_IPHONE
#   define LP_PLATFORM "iOS"
# else
#   define LP_PLATFORM "macOS"
# endif
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
    else
        lua_pushfstring(L, "%s:(errno=%d): %s", title ? title : fn, err, msg);
    return -2;
}

static int lpL_ansi(lua_State *L)
{ return lua_isstring(L, 1) ? lua_settop(L, 1), 1 : 0; }

static int lpL_utf8(lua_State *L)
{ return lua_isstring(L, 1) ? lua_settop(L, 1), 1 : 0; }

/* scandir */

typedef struct lp_WalkLevel {
    DIR      *dir;
    unsigned  pos;
} lp_WalkLevel;

struct lp_Walker {
    LP_WALKER_PUBLIC;
};

static void lp_initwalker(lp_State *S, lp_Walker *w, char *s, int limit) {
    (void)S;
    memset(w, 0, sizeof(*w));
    w->buf  = s;
    w->limit = limit;
    w->state = LP_WALKINIT;
}

static void lp_freewalker(lp_Walker *w) {
    int i, len;
    for (i = 0, len = vec_len(w->levels); i < len; ++i)
        closedir(w->levels[i].dir);
    vec_free(w->buf);
    vec_free(w->levels);
}

static int lpP_isdir(lp_Walker *w, struct dirent *ent) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(__APPLE__)
    return (void)w, ent->d_type == DT_DIR;
#else
    struct stat buf;
    return (void)ent, lstat(w->buf, &buf) == 0 && S_ISDIR(buf.st_mode);
#endif
}

static int lpW_init(lua_State *L, lp_Walker *w) {
    struct stat buf;
    (void)L;
    if (lstat(*w->buf ? w->buf : LP_CURDIR, &buf) < 0) return 0;
    return w->state = ((w->buf[0] == '\0' || S_ISDIR(buf.st_mode)) ?
            (w->limit ? LP_WALKIN : LP_WALKDIR) : LP_WALKFILE);
}

static int lpW_in(lua_State *L, lp_Walker *w) {
    lp_WalkLevel *top = vec_grow(L, w->levels, 1);
    if (w->limit == 0) return 0;
    top->dir = opendir(*w->buf ? w->buf : LP_CURDIR);
    if (top->dir == NULL) return lp_pusherror(L, "walkin", w->buf);
    if (vec_len(w->buf) && !lp_isdirsep(vec_rawend(w->buf)[-1]))
        vec_push(L, w->buf, LP_DIRSEP[0]);
    top->pos = vec_rawlen(w->buf);
    vec_rawlen(w->levels) += 1;
    return --w->limit, 0;
}

static int lpW_out(lua_State *L, lp_Walker *w) {
    lp_WalkLevel *top = vec_rawend(w->levels) - 1;
    if (closedir(top->dir) < 0) return lp_pusherror(L, "walkout", w->buf);
    vec_rawlen(w->buf)  = top->pos ? top->pos - 1 : 0;
    *vec_rawend(w->buf) = 0;
    vec_rawlen(w->levels) -= 1;
    return ++w->limit, 0;
}

static int lpW_file(lua_State *L, lp_Walker *w) {
    lp_WalkLevel *top;
    struct dirent *ent;
    if (vec_len(w->levels) == 0) return 0;
    top = vec_rawend(w->levels) - 1;
    errno = 0;
    ent = readdir(vec_rawend(w->levels)[-1].dir);
    if (!ent && errno) return lp_pusherror(L, "walknext", w->buf);
    if (ent == NULL) return LP_WALKOUT;
    if (strcmp(ent->d_name, LP_CURDIR) == 0
            || strcmp(ent->d_name, LP_PARDIR) == 0)
        return LP_WALKSYS;
    vec_rawlen(w->buf) = top->pos;
    vec_concat(L, w->buf, ent->d_name);
    *vec_grow(L, w->buf, 1) = 0;
    return lpP_isdir(w, ent) ?
        (w->limit ? LP_WALKIN : LP_WALKDIR) : LP_WALKFILE;
}

/* dir operations */

static int lpL_getcwd(lua_State *L) {
    lp_State *S = lp_getstate(L);
    char *ret = vec_grow(L, S->buf, PATH_MAX);
    ret = getcwd(ret, PATH_MAX);
    if (ret == NULL) return -lp_pusherror(L, "getcwd", NULL);
    lua_pushstring(L, ret);
    return 1;
}

static int lpL_binpath(lua_State *L) {
#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
    lua_pushnil(L);
    lua_pushstring(L, "binpath not supoort on iOS");
    return 2;
#else
    lp_State *S = lp_getstate(L);
#ifdef __APPLE__
    char *ret = vec_grow(L, S->buf, PROC_PIDPATHINFO_MAXSIZE);
    int r = proc_pidpath(getpid(), ret, PROC_PIDPATHINFO_MAXSIZE);
    if (r < 0) return -lp_pusherror(L, "binpath", NULL);
#else
    char *ret = vec_grow(L, S->buf, PATH_MAX);
    int r = readlink("/proc/self/exe", ret, PATH_MAX);
    if (r < 0) return -lp_pusherror(L, "binpath", NULL);
#endif
    return lua_pushstring(L, ret), 1;
#endif
}

static int lp_abs(lp_State *S, const char *s) {
    lua_State *L = S->L;
    lua_pushstring(L, s);
    if (!lp_isdirsep(*s)) {
        size_t len;
        int ret;
        vec_reset(S->buf);
        if ((ret = lpL_getcwd(L)) != 1) return ret;
        s = lua_tolstring(L, -1, &len);
        if (len > 0 && lp_isdirsep(s[len-1]))
            return lua_insert(L, -2), lua_concat(L, 2), 1;
        lua_pushfstring(L, "%s" LP_DIRSEP "%s",
                lua_tostring(L, -1), lua_tostring(L, -2));
    }
    return 1;
}

static int lp_chdir(lp_State *S, const char *s)
{ return chdir(s) ? lp_pusherror(S->L, "chdir", s) : 0; }

static int lp_mkdir(lp_State *S, const char *s) {
    int r = mkdir(s, 0777);
    return (r != 0 && errno != EEXIST) ? lp_pusherror(S->L, "mkdir", s) : 0;
}

static int lp_rmdir(lp_State *S, const char *s)
{ return rmdir(s) ? lp_pusherror(S->L, "rmdir", s) : 0; }

static int lp_makedirs(lp_State *S, char *s) {
    lp_Part drive;
    size_t i = (lp_splitdrive(s, &drive), drive.e - s) + 1;
    size_t len = (s == S->buf ? vec_len(s) : strlen(s));
    for (; i <= len; ++i) {
        int r;
        while (i < len && !lp_isdirsep(s[i])) ++i;
        s[i] = 0;
        r = (mkdir(s, 0777) != 0);
        if (r && errno != EEXIST) return lp_pusherror(S->L, "makedirs", s);
        if (i != len) s[i] = LP_DIRSEP[0];
    }
    return 0;
}

static int lp_removedirs(lp_State *S, lp_Walker *w, int *pcount, void *ud) {
    (void)ud;
    if (w->state == LP_WALKFILE) {
        int r = remove(w->buf) == 0;
        return ++*pcount, r ? 0 : lp_pusherror(S->L, "remove", w->buf);
    }
    if (w->state == LP_WALKDIR || w->state == LP_WALKOUT) {
        int r = rmdir(w->buf) == 0;
        return ++*pcount, r ? 0 : lp_pusherror(S->L, "rmdir", w->buf);
    }
    return 0;
}

static int lp_unlockdirs(lp_State *S, lp_Walker *w, int *pcount, void *ud) {
    struct stat buf;
    (void)ud;
    if (w->state == LP_WALKFILE) {
        int r = stat(w->buf, &buf) == 0
            && chmod(w->buf, buf.st_mode | S_IWUSR) == 0;
        return ++*pcount, r ? 0 : lp_pusherror(S->L, "unlock", w->buf);
    }
    return 0;
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
    if (mkdir(dir, 0777) != 0) return -lp_pusherror(L, "tmpdir", dir);
    return 1;
}

/* file operations */

static int lp_remove(lp_State *S, const char *s)
{ return remove(s) ? lp_pusherror(S->L, "remove", s) : 0; }

static int lp_exists(lp_State *S, const char *s)
{ struct stat buf; return lp_bool(S->L, stat(s, &buf) == 0); }

static int lp_size(lp_State *S, const char *s) {
    struct stat buf;
    return stat(s, &buf) == 0 ? (lua_pushinteger(S->L, buf.st_size), 1) :
        lp_pusherror(S->L, "size", s);
}

static int lpL_touch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    struct utimbuf utb, *buf;
    int fh = open(s, O_WRONLY|O_CREAT, 0644), err = errno;
    if (fh >= 0) close(fh);
    else if (err!=EISDIR) return errno=err, -lp_pusherror(L, "touch", s);
    if (lua_gettop(L) == 1) /* set to current date/time */
        buf = NULL;
    else {
        utb.actime = (time_t)luaL_optinteger(L, 2, time(NULL));
        utb.modtime = (time_t)luaL_optinteger(L, 3, utb.actime);
        buf = &utb;
    }
    return utime(s, buf) == 0 ? lp_bool(L, 1) : -lp_pusherror(L, "utime", s);
}

static int lpL_rename(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    return rename(from, to) == 0 ? lp_bool(L, 1) :
        -lp_pusherror(L, "rename", to);
}

static int lpL_copy(lua_State *L) {
    lp_State *S = lp_getstate(L);
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int excl = lua_toboolean(L, 3);
    lua_Integer mode = luaL_optinteger(L, 4, 0644);
    char *buf = vec_grow(L, S->buf, BUFSIZ);
    size_t size;
    int dest, source = open(from, O_RDONLY, 0);
    if (source < 0) return -lp_pusherror(L, "open", from);
    dest = open(to, O_WRONLY|O_CREAT|(excl ? O_EXCL : O_TRUNC), mode);
    if (dest < 0) return -lp_pusherror(L, "open", to);
    while ((size = read(source, buf, BUFSIZ)) > 0) {
        int r = write(dest, buf, size)<0 ? (close(source),close(dest),1) : 0;
        if (r) return -lp_pusherror(L, "write", to);
    }
    close(source), close(dest);
    return lp_bool(L, 1);
}

static int lpL_symlink(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    return symlink(from, to) == 0 ? lp_bool(L, 1) :
        -lp_pusherror(L, "symlink", to);
}

/* path informations */

static int lp_realpath(lp_State *S, const char *s) {
    char *ret = (vec_push(S->L, S->buf, 0),
            vec_grow(S->L, S->buf, PATH_MAX));
    return realpath(s, ret) ?  (lua_pushstring(S->L, ret), 1) :
        lp_pusherror(S->L, "realpath", s);
}

#define lpP_isattr(CHECK)                              do { \
    struct stat buf;                                        \
    return lstat(s, &buf) == 0 && CHECK(buf.st_mode) ? 0 :  \
        lp_bool(S->L, 0);                                 } while (0)

static int lp_islink(lp_State *S, const char *s) { lpP_isattr(S_ISLNK); }
static int lp_isdir(lp_State *S, const char *s)  { lpP_isattr(S_ISDIR); }
static int lp_isfile(lp_State *S, const char *s) { lpP_isattr(S_ISREG); }

static int lp_ismount(lp_State *S, const char *s) {
    struct stat buf1, buf2;
    char *buf;
    assert(s == S->buf);
    if (lstat(s, &buf1) != 0 || S_ISLNK(buf1.st_mode))
        return lp_bool(S->L, 0);
    lp_joinparts(S->L, LP_PARDIR, &S->p);
    vec_reset(S->buf);
    lp_applyparts(S->L, &S->buf, &S->p);
    vec_push(S->L, S->buf, 0);
    buf = vec_grow(S->L, S->buf, PATH_MAX);
    if (realpath(S->buf, buf) == NULL || lstat(buf, &buf2) < 0) return 0;
    return (buf1.st_dev != buf2.st_dev) || (buf1.st_ino == buf2.st_ino) ? 0 :
        lp_bool(S->L, 0);
}

#define lp_time(L, time) do {                                    \
    struct stat buf;                                             \
    if (lstat(s, &buf) < 0) return lp_pusherror(S->L, #time, s); \
    return lua_pushinteger(S->L, buf.st_##time), 1; } while (0)

static int lp_ctime(lp_State *S, const char *s) { lp_time(L, ctime); }
static int lp_mtime(lp_State *S, const char *s) { lp_time(L, mtime); }
static int lp_atime(lp_State *S, const char *s) { lp_time(L, atime); }

/* utils */

static int lpL_uname(lua_State *L) {
    struct utsname buf;
    int r = uname(&buf);
    if (r != 0) return -lp_pusherror(L, "platform", NULL);
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

static int lpL_getenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = getenv(name);
    return lua_pushstring(L, value), 1;
}

static int lpL_setenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    if (value == NULL) {
        int r = unsetenv(name);
        return r ? -lp_pusherror(L, "unsetenv", NULL) : (lua_settop(L, 2), 1);
    }
    return setenv(name, value, 1) == 0 ?
        (lua_settop(L, 2), 1) : -lp_pusherror(L, "setenv", NULL);
}

#if !defined(__ANDROID__) && (!defined(__APPLE__) || defined(TARGET_OS_OSX))
static int lp_exapndvars(lua_State *L) {
    wordexp_t *p = (wordexp_t*)lua_touserdata(L, 1);
    lp_State *S = (lp_State*)lua_touserdata(L, 2);
    int i, res = wordexp(S->buf, p, 0);
    const char *errmsg = NULL;
    switch (res) {
    case WRDE_BADCHAR: errmsg = "invalid char"; break;
    case WRDE_NOSPACE: errmsg = "out of memory"; break;
    case WRDE_SYNTAX:  errmsg = "syntax error"; break;
    }
    if (errmsg) return luaL_error(L, errmsg);
    luaL_checkstack(L, p->we_wordc, "too many results");
    for (i = 0; i < (int)p->we_wordc; ++i)
        lua_pushstring(L, p->we_wordv[i]);
    return i;
}
#endif

static int lpL_expandvars(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_applyparts(L, &S->buf, &S->p);
#ifdef __ANDROID__
    lua_pushnil(L);
    lua_pushstring(L, "expandvars not support on Android");
    return -2;
#elif defined(__APPLE__) && !defined(TARGET_OS_OSX)
    lua_pushnil(L);
    lua_pushstring(L, "expandvars not support on iOS");
    return -2;
#else
    wordexp_t p;
    int ret, top = lua_gettop(L);
    memset(&p, 0, sizeof(wordexp_t));
    lua_pushcfunction(L, lp_exapndvars);
    lua_pushlightuserdata(L, &p);
    lua_pushlightuserdata(L, S);
    ret = lua_pcall(L, 2, LUA_MULTRET, 0);
    wordfree(&p);
    if (ret != LUA_OK) return lua_pushnil(L), lua_insert(L, -2), 2;
    return lua_gettop(L) - top;
#endif
}

#endif /* systems */

/* dir iterations */

typedef struct lp_ScanDir {
    lp_Walker w;
    int       inout;
} lp_ScanDir;

static int lp_walknext(lua_State *L, lp_Walker *w) {
    if (w->state == LP_WALKINIT) return lpW_init(L, w);
    for (;;) {
        int r;
        if (w->state == LP_WALKOUT && vec_len(w->levels) == 0) return 0;
        if (w->state == LP_WALKIN && (r = lpW_in(L, w)) < 0)   return r;
        switch (w->state = lpW_file(L, w)) {
        case LP_WALKOUT: if ((r = lpW_out(L, w)) < 0) return r; break;
        case LP_WALKSYS: continue; /* '.' or '..' */
        default: break;
        }
        return w->state;
    }
}

static int lpL_dirclose(lua_State *L) {
    lp_ScanDir *sd = (lp_ScanDir*)luaL_checkudata(L, 1, LP_WALKER_TYPE);
    lp_freewalker(&sd->w);
    return 0;
}

static int lp_pushdirresult(lua_State* L, lp_Walker* w) {
    const char *states[] = {"init", "in", "file", "out", "dir"};
    lua_pushstring(L, vec_len(w->buf) ? w->buf : LP_CURDIR);
    lua_pushstring(L, states[w->state]);
    return 2;
}

static int lpL_diriter(lua_State *L) {
    lp_ScanDir *ds = (lp_ScanDir*)luaL_checkudata(L, 1, LP_WALKER_TYPE);
    for (;;) {
        int ret = lp_walknext(L, &ds->w);
        if (ret < 0) lua_error(L);
        if (ret == 0) return 0;
        if (ds->inout || (ret != LP_WALKIN && ret != LP_WALKOUT))
            return lp_pushdirresult(L, &ds->w);
    }
}

static int lp_pushdir(lp_State *S, lp_Walker *w, int limit) {
    lua_State *L = S->L;
    if (vec_len(S->p.parts) > 1)
        lp_applyparts(L, &S->buf, &S->p);
    else
        *vec_grow(L, S->buf, 1) = 0;
    lp_initwalker(S, w, S->buf, limit);
    if (luaL_newmetatable(L, LP_WALKER_TYPE)) {
        lua_pushcfunction(L, lpL_dirclose);
        lua_pushvalue(L, -1); lua_setfield(L, -3, "__gc");
        lua_setfield(L, -2, "__close");
    }
    lua_setmetatable(L, -2);
    S->buf = NULL, lp_resetpath(&S->p);
    lua_pushcfunction(L, lpL_diriter);
    lua_pushvalue(L, -2);
    lua_pushnil(L);
    lua_pushvalue(L, -2);
    return 4;
}

static int lpL_dir(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_ScanDir *ds = lua_newuserdata(L, sizeof(lp_ScanDir));
    ds->inout = 0;
    return lp_pushdir(S, &ds->w, 1);
}

static int lpL_scandir(lua_State *L) {
    int isint, limit = (int)lua_tointegerx(L, -1, &isint);
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L) - isint);
    lp_ScanDir *ds = lua_newuserdata(L, sizeof(lp_ScanDir));
    ds->inout = 1;
    return lp_pushdir(S, &ds->w, limit ? limit : -1);
}

/* fnmatch & glob */

static int lp_matchone(int ch, lp_Part *p) {
    const char *s = p->s, *e = p->e;
    int inv = 0, res = 0;
    if (*s == '?' || (*s != '[' && lp_normchar(*s) == ch))
        return (p->s = s + 1, 1);
    if (*s != '[') return 0;
    if (*++s == '!') inv = 1, ++s;
    if (*s == ']') res = (ch == ']'), ++s;
    for (; !res && s < e && *s != ']'; ++s) {
        int range = s+1 < e && s[1] == '-'
                 && s+2 < e && s[2] != ']';
        res = range ? lp_normchar(*s) <= ch && ch <= lp_normchar(*s+2) :
            ch == lp_normchar(*s);
        if (range) s += 2;
    }
    while (s < e && *s != ']') ++s;
    if (s == e) s = p->s, res = (ch == '['), inv = 0;
    return res != inv ? (p->s = s + 1, 1) : 0;
}

static int lp_fnmatch(lp_Part s, lp_Part p) {
    const char *start = NULL, *match = NULL;
    if (p.s >= p.e) return s.s >= s.e;
    if (lp_len(p) == 1 && *p.s == '*') return 1;
    while (s.s < s.e) {
        if (p.s < p.e && lp_matchone(lp_normchar(*s.s), &p))
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

static int lpL_fnmatch(lua_State *L) {
    size_t slen, plen;
    const char *s = luaL_checklstring(L, 1, &slen);
    const char *p = luaL_checklstring(L, 2, &plen);
    return lp_bool(L, lp_fnmatch(lp_part(s, slen), lp_part(p, plen)));
}

static int lpL_match(lua_State *L) {
    lp_State *S = lp_getstate(L);
    const char *s = luaL_checkstring(L, 1);
    const char *p = luaL_checkstring(L, 2);
    unsigned i, j;
    luaL_argcheck(L, *p != '\0', 2, "empty pattern");
    lp_joinparts(L, s, &S->p), lp_joinparts(L, p, &S->pp);
    i = vec_rawlen(S->pp.parts), j = vec_rawlen(S->p.parts);
    if (lp_len(*S->pp.parts) != 0 /* pattern drive (if exists) must equal */
            && !lp_driveequal(*S->p.parts, *S->pp.parts)) return 0;
    if (S->pp.dots > 0) { /* has pattern '..' prefixed? */
        if (S->pp.dots != S->p.dots || i != j) return 0;
    } else if (S->pp.dots < 0) { /* is pattern absolute? */
        if (S->p.dots >= 0 || i != j) return 0; /* parts must be equal */
    } else if (i > j) return 0; /* pattern parts must be less than path */
    while (--i > 0 && --j > 0)
        if (!lp_fnmatch(S->p.parts[j], S->pp.parts[i])) return 0;
    return lua_settop(L, 1), 1;
}

typedef struct lp_GlobLevel {
    lp_Part *pat;   /* parts after '**' */
    int      len;   /* parts length */
    int      start; /* current match start level */
    int      match; /* current matched parts index, ==len -> complete match */
} lp_GlobLevel;

typedef struct lp_Glob {
    lp_Walker     w;
    lp_Path       p;               /* pattern path */
    char         *pat;             /* pattern buffer */
    lp_GlobLevel *stack;           /* '**' stack */
    lp_GlobLevel *current;         /* current level */
    char          dironly;         /* only match dir */
} lp_Glob;

static int lpG_ismagic(lp_Part p) {
    const char *s = p.s, *e = p.e;
    for (; s < e; ++s) if (*s == '?' || *s == '*' || *s == '[') return 1;
    return 0;
}

static lp_Part lpG_part(lp_Glob *g, unsigned idx) {
    lp_WalkLevel *wp = g->w.levels;
    size_t pos = (assert(idx < vec_len(g->w.levels)), wp[idx].pos);
    size_t end = idx+1 < vec_rawlen(wp) ?
        wp[idx+1].pos-1 : vec_rawlen(g->w.buf);
    return lp_part(g->w.buf + pos, (size_t)end - pos);
}

static int lpL_globclose(lua_State *L) {
    lp_Glob *g = (lp_Glob*)luaL_checkudata(L, 1, LP_GLOB_TYPE);
    if (g->pat) {
        lp_freewalker(&g->w);
        lp_freepath(&g->p);
        vec_free(g->stack);
        vec_free(g->pat);
    }
    return 0;
}

static void lpG_init(lp_State *S, lp_Glob *g, int limit) {
    lua_State *L = S->L;
    lp_Part *i, *s, *e, *ps;
    lp_GlobLevel gl = {NULL, 0, 0, 0};
    g->pat = lp_applyparts(L, &S->buf, &S->p), S->buf = NULL;
    lp_joinparts(L, g->pat, &g->p);
    s = &g->p.parts[1], e = vec_rawend(g->p.parts);
    vec_extend(L, g->w.buf, g->pat, lp_len(g->p.parts[0]));
    for (i = s; i < e && !lpG_ismagic(*i); ++i) {
        if (i > s || g->p.dots < 0) vec_push(L, g->w.buf, LP_DIRSEP[0]);
        vec_extend(L, g->w.buf, i->s, lp_len(*i));
    }
    *vec_grow(L, g->w.buf, 1) = 0;
    lp_initwalker(S, &g->w, g->w.buf, limit);
    if (i == e) return;
    for (s = ps = i; i < e; ++i) {
        if (lp_len(*i) != 2 || i->s[0] != '*' || i->s[1] != '*') continue;
        if (i == s || i != ps) /* first, or not empty */
            gl.pat = ps, gl.len = (int)(i-ps), vec_push(L, g->stack, gl);
        ps = i + 1; /* ps points to pattern next to '**' */
    }
    if (lp_len(i[-1]) == 0) i -= 1, g->dironly = 1;
    gl.pat = ps, gl.len = (int)(i-ps), vec_push(L, g->stack, gl);
    g->current = (assert(vec_len(g->stack) >= 1), &g->stack[0]);
}

static int lpG_match(lp_Glob *g, int level, int r);

static int lpG_matchcurrent(lp_Glob *g, lp_GlobLevel *cur) {
    int level = (int)vec_len(g->w.levels);
    if (cur->start+cur->match >= level) cur->match = level-cur->start-1;
    for (; cur->match < cur->len; ++cur->match) {
        int r = lp_fnmatch(lpG_part(g, cur->start+cur->match),
                cur->pat[cur->match]);
        if (!r) return 0;
    }
    return 1;
}

static int lpG_matchstart(lp_Glob *g, int level, int r) {
    lp_GlobLevel *cur = g->current;
    if (cur->len == 0) {
        g->current += 1;
        g->current->start = cur->start + cur->len;
        g->current->match = 0;
        return lpG_match(g, level, r);
    }
    if (level < cur->len) return 0;
    if (vec_rawlen(g->stack) == 1 && level == cur->len && r == LP_WALKIN)
        g->w.state = LP_WALKDIR;
    return lpG_matchcurrent(g, cur);
}

static int lpG_match(lp_Glob *g, int level, int r) {
    lp_GlobLevel *cur = g->current;
    int start, islast = (cur+1 == vec_rawend(g->stack));
    if (cur == g->stack) return lpG_matchstart(g, level, r);
    start = g->current[-1].start + g->current[-1].len;
    if (level < start) /* back to previous match */
        return g->current -= 1, lpG_match(g, level, r);
    if (islast && cur->len == 0) /* ends with '**' */
        return r != LP_WALKFILE;
    if (level < start+cur->len) /* has not enough room to match */
        return 0;
    if (cur->start != level - cur->len) {
        cur->start = level - cur->len;
        cur->match = 0;
    }
    if (lpG_matchcurrent(g, cur)) {
        if (islast) return !g->dironly || r != LP_WALKFILE;
        g->current += 1;
        g->current->start = cur->start + cur->len;
        g->current->match = 0;
        return lpG_match(g, level, r);
    }
    return 0;
}

static int lpL_globiter(lua_State *L) {
    lp_Glob *g = luaL_checkudata(L, 1, LP_GLOB_TYPE);
    for (;;) {
        int r = lp_walknext(L, &g->w);
        int level = (int)vec_len(g->w.levels);
        if (r <= 0) return r ? lua_error(L) : 0;
        if (vec_len(g->stack) == 0) {
            if (r == LP_WALKIN) g->w.state = LP_WALKDIR;
            assert(r==LP_WALKIN || r==LP_WALKFILE || r==LP_WALKDIR); 
            return lp_pushdirresult(L, &g->w);
        }
        if (lpG_match(g, level, r)) return lp_pushdirresult(L, &g->w);
    }
}

static int lpL_glob(lua_State *L) {
    int isint, limit = (int)lua_tointegerx(L, -1, &isint);
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L) - isint);
    lp_Glob *g = (luaL_argcheck(L, vec_len(S->p.parts) > 1,
                1, "Unacceptable pattern: ''"),
            lua_newuserdata(L, sizeof(lp_Glob)));
    memset(g, 0, sizeof(*g));
    if (luaL_newmetatable(L, LP_GLOB_TYPE)) {
        lua_pushcfunction(L, lpL_globclose);
        lua_pushvalue(L, -1); lua_setfield(L, -3, "__gc");
        lua_setfield(L, -2, "__close");
    }
    lua_setmetatable(L, -2);
    lpG_init(S, g, limit ? limit : -1);
    lua_pushcfunction(L, lpL_globiter);
    lua_pushvalue(L, -2);
    lua_pushnil(L);
    lua_pushvalue(L, -2); /* __close */
    return 4;
}

/* dir & file */

typedef int lp_DirOper(lp_State *S, lp_Walker *w, int *pcount, void *ud);

typedef struct lp_DirOp {
    lp_Walker   w;
    int         count;
    lp_DirOper *f;
    void       *ud;
} lp_DirOp;

static int lp_dirop_walker(lua_State *L) {
    lp_State *S = lp_getstate(L);
    lp_DirOp *op = (lp_DirOp*)lua_touserdata(L, 1);
    while (lp_walknext(L, &op->w)) {
        int ret = op->f(S, &op->w, &op->count, op->ud);
        if (ret < 0) return lua_error(L);
        if (ret > 0) return ret;
    }
    return 0;
}

static int lp_dirop(lua_State* L, lp_DirOper *f, void *ud) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_DirOp dirop;
    int ret;
    dirop.count = 0, dirop.f = f, dirop.ud = ud;
    lp_initwalker(S, &dirop.w, lp_applyparts(L, &S->buf, &S->p), -1);
    S->buf = NULL;
    lua_pushcfunction(L, lp_dirop_walker);
    lua_pushlightuserdata(L, &dirop);
    ret = lua_pcall(L, 1, 0, 0);
    lp_freewalker(&dirop.w);
    if (ret != LUA_OK) return lua_error(L);
    return lua_pushinteger(L, dirop.count), 1;
}

#define lp_routine(L,f)                                   do { \
    lp_State* S = lp_joinargs(L, 1, lua_gettop(L));            \
    int ret = f(S, lp_applyparts(L, &S->buf, &S->p));         \
    return ret ? (ret < 0 ? -ret : ret) : lp_pushresult(S);  } while (0)

static int lpL_resolve(lua_State *L)  { lp_routine(L, lp_realpath); }

static int lpL_chdir(lua_State* L)    { lp_routine(L, lp_chdir);    }
static int lpL_mkdir(lua_State* L)    { lp_routine(L, lp_mkdir);    }
static int lpL_rmdir(lua_State* L)    { lp_routine(L, lp_rmdir);    }
static int lpL_makedirs(lua_State* L) { lp_routine(L, lp_makedirs); }

static int lpL_removedirs(lua_State *L) { return lp_dirop(L, lp_removedirs, NULL); }
static int lpL_unlockdirs(lua_State *L) { return lp_dirop(L, lp_unlockdirs, NULL); }

static int lpL_isdir(lua_State *L)   { lp_routine(L, lp_isdir);   }
static int lpL_islink(lua_State *L)  { lp_routine(L, lp_islink);  }
static int lpL_isfile(lua_State *L)  { lp_routine(L, lp_isfile);  }
static int lpL_ismount(lua_State *L) { lp_routine(L, lp_ismount); }

static int lpL_ctime(lua_State *L) { lp_routine(L, lp_ctime);  }
static int lpL_mtime(lua_State *L) { lp_routine(L, lp_mtime);  }
static int lpL_atime(lua_State *L) { lp_routine(L, lp_atime);  }

static int lpL_exists(lua_State* L) { lp_routine(L, lp_exists); }
static int lpL_size(lua_State* L)   { lp_routine(L, lp_size);   }
static int lpL_remove(lua_State* L) { lp_routine(L, lp_remove); }

/* path information */

static int lpL_abs(lua_State* L) {
    lp_State* S = lp_joinargs(L, 1, lua_gettop(L));
    return lp_abs(S, lp_applyparts(L, &S->buf, &S->p));
}

static int lp_rel(lp_State *S, const char *p, const char *s) {
    lp_Part pd, sd;
    const char *pp = (lp_splitdrive(p, &pd), pd.e);
    const char *sp = (lp_splitdrive(s, &sd), sd.e);
    int i, dots = 0;
    /* return original when drive differs */
    if (!lp_driveequal(pd, sd)) return 0;
    while (*pp != '\0' && *sp != '\0' && lp_charequal(*sp, *pp))
        ++pp, ++sp;                               /* find common prefix, */
    if (*pp == '\0' && *sp == '\0')      /* return '.' when all the same */
        return lua_pushstring(S->L, LP_CURDIR), 1;
    while (p < pp && !(lp_isdirend(*pp) && lp_isdirend(*sp)))
        --pp, --sp;       /* find the beginning of first different part, */
    while (*sp) dots += lp_isdirsep(*sp), ++sp;    /* count remain parts */
    dots -= (s < sp && lp_isdirsep(sp[-1]));      /* remove trailing '/' */
    pp += (dots == 0 || !pp[1]);                   /* remove leading '/' */
    vec_reset(S->buf);
    if (dots) vec_concat(S->L, S->buf, LP_PARDIR);  /* write first '..'s */
    for (i = 1; i < dots; ++i)                       /* and other '/..'s */
        vec_concat(S->L, S->buf, LP_DIRSEP LP_PARDIR);
    vec_concat(S->L, S->buf, pp);
    return lp_pushresult(S);
}

static int lpL_rel(lua_State *L) {
    lp_State *S = lp_getstate(L);
    const char *s = luaL_checkstring(L, 1);
    const char *start = luaL_optstring(L, 2, NULL);
    int ret = (start ? lp_abs(S, start) : lpL_getcwd(L));
    start = lua_tostring(L, -1);
    if ((ret = lp_abs(lp_resetstate(S), s)) < 0) return -ret;
    if (lp_rel(S, lua_tostring(L, -1), start) == 0) {
        lp_resetstate(S);
        lp_joinparts(L, s, &S->p);
        lp_applyparts(L, &S->buf, &S->p);
        return lp_pushresult(S);
    }
    return 1;
}

static int lp_delparts(lua_State *L) {
    lp_Path *p = luaL_testudata(L, 1, LP_PARTS_ITER);
    lp_freepath(p);
    return 0;
}

static int lp_iterparts(lua_State *L) {
    lp_Path *p = luaL_checkudata(L, 1, LP_PARTS_ITER);
    int idx = (int)luaL_optinteger(L, 2, 0) + 1;
    if (lp_indexparts(L, idx, p) == 0)
        return 0;
    lua_pushinteger(L, idx);
    lua_insert(L, -2);
    return 2;
}

static int lp_newpartsiter(lua_State *L) {
    lp_Path *p = lua_newuserdata(L, sizeof(lp_Path));
    memset(p, 0, sizeof(*p));
    if (luaL_newmetatable(L, LP_PARTS_ITER)) {
        lua_pushcfunction(L, lp_delparts);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "__gc");
        lua_setfield(L, -2, "__close");
    }
    lua_setmetatable(L, -2);
    lp_joinparts(L, lua_tostring(L, -2), p);
    lua_pushvalue(L, -2);
    lua_setuservalue(L, -2);
    lua_pushcfunction(L, lp_iterparts);
    lua_insert(L, -2);
    lua_pushnil(L);
    lua_pushvalue(L, -2);
    return 4;
}

static int lpL_parts(lua_State *L) {
    lp_State *S = lp_getstate(L);
    int isint, idx = (int)lua_tointegerx(L, -1, &isint);
    int i, top = lua_gettop(L) - isint;
    for (i = 1; i <= top; ++i)
        lp_joinparts(L, luaL_checkstring(L, i), &S->p);
    if (isint) return lp_indexparts(L, idx, &S->p);
    lp_applyparts(L, &S->buf, &S->p), lp_pushresult(S);
    return lp_newpartsiter(L);
}

static int lpL_drive(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    return lp_applydrive(L, LP_DIRSEP[0], &S->buf, S->p.parts[0]), lp_pushresult(S);
}

static int lpL_root(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    return lua_pushstring(L, S->p.dots == -1 ?
            LP_DIRSEP : S->p.dots == -2 ? LP_DIRSEP LP_DIRSEP : ""), 1;
}

static int lpL_anchor(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_applydrive(L, LP_DIRSEP[0], &S->buf, S->p.parts[0]);
    vec_concat(L, S->buf, S->p.dots == -1 ?
            LP_DIRSEP : S->p.dots == -2 ? LP_DIRSEP LP_DIRSEP : "");
    return lp_pushresult(S);
}

static int lpL_parent(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_joinparts(L, LP_PARDIR, &S->p);
    return lp_applyparts(L, &S->buf, &S->p), lp_pushresult(S);
}

static int lpL_name(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_Part name = lp_name(&S->p);
    return lua_pushlstring(L, name.s, lp_len(name)), 1;
}

static int lpL_stem(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_Part name = lp_name(&S->p);
    const char *ext = lp_splitext(name);
    return lua_pushlstring(L, name.s, ext - name.s), 1;
}

static int lpL_suffix(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_Part name = lp_name(&S->p);
    const char *ext = lp_splitext(name);
    return lua_pushlstring(L, ext, name.e - ext), 1;
}

static int lp_itersuffixes(lua_State *L) {
    size_t len, pos = (size_t)lua_tointeger(L, lua_upvalueindex(2)), end = pos;
    const char *s = lua_tolstring(L, lua_upvalueindex(1), &len);
    lua_Integer i = lua_tointeger(L, lua_upvalueindex(3));
    if (end >= len) return 0;
    while (++end < len) if (s[end] == LP_EXTSEP[0]) break;
    lua_pushinteger(L, end);
    lua_replace(L, lua_upvalueindex(2));
    lua_pushinteger(L, i + 1);
    lua_pushvalue(L, -1);
    lua_replace(L, lua_upvalueindex(3));
    lua_pushlstring(L, s + pos, end - pos);
    return 2;
}

static int lpL_suffixes(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    lp_Part name = lp_name(&S->p);
    const char *ext = name.s < name.e && name.e[-1] == LP_EXTSEP[0] ?
        name.e : name.s;
    if (ext) {
        if (*ext == LP_EXTSEP[0]) ++ext;
        while (ext < name.e && *ext != LP_EXTSEP[0]) ++ext;
    }
    lua_pushlstring(L, ext, name.e - ext);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return lua_pushcclosure(L, lp_itersuffixes, 3), 1;
}

static int lpL_alt(lua_State *L) {
    lp_State *S = lp_joinargs(L, 1, lua_gettop(L));
    return lua_pushstring(L, lp_applyaltparts(L, &S->buf, &S->p)), 1;
}

static int lpL_libcall(lua_State *L) {
    lp_State *S = lp_joinargs(L, 2, lua_gettop(L));
    return lua_pushstring(L, lp_applyparts(L, &S->buf, &S->p)), 1;
}

/* entry */

#define LP_COMMON(X) \
    X(exists), X(resolve), X(getcwd), X(binpath), \
    X(isdir),  X(islink),  X(isfile), X(ismount),

LUAMOD_API int luaopen_path(lua_State *L) {
    luaL_Reg libs[] = {
        { "cwd", lpL_getcwd  },
        { "bin", lpL_binpath },
#define ENTRY(n) { #n, lpL_##n }
        ENTRY(ansi),
        ENTRY(utf8),
        ENTRY(alt),
        ENTRY(abs),
        ENTRY(rel),
        ENTRY(fnmatch),
        ENTRY(match),
        ENTRY(parts),
        ENTRY(drive),
        ENTRY(root),
        ENTRY(anchor),
        ENTRY(parent),
        ENTRY(name),
        ENTRY(stem),
        ENTRY(suffix),
        ENTRY(suffixes),
        LP_COMMON(ENTRY)
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
        { "realpath", lpL_resolve },
#define ENTRY(n) { #n, lpL_##n }
        ENTRY(dir),
        ENTRY(scandir),
        ENTRY(glob),
        ENTRY(chdir),
        ENTRY(mkdir),
        ENTRY(rmdir),
        ENTRY(makedirs),
        ENTRY(removedirs),
        ENTRY(unlockdirs),
        ENTRY(tmpdir),
        ENTRY(ctime),
        ENTRY(mtime),
        ENTRY(atime),
        ENTRY(size),
        ENTRY(touch),
        ENTRY(remove),
        ENTRY(copy),
        ENTRY(rename),
        ENTRY(symlink),
        LP_COMMON(ENTRY)
#undef  ENTRY
        { NULL, NULL }
    };
    return luaL_newlib(L, libs), 1;
}

LUAMOD_API int luaopen_path_env(lua_State *L) {
    luaL_Reg libs[] = {
        { "get",    lpL_getenv     },
        { "set",    lpL_setenv     },
        { "expand", lpL_expandvars },
        { "uname",  lpL_uname      },
        { NULL, NULL }
    };
    return luaL_newlib(L, libs), 1;
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

/* cc: flags+='-ggdb -Wextra -Wno-cast-function-type --coverage' run='lua test.lua'
 * unixcc: flags+='-O3 -shared -fPIC' output='path.so'
 * maccc: flags+='-shared -undefined dynamic_lookup' output='path.so'
 * win32cc: lua='Lua54' flags+='-s -O3 -mdll -DLUA_BUILD_AS_DLL -IC:/Devel/$lua/include'
 * win32cc: libs+='-L C:/Devel/$lua/lib -l$lua' output='path.dll' */

