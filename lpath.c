#define LUA_LIB
#ifdef __cplusplus
extern "C" {
#endif
#  include <lua.h>
#  include <lauxlib.h>
#ifdef __cplusplus
}
#endif

#define LPATH_VERSION "path 0.1"

#if LUA_VERSION_NUM < 502
# define lua_rawlen lua_objlen
# define luaL_newlib(L,libs) luaL_register(L, lua_tostring(L, 1), libs);
#endif

#ifdef __GNUC__
# define lp_unused __attribute__((unused)) static
#else
# define lp_unused static
#endif

#ifdef _WIN32
#  define ALT_SEP  "/"
#  define CUR_DIR  "."
#  define DEV_NULL "nul"
#  define DIR_SEP  "\\"
#  define EXT_SEP  "."
#  define PAR_DIR  ".."
#  define PATH_SEP ";"
#else
#  define ALT_SEP  "/"
#  define CUR_DIR  "."
#  define DEV_NULL "/dev/null"
#  define DIR_SEP  "/"
#  define EXT_SEP  "."
#  define PAR_DIR  ".."
#  define PATH_SEP ":"
#endif


/* path algorithms */

#include <assert.h>
#include <string.h>

#define ALT_SEP_CHAR  (ALT_SEP[0])
#define DIR_SEP_CHAR  (DIR_SEP[0])
#define EXT_SEP_CHAR  (EXT_SEP[0])
#define PATH_SEP_CHAR (PATH_SEP[0])
#define CURDIR_LEN    (sizeof(CUR_DIR)-1)
#define PARDIR_LEN    (sizeof(PAR_DIR)-1)

#define isdirsep(ch)   ((ch) == DIR_SEP_CHAR || (ch) == ALT_SEP_CHAR)
#define iseos(ch)       ((ch) == '\0')
#define isdirend(ch)   (isdirsep(ch) || iseos(ch))
#define iscurdir(s)    (memcmp((s), CUR_DIR, CURDIR_LEN) == 0 && isdirend(s[CURDIR_LEN]))
#define ispardir(s)    (memcmp((s), PAR_DIR, PARDIR_LEN) == 0 && isdirend(s[PARDIR_LEN]))

#define COMP_MAX 50

static size_t normpath(char *out, const char *in) {
    char *pos[COMP_MAX], **top = pos, *head = out;
    int isabs = isdirsep(*in);

    if (isabs) *out++ = DIR_SEP_CHAR;
    *top++ = out;

    while (!iseos(*in)) {
        while (isdirsep(*in)) ++in;

        if (iseos(*in))
            break;

        if (iscurdir(in)) {
            in += CURDIR_LEN;
            continue;
        }

        if (ispardir(in)) {
            in += PARDIR_LEN;
            if (top != pos + 1)
                out = *--top;
            else if (isabs)
                out = top[-1];
            else {
                memcpy(out, PAR_DIR, PARDIR_LEN);
                out[PARDIR_LEN] = DIR_SEP_CHAR;
                out += PARDIR_LEN+1;
            }
            continue;
        }

        if (top - pos >= COMP_MAX)
            return 0; /* path too complicate */

        *top++ = out;
        while (!isdirend(*in))
            *out++ = *in++;
        if (isdirsep(*in))
            *out++ = DIR_SEP_CHAR;
    }

    *out = '\0';
    if (*head == '\0') {
        memcpy(head, CUR_DIR, CURDIR_LEN);
        return 1;
    }
    return out - head;
}

lp_unused int normpath_base(lua_State *L, const char *s, size_t len) {
    if (len < BUFSIZ) {
        char buff[BUFSIZ];
        lua_pushlstring(L, buff, normpath(buff, s));
    }
    else {
        char *buff = (char*)lua_newuserdata(L, len);
        lua_pushlstring(L, buff, normpath(buff, s));
        lua_remove(L, -2);
    }
    return 1;
}

static int fn_equal(int ch1, int ch2) {
#ifndef _WIN32
    if (ch1 == ch2)
        return 1;
    if (ch1 >= 'A' && ch1 <= 'Z')
        ch1 += 'a' - 'A';
    if (ch2 >= 'A' && ch2 <= 'Z')
        ch1 += 'a' - 'A';
#endif
    return ch1 == ch2;
}

lp_unused int relpath_base(lua_State *L, const char *fn, const char *path) {
    luaL_Buffer b;
    int count_dot2 = 0;
    const char *pf = fn, *pp = path;
    if (iscurdir(path)) {
        path += CURDIR_LEN;
        if (isdirsep(*path)) ++path;
        if (iseos(*path)) {
            lua_pushstring(L, fn);
            return 1;
        }
    }
    while (*pf != '\0' && *pp != '\0' && fn_equal(*pf, *pp))
        ++pf, ++pp;
    if (*pf == '\0' && *pp == '\0') {
        lua_pushstring(L, CUR_DIR);
        return 1;
    }
    if (pf != fn && !isdirsep(*pf)) {
        while (fn < pf && !isdirsep(*pf))
            --pf, --pp;
    }
    if (path == pp && !isdirsep(*pp))
        ++count_dot2;
    for (; *pp != '\0'; ++pp)
        if (isdirsep(*pp))
            ++count_dot2;
    if (path < pp && isdirsep(pp[-1]))
        --count_dot2;
    luaL_buffinit(L, &b);
    while (count_dot2--) {
        luaL_addlstring(&b, PAR_DIR, PARDIR_LEN);
        luaL_addchar(&b, DIR_SEP_CHAR);
    }
    luaL_addstring(&b, pf == fn && !isdirsep(*pf) ? pf : pf + 1);
    luaL_pushresult(&b);
    return 1;
}

static int add_dirsep(lua_State *L, const char *s, size_t len) {
    size_t oldlen = len;
    while (len > 1 && isdirsep(s[len-1]))
        --len;
    if (len < oldlen)
        lua_pushlstring(L, s, len+1);
    else
        lua_pushfstring(L, "%s%s", s, DIR_SEP);
    return 1;
}

