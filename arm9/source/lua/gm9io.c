#ifndef NO_LUA
#include "gm9io.h"
#include "fs.h"
#include "ui.h"

#include <math.h>
#include <errno.h>

typedef struct gm9lua_iofile {
    FIL f;
} gm9lua_iofile;

BYTE mode_to_byte(const char* mode, size_t modelen) {
    bool update_mode = false;
    if (modelen < 1) {
        return FA_READ;
    }
    if (mode[1] == '+') update_mode = true;
    switch (mode[0]) {
        case 'r':
            return update_mode ? FA_WRITE | FA_READ : FA_READ;
        case 'w':
            return update_mode ? FA_CREATE_ALWAYS | FA_WRITE | FA_READ : FA_CREATE_ALWAYS | FA_WRITE;
        case 'a':
            return update_mode ? FA_OPEN_APPEND | FA_WRITE | FA_READ : FA_OPEN_APPEND | FA_WRITE;
        default: 
            return FA_READ;
    }
}

static int io_open(lua_State* L) {
    size_t nlen = 0, mlen = 0;
    const char* name = luaL_checklstring(L, 1, &nlen);
    const char* mode = luaL_checklstring(L, 2, &mlen);

    //create file userdata
    gm9lua_iofile* file = lua_newuserdatauv(L, sizeof(gm9lua_iofile), 0);
    luaL_setmetatable(L, "gm9file");
    memset(&file->f, 0, sizeof(FIL));
    
    FRESULT res = fvx_open(&file->f, name, mode_to_byte(mode, mlen));
    if (res != FR_OK) {
        luaL_pushfail(L);
        lua_pushstring(L, "Please consult the third argument(FRESULT from FatFs).");
        lua_pushinteger(L, res);
        return 3;
    }
    return 1;
}

static int readnumber(lua_State* L, FIL* f, FRESULT* res) {
    char buffer[201];
    UINT br;
    *res = fvx_read(f, buffer, 200, &br);
    if (*res != FR_OK) {
        return 0;
    }
    buffer[br] = '\0';
    size_t size = lua_stringtonumber(L, buffer);
    if (!size) {
        lua_pushnil(L);
        return 1;
    }
    f->fptr -= br-size;
    return 1;
}

static int readall(lua_State* L, FIL* f, FRESULT* res) {
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    UINT br = 0;
    do {
        *res = fvx_read(f, luaL_prepbuffer(&buffer), LUAL_BUFFERSIZE, &br);
        if (*res != FR_OK) {
            return 0;
        }
        luaL_addsize(&buffer, br);
    } while (br);
    luaL_pushresult(&buffer);
    return 1;
}

static int readsize(lua_State* L, FIL* f, UINT size, FRESULT* res) {
    if (f->fptr == fvx_size(f)) { //eof
        luaL_pushfail(L);
        return 1;
    }
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    UINT br = 0;
    do {
        if (size <= LUAL_BUFFERSIZE) {
            *res = fvx_read(f, luaL_prepbuffsize(&buffer, size), size, &br);
            if (*res != FR_OK) {
                return 0;
            }
            luaL_addsize(&buffer, br);
            break;
        } else {
            *res = fvx_read(f, luaL_prepbuffer(&buffer), LUAL_BUFFERSIZE, &br);
            if (*res != FR_OK) {
                return 0;
            }
            luaL_addsize(&buffer, br);
        }
    } while (br);
    luaL_pushresult(&buffer);
    return 1;
}

static int find_newline_offset_in_string(char* string, int maxlen) {
    for (int i = 0;i<maxlen;i++) {
        if (string[i] == '\n')
            return i;
        if (string[i] == '\0')
            return i;
    }
    return -1;
}

static int readline(lua_State* L, FIL* f, FRESULT* res, bool keep_newline) {
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    UINT br = 0;
    char* str;
    int newlineoffset;
    do {
        str = luaL_prepbuffer(&buffer);
        *res = fvx_read(f, str, LUAL_BUFFERSIZE, &br);
        if (br == 0) { //eof
            luaL_pushfail(L);
            return 1;
        }
        if (*res != FR_OK) {
            return 0;
        }
        newlineoffset = find_newline_offset_in_string(str, br);
        if (newlineoffset >= 0) { //end found
            luaL_addsize(&buffer, newlineoffset+keep_newline);
            f->fptr -= (br - (newlineoffset + keep_newline+1)); //correct offset to point at first char after newline
            luaL_pushresult(&buffer);
            return 1;
        }
        luaL_addsize(&buffer, br);
    } while (br);
    luaL_pushresult(&buffer);
    return 1;
}

