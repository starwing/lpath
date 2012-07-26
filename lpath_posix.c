#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define PLAT "posix"

struct DirData {
    int closed;
    DIR *dir;
};

static int push_posixerror(lua_State *L, int errnum) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errnum));
    return 2;
}

static int push_lasterror(lua_State *L) {
    return push_posixerror(L, errno);
}

static void dir_close(DirData *d) {
    closedir(d->dir);
    d->closed = 1;
}

static int Ldir_gc(lua_State *L) {
    DirData *d = (DirData*)lua_touserdata(L, 1);
    if (!d->closed)
        dir_close(d);
    return 0;
}

static int dir_iter(lua_State *L) {
    DirData *d = lua_touserdata(L, lua_upvalueindex(1));
    struct dirent *dir;
    if (d->closed) return 0;
    errno = 0;
    dir = readdir(d->dir);
    if (dir == NULL) {
        if (errno == 0) {
            dir_close(d);
            return 0;
        }
        push_posixerror(L, errno);
        return lua_error(L);
    }
    lua_pushstring(L, dir->d_name);
    return 1;
}

static int dir_impl(lua_State *L, DirData *d, const char *s) {
    if ((d->dir = opendir(s)) == NULL) {
        push_lasterror(L);
        return lua_error(L);
    }
    d->closed = 0;
    lua_pushcclosure(L, dir_iter, 1);
    return 1;
}

static int chdir_impl(lua_State *L, const char *s) {
    if (chdir(s) != 0)
        return push_lasterror(L);
    return 0;
}

static int mkdir_impl(lua_State *L, const char *s) {
    if (mkdir(s, 0777) != 0) {
        if (errno == EEXIST)
            return 0;
        return push_posixerror(L, errno);
    }
    return 0;
}

static int rmdir_impl(lua_State *L, const char *s) {
    if (rmdir(s) != 0)
        return push_lasterror(L);
    return 0;
}

static int remove_impl(lua_State *L, const char *s) {
    if (remove(s) != 0)
        return push_lasterror(L);
    return 0;
}

static int abspath_impl(lua_State *L, const char *s, size_t *ppos) {
    char buff[PATH_MAX];
    if (realpath(s, buff) == NULL)
        return push_lasterror(L);
    if (ppos) {
        char *end = &buff[strlen(buff)];
        while (*--end != '/')
            ;
        *ppos = end - buff + 1;
    }
    return 0;
}

static int walkpath_impl(lua_State *L, const char *s, WalkFunc *walk) {
    DIR *dirp;
    int nrets = 0;
    if (chdir_impl(L, s) != 0) return 2;
    if ((dirp = opendir(s)) == NULL)
        return push_lasterror(L);
    do {
        struct dirent *dir;
        int isdir;
        errno = 0;
        if ((dir = readdir(dirp)) == NULL) {
            if (errno == 0)
                break;
            return push_posixerror(L, errno);
        }
#ifdef _BSD_SOURCE
        isdir = (dir->d_type == DT_DIR);
#else
        struct stat buf;
        if (lstat(dir->d_name, &buf) != 0)
            return push_lasterror(L);
        isdir = S_ISDIR(buf.st_mode);
#endif
        if (isdir && (
                    !strcmp(dir->d_name, CUR_PATH) ||
                    !strcmp(dir->d_name, PAR_PATH)))
            continue;
        if ((nrets = (isdir ?
                        walkpath_impl(L, dir->d_name, walk) :
                        walk(L, dir->d_name, 0))) != 0) {
            closedir(dirp);
            return nrets;
        }
    } while (1);
    closedir(dirp);
    if (    (nrets = chdir_impl(L, PAR_PATH)) != 0 ||
            (nrets = walk(L, s, 1)) != 0)
        return nrets;
    return 0;
}

static int Lexists(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    if (access(s, F_OK) != 0)
        return push_lasterror(L);
    return abspath_impl(L, s, NULL);
}

static int Lgetcwd(lua_State *L) {
    char buff[PATH_MAX];
    if (getcwd(buff, PATH_MAX) != 0)
        return push_lasterror(L);
    lua_pushstring(L, buff);
    return 1;
}

static int Lsetenv(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_optstring(L, 2, NULL);
    if (setenv(name, value, 1) != 0)
        return push_lasterror(L);
    return first_arg(L);
}

static int Ltouch(lua_State *L) {
    const char *file = luaL_checkstring (L, 1);
    struct utimbuf utb, *buf;

    if (lua_gettop (L) == 1) /* set to current date/time */
        buf = NULL;
    else {
        utb.actime = (time_t)luaL_optnumber (L, 2, time(NULL));
        utb.modtime = (time_t)luaL_optnumber (L, 3, utb.actime);
        buf = &utb;
    }
    if (utime(file, buf) != 0)
        return push_lasterror(L);
    return first_arg(L);
}

static int Lfiletime(lua_State *L) {
    struct stat buf;
    const char *s = luaL_checkstring(L, 1);
    if (stat(s, &buf) != 0)
        return push_lasterror(L);
    lua_pushnumber(L, buf.st_ctime);
    lua_pushnumber(L, buf.st_atime);
    lua_pushnumber(L, buf.st_mtime);
    return 3;
}

static int Lfilesize(lua_State *L) {
    struct stat buf;
    const char *s = luaL_checkstring(L, 1);
    if (stat(s, &buf) != 0)
        return push_lasterror(L);
    lua_pushnumber(L, buf.st_size);
    return 1;
}

static int Lisdir(lua_State *L) {
    struct stat buf;
    const char *s = luaL_checkstring(L, 1);
    if (stat(s, &buf) != 0)
        return push_lasterror(L);
    lua_pushboolean(L, S_ISDIR(buf.st_mode));
    return 1;
}

static int Lcmptime(lua_State *L) {
    struct stat buf1, buf2;
    const char *f1 = luaL_checkstring(L, 1);
    const char *f2 = luaL_checkstring(L, 1);
    if (stat(f1, &buf1) != 0 ||
            stat(f2, &buf2) != 0)
        return push_lasterror(L);
    lua_pushinteger(L, buf1.st_ctime - buf1.st_ctime);
    lua_pushinteger(L, buf1.st_atime - buf1.st_atime);
    lua_pushinteger(L, buf1.st_mtime - buf1.st_mtime);
    return 3;
}

static int Lnormpath(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    while (isspace(*s)) ++s;
    return normpath_impl(L, s, '/');
}