lp_unused int joinpath_base(lua_State *L) {
    luaL_Buffer b;
    int i, top = lua_gettop(L);
    luaL_buffinit(L, &b);
    for (i = 1; i <= top; ++i) {
        size_t len;
        const char *s = luaL_checklstring(L, i, &len);
        if (isdirsep(*s)) { /* absolute path? reset buffer */
#if LUA_VERSION_NUM >= 502
            b.n = 0; /* reset buffer */
#else
            luaL_pushresult(&b);
            lua_pop(L, 1);
            luaL_buffinit(L, &b);
#endif
        }
        if (i == top)
            lua_pushvalue(L, top);
        else
            add_dirsep(L, s, len);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    return 1;
}

lp_unused int splitpath_base(lua_State *L, const char *s, size_t len) {
    const char *last = &s[len-1];
    for (; s < last && !isdirsep(*last); --last)
        ;
#ifdef _WIN32 /* handle UNC path */
    if (last == s+1 && isdirsep(*s)) {
        lua_pushlstring(L, s, len);
        lua_pushliteral(L, "");
        return 2;
    }
#endif
    if (isdirsep(*last)) {
        const char *first = last;
        /* remove trailing slashes from head, unless it's all slashes */
        for (; s < first && isdirsep(*first); --first)
            ;
        if (isdirsep(*first))
            lua_pushlstring(L, s, last - s);
        else
            lua_pushlstring(L, s, first - s + 2); /* remain a splash '/' */
        lua_pushstring(L, last + 1);
    }
    else {
        lua_pushliteral(L, "");
        lua_pushlstring(L, s, len);
    }
    return 2;
}

lp_unused int splitext_base(lua_State *L, const char *s, size_t len) {
    const char *last = &s[len];
    while (s < last && *--last != EXT_SEP_CHAR)
        ;
    if (*last != EXT_SEP_CHAR) {
        lua_pushlstring(L, s, len);
        lua_pushliteral(L, "");
    }
    else {
        lua_pushlstring(L, s, last - s);
        lua_pushstring(L, last);
    }
    return 2;
}

/*
 * these algorithms are base algorithms, so in a specfied system, one
 * can "override" these. to do that, just #undef these macros, and
 * declare real foo_impl functions in your system. see examples in
 * windows port below.
 */
#define normpath_impl normpath_base
#define relpath_impl relpath_base
#define joinpath_impl joinpath_base
#define splitpath_impl splitpath_base
#define splitext_impl splitext_base

/* system specfied utils */

#define DIR_DATA "Dir Context"

typedef struct DirData DirData;
typedef struct WalkState WalkState;

typedef int WalkFunc(WalkState *S);

struct WalkState {
    WalkFunc *walk;   /* current callback */
    void *data;       /* userdata for callback */
    const char *path; /* base path to walk */
    const char *comp; /* current walking path */
    int isdir;        /* is current path a folder? */
    lua_State *L;     /* lua handler */
    int base;         /* the original stack top before walk */
    int limit;        /* the remaining level for walk */
};

static const char *check_pathcomps(lua_State *L, size_t *psz);
#define return_self(L) do { lua_settop((L), 1); return 1; } while (0)

/* all these functions must implement in specfied system */
/* miscs */
static int push_lasterror(lua_State *L, const char *title, const char *fn);
static int Lsetenv(lua_State *L);
/* path utils */
static int abs_impl(lua_State *L, const char *s);
static int Lexpandvars(lua_State *L);
static int Lrealpath(lua_State *L);
/* dir utils */
static void dir_close(DirData *d);
static int dir_impl(lua_State *L, DirData *d, const char *s);
static int isdir_impl(lua_State *L, const char *s);
static int chdir_impl(lua_State *L, const char *s);
static int mkdir_impl(lua_State *L, const char *s);
static int rmdir_impl(lua_State *L, const char *s);
static int walkpath_dfs(WalkState* S);
static int Lgetcwd(lua_State *L);
static int Lplatform(lua_State *L);
static int Ltype(lua_State *L);
/* file utils */
static int exists_impl(lua_State *L, const char *s);
static int remove_impl(lua_State *L, const char *s);
static int Lcmpftime(lua_State *L);
static int Lcopy(lua_State *L);
static int Lfsize(lua_State *L);
static int Lftime(lua_State *L);
static int Lrename(lua_State *L);
static int Ltouch(lua_State *L);

/* default system specfied implements  */

lp_unused int Lisabs_def(lua_State *L) {
    const char *s = check_pathcomps(L, NULL);
    lua_pushboolean(L, isdirsep(*s));
    return 1;
}

lp_unused int splitdrive_def(lua_State *L, const char *s, size_t len) {
    lua_pushliteral(L, "");
    lua_pushlstring(L, s, len);
    return 2;
}

static int Ljoin(lua_State *L);
lp_unused int Lnormcase_def(lua_State *L) { return Ljoin(L); }
lp_unused int Lansi_def(lua_State *L) { return lua_gettop(L); }
lp_unused int Lutf8_def(lua_State *L) { return lua_gettop(L); }

#define Lansi Lansi_def
#define Lisabs Lisabs_def
#define Lnormcase Lnormcase_def
#define Lutf8 Lutf8_def
#define splitdrive_impl splitdrive_def

#ifdef _WIN32

#include <Windows.h>

#define PLAT "windows"

/* windows-spec implements */
#undef Lansi
#undef Lisabs
#undef Lnormcase
#undef Lutf8
#undef joinpath_impl
#undef normpath_impl
#undef relpath_impl
#undef splitdrive_impl

static const char *next_dirsep(const char *p) {
    while (*p != '\0' && !isdirsep(*p))
        ++p;
    return *p == '\0' ? NULL : p;
}

static int splitdrive_impl(lua_State *L, const char *s, size_t len) {
    if (isdirsep(s[0]) && isdirsep(s[1]) && !isdirsep(s[2])) {
        const char *mp, *mp2;
        /* is a UNC path:
         * vvvvvvvvvvvvvvvvvvvv drive letter or UNC path
         * \\machine\mountpoint\directory\etc\...
         *           directory ^^^^^^^^^^^^^^^
         */
        if ((mp = next_dirsep(s + 2)) == NULL || isdirsep(mp[1])) {
            /* a UNC path can't have two slashes in a row
               (after the initial two) */
            lua_pushliteral(L, "");
            lua_insert(L, -2);
            return 2;
        }
        if ((mp2 = next_dirsep(mp + 2)) == NULL) {
            lua_pushlstring(L, s, len);
            lua_pushliteral(L, "");
            return 2;
        }
        if (s[0] != ALT_SEP_CHAR && s[1] != ALT_SEP_CHAR
                && *mp != ALT_SEP_CHAR)
            lua_pushlstring(L, s, mp2 - s);
        else {
            luaL_Buffer b;
            luaL_buffinit(L, &b);
            luaL_addchar(&b, '\\');
            luaL_addchar(&b, '\\');
            luaL_addlstring(&b, s + 2, mp - s - 2);
            luaL_addchar(&b, '\\');
            luaL_addlstring(&b, mp+1, mp2 - mp - 1);
            luaL_pushresult(&b);
        }
        lua_pushlstring(L, mp2, len-(mp2-s));
        return 2;
    }
    else if (s[1] == ':') {
        lua_pushfstring(L, "%c:", s[0]);
        lua_pushlstring(L, s+2, len-2);
        return 2;
    }
    lua_pushliteral(L, "");
    lua_pushlstring(L, s, len);
    return 2;
}

static int normpath_impl(lua_State *L, const char *s, size_t len) {
    splitdrive_impl(L, s, len);
    s = lua_tolstring(L, -1, &len);
    normpath_base(L, s, len);
    lua_remove(L, -2);
    lua_concat(L, 2);
    return 1;
}

static int drive_equal(const char *d1, const char *d2, int *same_case) {
    int issame = 1;
    while (*d1 != '\0' && *d2 != '\0' && fn_equal(*d1, *d2)) {
        if (*d1 != *d2) issame = 0;
        ++d1, ++d2;
    }
    if (same_case) *same_case = issame;
    return *d1 == '\0' && *d2 == '\0';
}

static int relpath_impl(lua_State *L, const char *fn, const char *path) {
    size_t fdlen, dlen;
    const char *fdrive, *pdrive;
    int ret;
    /* check fn and path at the same drive */
    splitdrive_impl(L, fn, strlen(fn));
    splitdrive_impl(L, path, strlen(path));
    fdrive = lua_tolstring(L, -4, &fdlen);
    pdrive = lua_tolstring(L, -2, &dlen);
    ret = !fdlen || !dlen || drive_equal(fdrive, pdrive, NULL);
    lua_pop(L, 4);
    if (!ret) {
        lua_pushstring(L, fn);
        return 1;
    }
    return relpath_base(L, fn, path);
}

static int joinpath2(lua_State *L, int top) {
    size_t dlen, len;
    const char *d, *s = luaL_checklstring(L, 1, &len);
    int i;
    splitdrive_impl(L, s, len); /* drive(d) */
    d = lua_tolstring(L, top+1, &dlen);
    for (i = 2; i <= top; ++i) {
        int same_case = 1;
        size_t cdlen, cplen;
        const char *cd, *cp;
        s = luaL_checklstring(L, i, &len);
        splitdrive_impl(L, s, len); /* cur_drive(cd), cur_path(cp) */
        cd = lua_tolstring(L, -2, &cdlen);
        cp = lua_tolstring(L, -1, &cplen);
        if (cplen != 0 && isdirsep(cp[0])) {
            /* second path is absolute */
            lua_replace(L, top+2);
            if (cdlen || !dlen) {
                lua_replace(L, top+1);
                d = cd, dlen = cdlen;
            }
            else
                lua_pop(L, 1);
            continue;
        }
        if (cdlen && !drive_equal(d, cd, &same_case)) {
            /* different drives => ignore the first path entirely */
            lua_replace(L, top+2);
            lua_replace(L, top+1);
            d = cd, dlen = cdlen;
            continue;
        }
        if (!same_case) {
            /* same drive in different case */
            lua_pushvalue(L, -2);
            lua_replace(L, top+1);
            d = cd, dlen = cdlen;
        }
        /* second path is relative to the first */
        s = lua_tolstring(L, top+2, &len);
        add_dirsep(L, s, len);
        lua_pushvalue(L, -2); /* cp */
        lua_concat(L, 2);
        lua_replace(L, top+2);
        lua_pop(L, 2); /* pop drive and path */
    }
    return 2;
}

static int joinpath_impl(lua_State *L) {
    joinpath2(L, lua_gettop(L));
    lua_concat(L, 2); /* concat drive and path */
    return 1;
}

/* code page support */

static UINT current_cp = CP_ACP;

static int push_win32error(lua_State *L, DWORD errnum, const char *title, const char *fn) {
    /* error message are always ANSI copy page */
    LPSTR msg;
    lua_pushnil(L);
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        errnum,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg,
        0, NULL);
    if (title && fn)
        lua_pushfstring(L, "%s(%s): %s", title, fn, msg);
    else if (title || fn)
        lua_pushfstring(L, "%s: %s", title ? title : fn, msg);
    else
        lua_pushstring(L, msg);
    LocalFree(msg);
    return 2;
}

