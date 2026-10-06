/* Exercise actual guest file operations on a linked overlay and writable saves. */
#include "../src/runtime_file.c"
#include <assert.h>
#ifdef _WIN32
/* Windows: a directory in the temporary folder, and the hard link mods.py makes when symbolic
 * links need privileges. */
static char *temp_directory(char *name) {
    char base[MAX_PATH];
    const char *suffix = strrchr(name, '/');
    if (!GetTempPathA(sizeof(base), base) || _mktemp_s((char *)suffix + 1, strlen(suffix + 1) + 1)) return NULL;
    static char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s%s", base, suffix + 1);
    for (char *c = path; *c; ++c) if (*c == '\\') *c = '/';
    if (_mkdir(path)) return NULL;
    strcpy(name, path); /* the template is large enough: see main */
    return name;
}
static int symlink(const char *target, const char *link) { return CreateHardLinkA(link, target, NULL) ? 0 : -1; }
#define mkdtemp temp_directory
#endif
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
/* Reads into guest memory tell the GPU side (runtime_memory.c); nothing to tell here. */
void runtime_memory_note_write(uintptr_t address, uint64_t size) { (void)address; (void)size; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    char root[1024]="/tmp/bbport-mod-files-XXXXXX";
    assert(mkdtemp(root));
    char game[512],user[512],source[512],link[512];
    snprintf(game,sizeof(game),"%s/game",root);
    snprintf(user,sizeof(user),"%s/user",root);
    snprintf(source,sizeof(source),"%s/mod.dcx",root);
    snprintf(link,sizeof(link),"%s/game/asset.dcx",root);
    assert(!mkdir(game,0755));
    FILE *f=fopen(source,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
    assert(!symlink(source,link));
    runtime_file_configure(game,user);
    for (int i=0;i<3;++i) {
        const char *path=i==0 ? "/app0/asset.dcx" : i==1 ? "/hostapp/asset.dcx" : "asset.dcx";
        int fd=(int)do_open(path,0,0); assert(fd>=3);
        char content[8]={0}; assert(do_read(fd,content,6)==6 && !strcmp(content,"modded"));
        GuestStat info; assert(!do_stat(path,&info) && info.size==6);
        assert(!do_close(fd));
        assert(do_open(path,2,0)==-EROFS);
        assert(do_open(path,0x400,0)==-EROFS);
        assert(do_truncate(path,0)==-EROFS);
        assert(path_op(path,2,0)==-EROFS);
        assert(do_rename(path,"/data/moved")==-EROFS);
    }
    int dir=(int)do_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int64_t count=do_getdents(dir,entries,sizeof(entries),NULL); assert(count>0);
    int found=0;
    for (int64_t p=0;p<count;) {
        uint16_t length; memcpy(&length,entries+p+4,2);
        assert(length);
        if (!strcmp(entries+p+8,"asset.dcx")) { assert(entries[p+6]==8); found=1; }
        p+=length;
    }
    assert(found && !do_close(dir));
    int save=(int)do_open("/data/test-save",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && !do_close(save));
    assert(!path_op("/data/test-save",2,0));
    /* The file position: read and lseek move it, pread/pwrite do not; append writes at the end. */
    int rw=(int)do_open("/data/positions",0x202,0644); assert(rw>=3);
    assert(do_write(rw,"0123456789",10)==10);
    assert(do_lseek(rw,0,1)==10 && do_lseek(rw,2,0)==2);
    char got[8]={0};
    assert(do_read(rw,got,2)==2 && !memcmp(got,"23",2));
    assert(do_pread(rw,got,3,7)==3 && !memcmp(got,"789",3));
    assert(do_read(rw,got,2)==2 && !memcmp(got,"45",2));
    assert(do_pwrite(rw,"ab",2,0)==2 && do_lseek(rw,0,1)==6);
    assert(do_lseek(rw,-1,2)==9 && do_read(rw,got,4)==1 && got[0]=='9');
    assert(do_read(rw,got,4)==0); /* end of file */
    assert(do_lseek(rw,-20,1)==-EINVAL);
    assert(!do_ftruncate(rw,4));
    GuestStat info; assert(!do_fstat(rw,&info) && info.size==4 && (info.mode&0170000)==0100000);
    assert(!do_close(rw));
    int add=(int)do_open("/data/positions",0x9,0); assert(add>=3); /* O_WRONLY|O_APPEND */
    assert(do_write(add,"Z",1)==1 && !do_close(add));
    rw=(int)do_open("/data/positions",0,0); assert(rw>=3);
    memset(got,0,sizeof(got));
    assert(do_read(rw,got,8)==5 && !memcmp(got,"ab23Z",5) && !do_close(rw));
    /* Rename replaces an existing file, as POSIX rename. */
    int other=(int)do_open("/data/other",0x202,0644); assert(other>=3 && do_write(other,"x",1)==1 && !do_close(other));
    assert(!do_rename("/data/positions","/data/other"));
    assert(do_stat("/data/positions",&info)==-ENOENT && !do_stat("/data/other",&info) && info.size==5);
    assert(do_open("/data/missing",0,0)==-ENOENT);
    /* Directories: opened read-only, listed, stat as directories; exclusive create of a file. */
    assert(!path_op("/data/sub",0,0755) && path_op("/data/sub",0,0755)==-EEXIST);
    int sub=(int)do_open("/data/sub",0,0); assert(sub>=3);
    assert(!do_fstat(sub,&info) && (info.mode&0170000)==0040000 && !do_close(sub));
    assert(do_open("/data/sub",2,0)==-EISDIR);
    assert(do_open("/data/other",0x20000,0)==-ENOTDIR);
    assert(do_open("/data/other",0xa02,0644)==-EEXIST); /* O_CREAT|O_EXCL */
    assert(!path_op("/data/sub",1,0));
    /* A name outside ASCII (UTF-8). */
    int utf=(int)do_open("/data/\xc3\xa9t\xc3\xa9",0x202,0644); assert(utf>=3);
    assert(do_write(utf,"ok",2)==2 && !do_close(utf));
    assert(!do_stat("/data/\xc3\xa9t\xc3\xa9",&info) && info.size==2);
    assert(!path_op("/data/\xc3\xa9t\xc3\xa9",2,0) && !path_op("/data/other",2,0));
    assert(!unlink(link) && !unlink(source) && !rmdir(game));
    const char *dirs[]={"temp0","download0","data"};
    for (int i=0;i<3;++i) { char p[1024]; snprintf(p,sizeof(p),"%s/%s",user,dirs[i]); assert(!rmdir(p)); }
    assert(!rmdir(user) && !rmdir(root));
    puts("Guest mod files: reads, stat, merged listing, readonly assets and writable saves PASS");
}