static int gm9file_read(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    u32 num_inargs = lua_gettop(L);
    FRESULT res;
    u32 num_outargs = 0;
    switch (num_inargs) {
        case 0:
            return luaL_error(L, "io.read: invalid args (got %d)", num_inargs);
        case 1: //no args -> default is read first line and return
            num_outargs += readline(L, &file->f, &res, false);
            if (res != FR_OK) {
                return luaL_error(L, "file:read: read failed somehow, ff err %d at arg %d", res, num_outargs);
            }
            return num_outargs;
        default: //loop through args
            for (u32 i=2;i<num_inargs+1;i++) { //ignore first arg because it is the object so i starts at 1 and also add 1 to both because arg index starts at 1
                if (lua_type(L, i) == LUA_TSTRING) {
                    const char* str = luaL_checkstring(L, i);
                    switch (str[str[0] == '*' ? 1 : 0]) {
                        case 'n': //numeral (not implemented)
                            num_outargs += readnumber(L, &file->f, &res);
                            break;
                        case 'a':
                            num_outargs += readall(L, &file->f, &res);
                            break;
                        case 'l':
                            num_outargs += readline(L, &file->f, &res, false);
                            break;
                        case 'L':
                            num_outargs += readline(L, &file->f, &res, true);
                            break;
                    }
                } else {
                    num_outargs += readsize(L, &file->f, lua_tonumber(L, i), &res);
                }
                if (res != FR_OK) {
                    return luaL_error(L, "file:read: read failed somehow, ff err %d at arg %d", res, num_outargs);
                }
            }
            return num_outargs;
    }
}