static LPCSTR push_multibyte(lua_State *L, LPCWSTR ws, int cp) {
    LPSTR buff;
    int len = (int)wcslen(ws); /* doesn't need include '\0' in lua string */
    int bytes = WideCharToMultiByte(cp, 0, ws, len, NULL, 0, NULL, NULL);
    if (bytes == 0) goto error;
    buff = (LPSTR)lua_newuserdata(L, bytes * sizeof(CHAR));
    bytes = WideCharToMultiByte(cp, 0, ws, len, buff, bytes, NULL, NULL);
    if (bytes == 0) goto error;
    lua_pushlstring(L, (const char*)buff, bytes*sizeof(CHAR));
    lua_remove(L, -2);
    return lua_tostring(L, -1);
error:
    push_win32error(L, GetLastError(), "multibyte", NULL);
    lua_error(L);
    return NULL; /* to avoid warning */
}

static LPCWSTR push_widechar(lua_State *L, LPCSTR s, int cp) {
    LPWSTR buff;
    int bytes = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
    if (bytes == 0) goto error;
    buff = (LPWSTR)lua_newuserdata(L, bytes * sizeof(WCHAR));
    bytes = MultiByteToWideChar(cp, 0, s, -1, buff, bytes);
    if (bytes == 0) goto error;
    lua_pushlstring(L, (const char*)buff, bytes*sizeof(WCHAR));
    lua_remove(L, -2);
    return (LPCWSTR)lua_tostring(L, -1);
error:
    push_win32error(L, GetLastError(), "unicode", NULL);
    lua_error(L);
    return NULL; /* to avoid warning */
}

static LPCSTR push_pathA(lua_State *L, LPCWSTR ws) {
    CHAR buff[MAX_PATH];
    LPSTR p = buff;
    int len = (int)wcslen(ws);
    int bytes = WideCharToMultiByte(current_cp, 0, ws, len, p, MAX_PATH, NULL, NULL);
    if (bytes == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER)
            return push_multibyte(L, ws, current_cp);
        push_win32error(L, err, "ansi_path", NULL);
        lua_error(L);
    }
    lua_pushlstring(L, (const char*)p, bytes*sizeof(CHAR));
    if (p != buff) lua_remove(L, -2);
    return (LPCSTR)lua_tostring(L, -1);
}

static LPCWSTR push_pathW(lua_State *L, LPCSTR s) {
    WCHAR buff[MAX_PATH];
    LPWSTR p = buff;
    int bytes = MultiByteToWideChar(current_cp, 0, s, -1, p, MAX_PATH);
    if (bytes == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER)
            return push_widechar(L, s, current_cp);
        push_win32error(L, err, "wide_path", s);
        lua_error(L);
    }
    lua_pushlstring(L, (const char*)p, bytes*sizeof(WCHAR));
    if (p != buff) lua_remove(L, -2);
    return (LPCWSTR)lua_tostring(L, -1);
}

static int Lansi(lua_State *L) {
    if (lua_isstring(L, 1)) {
        const char *utf8 = luaL_checkstring(L, 1);
        LPCWSTR ws = push_widechar(L, utf8, CP_UTF8);
        push_multibyte(L, ws, CP_ACP);
        return 1;
    }
    if (lua_isnone(L, 1) || lua_toboolean(L, 1))
        current_cp = CP_ACP;
    return 0;
}

static int Lutf8(lua_State *L) {
    if (lua_isstring(L, 1)) {
        const char *ansi = luaL_checkstring(L, 1);
        LPCWSTR ws = push_widechar(L, ansi, CP_ACP);
        push_multibyte(L, ws, CP_UTF8);
        return 1;
    }
    if (lua_isnone(L, 1) || lua_toboolean(L, 1))
        current_cp = CP_UTF8;
    return 0;
}

/* misc utils */

static int push_lasterror(lua_State *L, const char *title, const char *fn) {
    return push_win32error(L, GetLastError(), title, fn);
}

static int Lplatform(lua_State *L) {
    OSVERSIONINFOW ver;
    ver.dwOSVersionInfoSize = sizeof(ver);
    if (!GetVersionExW(&ver))
        return push_lasterror(L, "platform", NULL);
    lua_pushfstring(L, "Windows %d.%d Build %d",
            ver.dwMajorVersion,
            ver.dwMinorVersion,
            ver.dwBuildNumber);
    lua_pushinteger(L, ver.dwMajorVersion);
    lua_pushinteger(L, ver.dwMinorVersion);
    lua_pushinteger(L, ver.dwBuildNumber);
    return 4;
}

static void push_word64(lua_State *L, ULONGLONG ull) {
    if (sizeof(lua_Integer) >= 8)
        lua_pushinteger(L, (lua_Integer)ull);
    else
        lua_pushnumber(L, (lua_Number)ull);
}

static ULONGLONG check_word64(lua_State *L, int idx) {
    ULONGLONG ull;
    if (sizeof(lua_Integer) >= 8)
        ull = (ULONGLONG)luaL_checkinteger(L, idx);
    else
        ull = (ULONGLONG)luaL_checknumber(L, idx);
    return ull;
}

static void push_filetime(lua_State *L, PFILETIME pft) {
    ULARGE_INTEGER ln;
    ln.LowPart = pft->dwLowDateTime;
    ln.HighPart = pft->dwHighDateTime;
    push_word64(L, ln.QuadPart);
}

static int opt_filetime(lua_State *L, int idx, PFILETIME pft) {
    ULARGE_INTEGER ln;
    if (lua_isnoneornil(L, idx))
        return 0;
    ln.QuadPart = check_word64(L, idx);
    pft->dwLowDateTime = ln.LowPart;
    pft->dwHighDateTime = ln.HighPart;
    return 1;
}

static int Lsetenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    LPCWSTR wname = push_pathW(L, name);
    LPCWSTR wvalue = value ? push_pathW(L, name) : NULL;
    BOOL ret = SetEnvironmentVariableW(wname, wvalue);
    lua_settop(L, 1);
    return ret ? 1 : push_lasterror(L, "setenv", NULL);
}

/* path utils */

typedef DWORD WINAPI PGetFinalPathNameByHandleW(
  _In_   HANDLE hFile,
  _Out_  LPWSTR lpszFilePath,
  _In_   DWORD cchFilePath,
  _In_   DWORD dwFlags
);
static PGetFinalPathNameByHandleW init_GetFinalPathNameByHandleW;
static PGetFinalPathNameByHandleW *pGetFinalPathNameByHandleW = &init_GetFinalPathNameByHandleW;
static DWORD WINAPI init_GetFinalPathNameByHandleW(
  _In_   HANDLE hFile,
  _Out_  LPWSTR lpszFilePath,
  _In_   DWORD cchFilePath,
  _In_   DWORD dwFlags) {
    HMODULE hModule;
    hModule = GetModuleHandleA("KERNEL32.dll");
    pGetFinalPathNameByHandleW = (PGetFinalPathNameByHandleW*)GetProcAddress(
            hModule, "GetFinalPathNameByHandleW");
    if (pGetFinalPathNameByHandleW == NULL)
        return 0;
    return pGetFinalPathNameByHandleW(hFile, lpszFilePath, cchFilePath, dwFlags);
}

static int abs_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    WCHAR buff[MAX_PATH];
    LPWSTR p = buff;
    DWORD len = GetFullPathNameW(ws, MAX_PATH, p, NULL);
    if (len > MAX_PATH) {
        p = (LPWSTR)lua_newuserdata(L, len * sizeof(WCHAR));
        len = GetFullPathNameW(ws, len, p, NULL);
    }
    if (len == 0) return push_lasterror(L, "abs", s);
    push_pathA(L, p);
    return 1;
}

