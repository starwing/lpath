#define LUA_LIB
#ifdef __cplusplus
extern "C" {
#endif
#  include <lua.h>
#  include <lauxlib.h>
#ifdef __cplusplus
}
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
#include <ctype.h>
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

static int normpath_base(lua_State *L, const char *s, size_t len) {
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

static int relpath_base(lua_State *L, const char *fn, const char *path) {
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
#ifdef WIN32
    while (*pf != '\0' && *pp != '\0'
            && tolower(*pf) == tolower(*pp))
        ++pf, ++pp;
#else
    while (*pf != '\0' && *pp != '\0' && *pf == *pp)
        ++pf, ++pp;
#endif
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

static int joinpath_base(lua_State *L) {
    luaL_Buffer b;
    int i, top = lua_gettop(L);
    luaL_buffinit(L, &b);
    for (i = 1; i <= top; ++i) {
        size_t len;
        const char *s = luaL_checklstring(L, i, &len);
        if (i == top)
            lua_pushvalue(L, top);
        else
            add_dirsep(L, s, len);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    return 1;
}

static int splitpath_base(lua_State *L, const char *s, size_t len) {
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
        const char *oldlast = last;
        /* remove trailing slashes from head, unless it's all slashes */
        for (; s < last && isdirsep(*last); --last)
            ;
        if (isdirsep(*last))
            lua_pushlstring(L, s, oldlast - s);
        else
            lua_pushlstring(L, s, last - s + 1);
        lua_pushstring(L, oldlast + 1);
    }
    else {
        lua_pushliteral(L, "");
        lua_pushlstring(L, s, len);
    }
    return 2;
}

static int splitext_base(lua_State *L, const char *s, size_t len) {
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

#define normpath_impl normpath_base
#define relpath_impl relpath_base
#define joinpath_impl joinpath_base
#define splitpath_impl splitpath_base
#define splitext_impl splitext_base

/* system specfied utils */

#define DIR_DATA "Dir Context"

typedef struct DirData DirData;
typedef struct WalkData WalkData;
typedef int WalkFunc(lua_State *L, const char *s, int isdir);

static const char *check_pathcomps(lua_State *L, size_t *psz);
#define return_self(L) do { lua_settop((L), 1); return 1; } while (0)

/* all these functions must implement in specfied system */
/* miscs */
static int push_lasterror(lua_State *L, const char *title, const char *fn);
static int Lsetenv(lua_State *L);
/* path utils */
static int Labs(lua_State *L);
static int Lexpandvars(lua_State *L);
static int Lrealpath(lua_State *L);
/* dir utils */
static void dir_close(DirData *d);
static int chdir_impl(lua_State *L, const char *s);
static int dir_impl(lua_State *L, DirData *d, const char *s);
static int mkdir_impl(lua_State *L, const char *s);
static int rmdir_impl(lua_State *L, const char *s);
static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk);
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

static int Lisabs_base(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    lua_pushboolean(L, isdirsep(s[0]));
    return 1;
}

static int Lsplitdrive_base(lua_State *L) {
    check_pathcomps(L, NULL);
    lua_pushliteral(L, "");
    lua_insert(L, -2);
    return 2;
}

static int Lnormpath(lua_State *L);
static int Lnormcase_base(lua_State *L) { return Lnormpath(L); }
static int Lansi_base(lua_State *L) { return lua_gettop(L); }
static int Lutf8_base(lua_State *L) { return lua_gettop(L); }

#define override_base(name) ((void)(void(*)())name##_base)

#define Lansi Lansi_base
#define Lisabs Lisabs_base
#define Lnormcase Lnormcase_base
#define Lsplitdrive Lsplitdrive_base
#define Lutf8 Lutf8_base

#ifdef _WIN32

#include <assert.h>
#include <ctype.h>
#include <Windows.h>

#define PLAT "windows"

/* windows-spec implements */
#undef Lansi
#undef Lisabs
#undef Lnormcase
#undef Lsplitdrive
#undef Lutf8
#undef joinpath_impl
#undef normpath_impl
#undef relpath_impl

static int normpath_impl(lua_State *L, const char *s, size_t len) {
    if (len > 2 && isdirsep(s[0]) && isdirsep(s[1]) && !isdirsep(s[2])) {
        /* UNC path? */
        lua_pushlstring(L, s, len);
        return 1;
    }
    if (isalpha(s[0]) && s[1] == ':') { /* with drive letter? */
        lua_pushfstring(L, "%c:", toupper(*s));
        normpath_base(L, s + 2, len);
        lua_concat(L, 2);
        return 1;
    }
    else if (len > 4 &&
            isdirsep(s[0]) && isdirsep(s[1]) && s[2] == '?' && isdirsep(s[3])) {
        /* externed path? */
        lua_pushliteral(L,  DIR_SEP);
        normpath_base(L, s+1, len-1);
        lua_concat(L, 2);
        return 1;
    }
    return normpath_base(L, s, len);
}

static int splitdrive_impl(lua_State *L, const char *s, size_t len) {
    if (isdirsep(s[0]) && isdirsep(s[1]) && !isdirsep(s[2])) {
        const char *mp, *mp2;
        /* is a UNC path:
         * vvvvvvvvvvvvvvvvvvvv drive letter or UNC path
         * \\machine\mountpoint\directory\etc\...
         *           directory ^^^^^^^^^^^^^^^
         */
        if (((mp = strchr(s+2, DIR_SEP_CHAR)) == NULL
                    && (mp = strchr(s+2, ALT_SEP_CHAR)) == NULL)
                || isdirsep(mp[1])) {
            /* a UNC path can't have two slashes in a row
               (after the initial two) */
            lua_pushliteral(L, "");
            lua_insert(L, -2);
            return 2;
        }
        if ((mp2 = strchr(mp+2, DIR_SEP_CHAR)) == NULL
                && (mp2 = strchr(mp+2, ALT_SEP_CHAR)) == NULL) {
            lua_pushlstring(L, s, len);
            lua_pushliteral(L, "");
            return 2;
        }
        lua_pushlstring(L, s, mp2-s);
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

static int drive_equal(const char *d1, const char *d2, int *same_case) {
    int issame = 1;
    while (*d1 != '\0' && *d2 != '\0'
            && tolower(*d1) == tolower(*d2)) {
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

static int joinpath_impl(lua_State *L) {
    size_t dlen, len;
    const char *d, *s = luaL_checklstring(L, 1, &len);
    int i, top = lua_gettop(L);
    override_base(joinpath);
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
    lua_concat(L, 2); /* concat drive and path */
    return 1;
}

/* error process and utils */

static int Lplatform(lua_State *L) {
    lua_pushstring(L, PLAT);
    return 1;
}

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
    int bytes = WideCharToMultiByte(CP_ACP, 0, ws, len, p, MAX_PATH, NULL, NULL);
    if (bytes == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER)
            return push_multibyte(L, ws, CP_ACP);
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
    int bytes = MultiByteToWideChar(CP_ACP, 0, s, -1, p, MAX_PATH);
    if (bytes == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER)
            return push_widechar(L, s, CP_ACP);
        push_win32error(L, err, "wide_path", s);
        lua_error(L);
    }
    lua_pushlstring(L, (const char*)p, bytes*sizeof(WCHAR));
    if (p != buff) lua_remove(L, -2);
    return (LPCWSTR)lua_tostring(L, -1);
}

static int push_lasterror(lua_State *L, const char *title, const char *fn) {
    return push_win32error(L, GetLastError(), title, fn);
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

static int Lansi(lua_State *L) {
    const char *utf8 = luaL_checkstring(L, 1);
    LPCWSTR ws = push_widechar(L, utf8, CP_UTF8);
    override_base(Lansi);
    push_multibyte(L, ws, CP_ACP);
    return 1;
}

static int Lutf8(lua_State *L) {
    const char *ansi = luaL_checkstring(L, 1);
    LPCWSTR ws = push_widechar(L, ansi, CP_ACP);
    override_base(Lutf8);
    push_multibyte(L, ws, CP_UTF8);
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

static int Labs(lua_State *L) {
    const char *s = check_pathcomps(L, NULL);
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

static HANDLE open_file_for_handle(lua_State *L, const char *s) {
    LPCWSTR ws = push_pathW(L, s);
    HANDLE hFile = CreateFileW(ws, /* file to open         */
            0,                     /* open only for handle */
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, /* share for everything */
            NULL,                  /* default security     */
            OPEN_EXISTING,         /* existing file only   */
            0,                     /* no file attributes   */
            NULL);                 /* no attr. template    */
    lua_pop(L, 1); /* remove wide path */
    return hFile;
}

static int Lrealpath(lua_State *L) {
#if _WIN32_WINNT < 0x0600
    return Labs(L);
#else
    WCHAR buff[MAX_PATH], *p = buff;
    DWORD bytes;
    const char *s = check_pathcomps(L, NULL);
    HANDLE hFile = open_file_for_handle(L, s);
    if(hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "open", s);
    bytes = GetFinalPathNameByHandleW(hFile, p, MAX_PATH, 0);
    if (bytes > MAX_PATH) {
        p = (LPWSTR)lua_newuserdata(L, bytes * sizeof(WCHAR));
        bytes = GetFinalPathNameByHandleW(hFile, p, bytes, 0);
    }
    CloseHandle(hFile);
    if (bytes == 0) return push_lasterror(L, "realpath", s);
    if (bytes - 4 > MAX_PATH)
        push_pathA(L, p);
    else
        push_pathA(L, p + 4);
    return 1;
#endif
}

static int Lnormcase(lua_State *L) {
    size_t len;
    const char *s;
    size_t i, start = 0;
    luaL_Buffer b;
    Lnormcase_base(L);
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
    override_base(Lisabs);
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

static int Lsplitdrive(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    override_base(Lsplitdrive);
    return splitdrive_impl(L, s, len);
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
        push_filetime(L, &d->wfd.ftLastAccessTime);
        push_filetime(L, &d->wfd.ftLastWriteTime);
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

static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk) {
    WIN32_FIND_DATAW wfd;
    HANDLE hFile;
    LPCWSTR ws = push_pathW(L, s);
    BOOL ret = SetCurrentDirectoryW(ws);
    lua_pop(L, 1); /* remove wide path */
    if (!ret) return push_lasterror(L, "chdir", s);
    hFile = FindFirstFileW(L"*", &wfd);
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "dir", s);
    do {
        int nrets;
        int isdir = wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        LPCSTR fn;
        LPCWSTR wfn = wfd.cFileName;
        if (isdir && (!wcscmp(wfn, L".") || !wcscmp(wfn, L"..")))
            continue;

        fn = push_pathA(L, wfn);
        nrets = isdir ? walkpath_impl(L, fn, walk) :
                        walk(L, fn, 0);
        lua_pop(L, 1); /* remove ansi path */
        if (nrets) {
            FindClose(hFile);
            return nrets;
        }
    } while (FindNextFileW(hFile, &wfd));
    FindClose(hFile);
    if (!SetCurrentDirectoryW(L".."))
        return push_lasterror(L, "chdir", "..");
    return walk(L, s, 1);
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
    HANDLE hFile = open_file_for_handle(L, s);
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
    LPCWSTR wf1 = push_pathW(L, f1);
    LPCWSTR wf2 = push_pathW(L, f2);
    HANDLE hFile1, hFile2;
    if ((hFile1 = FindFirstFileW(wf1, &wfd1)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "cmpftime", f1);
    FindClose(hFile1);
    if ((hFile2 = FindFirstFileW(wf2, &wfd2)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L, "cmpftime", f2);
    FindClose(hFile2);
    lua_pushinteger(L, CompareFileTime(&wfd1.ftCreationTime,
                                       &wfd2.ftCreationTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastAccessTime,
                                       &wfd2.ftLastAccessTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastWriteTime,
                                       &wfd2.ftLastWriteTime));
    return 3;
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
    push_filetime(L, &wfd.ftLastAccessTime);
    push_filetime(L, &wfd.ftLastWriteTime);
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
    SystemTimeToFileTime(&st, &at);
    mt = at;
    opt_filetime(L, 2, &at);
    opt_filetime(L, 4, &mt);
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

#include <assert.h>
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

static int Labs(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    if (!isdirsep(s[0])) { /* abspath? */
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
        push_word64(L, buf.st_atime);
        push_word64(L, buf.st_mtime);
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

static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk) {
    DIR *dirp;
    if (chdir(s) != 0)
        return push_lasterror(L, "chdir", s);
    if ((dirp = opendir(CUR_DIR)) == NULL)
        return push_lasterror(L, "opendir", s);
    do {
        int nrets = 0;
        struct dirent *dir;
        int isdir;
        errno = 0;
        if ((dir = readdir(dirp)) == NULL) {
            if (errno == 0)
                break;
            return push_lasterror(L, "readdir", s);
        }
#ifdef _BSD_SOURCE
        isdir = (dir->d_type == DT_DIR);
#else
        struct stat buf;
        if (lstat(dir->d_name, &buf) != 0)
            return push_lasterror(L, "lstat", dir->d_name);
        isdir = S_ISDIR(buf.st_mode);
#endif
        if (isdir && (iscurdir(dir->d_name) || ispardir(dir->d_name)))
            continue;
        nrets = isdir ?
            walkpath_impl(L, dir->d_name, walk) :
            walk(L, dir->d_name, 0);
        if (nrets != 0) {
            closedir(dirp);
            return nrets;
        }
    } while (1);
    closedir(dirp);
    if (chdir(PAR_DIR) != 0)
        return push_lasterror(L, "chdir", PAR_DIR);
    return walk(L, s, 1);
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

static int Lcmpftime(lua_State *L) {
    struct stat buf1, buf2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 1);
    if (stat(f1, &buf1) != 0)
        return push_lasterror(L, "stat", f1);
    if (stat(f2, &buf2) != 0)
        return push_lasterror(L, "stat", f2);
    lua_pushinteger(L, buf1.st_ctime - buf1.st_ctime);
    lua_pushinteger(L, buf1.st_atime - buf1.st_atime);
    lua_pushinteger(L, buf1.st_mtime - buf1.st_mtime);
    return 3;
}

static int Lcopy(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    int failIfExists = lua_toboolean(L, 3);
    int mode = luaL_optint(L, 4, 0644);
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

    while ((size = read(source, buf, BUFSIZ)) > 0)
        write(dest, buf, size);

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
        utb.actime = (time_t)opt_word64(L, 2, time(NULL));
        utb.modtime = (time_t)opt_word64(L, 3, utb.actime);
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
    push_word64(L, buf.st_atime);
    push_word64(L, buf.st_mtime);
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
        lua_pushstring(L, "%s(%s)", title, fn);
    else
        lua_pushstring(L, title ? title : fn);
    else
        lua_pushstring(L, "Not implement yet");
    return 2;
}

static int LNYI(lua_State *L) {
    push_lasterror(L, NULL, NULL);
    return 2;
}

#define NYI_impl(n,arg) static int n##_impl arg { return LNYI(L); }
NYI_impl(chdir,    (lua_State *L, const char *s))
NYI_impl(dir,      (lua_State *L, DirData *d, const char *s))
NYI_impl(exists,   (lua_State *L, const char *s))
NYI_impl(mkdir,    (lua_State *L, const char *s))
NYI_impl(remove,   (lua_State *L, const char *s))
NYI_impl(rmdir,    (lua_State *L, const char *s))
NYI_impl(walkpath, (lua_State *L, const char *s, WalkFunc *walk))
#undef NYI_impl

#define NYI_impl(n) static int L##n(lua_State *L) { return LNYI(L); }
NYI_impl(abs)
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
        return ".";
    }
    if (top > 1) {
        joinpath_impl(L);
        lua_replace(L, 1);
        lua_settop(L, 1);
    }
    return lua_tolstring(L, 1, psz);
}

static int Lnormpath(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return normpath_impl(L, s, len);
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
    return joinpath_impl(L);
}

static int Lsplit(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return splitpath_impl(L, s, len);
}

static int Lsplitext(lua_State *L) {
    size_t len;
    const char *s = check_pathcomps(L, &len);
    return splitext_impl(L, s, len);
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

static int rmdir_rec_walk(lua_State *L, const char *s, int isdir) {
    return isdir ? rmdir_impl(L, s) : remove_impl(L, s);
}

static int Lremovedirs(lua_State *L) {
    const char *s = check_pathcomps(L, NULL);
    int res;
    if (Lgetcwd(L) != 1) return 2;
    res = walkpath_impl(L, s, rmdir_rec_walk);
    if (chdir_impl(L, lua_tostring(L, -res-1)) != 0 || res != 0)
        return 2;
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
        lua_pushinteger(L, pend + 1);
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
#if LUA_VERSION_NUM >= 502
    int nrets, stacktop = lua_rawlen(L, 1);
#else
    int nrets, stacktop = lua_objlen(L, 1);
#endif
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
        ENTRY(normpath),
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

#if LUA_VERSION_NUM >= 502
    luaL_newlib(L, libs);
#else
    luaL_register(L, lua_tostring(L, 1), libs);
#endif
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
        ENTRY(fsize),
        ENTRY(ftime),
        ENTRY(getcwd),
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

#if LUA_VERSION_NUM >= 502
    luaL_newlib(L, libs);
#else
    luaL_register(L, lua_tostring(L, 1), libs);
#endif
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
 * cc: lua='lua52' libs+='d:/$lua/$lua.dll'
 * cc: flags+='-s -O3 -pedantic -mdll -DLUA_BUILD_AS_DLL -Id:/$lua/include'
 * cc: output='path.dll' run='lua test.lua'
 */