static int gm9file_close(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    FRESULT res = fvx_close(&file->f);
    if (res != FR_OK) {
        luaL_pushfail(L);
        lua_pushstring(L, "Please consult the third argument(FRESULT from FatFs).");
        lua_pushinteger(L, res);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int gm9file_write(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    int num_inargs = lua_gettop(L);
    FRESULT res;
    UINT bw;
    for (int i=2;i<num_inargs+1;i++) {
        switch (lua_type(L, i)) {
            case LUA_TSTRING:
                size_t sz;
                const char* str = lua_tolstring(L, i, &sz);
                res = fvx_write(&file->f, str, sz, &bw);
                if (bw != sz) {
                    luaL_pushfail(L);
                    lua_pushstring(L, "File write failed, see arg 3 for bytes written.");
                    lua_pushinteger(L, bw);
                    return 3;
                }
                break;
            case LUA_TNUMBER:
                char buffer[64]; //default size in
                int len = 0;
                if (lua_isinteger(L, 1))
                    len = snprintf(buffer, 64, LUA_INTEGER_FMT, lua_tointeger(L, i));
                else
                    len = snprintf(buffer, 64, LUA_NUMBER_FMT, lua_tonumber(L, i));
                res = fvx_write(&file->f, buffer, len, &bw);
                if (bw != (UINT)len) {
                    luaL_pushfail(L);
                    lua_pushstring(L, "File write failed, see arg 3 for bytes written");
                    lua_pushinteger(L, bw);
                    return 3;
                }
                break;
            default:
                luaL_pushfail(L);
                lua_pushstring(L, "Bad type passed at argument (next arg has the index, 1 is file).");
                lua_pushinteger(L, i);
                return 3;
        }
        if (res != FR_OK) {
            return luaL_error(L, "file:write: write failed somehow, ff err %d at arg %d", res, i);
        }
    }
    return 1;
}

static int gm9file_lines_iterator(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, lua_upvalueindex(1), "gm9file");
    int num_inargs = lua_tointeger(L, lua_upvalueindex(2));
    int num_outargs = 0;
    FRESULT res = 0;
    if (num_inargs == 0) { //called without args
        num_outargs += readline(L, &file->f, &res, false);
        if (res != FR_OK) {
                return luaL_error(L, "file:read: read failed somehow, ff err %d at arg %d", res, num_outargs);
        }
        return num_outargs;
    }
    for (int i = 1;i < num_inargs+1;i++) {
        switch(lua_type(L, lua_upvalueindex(i+2))) {
            case LUA_TNUMBER:
                num_outargs += readsize(L, &file->f, lua_tonumber(L, lua_upvalueindex(i+2)), &res);
                break;
            case LUA_TSTRING:
                const char* str = luaL_checkstring(L, lua_upvalueindex(i+2));
                switch (str[str[0] == '*' ? 1 : 0]) {
                        case 'n': //numeral (not implemented)
                            num_outargs += readnumber(L, &file->f, &res);
                            break;
                        case 'a':
                            num_outargs += readall(L, &file->f, &res);
                            break;
                        case 'l':
                            num_outargs += readline(L, &file->f, &res, false);
                            break;
                        case 'L':
                            num_outargs += readline(L, &file->f, &res, true);
                            break;
                        default:
                            return luaL_error(L, "file:lines invalid arg formatting at arg %d (starts at 1)", i+1);
                }
                break;
            default:
                return luaL_error(L, "file:lines invalid arg formatting at arg %d (starts at 1)", i+1);
        }
        if (res != FR_OK) {
            return luaL_error(L, "file:lines: read failed somehow, ff err %d at arg %d", res, i+1);
        }
    }
    return num_outargs;
    
}

static int gm9file_lines(lua_State* L) {
    int num_inargs = lua_gettop(L) - 1; //we have user data as arg 1
    lua_pushvalue(L, 1); //re organize stack, so all args first then file, then the number of args
    lua_pushinteger(L, num_inargs);
    lua_rotate(L, 2, 2); //rotate stack so all values come before the inargs starting at 2: file | a b c file inargs --> file | file inargs a b c
    lua_pushcclosure(L, gm9file_lines_iterator, 2 + num_inargs); //upvalue file and inargs + all args
    return 1;
}

static int gm9file_flush(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    fvx_sync(&file->f);
    return 0;
}

static int gm9file_setvbuf() {
    return 0; //i did not find an equivalent in fatfs docs -> stub
}

static int gm9file_seek(lua_State* L) {
    int num_inargs = lua_gettop(L) - 1;
    if (!num_inargs) //no args -> no file
        return luaL_error(L, "file:seek invalid args (got %d)", num_inargs);
    if (num_inargs > 2) //too many args
        return luaL_error(L, "file:seek invalid args (got %d)", num_inargs);
    
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    int offset = 0;
    if (num_inargs == 2) {
        offset = lua_tointeger(L, 3);
    }
    const char* whence = luaL_checkstring(L, 2);
    FRESULT res = 0;
    if (strcmp(whence, "set") == 0) { //set mode -> seek to 0 + offset
        res = fvx_lseek(&file->f, offset);
    } else if (strcmp(whence, "cur")) { //cur mode -> seek to cur + offset
        res = fvx_lseek(&file->f, file->f.fptr + offset);
    } else if (strcmp(whence, "end")) { //end mode -> seek to end of file + offset
        res = fvx_lseek(&file->f, fvx_size(&file->f) + offset);
    } else {
        return luaL_error(L, "file:seek invalid whence");
    }
    if (res) {
        luaL_pushfail(L);
        lua_pushfstring(L, "Please consult the third argument(FRESULT from FatFs).");
        lua_pushinteger(L, res);
        return 3;
    }
    lua_pushinteger(L, file->f.fptr);
    return 1;
}

static int gm9file_tostring(lua_State* L) {
    gm9lua_iofile* file = luaL_checkudata(L, 1, "gm9file");
    if (fvx_opened(&file->f)) {
        lua_pushstring(L, "file");
    } else {
        lua_pushstring(L, "file (closed)");
    }
    return 1;
}

static const luaL_Reg io[] = {
  {"open", io_open},
  {NULL, NULL}
};

static const luaL_Reg gm9file_methods[] = {
  {"read", gm9file_read},
  {"close", gm9file_close},
  {"write", gm9file_write},
  {"lines", gm9file_lines},
  {"flush", gm9file_flush},
  {"seek", gm9file_seek},
  {"setvbuf", gm9file_setvbuf},
  {NULL, NULL}
};

static const luaL_Reg gm9file_metamethods[] = {
    {"__gc", gm9file_close},
    {"__close", gm9file_close},
    {"__tostring", gm9file_tostring}, //bruh who implemented this
    {NULL, NULL} 
};

int gm9lua_open_io(lua_State* L) {
    luaL_newlib(L, io);
    //create metatable for files
    luaL_newmetatable(L, "gm9file");
    luaL_setfuncs(L, gm9file_metamethods, 0);
    luaL_newlib(L, gm9file_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    return 1;
}
#endif