static int realpath_impl(lua_State *L, const char *s) {
    WCHAR buff[MAX_PATH], *p = buff;
    DWORD bytes;
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            0,                     /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            0,                     /* no file attributes   */
            NULL);                 /* no attr. template    */
    lua_pop(L, 1); /* remove wide path */
    if(hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "open", s);
    bytes = pGetFinalPathNameByHandleW(hFile, p, MAX_PATH, 0);
    if (bytes > MAX_PATH) {
        p = (LPWSTR)lua_newuserdata(L, bytes * sizeof(WCHAR));
        bytes = pGetFinalPathNameByHandleW(hFile, p, bytes, 0);
    }
    CloseHandle(hFile);
    if (bytes == 0) {
        if (pGetFinalPathNameByHandleW == NULL)
            return abs_impl(L, s);
        return push_lasterror(L, "realpath", s);
    }
    if (bytes - 4 > MAX_PATH)
        push_pathA(L, p);
    else
        push_pathA(L, p + 4);
    return 1;
}

static int Lrealpath(lua_State *L) {
    const char *s = check_pathcomps(L, NULL);
    if (pGetFinalPathNameByHandleW == NULL)
        return abs_impl(L, s);
    return realpath_impl(L, s);
}

static int Lnormcase(lua_State *L) {
    size_t len;
    const char *s;
    size_t i, start = 0;
    luaL_Buffer b;
    Lnormcase_def(L);
    s = lua_tolstring(L, -1, &len);
    luaL_buffinit(L, &b);
    if (s[1] == ':')
        start = 2;
    for (i = 0; i < len; ++i)
        luaL_addchar(&b, i >= start ? toupper(s[i]) : s[i]);
    luaL_pushresult(&b);
    return 1;
}

static int Lisabs(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    if (isdirsep(s[0]) && isdirsep(s[1]) && !isdirsep(s[2])) {
        const char *mp, *mp2;
        lua_pushboolean(L,
                ((mp = strchr(s+2, DIR_SEP_CHAR)) == NULL
                 && (mp = strchr(s+2, ALT_SEP_CHAR)) == NULL)
                || isdirsep(mp[1])
                || ((mp2 = strchr(mp+2, DIR_SEP_CHAR)) == NULL
                    && (mp2 = strchr(mp+2, ALT_SEP_CHAR)) == NULL));
    }
    else if (s[1] == ':')
        lua_pushboolean(L, isdirsep(s[2]));
    else
        lua_pushboolean(L, isdirsep(s[0]));
    return 1;
}

static int Lexpandvars(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    LPCWSTR ws = push_pathW(L, s);
    WCHAR buff[MAX_PATH], *p = buff;
    DWORD bytes = ExpandEnvironmentStringsW(ws, p, MAX_PATH);
    if (bytes > MAX_PATH) {
        p = (LPWSTR)lua_newuserdata(L, bytes * sizeof(WCHAR));
        bytes = ExpandEnvironmentStringsW(ws, p, bytes);
    }
    if (bytes == 0) return push_lasterror(L, "expand", NULL);
    push_pathA(L, p);
    return 1;
}

/* dir utils */

struct DirData {
    HANDLE hFile;
    WIN32_FIND_DATAW wfd;
    DWORD lasterror;
};

static void dir_close(DirData *d) {
    if (d->lasterror == NO_ERROR)
        FindClose(d->hFile);
    d->hFile = INVALID_HANDLE_VALUE;
    d->lasterror = ERROR_NO_MORE_FILES;
}

static int dir_iter(lua_State *L) {
    int skip;
    DirData *d = (DirData*)lua_touserdata(L, 1);
    assert(d != NULL);
redo:
    if (d->lasterror == ERROR_NO_MORE_FILES) {
        FindClose(d->hFile);
        d->hFile = INVALID_HANDLE_VALUE;
        return 0;
    }
    if (d->lasterror != NO_ERROR) {
        push_win32error(L, d->lasterror, "dir.next", NULL);
        return lua_error(L);
    }
    skip = !wcscmp(d->wfd.cFileName, L".") || !wcscmp(d->wfd.cFileName, L"..");
    if (!skip) {
        ULARGE_INTEGER ul;
        push_pathA(L, d->wfd.cFileName);
        lua_pushstring(L, d->wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ?
                "dir" : "file");
        ul.LowPart = d->wfd.nFileSizeLow;
        ul.HighPart = d->wfd.nFileSizeHigh;
        push_word64(L, ul.QuadPart);
        push_filetime(L, &d->wfd.ftCreationTime);
        push_filetime(L, &d->wfd.ftLastWriteTime);
        push_filetime(L, &d->wfd.ftLastAccessTime);
    }
    if (!FindNextFileW(d->hFile, &d->wfd))
        d->lasterror = GetLastError();
    if (skip) goto redo;
    return 6;
}

static int dir_impl(lua_State *L, DirData *d, const char *s) {
    HANDLE hFile;
    LPCWSTR wpattern;
    add_dirsep(L, s, strlen(s));
    lua_pushliteral(L, "*");
    lua_concat(L, 2);
    wpattern = push_pathW(L, lua_tostring(L, -1));
    hFile = FindFirstFileW(wpattern, &d->wfd);
    lua_pop(L, 2); /* remove pattern and wide path */
    if (hFile == INVALID_HANDLE_VALUE) {
        push_lasterror(L, "dir", s);
        return lua_error(L);
    }
    d->lasterror = NO_ERROR;
    d->hFile = hFile;
    lua_pushcfunction(L, dir_iter);
    lua_insert(L, -2);
    return 2;
}

static int walkpath_dfs(WalkState *S) {
    lua_State *L = S->L;
    int nrets = 0;
    WIN32_FIND_DATAW wfd;
    LPCWSTR ws;
    HANDLE hFile;
    if (S->limit == 0 || !S->isdir)
        return S->walk(S);

    lua_pushfstring(L, "%s" DIR_SEP "%s*", S->path, S->comp);
    ws = push_pathW(L, lua_tostring(L, -1));
    hFile = FindFirstFileW(ws, &wfd);
    lua_pop(L, 2); /* remove path and wide path */

    if (hFile == INVALID_HANDLE_VALUE) {
        lua_pushfstring(L, "%s" DIR_SEP "%s", S->path, S->comp);
        nrets = push_lasterror(L, "dir", lua_tostring(L, -1));
        lua_remove(L, -nrets-1);
        return nrets;
    }

    do {
        const char *old_comp = S->comp;
        LPCWSTR wfn = wfd.cFileName;
        S->isdir = wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        if (S->isdir && (!wcscmp(wfn, L".") || !wcscmp(wfn, L"..")))
            continue;
        luaL_checkstack(L, 2, "directory too deep");
        push_pathA(L, wfn);
        lua_pushfstring(L, "%s%s%s", S->comp, lua_tostring(L, -1),
                S->isdir ? DIR_SEP : "");

        S->comp = lua_tostring(L, -1);
        --S->limit;
        nrets = walkpath_dfs(S);
        ++S->limit;
        S->comp = old_comp;

        if (nrets) {
            lua_remove(L, -nrets-1);
            lua_remove(L, -nrets-1);
            FindClose(hFile);
            return nrets;
        }
        lua_pop(L, 2);
    } while (FindNextFileW(hFile, &wfd));
    FindClose(hFile);

    S->isdir = 1;
    return S->walk(S);
}

static int isdir_impl(lua_State *L, const char *s) {
    WIN32_FIND_DATAW wfd;
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = FindFirstFileW(ws, &wfd);
    lua_pop(L, 1);
    if (hFile != INVALID_HANDLE_VALUE)
        FindClose(hFile);
    return hFile != INVALID_HANDLE_VALUE &&
        (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int chdir_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    BOOL ret = SetCurrentDirectoryW(ws);
    lua_pop(L, 1); /* remove wide path */
    return ret ? 0 : push_lasterror(L, "chdir", s);
}

static int mkdir_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    BOOL ret = CreateDirectoryW(ws, NULL);
    lua_pop(L, 1); /* remove wide path */
    if (!ret) {
        DWORD lasterror = GetLastError();
        if (lasterror == ERROR_ALREADY_EXISTS)
            return 0;
        return push_win32error(L, lasterror, "mkdir", s);
    }
    return 0;
}

