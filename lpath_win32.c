#include <ctype.h>
#include <Windows.h>

#define PLAT "win32"

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

static void push_word64(lua_State *L, DWORD low, DWORD high) {
    lua_Number time = high/10;
    time = time*~(DWORD)0 + time + low/10;
    lua_pushnumber(L, time);
}

static void push_filetime(lua_State *L, PFILETIME pft) {
    push_word64(L, pft->dwLowDateTime, pft->dwHighDateTime);
}

static void to_filetime(lua_Number number, PFILETIME pft) {
    DWORD highTime = (DWORD)(number/(~(DWORD)0+1.0));
    pft->dwHighDateTime = highTime/10;
    pft->dwLowDateTime = (DWORD)((number-highTime)/10);
}

struct DirData {
    HANDLE hFile;
    WIN32_FIND_DATAA wfd;
    DWORD lasterror;
};

static void dir_close(DirData *d) {
    FindClose(d->hFile);
    d->hFile = INVALID_HANDLE_VALUE;
    d->lasterror = ERROR_NO_MORE_FILES;
}

static int Ldir_gc(lua_State *L) {
    DirData *d = (DirData*)lua_touserdata(L, 1);
    if (d->lasterror == NO_ERROR)
        dir_close(d);
    return 0;
}

static int dir_iter(lua_State *L) {
    int invalid;
    DirData *d = lua_touserdata(L, 1);
    assert(d != NULL);
redo:
    if (d->lasterror == ERROR_NO_MORE_FILES) {
        dir_close(d);
        return 0;
    }
    if (d->lasterror != NO_ERROR) {
        push_win32error(L, d->lasterror);
        return lua_error(L);
    }
    invalid = !strcmp(d->wfd.cFileName, CUR_PATH) ||
              !strcmp(d->wfd.cFileName, PAR_PATH);
    if (!invalid) {
        lua_pushstring(L, d->wfd.cFileName);
        lua_pushstring(L, d->wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ?
                "dir" : "file");
        push_word64(L, d->wfd.nFileSizeLow, d->wfd.nFileSizeHigh);
        push_filetime(L, &d->wfd.ftCreationTime);
        push_filetime(L, &d->wfd.ftLastAccessTime);
        push_filetime(L, &d->wfd.ftLastWriteTime);
    }
    if (!FindNextFileA(d->hFile, &d->wfd))
        d->lasterror = GetLastError();
    if (invalid)
        goto redo;
    return 6;
}

static int dir_impl(lua_State *L, DirData *d, const char *s) {
    const char *pattern = lua_pushfstring(L, "%s%c*", s, PATH_SEP);
    HANDLE hFile;
    if ((hFile = FindFirstFileA(pattern, &d->wfd)) == INVALID_HANDLE_VALUE) {
        push_lasterror(L);
        return lua_error(L);
    }
    lua_pop(L, 1);
    d->lasterror = NO_ERROR;
    d->hFile = hFile;
    lua_pushcfunction(L, dir_iter);
    lua_insert(L, -2);
    return 2;
}

static int chdir_impl(lua_State *L, const char *s) {
    if (!SetCurrentDirectoryA(s))
        return push_lasterror(L);
    return 0;
}

static int mkdir_impl(lua_State *L, const char *s) {
    if (!CreateDirectoryA(s, NULL)) {
        DWORD lasterror = GetLastError();
        if (lasterror == ERROR_ALREADY_EXISTS)
            return 0;
        return push_win32error(L, lasterror);
    }
    return 0;
}

static int rmdir_impl(lua_State *L, const char *s) {
    if (!RemoveDirectoryA(s))
        return push_lasterror(L);
    return 0;
}

static int remove_impl(lua_State *L, const char *s) {
    if (!DeleteFileA(s))
        return push_lasterror(L);
    return 0;
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
    if (ppos != NULL) *ppos = pbase - ppath;
    pb_addsize(&b, len);
    pb_pushresult(&b);
    return 1;
}