static int rmdir_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    BOOL ret = RemoveDirectoryW(ws);
    lua_pop(L, 1); /* remove wide path */
    return ret ? 0 : push_lasterror(L, "rmdir", s);
}

static int Lgetcwd(lua_State *L) {
    WCHAR buff[MAX_PATH];
    LPWSTR p = buff;
    DWORD len = GetCurrentDirectoryW(MAX_PATH, p);
    if (len > MAX_PATH) {
        p = (LPWSTR)lua_newuserdata(L, len * sizeof(WCHAR));
        len = GetCurrentDirectoryW(len, p);
    }
    if (len == 0) return push_lasterror(L, "getcwd", NULL);
    push_pathA(L, p);
    return 1;
}

static int Ltype(lua_State *L) {
    WIN32_FIND_DATAW wfd;
    const char *s = check_pathcomps(L, NULL);
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = FindFirstFileW(ws, &wfd);
    int isdir, islink;
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "type", s);
    FindClose(hFile);
    isdir = wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    islink = wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
    lua_pushstring(L, islink ? "link" :
                      isdir ? "dir" : "file");
    return 1;
}

/* file utils */

static int exists_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            FILE_WRITE_ATTRIBUTES, /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            FILE_FLAG_BACKUP_SEMANTICS, /* open directory also */
            NULL);                 /* no attr. template    */
    lua_pop(L, 1); /* remove wide path */
    CloseHandle(hFile);
    return hFile != INVALID_HANDLE_VALUE;
}

static int remove_impl(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    BOOL ret = DeleteFileW(ws);
    lua_pop(L, 1); /* remove wide path */
    return ret ? 0 : push_lasterror(L, "remove", s);
}

static int Lcmpftime(lua_State *L) {
    WIN32_FIND_DATAW wfd1, wfd2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 2);
    int use_atime = lua_toboolean(L, 3);
    LPCWSTR wf1 = push_pathW(L, f1);
    LPCWSTR wf2 = push_pathW(L, f2);
    HANDLE hFile1, hFile2;
    LONG cmp_c, cmp_m, cmp_a;
    if ((hFile1 = FindFirstFileW(wf1, &wfd1)) == INVALID_HANDLE_VALUE) {
        lua_pushinteger(L, -1);
        return 1;
    }
    FindClose(hFile1);
    if ((hFile2 = FindFirstFileW(wf2, &wfd2)) == INVALID_HANDLE_VALUE) {
        lua_pushinteger(L, 1);
        return 1;
    }
    FindClose(hFile2);
    cmp_c = CompareFileTime(&wfd1.ftCreationTime, &wfd2.ftCreationTime);
    cmp_m = CompareFileTime(&wfd1.ftLastWriteTime, &wfd2.ftLastWriteTime);
    cmp_a = CompareFileTime(&wfd1.ftLastAccessTime, &wfd2.ftLastAccessTime);
    if (use_atime)
        lua_pushinteger(L, cmp_c == 0 ? cmp_m == 0 ? cmp_a : cmp_m : cmp_c);
    else
        lua_pushinteger(L, cmp_c == 0 ? cmp_m : cmp_c);
    return 1;
}

static int Lcopy(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int failIfExists = lua_toboolean(L, 3);
    LPCWSTR wfrom = push_pathW(L, from);
    LPCWSTR wto = push_pathW(L, to);
    if (!CopyFileW(wfrom, wto, failIfExists))
        return push_lasterror(L, "copy", NULL);
    lua_pushboolean(L, 1);
    return 1;
}

static int Lfsize(lua_State *L) {
    WIN32_FIND_DATAW wfd;
    ULARGE_INTEGER  ul;
    const char *s = check_pathcomps(L, NULL);
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = FindFirstFileW(ws, &wfd);
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "fsize", s);
    FindClose(hFile);
    ul.LowPart = wfd.nFileSizeLow;
    ul.HighPart = wfd.nFileSizeHigh;
    push_word64(L, ul.QuadPart);
    return 1;
}

static int Lftime(lua_State *L) {
    WIN32_FIND_DATAW wfd;
    const char *s = check_pathcomps(L, NULL);
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = FindFirstFileW(ws, &wfd);
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "ftime", s);
    FindClose(hFile);
    push_filetime(L, &wfd.ftCreationTime);
    push_filetime(L, &wfd.ftLastWriteTime);
    push_filetime(L, &wfd.ftLastAccessTime);
    return 3;
}

static int Ltouch(lua_State *L) {
    BOOL success;
    FILETIME at, mt;
    SYSTEMTIME st;
    const char *s = luaL_checkstring(L, 1);
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = CreateFileW(ws, /* file to open       */
            FILE_WRITE_ATTRIBUTES, /* open for write attributes   */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security   */
            OPEN_ALWAYS,           /* existing file only */
            0,                     /* no file attributes */
            NULL);                 /* no attr. template  */
    lua_pop(L, 1); /* remove wide path */
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "touch", s);
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &mt);
    at = mt;
    opt_filetime(L, 2, &mt);
    opt_filetime(L, 3, &at);
    success = SetFileTime(hFile,
            NULL, /* create time */
            &at,  /* access time */
            &mt   /* modify time */
            );
    if (!CloseHandle(hFile) || !success)
        return push_lasterror(L, "settime", s);
    return_self(L);
}

static int Lrename(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    LPCWSTR wfrom = push_pathW(L, from);
    LPCWSTR wto = push_pathW(L, to);
    if (!MoveFileW(wfrom, wto))
        return push_lasterror(L, "rename", NULL);
    lua_pushboolean(L, 1);
    return 1;
}

#elif defined(_POSIX_SOURCE) || defined(__ANDROID__)

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#ifndef __ANDROID__
# include <wordexp.h>
#endif

#ifdef __ANDROID__
# define PLAT "android"
#else
# define PLAT "posix"
#endif

#ifdef DISABLE_64
# define push_word64 lua_pushnumber
# define check_word64 luaL_checknumber
# define opt_word64 luaL_optnumber
#else
# define opt_word64(L,idx,dft) (lua_isnoneornil(L, idx)?dft:check_word64(L,idx))
static void push_word64(lua_State *L, unsigned long long ull) {
    if (sizeof(lua_Integer) >= 8)
        lua_pushinteger(L, (lua_Integer)ull);
    else
        lua_pushnumber(L, (lua_Number)ull);
}

static unsigned long long check_word64(lua_State *L, int idx) {
    unsigned long long ull;
    if (sizeof(lua_Integer) >= 8)
        ull = (unsigned long long)luaL_checkinteger(L, idx);
    else
        ull = (unsigned long long)luaL_checknumber(L, idx);
    return ull;
}
#endif /* DISABLE_64 */

/* error process and utils */

static int push_posixerror(lua_State *L, int errnum, const char *title, const char *fn) {
    lua_pushnil(L);
    if (title && fn)
        lua_pushfstring(L, "%s(%s): %s", title, fn, strerror(errnum));
    else if (title || fn)
        lua_pushfstring(L, "%s: %s", title ? title : fn, strerror(errnum));
    else
        lua_pushstring(L, strerror(errnum));
    return 2;
}

static int push_lasterror(lua_State *L, const char *title, const char *fn) {
    return push_posixerror(L, errno, title, fn);
}

static int Lsetenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    if (setenv(name, value, 1) != 0)
        return push_lasterror(L, "setenv", NULL);
    return_self(L);
}

/* path utils */

static int abs_impl(lua_State *L, const char *s) {
    assert(lua_gettop(L) == 1);
    if (!isdirsep(s[0])) { /* abspath? */
        size_t len;
        char buff[PATH_MAX];
        if (getcwd(buff, PATH_MAX) == NULL)
            return push_lasterror(L, "getcwd", s);
        lua_pushfstring(L, "%s%s%s", buff, DIR_SEP, s);
        s = lua_tolstring(L, -1, &len);
        normpath_impl(L, s, len);
        lua_remove(L, -2);
    }
    return 1;
}

static int Lexpandvars(lua_State *L) {
#ifdef __ANDROID__
    return luaL_error(L, "expandvars not support on Android");
#else
    const char *s = check_pathcomps(L, NULL);
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

static int Lrealpath(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    char buff[PATH_MAX];
    if (realpath(s, buff) == NULL)
        return push_lasterror(L, "realpath", s);
    lua_pushstring(L, buff);
    return 1;
}

/* dir utils */

static int Lplatform(lua_State *L) {
    struct utsname buf;
    if (uname(&buf) != 0) {
        int nrets;
        lua_pushstring(L, PLAT);
        nrets = push_lasterror(L, "platform", NULL);
        lua_remove(L, -nrets); /* remove nil */
        return nrets;
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

struct DirData {
    int closed;
    DIR *dir;
};

static void dir_close(DirData *d) {
    if (!d->closed) {
        closedir(d->dir);
        d->closed = 1;
    }
}

static int dir_iter(lua_State *L) {
    DirData *d = lua_touserdata(L, 1);
    struct dirent *dir;
    assert(d != NULL);
    if (d->closed) return 0;
redo:
    errno = 0;
    dir = readdir(d->dir);
    if (dir == NULL) {
        if (errno == 0) {
            dir_close(d);
            return 0;
        }
        push_posixerror(L, errno, "readdir", NULL);
        return lua_error(L);
    }
    if (iscurdir(dir->d_name) || ispardir(dir->d_name))
        goto redo;
    lua_pushstring(L, dir->d_name);

    /* read some informations */
    {
        struct stat buf;
        int fd = dirfd(d->dir);
        if (fd < 0 || fstatat(fd, dir->d_name, &buf, AT_SYMLINK_NOFOLLOW) != 0)
            return 1;
        lua_pushstring(L, S_ISDIR(buf.st_mode) ? "dir" : "file");
        push_word64(L, buf.st_size);
        push_word64(L, buf.st_ctime);
        push_word64(L, buf.st_mtime);
        push_word64(L, buf.st_atime);
        return 6;
    }
}

static int dir_impl(lua_State *L, DirData *d, const char *s) {
    if ((d->dir = opendir(s)) == NULL) {
        push_lasterror(L, "opendir", s);
        return lua_error(L);
    }
    d->closed = 0;
    lua_pushcfunction(L, dir_iter);
    lua_insert(L, -2);
    return 2;
}

static int walkpath_dfs(WalkState *S) {
    lua_State *L = S->L;
    int nrets = 0;
    DIR *dirp;
    if (S->limit == 0 || !S->isdir)
        return S->walk(S);

    lua_pushfstring(L, "%s" DIR_SEP "%s", S->path, S->comp);
    dirp = opendir(lua_tostring(L, -1));
    if (dirp == NULL) {
        nrets = push_lasterror(L, "dir", lua_tostring(L, -1));
        lua_remove(L, -nrets-1);
        return nrets;
    }
    lua_pop(L, 1); /* remove path */

    do {
        const char *old_comp = S->comp;
        struct dirent *dir;
        errno = 0;
        if ((dir = readdir(dirp)) == NULL) {
            if (errno == 0)
                break;
            return push_lasterror(L, "readdir", S->comp);
        }
#ifdef _BSD_SOURCE
        S->isdir = (dir->d_type == DT_DIR);
#else
        struct stat buf;
        if (lstat(dir->d_name, &buf) != 0)
            return push_lasterror(L, "lstat", dir->d_name);
        S->isdir = S_ISDIR(buf.st_mode);
#endif
        if (S->isdir && (iscurdir(dir->d_name) || ispardir(dir->d_name)))
            continue;
        luaL_checkstack(L, 1, "directory too deep");
        lua_pushfstring(L, "%s%s%s", S->comp, dir->d_name,
                S->isdir ? DIR_SEP : "");

        S->comp = lua_tostring(L, -1);
        --S->limit;
        nrets = walkpath_dfs(S);
        ++S->limit;
        S->comp = old_comp;

        if (nrets) {
            lua_remove(L, -nrets-1);
            closedir(dirp);
            return nrets;
        }
        lua_pop(L, 1);
    } while (1);
    closedir(dirp);

    S->isdir = 1;
    return S->walk(S);
}

static int isdir_impl(lua_State *L, const char *s) {
    struct stat buf;
    return stat(s, &buf) == 0 && S_ISDIR(buf.st_mode);
}

static int chdir_impl(lua_State *L, const char *s) {
    if (chdir(s) != 0)
        return push_lasterror(L, "chdir", s);
    return 0;
}

static int mkdir_impl(lua_State *L, const char *s) {
    if (mkdir(s, 0777) != 0) {
        if (errno == EEXIST)
            return 0;
        return push_lasterror(L, "mkdir", s);
    }
    return 0;
}

static int rmdir_impl(lua_State *L, const char *s) {
    if (rmdir(s) != 0)
        return push_lasterror(L, "rmdir", s);
    return 0;
}

static int Lgetcwd(lua_State *L) {
    char buff[PATH_MAX];
    if (getcwd(buff, PATH_MAX) == NULL)
        return push_lasterror(L, "getcwd", NULL);
    lua_pushstring(L, buff);
    return 1;
}

static int Ltype(lua_State *L) {
    struct stat buf;
    const char *s = check_pathcomps(L, NULL);
    if (stat(s, &buf) != 0)
        return push_lasterror(L, "stat", s);
    lua_pushstring(L,
            S_ISLNK(buf.st_mode) ? "link" :
            S_ISDIR(buf.st_mode) ? "dir" : "file");
    return 1;
}

/* file utils */

static int exists_impl(lua_State *L, const char *s) {
    struct stat buf;
    return stat(s, &buf) == 0;
}

static int remove_impl(lua_State *L, const char *s) {
    if (remove(s) != 0)
        return push_lasterror(L, "remove", s);
    return 0;
}

static int cmptime(time_t t1, time_t t2) {
    return t1 < t2 ? -1 : t1 > t2 ? 1 : 0;
}

static int Lcmpftime(lua_State *L) {
    struct stat buf1, buf2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 2);
    int use_atime = lua_toboolean(L, 3);
    int cmp_c, cmp_m, cmp_a;
    int nrets = 0;
    if (stat(f1, &buf1) != 0) {
        lua_pushinteger(L, -1);
        return 1;
    }
    if (stat(f2, &buf2) != 0) {
        lua_pushinteger(L, 1);
        return 1;

    }
    cmp_c = cmptime(buf1.st_ctime, buf2.st_ctime);
    cmp_m = cmptime(buf1.st_mtime, buf2.st_mtime);
    cmp_a = cmptime(buf1.st_atime, buf2.st_atime);
    if (use_atime)
        lua_pushinteger(L, cmp_c == 0 ? cmp_m == 0 ? cmp_a : cmp_m : cmp_c);
    else
        lua_pushinteger(L, cmp_c == 0 ? cmp_m : cmp_c);
    return 1;
}

static int Lcopy(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int failIfExists = lua_toboolean(L, 3);
    lua_Integer mode = luaL_optinteger(L, 4, 0644);
    char buf[BUFSIZ];
    size_t size;
    int source, dest;

    if ((source = open(from, O_RDONLY, 0)) < 0)
        return push_lasterror(L, "open", from);

    if (failIfExists)
        dest = open(to, O_WRONLY|O_CREAT|O_EXCL, mode);
    else
        dest = open(to, O_WRONLY|O_CREAT|O_TRUNC, mode);
    if (dest < 0) return push_lasterror(L, "open", to);

    while ((size = read(source, buf, BUFSIZ)) > 0) {
        if (write(dest, buf, size) < 0) {
            close(source);
            close(dest);
            return push_lasterror(L, "write", to);
        }
    }

    close(source);
    close(dest);
    lua_pushboolean(L, 1);
    return 1;
}

static int Ltouch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    struct utimbuf utb, *buf;
    int fh = open(s, O_WRONLY|O_CREAT, 0644);
    if (fh < 0)
        return push_lasterror(L, "touch", s);
    close(fh);

    if (lua_gettop(L) == 1) /* set to current date/time */
        buf = NULL;
    else {
        utb.modtime = (time_t)opt_word64(L, 2, time(NULL));
        utb.actime = (time_t)opt_word64(L, 3, utb.modtime);
        buf = &utb;
    }
    if (utime(s, buf) != 0)
        return push_lasterror(L, "utime", s);
    return_self(L);
}

static int Lftime(lua_State *L) {
    struct stat buf;
    const char *s = check_pathcomps(L, NULL);
    if (stat(s, &buf) != 0)
        return push_lasterror(L, "stat", s);
    push_word64(L, buf.st_ctime);
    push_word64(L, buf.st_mtime);
    push_word64(L, buf.st_atime);
    return 3;
}

static int Lfsize(lua_State *L) {
    struct stat buf;
    const char *s = check_pathcomps(L, NULL);
    if (stat(s, &buf) != 0)
        return push_lasterror(L, "stat", s);
    push_word64(L, buf.st_size);
    return 1;
}

static int Lrename(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    if (!rename(from, to))
        return push_lasterror(L, "rename", NULL);
    lua_pushboolean(L, 1);
    return 1;
}

#else

#define PLAT "unknown"

struct DirData {
    int dummy;
};

static int Lplatform(lua_State *L) {
    lua_pushstring(L, PLAT);
    return 1;
}

void dir_close(DirData *d) { }

static int push_lasterror(lua_State *L, const char *title, const char *fn) {
    lua_pushnil(L);
    if (title && fn)
        lua_pushfstring(L, "%s(%s)", title, fn);
    else if (title || fn)
        lua_pushstring(L, title ? title : fn);
    else
        lua_pushstring(L, "Not implement yet");
    return 2;
}

static int walkpath_dfs(WalkState *S) {
    return push_lasterror(S->L, NULL, NULL);
}

static int LNYI(lua_State *L) {
    return push_lasterror(L, NULL, NULL);
}

#define NYI_impl(n,arg) static int n##_impl arg { return LNYI(L); }
NYI_impl(dir,      (lua_State *L, DirData *d, const char *s))
NYI_impl(abs,      (lua_State *L, const char *s))
NYI_impl(exists,   (lua_State *L, const char *s))
NYI_impl(isdir,    (lua_State *L, const char *s))
NYI_impl(chdir,    (lua_State *L, const char *s))
NYI_impl(mkdir,    (lua_State *L, const char *s))
NYI_impl(rmdir,    (lua_State *L, const char *s))
NYI_impl(remove,   (lua_State *L, const char *s))
#undef NYI_impl

#define NYI_impl(n) static int L##n(lua_State *L) { return LNYI(L); }
NYI_impl(cmpftime)
NYI_impl(copy)
NYI_impl(expandvars)
NYI_impl(fsize)
NYI_impl(ftime)
NYI_impl(getcwd)
NYI_impl(realpath)
NYI_impl(rename)
NYI_impl(setenv)
NYI_impl(touch)
NYI_impl(type)
#undef NYI_impl

#endif


/* path utils */

static const char *check_pathcomps(lua_State *L, size_t *psz) {
    int top = lua_gettop(L);
    if (top == 0) {
        if (psz) *psz = 1;
        lua_pushstring(L, ".");
        return ".";
    }
    if (top > 1) {
        joinpath_impl(L);
        lua_replace(L, 1);
        lua_settop(L, 1);
    }
    return lua_tolstring(L, 1, psz);
}

static int Labs(lua_State *L) {
    const char *s = check_pathcomps(L, NULL);
    return abs_impl(L, s);
}

static int Lrel(lua_State *L) {
    size_t lf, lp;
    const char *fn = luaL_checklstring(L, 1, &lf);
    const char *path = luaL_checklstring(L, 2, &lp);
    normpath_impl(L, fn, lf);
    normpath_impl(L, path, lp);
    fn = lua_tostring(L, -2);
    path = lua_tostring(L, -1);
    return relpath_impl(L, fn, path);
}

static int Ljoin(lua_State *L) {
    size_t len;
    const char *s;
#ifdef _WIN32 /* special optimation for Windows */
    int top = lua_gettop(L);
    if (top == 0) {
        lua_pushstring(L, ".");
        return 1;
    }
    joinpath2(L, top);
    s = lua_tolstring(L, -1, &len);
    normpath_base(L, s, len);
    lua_remove(L, -2);
    lua_concat(L, 2);
#else
    s = check_pathcomps(L, &len);
    normpath_impl(L, s, len);
#endif
    return 1;
}

static int Lsplit(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return splitpath_impl(L, s, len);
}

static int Lsplitdrive(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return splitdrive_impl(L, s, len);
}

static int Lsplitext(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return splitext_impl(L, s, len);
}

/* a simple glob implement */

static int walkpath_impl(lua_State *L, const char *path, int deep, WalkFunc *walk, void *data) {
    WalkState S;
    int nrets;
    S.walk = walk;
    S.data = data;
    S.path = path;
    S.comp = "";
    S.isdir = isdir_impl(L, path);
    S.L = L;
    S.base = lua_gettop(L);
    S.limit = deep;
    nrets = walkpath_dfs(&S);
    assert(lua_gettop(L) == S.base + nrets);
    return nrets;
}

static const char *classend(const char *p) {
    const char *op = p;
    if (p[1] == '^') ++p;
    while (*++p != '\0' && *p != ']')
        ;
    return *p == ']' ? p+1 : op;
}

static int matchclass(int c, const char *p, const char *ec) {
  int sig = 1;
  if (*(p+1) == '^') {
    sig = 0;
    p++;  /* skip the '^' */
  }
  while (++p < ec) {
      if (p[1] == '-' && p+2 < ec) {
          p+=2;
          if ((p[-2]&0xFF) <= c && c <= (p[0]&0xFF))
              return sig;
      }
      else if ((p[0]&0xFF) == c) return sig;
  }
  return !sig;
}

static int fnmatch(const char *pattern, const char *s, size_t len) {
    const char *s_end = s+len;
    while (*pattern != '\0') {
        const char *ec;
        size_t i, min = 0, hasmax = 0;
        switch (*pattern) {
        case '*': case '?':
            while (*pattern == '*' || *pattern == '?') {
                if (*pattern++ == '?')
                    ++min;
                else
                    hasmax = 1;
            }
            if (s_end - s < min)
                return 0;
            if (!hasmax)
                return fnmatch(pattern, s+min, (s_end-s) - min);
            len = (s_end-s) - min;
            for (i = 0; i <= len; ++i) {
                if (fnmatch(pattern, s_end-i, i))
                    return 1;
            }
            return 0;
        case '[':
            ec = classend(pattern);
            if (ec != pattern) {
                if (!matchclass(*s, pattern, ec))
                    return 0;
                pattern = ec;
                ++s;
                break;
            }
            /* fall though */
        default:
            if (!fn_equal(*pattern, *s))
                return 0;
            ++pattern, ++s;
        }
    }
    return s == s_end ||
        (isdirsep(s[0]) && iseos(s[1]));
}

typedef struct GlobState {
    const char *pattern;
    int idx;
} GlobState;

static int glob_walker(WalkState *S) {
    GlobState *gs = (GlobState*)S->data;
    size_t len;
    const char *s = lua_tolstring(S->L, -1, &len);
    if (len != 0 && fnmatch(gs->pattern, s, len)) {
        lua_pushvalue(S->L, -1);
        lua_rawseti(S->L, 3, gs->idx++);
    }
    return 0;
}

static int Lglob(lua_State *L) {
    GlobState gs;
    const char *p = luaL_checkstring(L, 1);
    const char *dir = luaL_optstring(L, 2, ".");
    lua_Integer deep = luaL_optinteger(L, 4, -1);
    int nrets;
    lua_settop(L, 3);
    if (lua_istable(L, 3))
        gs.idx = lua_rawlen(L, 3) + 1;
    else {
        lua_newtable(L);
        lua_replace(L, 3);
        gs.idx = 1;
    }
    gs.pattern = p;
    nrets = walkpath_impl(L, dir, deep, glob_walker, &gs);
    return nrets == 0 ? 1 : nrets;
}

static int Lfnmatch(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    const char *pattern = luaL_checkstring(L, 2);
    lua_pushboolean(L, fnmatch(pattern, s, len));
    return 1;
}

/* dir utils */

static int unary_func(lua_State *L, int (*f)(lua_State *, const char *)) {
    int nrets;
    if ((nrets = f(L, check_pathcomps(L, NULL))) != 0)
        return nrets;
    return_self(L);
}

static int Lchdir(lua_State *L) { return unary_func(L, chdir_impl); }
static int Lmkdir(lua_State *L) { return unary_func(L, mkdir_impl); }
static int Lrmdir(lua_State *L) { return unary_func(L, rmdir_impl); }
static int Lremove(lua_State *L) { return unary_func(L, remove_impl); }

static int Lexists(lua_State *L) {
    lua_pushboolean(L, exists_impl(L, check_pathcomps(L, NULL)));
    return 1;
}

static int makedirs_impl(lua_State *L, const char *s, size_t len) {
    /* XXX currently need not maintain the balance of stack: do not
     * used other place.  */
    int nrets;
    size_t hlen, tlen;
    const char *head, *tail;
    luaL_checkstack(L, 4, "too many folder level");
    splitpath_impl(L, s, len);
    head = lua_tolstring(L, -2, &hlen);
    tail = lua_tolstring(L, -1, &tlen);
    if (tlen == 0) {
        splitpath_impl(L, head, hlen);
        /*lua_remove(L, -3);*/
        /*lua_remove(L, -3);*/
        head = lua_tolstring(L, -2, &hlen);
        tail = lua_tolstring(L, -1, &tlen);
    }
    if (hlen && tlen && !exists_impl(L, head)) {
        if ((nrets = makedirs_impl(L, head, len)) != 0) {
            /*lua_remove(L, -nrets-1);*/
            /*lua_remove(L, -nrets-1);*/
            return nrets;
        }
        if (iscurdir(tail)) {
            /*lua_pop(L, 2);*/
            return 0;
        }
    }
    if ((nrets = mkdir_impl(L, s)) != 0) {
        /*lua_remove(L, -nrets-1);*/
        /*lua_remove(L, -nrets-1);*/
        return nrets;
    }
    /*lua_pop(L, 2);*/
    return 0;
}

static int Lmakedirs(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return makedirs_impl(L, s, len);
}

static int rmdir_walker(WalkState *S) {
    lua_State *L = S->L;
    int nrets;
    if (!S->isdir && *S->comp == '\0') /* root and not dir? */
        return remove_impl(L, S->path);

    lua_pushfstring(L, "%s" DIR_SEP "%s", S->path, S->comp);
    if (S->isdir)
        nrets = rmdir_impl(L, lua_tostring(L, -1));
    else
        nrets = remove_impl(L, lua_tostring(L, -1));
    lua_remove(L, -nrets-1);
    return nrets;
}

static int Lremovedirs(lua_State *L) {
    int nrets;
    check_pathcomps(L, NULL);
    nrets = walkpath_impl(L, lua_tostring(L, -1), -1, rmdir_walker, NULL);
    if (nrets) return nrets;
    return_self(L);
}

static int Ldir_gc(lua_State *L) {
    DirData *d = (DirData*)lua_touserdata(L, 1);
    if (d != NULL) dir_close(d);
    return 0;
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
    const char *s = check_pathcomps(L, NULL);
    return dir_impl(L, dirdata_new(L), s);
}

static int itercomp_iter(lua_State *L) {
    const char *s = lua_tostring(L, 1);
    int p = lua_tointeger(L, lua_upvalueindex(1));
    if (p < 0) {
        lua_pushinteger(L, 0);
        lua_replace(L, lua_upvalueindex(1));
        lua_pushvalue(L, 2);
        return 1;
    }
    assert(s != NULL);
    if (p == 0 && s[0] == DIR_SEP_CHAR) {
        char root[] = DIR_SEP;
        lua_pushinteger(L, p + 1);
        lua_replace(L, lua_upvalueindex(1));
        lua_pushstring(L, root);
    }
    else {
        int pend = p;
        while (s[pend] != '\0' && s[pend] != DIR_SEP_CHAR)
            ++pend;
        if (pend == p) return 0;
        lua_pushinteger(L, pend + (s[pend] != '\0'));
        lua_replace(L, lua_upvalueindex(1));
        lua_pushlstring(L, &s[p], pend - p);
    }
    return 1;
}

static int Litercomp(lua_State *L) {
    size_t drive_size, path_size;
    const char *path;
    Lsplitdrive(L);
    lua_tolstring(L, -2, &drive_size);
    path = lua_tolstring(L, -1, &path_size);
    if (drive_size == 0) {
        lua_pushinteger(L, 0);
        lua_pushcclosure(L, itercomp_iter, 1);
        normpath_impl(L, path, path_size);
        return 2;
    }
    lua_pushinteger(L, -1);
    lua_pushcclosure(L, itercomp_iter, 1);
    normpath_impl(L, path, path_size);
    lua_pushvalue(L, -4); /* drive */
    return 3;
}

static int walkpath_iter(lua_State *L) {
    const char *path;
    /* stack: stack_table */
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
    if (iscurdir(path) || ispardir(path))
        goto redo; /* tail return walkpath_iter(L); */
    lua_rawgeti(L, 1, stacktop-2);
    lua_pushvalue(L, 2);
    lua_concat(L, 2);
    lua_replace(L, 2);
    /* the second return value is "file" or "dir" */
    if (*lua_tostring(L, 3) == 'd') {
        lua_pushfstring(L, "%s%c", lua_tostring(L, 2), DIR_SEP_CHAR);
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
    size_t len;
    const char *s = check_pathcomps(L, &len); /* 1 */
    normpath_impl(L, s, len); /* 1 */
    s = lua_tolstring(L, -1, &len);
    lua_pushcfunction(L, walkpath_iter); /* 2 */
    lua_createtable(L, 3, 0); /* 3 */
    if (s[len-1] == DIR_SEP_CHAR)
        lua_pushvalue(L, -3); /* 1->4 */
    else
        lua_pushfstring(L, "%s%s", lua_tostring(L, -3), DIR_SEP); /* 4 */
    dir_impl(L, dirdata_new(L), s); /* ... path iter table path iter dirdata */
    lua_rawseti(L, -4, 3); 
    lua_rawseti(L, -3, 2);
    if (iscurdir(s)) {
        lua_pop(L, 1);
        lua_pushstring(L, "");
    }
    lua_rawseti(L, -2, 1);
    return 2;
}

/* register functions */

LUALIB_API int luaopen_path(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(n) { #n, L##n }
        ENTRY(abs),
        ENTRY(ansi),
        ENTRY(expandvars),
        ENTRY(isabs),
        ENTRY(itercomp),
        ENTRY(join),
        ENTRY(normcase),
        ENTRY(realpath),
        ENTRY(rel),
        ENTRY(split),
        ENTRY(splitdrive),
        ENTRY(splitext),
        ENTRY(type),
        ENTRY(utf8),
#undef  ENTRY
        { NULL, NULL }
    };

    luaL_newlib(L, libs);
    return 1;
}

LUALIB_API int luaopen_path_fs(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(n) { #n, L##n }
        ENTRY(chdir),
        ENTRY(cmpftime),
        ENTRY(copy),
        ENTRY(dir),
        ENTRY(exists),
        ENTRY(fnmatch),
        ENTRY(fsize),
        ENTRY(ftime),
        ENTRY(getcwd),
        ENTRY(glob),
        ENTRY(makedirs),
        ENTRY(mkdir),
        ENTRY(platform),
        ENTRY(remove),
        ENTRY(removedirs),
        ENTRY(rename),
        ENTRY(rmdir),
        ENTRY(setenv),
        ENTRY(touch),
        ENTRY(walk),
#undef  ENTRY
        { NULL, NULL }
    };

    luaL_newlib(L, libs);
    return 1;
}

LUALIB_API int luaopen_path_info(lua_State *L) {
    struct {
        const char *name;
        const char *value;
    } values[] = {
        { "altsep",   ALT_SEP  },
        { "curdir",   CUR_DIR  },
        { "devnull",  DEV_NULL },
        { "extsep",   EXT_SEP  },
        { "pardir",   PAR_DIR  },
        { "pathsep",  PATH_SEP },
        { "platform", PLAT     },
        { "sep",      DIR_SEP  },
        { "version",  LPATH_VERSION },
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

/*
 * linuxcc: flags+='-O2 -shared'
 * linuxcc: output='path.so' run='lua test.lua'
 * cc: lua='lua53' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-s -O3 -std=c99 -pedantic -mdll -DLUA_BUILD_AS_DLL -Id:/$lua/include'
 * cc: output='path.dll' run='lua test.lua'
 */