static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk) {
    WIN32_FIND_DATA wfd;
    HANDLE hFile;
    int nrets = 0;
    if (chdir_impl(L, s) != 0) return 2;
    if ((hFile = FindFirstFileA("*", &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    do {
        int isdir = wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        if (isdir && (
                    !strcmp(wfd.cFileName, CUR_PATH) ||
                    !strcmp(wfd.cFileName, PAR_PATH)))
            continue;
        if ((nrets = (isdir ?
                        walkpath_impl(L, wfd.cFileName, walk) :
                        walk(L, wfd.cFileName, 0))) != 0) {
            FindClose(hFile);
            return nrets;
        }
    } while (FindNextFile(hFile, &wfd));
    FindClose(hFile);
    if (    (nrets = chdir_impl(L, PAR_PATH)) != 0 ||
            (nrets = walk(L, s, 1)) != 0)
        return nrets;
    return 0;
}

static int Lsetenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    if (!SetEnvironmentVariableA(name, value))
        return push_lasterror(L);
    return first_arg(L);
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
        lua_pop(L, 1);
        return push_lasterror(L);
    }
    pb_addsize(&b, len);
    pb_pushresult(&b);
    return 1;
}

static int Lisdir(lua_State *L) {
    const char *s = get_single_pathname(L);
    DWORD attrs = GetFileAttributes(s);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return push_lasterror(L);
    lua_pushboolean(L, attrs & FILE_ATTRIBUTE_DIRECTORY);
    return 1;
}

static int Lexists(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = get_single_pathname(L);
    HANDLE hFile;
    if ((hFile = FindFirstFileA(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    lua_pushstring(L, wfd.cFileName);
    return 1;
}

static int Lfiletime(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = get_single_pathname(L);
    HANDLE hFile;
    if ((hFile = FindFirstFileA(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    push_filetime(L, &wfd.ftCreationTime);
    push_filetime(L, &wfd.ftLastAccessTime);
    push_filetime(L, &wfd.ftLastWriteTime);
    return 3;
}

static int Lfilesize(lua_State *L) {
    WIN32_FIND_DATA wfd;
    const char *s = get_single_pathname(L);
    HANDLE hFile;
    if ((hFile = FindFirstFileA(s, &wfd)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    push_word64(L, wfd.nFileSizeLow, wfd.nFileSizeHigh);
    return 1;
}

static int Lcmptime(lua_State *L) {
    WIN32_FIND_DATA wfd1, wfd2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 1);
    HANDLE hFile1, hFile2;
    if ((hFile1 = FindFirstFileA(f1, &wfd1)) == INVALID_HANDLE_VALUE ||
            (hFile2 = FindFirstFileA(f2, &wfd2)) == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    lua_pushinteger(L, CompareFileTime(&wfd1.ftCreationTime,
                                       &wfd2.ftCreationTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastAccessTime,
                                       &wfd2.ftLastAccessTime));
    lua_pushinteger(L, CompareFileTime(&wfd1.ftLastWriteTime,
                                       &wfd2.ftLastWriteTime));
    return 3;
}

static int Ltouch(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    BOOL success;
    FILETIME at, mt;
    SYSTEMTIME st;
    HANDLE hFile = CreateFileA(
            s,                     /* filepath */
            FILE_WRITE_ATTRIBUTES, /* desired rights */
            0,                     /* shared mode */
            NULL,                  /* security attribute */
            OPEN_ALWAYS,           /* creation disposition */
            0,                     /* flags and attributes */
            NULL                   /* template file */
            );
    if (hFile == INVALID_HANDLE_VALUE)
        return push_lasterror(L);
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &at);
    mt = at;
    if (lua_gettop(L) != 1) {
        lua_Number atn = luaL_optnumber(L, 2, -1);
        lua_Number mtn = luaL_optnumber(L, 3, atn);
        if ((DWORD)atn != -1) to_filetime(atn, &at);
        if ((DWORD)mtn != -1) to_filetime(mtn, &mt);
    }
    success = SetFileTime(hFile,
            NULL, /* create time */
            &at,  /* access time */
            &mt   /* modify time */
            );
    if (!CloseHandle(hFile) || !success)
        return push_lasterror(L);
    return first_arg(L);
}

static int Lnormpath(lua_State *L) {
    const char *s = get_single_pathname(L);
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
