/* Guest file system: PS4 mount points mapped onto host directories.
 *   /app0, /hostapp  -> game package root (read-only by convention)
 *   /temp0, /download0, /data, and mounts added by SaveData -> user directory
 * Guest descriptors are small integers in our own table; stdio 0-2 pass through.
 * Paths containing ".." components are rejected rather than normalized. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
/* Windows: files are handles with a position kept here (pread/pwrite leave it alone, as POSIX);
 * paths are UTF-8, opened as long wide paths. */
#include <windows.h>
#include <io.h>
#endif
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define MAX_FILES 1024
#define MAX_MOUNTS 16

typedef struct { int64_t sec, nsec; } GuestTimespec;
typedef struct {
    uint32_t dev, ino;
    uint16_t mode, nlink;
    uint32_t uid, gid, rdev;
    GuestTimespec atime, mtime, ctime;
    int64_t size, blocks;
    uint32_t blksize, flags, gen;
    int32_t lspare;
    GuestTimespec birthtime;
} GuestStat;
_Static_assert(sizeof(GuestStat)==120,"FreeBSD stat layout");

typedef struct { char *names; size_t count, *offsets; unsigned char *types; } Listing;
#ifdef _WIN32
typedef struct { int used, host; Listing *dir; size_t position; char path[512]; HANDLE handle; int64_t offset; int append; } File;
#else
typedef struct { int used, host; Listing *dir; size_t position; char path[512]; } File;
#endif
typedef struct { char guest[64]; char host[512]; } Mount;
static File files[MAX_FILES];
static Mount mounts[MAX_MOUNTS];
static size_t mount_count, opens, reads, writes, missing;
static uint64_t bytes_read;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;

int runtime_file_mount(const char *guest,const char *host) {
    pthread_mutex_lock(&lock);
    for (size_t i=0;i<mount_count;++i) if (!strcmp(mounts[i].guest,guest)) {
        snprintf(mounts[i].host,sizeof(mounts[i].host),"%s",host);
        pthread_mutex_unlock(&lock); return 0;
    }
    if (mount_count==MAX_MOUNTS || strlen(guest)>=64 || strlen(host)>=512) { pthread_mutex_unlock(&lock); return -1; }
    snprintf(mounts[mount_count].guest,64,"%s",guest);
    snprintf(mounts[mount_count].host,512,"%s",host);
    ++mount_count;
    pthread_mutex_unlock(&lock);
    return 0;
}
void runtime_file_unmount(const char *guest) {
    pthread_mutex_lock(&lock);
    for (size_t i=0;i<mount_count;++i) if (!strcmp(mounts[i].guest,guest)) {
        mounts[i]=mounts[--mount_count]; break;
    }
    pthread_mutex_unlock(&lock);
}
static char user_root[512]="user";
const char *runtime_file_user_dir(void) { return user_root; }
void runtime_file_configure(const char *app0,const char *user) {
    char path[600];
    snprintf(user_root,sizeof(user_root),"%s",user);
    runtime_file_mount("/app0",app0);
    runtime_file_mount("/hostapp",app0);
    const char *writable[]={"temp0","download0","data"};
    mkdir(user,0755);
    for (int i=0;i<3;++i) {
        snprintf(path,sizeof(path),"%s/%s",user,writable[i]);
        mkdir(path,0755);
        char guest[32]; snprintf(guest,sizeof(guest),"/%s",writable[i]);
        runtime_file_mount(guest,path);
    }
}
/* Resolve a guest path to a host path; returns 0 or a host errno. */
static int translate(const char *guest,char *out,size_t size) {
    if (!guest || !*guest) return ENOENT;
    char buffer[1024];
    if (guest[0]!='/') snprintf(buffer,sizeof(buffer),"/app0/%s",guest);
    else snprintf(buffer,sizeof(buffer),"%s",guest);
    for (const char *p=buffer;(p=strstr(p,".."));p+=2)
        if ((p==buffer || p[-1]=='/') && (p[2]==0 || p[2]=='/')) return EACCES;
    pthread_mutex_lock(&lock);
    size_t best=0; const Mount *m=NULL;
    for (size_t i=0;i<mount_count;++i) {
        size_t n=strlen(mounts[i].guest);
        if (!strncmp(buffer,mounts[i].guest,n) && (buffer[n]=='/' || !buffer[n]) && n>best) { best=n; m=&mounts[i]; }
    }
    int result=0;
    if (!m) result=ENOENT;
    else if ((size_t)snprintf(out,size,"%s%s",m->host,buffer+best)>=size) result=ENAMETOOLONG;
    pthread_mutex_unlock(&lock);
    if (result==ENOENT) fprintf(stderr,"Runtime: no mount for guest path %s\n",guest);
    return result;
}
#ifndef _WIN32
static int host_flags(int flags) {
    int r;
    switch (flags&3) { case 0: r=O_RDONLY; break; case 1: r=O_WRONLY; break; default: r=O_RDWR; }
    if (flags&0x4) r|=O_NONBLOCK;
    if (flags&0x8) r|=O_APPEND;
    if (flags&0x80) r|=O_SYNC;
    if (flags&0x200) r|=O_CREAT;
    if (flags&0x400) r|=O_TRUNC;
    if (flags&0x800) r|=O_EXCL;
    if (flags&0x20000) r|=O_DIRECTORY;
    return r|O_CLOEXEC;
}
#endif
static void free_listing(Listing *l) { if (l) { free(l->names); free(l->offsets); free(l->types); free(l); } }
#ifdef _WIN32
static int wide_path(const char *path, wchar_t *out, size_t size);
static int64_t do_stat(const char *guest,GuestStat *out);
static void listing_add(Listing *l, size_t *capacity, size_t *bytes, size_t *cap_names, const char *name, unsigned char type) {
    size_t n=strlen(name)+1;
    if (l->count==*capacity) {
        *capacity=*capacity ? *capacity*2 : 64;
        l->offsets=realloc(l->offsets,*capacity*sizeof(size_t));
        l->types=realloc(l->types,*capacity);
    }
    if (*bytes+n>*cap_names) { *cap_names=(*bytes+n)*2; l->names=realloc(l->names,*cap_names); }
    if (!l->offsets || !l->types || !l->names) { fputs("Out of memory listing directory\n",stderr); exit(1); }
    memcpy(l->names+*bytes,name,n);
    l->offsets[l->count]=*bytes;
    l->types[l->count]=type;
    ++l->count; *bytes+=n;
}
static Listing *list_directory(const char *path) {
    wchar_t pattern[1100];
    if (wide_path(path,pattern,sizeof(pattern)/sizeof(*pattern)-3)) return NULL;
    wcscat(pattern,L"\\*");
    WIN32_FIND_DATAW entry;
    HANDLE find=FindFirstFileW(pattern,&entry);
    if (find==INVALID_HANDLE_VALUE) return NULL;
    Listing *l=calloc(1,sizeof(*l));
    size_t capacity=0,bytes=0,cap_names=0;
    do {
        char name[1024];
        if (!WideCharToMultiByte(CP_UTF8,0,entry.cFileName,-1,name,sizeof(name),NULL,NULL)) continue;
        listing_add(l,&capacity,&bytes,&cap_names,name,(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8);
    } while (l && FindNextFileW(find,&entry));
    FindClose(find);
    return l;
}
#else
static Listing *list_directory(const char *path) {
    DIR *d=opendir(path);
    if (!d) return NULL;
    Listing *l=calloc(1,sizeof(*l));
    size_t capacity=0,bytes=0,cap_names=0;
    struct dirent *e;
    while (l && (e=readdir(d))) {
        size_t n=strlen(e->d_name)+1;
        if (l->count==capacity) {
            capacity=capacity ? capacity*2 : 64;
            l->offsets=realloc(l->offsets,capacity*sizeof(size_t));
            l->types=realloc(l->types,capacity);
        }
        if (bytes+n>cap_names) { cap_names=(bytes+n)*2; l->names=realloc(l->names,cap_names); }
        if (!l->offsets || !l->types || !l->names) { fputs("Out of memory listing directory\n",stderr); exit(1); }
        memcpy(l->names+bytes,e->d_name,n);
        l->offsets[l->count]=bytes;
        unsigned char type=e->d_type;
        if (type==DT_LNK || type==DT_UNKNOWN) {
            struct stat entry;
            if (!fstatat(dirfd(d),e->d_name,&entry,0))
                type=S_ISDIR(entry.st_mode) ? DT_DIR : S_ISREG(entry.st_mode) ? DT_REG : type;
        }
        l->types[l->count]=type==DT_DIR ? 4 : type==DT_REG ? 8 : type==DT_LNK ? 10 : 0;
        ++l->count; bytes+=n;
    }
    closedir(d);
    return l;
}
#endif
#ifndef _WIN32
static void convert_stat(const struct stat *s,GuestStat *g) {
    memset(g,0,sizeof(*g));
    g->dev=(uint32_t)s->st_dev; g->ino=(uint32_t)s->st_ino;
    g->mode=(uint16_t)s->st_mode; g->nlink=(uint16_t)s->st_nlink;
    g->size=s->st_size; g->blocks=s->st_blocks; g->blksize=(uint32_t)s->st_blksize;
    g->atime=(GuestTimespec){s->st_atim.tv_sec,s->st_atim.tv_nsec};
    g->mtime=(GuestTimespec){s->st_mtim.tv_sec,s->st_mtim.tv_nsec};
    g->ctime=(GuestTimespec){s->st_ctim.tv_sec,s->st_ctim.tv_nsec};
    g->birthtime=g->ctime;
}
#endif
static File *get(int fd) {
    if (fd<3 || fd>=MAX_FILES || !files[fd].used) return NULL;
    return &files[fd];
}
/* BB_AUDIO_TRACE=1: sound file opens and failed reads (missing game sounds). */
static int audio_trace(void) { static int v=-1; if (v<0) { const char *e=getenv("BB_AUDIO_TRACE"); v=e && e[0]=='1'; } return v; }
/* Game mounts (including linked mod overlays) are read-only. Saves use other mounts. */
static int game_path(const char *p) {
    if (!p || !*p) return 0;
    if (*p!='/') return 1;
    return (!strncmp(p,"/app0",5) && (!p[5] || p[5]=='/')) ||
           (!strncmp(p,"/hostapp",8) && (!p[8] || p[8]=='/'));
}
#ifdef _WIN32
/* UTF-8 host path -> absolute wide path with the long-path prefix. 0 or a host errno. */
static int wide_path(const char *path, wchar_t *out, size_t size) {
    wchar_t relative[1100], full[1100];
    if (!MultiByteToWideChar(CP_UTF8,0,path,-1,relative,(int)(sizeof(relative)/sizeof(*relative)))) return ENAMETOOLONG;
    for (wchar_t *c=relative;*c;++c) if (*c==L'/') *c=L'\\';
    DWORD n=GetFullPathNameW(relative,(DWORD)(sizeof(full)/sizeof(*full)),full,NULL);
    if (!n || n>=sizeof(full)/sizeof(*full)) return ENAMETOOLONG;
    /* Trailing separators (a directory named with a slash) are not part of the name. */
    while (n>3 && full[n-1]==L'\\') full[--n]=0;
    const wchar_t *prefix = !wcsncmp(full,L"\\\\",2) ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    const wchar_t *rest = !wcsncmp(full,L"\\\\",2) ? full+2 : full;
    if (wcslen(prefix)+wcslen(rest)+1>size) return ENAMETOOLONG;
    wcscpy(out,prefix); wcscat(out,rest);
    return 0;
}
static int host_error(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_DRIVE: return ENOENT;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: case ERROR_WRITE_PROTECT: return EACCES;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return EEXIST;
    case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    case ERROR_NOACCESS: return EFAULT;
    case ERROR_INVALID_HANDLE: return EBADF;
    case ERROR_FILENAME_EXCED_RANGE: return ENAMETOOLONG;
    case ERROR_DIRECTORY: return ENOTDIR;
    case ERROR_NEGATIVE_SEEK: case ERROR_INVALID_PARAMETER: return EINVAL;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return ENOMEM;
    case ERROR_TOO_MANY_OPEN_FILES: return EMFILE;
    default: return EIO;
    }
}
/* FILETIME (100 ns since 1601) -> Unix time. */
static GuestTimespec guest_time(FILETIME t) {
    const int64_t units=(int64_t)(((uint64_t)t.dwHighDateTime<<32)|t.dwLowDateTime)-INT64_C(116444736000000000);
    return (GuestTimespec){units/10000000,(units%10000000)*100};
}
static void guest_stat(DWORD attributes, uint64_t size, FILETIME access, FILETIME write, FILETIME created,
                       uint64_t index, GuestStat *g) {
    memset(g,0,sizeof(*g));
    const int dir=(attributes & FILE_ATTRIBUTE_DIRECTORY)!=0, readonly=(attributes & FILE_ATTRIBUTE_READONLY)!=0;
    g->mode=(uint16_t)(dir ? 0040000|(readonly ? 0555 : 0755) : 0100000|(readonly ? 0444 : 0644));
    g->nlink=1; g->ino=(uint32_t)index;
    g->size=dir ? 4096 : (int64_t)size; g->blocks=(g->size+511)/512; g->blksize=65536;
    g->atime=guest_time(access); g->mtime=guest_time(write); g->ctime=g->mtime; g->birthtime=guest_time(created);
}
/* Reads into guest memory fail with ERROR_NOACCESS when a page was protected again (GPU write
 * tracking) after touch_for_write: touched once more and retried. */
static int64_t handle_io(HANDLE h, void *buffer, uint64_t size, int64_t offset, int write) {
    uint64_t done=0;
    for (int retries=0; done<size;) {
        const DWORD chunk=size-done>(UINT64_C(1)<<30) ? (DWORD)1<<30 : (DWORD)(size-done);
        OVERLAPPED at; memset(&at,0,sizeof(at));
        const uint64_t position=(uint64_t)offset+done;
        at.Offset=(DWORD)position; at.OffsetHigh=(DWORD)(position>>32);
        DWORD n=0;
        BOOL ok=write ? WriteFile(h,(const char *)buffer+done,chunk,&n,&at) : ReadFile(h,(char *)buffer+done,chunk,&n,&at);
        if (!ok) {
            DWORD e=GetLastError();
            if (e==ERROR_HANDLE_EOF) break;
            if (e==ERROR_NOACCESS && !write && retries++<16) {
                volatile unsigned char *b=(volatile unsigned char *)buffer+done;
                for (uint64_t p=0;p<chunk;p+=4096) b[p]=b[p];
                continue;
            }
            return done ? (int64_t)done : -host_error(e);
        }
        done+=n;
        if (n<chunk) break;
    }
    return (int64_t)done;
}
/* As on Linux (touch_for_write below): each page of the buffer goes through the fault handler
 * before the kernel writes it. */
static void touch_for_write(void *buffer,uint64_t size) {
    if (!size) return;
    uintptr_t p=(uintptr_t)buffer & ~(uintptr_t)4095, end=(uintptr_t)buffer+size;
    for (; p<end; p+=4096) {
        volatile unsigned char *b=(volatile unsigned char *)(p<(uintptr_t)buffer ? (uintptr_t)buffer : p);
        *b=*b;
    }
}
#endif
/* All operations return >=0 or -(host errno); wrappers adapt the convention. */
#ifdef _WIN32
static int64_t do_open(const char *guest,int flags,int mode) {
    (void)mode;
    if (game_path(guest) && (flags & (3|0x8|0x200|0x400|0x800))) return -EROFS;
    char path[1024];
    wchar_t wide[1100];
    int e=translate(guest,path,sizeof(path));
    if (!e) e=wide_path(path,wide,sizeof(wide)/sizeof(*wide));
    if (e) return -e;
    const DWORD attributes=GetFileAttributesW(wide);
    HANDLE handle=INVALID_HANDLE_VALUE;
    Listing *dir=NULL;
    int64_t size=0;
    if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (flags & (3|0x200|0x400)) return -EISDIR;
        dir=list_directory(path);
        if (!dir) return -EACCES;
    } else {
        if (flags & 0x20000) return attributes==INVALID_FILE_ATTRIBUTES ? -ENOENT : -ENOTDIR;
        const DWORD access=(flags&3)==0 ? GENERIC_READ : (flags&3)==1 ? GENERIC_WRITE : GENERIC_READ|GENERIC_WRITE;
        const int create=(flags&0x200)!=0, truncate=(flags&0x400)!=0, exclusive=(flags&0x800)!=0;
        const DWORD disposition=create ? (exclusive ? CREATE_NEW : truncate ? CREATE_ALWAYS : OPEN_ALWAYS)
                                       : truncate ? TRUNCATE_EXISTING : OPEN_EXISTING;
        handle=CreateFileW(wide,access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,disposition,
                           FILE_ATTRIBUTE_NORMAL|((flags&0x80) ? FILE_FLAG_WRITE_THROUGH : 0),NULL);
        if (handle==INVALID_HANDLE_VALUE) {
            e=host_error(GetLastError());
            if (e==ENOENT) { ++missing; printf("Runtime: open(%s) -> not found\n",guest); }
            return -e;
        }
        LARGE_INTEGER bytes;
        if (GetFileSizeEx(handle,&bytes)) size=bytes.QuadPart;
    }
    pthread_mutex_lock(&lock);
    int fd=-1;
    for (int i=3;i<MAX_FILES;++i) if (!files[i].used) { fd=i; break; }
    if (fd<0) { pthread_mutex_unlock(&lock); if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); free_listing(dir); return -EMFILE; }
    files[fd]=(File){.used=1,.host=-1,.dir=dir,.handle=handle,.append=(flags&0x8)!=0};
    snprintf(files[fd].path,sizeof(files[fd].path),"%s",guest);
    ++opens;
    pthread_mutex_unlock(&lock);
    if (audio_trace() && strstr(guest,"sound/")) printf("Audio trace: open(%s) -> fd %d, %lld bytes\n",guest,fd,(long long)size);
    return fd;
}
static int64_t do_close(int fd) {
    if (fd>=0 && fd<3) return 0;
    pthread_mutex_lock(&lock);
    File *f=get(fd);
    if (!f) { pthread_mutex_unlock(&lock); return -EBADF; }
    if (f->handle && f->handle!=INVALID_HANDLE_VALUE) CloseHandle(f->handle);
    free_listing(f->dir);
    *f=(File){0};
    pthread_mutex_unlock(&lock);
    return 0;
}
/* The handle of a regular file (NULL with -errno in *error otherwise). */
static HANDLE file_handle(int fd,File **out,int *error) {
    File *f=get(fd);
    *out=f;
    if (!f) { *error=EBADF; return NULL; }
    if (f->dir) { *error=EISDIR; return NULL; }
    return f->handle;
}
static int64_t do_read(int fd,void *buffer,uint64_t size) {
    if (fd>=0 && fd<3) { int n=_read(fd,buffer,(unsigned)(size>0x7fffffff ? 0x7fffffff : size)); return n<0 ? -errno : n; }
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    if (!h) return -e;
    runtime_memory_note_write((uintptr_t)buffer,size);
    touch_for_write(buffer,size);
    const int64_t offset=__atomic_load_n(&f->offset,__ATOMIC_RELAXED);
    int64_t n=handle_io(h,buffer,size,offset,0);
    if (n<0) { if (audio_trace()) printf("Audio trace: read(fd %d, %llu) failed, errno %d\n",fd,(unsigned long long)size,(int)-n); return n; }
    __atomic_store_n(&f->offset,offset+n,__ATOMIC_RELAXED);
    if (n>0) runtime_memory_note_write((uintptr_t)buffer,(uint64_t)n); /* and once the data is there */
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pread(int fd,void *buffer,uint64_t size,int64_t offset) {
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    if (!h) return -e;
    if (offset<0) return -EINVAL;
    runtime_memory_note_write((uintptr_t)buffer,size);
    touch_for_write(buffer,size);
    int64_t n=handle_io(h,buffer,size,offset,0);
    if (n<0) { if (audio_trace()) printf("Audio trace: pread(fd %d, %llu @%lld) failed, errno %d\n",fd,(unsigned long long)size,(long long)offset,(int)-n); return n; }
    if (n>0) runtime_memory_note_write((uintptr_t)buffer,(uint64_t)n); /* and once the data is there */
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t file_size(HANDLE h) { LARGE_INTEGER bytes; return GetFileSizeEx(h,&bytes) ? bytes.QuadPart : -1; }
static int64_t do_write(int fd,const void *buffer,uint64_t size) {
    if (fd>=0 && fd<3) { int n=_write(fd,buffer,(unsigned)(size>0x7fffffff ? 0x7fffffff : size)); return n<0 ? -errno : n; }
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    if (!h) return -e;
    const int64_t offset=f->append ? file_size(h) : __atomic_load_n(&f->offset,__ATOMIC_RELAXED);
    int64_t n=handle_io(h,(void *)buffer,size,offset,1);
    if (n<0) return n;
    __atomic_store_n(&f->offset,offset+n,__ATOMIC_RELAXED);
    __atomic_add_fetch(&writes,1,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pwrite(int fd,const void *buffer,uint64_t size,int64_t offset) {
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    if (!h) return -e;
    if (offset<0) return -EINVAL;
    return handle_io(h,(void *)buffer,size,offset,1);
}
static int64_t do_lseek(int fd,int64_t offset,int whence) {
    File *f=get(fd);
    if (!f) return -EBADF;
    if (whence<0 || whence>2) return -EINVAL;
    if (f->dir) {
        /* Directory offsets are entry indices for getdirentries. */
        if (whence==0 && offset>=0) { f->position=(size_t)offset; return offset; }
        return -EINVAL;
    }
    int64_t base=whence==0 ? 0 : whence==1 ? __atomic_load_n(&f->offset,__ATOMIC_RELAXED) : file_size(f->handle);
    if (base<0) return -EIO;
    if (base+offset<0) return -EINVAL;
    __atomic_store_n(&f->offset,base+offset,__ATOMIC_RELAXED);
    return base+offset;
}
static int64_t do_fstat(int fd,GuestStat *out) {
    if (!out) return -EFAULT;
    File *f=get(fd);
    if (!f) return -EBADF;
    if (f->dir) { char path[1024]; int e=translate(f->path,path,sizeof(path)); return e ? -e : do_stat(f->path,out); }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(f->handle,&info)) return -host_error(GetLastError());
    guest_stat(info.dwFileAttributes,((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow,info.ftLastAccessTime,
               info.ftLastWriteTime,info.ftCreationTime,((uint64_t)info.nFileIndexHigh<<32)|info.nFileIndexLow,out);
    return 0;
}
static int64_t do_stat(const char *guest,GuestStat *out) {
    char path[1024]; wchar_t wide[1100];
    int e=translate(guest,path,sizeof(path));
    if (!e) e=wide_path(path,wide,sizeof(wide)/sizeof(*wide));
    if (e) return -e;
    if (!out) return -EFAULT;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(wide,GetFileExInfoStandard,&info)) return -host_error(GetLastError());
    uint64_t index=1469598103934665603ull; /* FNV-1a of the path: stable inode numbers */
    for (const char *c=path;*c;++c) index=(index^(unsigned char)*c)*1099511628211ull;
    guest_stat(info.dwFileAttributes,((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow,info.ftLastAccessTime,
               info.ftLastWriteTime,info.ftCreationTime,index,out);
    return 0;
}
#else
static int64_t do_open(const char *guest,int flags,int mode) {
    if (game_path(guest) && (flags & (3|0x8|0x200|0x400|0x800))) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    int host=open(path,host_flags(flags),mode ? mode : 0644);
    if (host<0) {
        e=errno;
        if (e==ENOENT) { ++missing; printf("Runtime: open(%s) -> not found\n",guest); }
        return -e;
    }
    struct stat s;
    Listing *dir=NULL;
    if (!fstat(host,&s) && S_ISDIR(s.st_mode)) dir=list_directory(path);
    pthread_mutex_lock(&lock);
    int fd=-1;
    for (int i=3;i<MAX_FILES;++i) if (!files[i].used) { fd=i; break; }
    if (fd<0) { pthread_mutex_unlock(&lock); close(host); free_listing(dir); return -EMFILE; }
    files[fd]=(File){.used=1,.host=host,.dir=dir};
    snprintf(files[fd].path,sizeof(files[fd].path),"%s",guest);
    ++opens;
    pthread_mutex_unlock(&lock);
    if (audio_trace() && strstr(guest,"sound/")) printf("Audio trace: open(%s) -> fd %d, %lld bytes\n",guest,fd,(long long)s.st_size);
    const char *mod_trace=getenv("BB_MOD_TRACE"), *mod_root=getenv("BB_MODS_DIR");
    if (mod_trace && mod_trace[0]=='1' && mod_root) {
        char actual[PATH_MAX],root[PATH_MAX];
        static unsigned traced;
        if (realpath(path,actual) && realpath(mod_root,root)) {
            size_t n=strlen(root);
            if (!strncmp(actual,root,n) && actual[n]=='/' &&
                __atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<32)
                printf("Mods: open %s -> %s\n",guest,actual);
        }
    }
    return fd;
}
static int64_t do_close(int fd) {
    if (fd>=0 && fd<3) return 0;
    pthread_mutex_lock(&lock);
    File *f=get(fd);
    if (!f) { pthread_mutex_unlock(&lock); return -EBADF; }
    close(f->host); free_listing(f->dir);
    *f=(File){0};
    pthread_mutex_unlock(&lock);
    return 0;
}
static int host_fd(int fd) {
    if (fd>=0 && fd<3) return fd;
    File *f=get(fd);
    return f ? f->host : -1;
}
/* The GPU side is told of the write first (runtime_memory_note_write: its tracking unprotects the
 * range). Pages protected again meanwhile would make the kernel's copy fail with EFAULT instead
 * of faulting to our handler: a user-mode write to each page first goes through the handler. */
static void touch_for_write(void *buffer,uint64_t size) {
    if (!size) return;
    uintptr_t p=(uintptr_t)buffer & ~(uintptr_t)4095, end=(uintptr_t)buffer+size;
    for (; p<end; p+=4096) {
        volatile unsigned char *b=(volatile unsigned char *)(p<(uintptr_t)buffer ? (uintptr_t)buffer : p);
        *b=*b;
    }
}
static int64_t do_read(int fd,void *buffer,uint64_t size) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    runtime_memory_note_write((uintptr_t)buffer,size);
    touch_for_write(buffer,size);
    ssize_t n=read(h,buffer,size);
    if (n<0) { if (audio_trace()) printf("Audio trace: read(fd %d, %llu) failed, errno %d\n",fd,(unsigned long long)size,errno); return -errno; }
    if (n>0) runtime_memory_note_write((uintptr_t)buffer,(uint64_t)n); /* and once the data is there */
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pread(int fd,void *buffer,uint64_t size,int64_t offset) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    runtime_memory_note_write((uintptr_t)buffer,size);
    touch_for_write(buffer,size);
    ssize_t n=pread(h,buffer,size,offset);
    if (n<0) { if (audio_trace()) printf("Audio trace: pread(fd %d, %llu @%lld) failed, errno %d\n",fd,(unsigned long long)size,(long long)offset,errno); return -errno; }
    if (n>0) runtime_memory_note_write((uintptr_t)buffer,(uint64_t)n); /* and once the data is there */
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_write(int fd,const void *buffer,uint64_t size) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    ssize_t n=write(h,buffer,size);
    if (n<0) return -errno;
    __atomic_add_fetch(&writes,1,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pwrite(int fd,const void *buffer,uint64_t size,int64_t offset) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    ssize_t n=pwrite(h,buffer,size,offset);
    return n<0 ? -errno : n;
}
static int64_t do_lseek(int fd,int64_t offset,int whence) {
    File *f=get(fd);
    if (!f) return -EBADF;
    if (whence<0 || whence>2) return -EINVAL;
    if (f->dir) {
        /* Directory offsets are entry indices for getdirentries. */
        if (whence==0 && offset>=0) { f->position=(size_t)offset; return offset; }
        return -EINVAL;
    }
    off_t r=lseek(f->host,offset,whence);
    return r<0 ? -errno : r;
}
static int64_t do_fstat(int fd,GuestStat *out) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    if (!out) return -EFAULT;
    struct stat s;
    if (fstat(h,&s)) return -errno;
    convert_stat(&s,out); return 0;
}
static int64_t do_stat(const char *guest,GuestStat *out) {
    char path[1024]; struct stat s;
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    if (!out) return -EFAULT;
    if (stat(path,&s)) return -errno;
    convert_stat(&s,out); return 0;
}
#endif
static int64_t do_getdents(int fd,char *buffer,uint64_t size,int64_t *basep) {
    pthread_mutex_lock(&lock);
    File *f=get(fd);
    int64_t result=0;
    if (!f) result=-EBADF;
    else if (!f->dir) result=-EINVAL;
    else if (!buffer) result=-EFAULT;
    else if (size<512) result=-EINVAL;
    else {
        if (basep) *basep=(int64_t)f->position;
        uint64_t written=0;
        while (f->position<f->dir->count) {
            const char *name=f->dir->names+f->dir->offsets[f->position];
            size_t n=strlen(name); if (n>255) n=255;
            uint16_t reclen=(uint16_t)((8+n+1+7)&~(size_t)7);
            if (written+reclen>size) break;
            char *p=buffer+written;
            memset(p,0,reclen);
            uint32_t ino=(uint32_t)f->position+1;
            memcpy(p,&ino,4); memcpy(p+4,&reclen,2);
            p[6]=(char)f->dir->types[f->position]; p[7]=(char)n;
            memcpy(p+8,name,n);
            written+=reclen; ++f->position;
        }
        result=(int64_t)written;
    }
    pthread_mutex_unlock(&lock);
    return result;
}
#ifdef _WIN32
static int64_t path_op(const char *guest,int op,int mode) {
    (void)mode;
    if (game_path(guest)) return -EROFS;
    char path[1024]; wchar_t wide[1100];
    int e=translate(guest,path,sizeof(path));
    if (!e) e=wide_path(path,wide,sizeof(wide)/sizeof(*wide));
    if (e) return -e;
    BOOL ok = op==0 ? CreateDirectoryW(wide,NULL) : op==1 ? RemoveDirectoryW(wide) : DeleteFileW(wide);
    return ok ? 0 : -host_error(GetLastError());
}
static int64_t do_rename(const char *from,const char *to) {
    if (game_path(from) || game_path(to)) return -EROFS;
    char a[1024],b[1024]; wchar_t wa[1100],wb[1100];
    int e=translate(from,a,sizeof(a));
    if (!e) e=translate(to,b,sizeof(b));
    if (!e) e=wide_path(a,wa,sizeof(wa)/sizeof(*wa));
    if (!e) e=wide_path(b,wb,sizeof(wb)/sizeof(*wb));
    if (e) return -e;
    /* POSIX rename replaces an existing target. */
    return MoveFileExW(wa,wb,MOVEFILE_REPLACE_EXISTING) ? 0 : -host_error(GetLastError());
}
static int64_t set_size(HANDLE h,int64_t length) {
    if (length<0) return -EINVAL;
    FILE_END_OF_FILE_INFO end={.EndOfFile.QuadPart=length};
    return SetFileInformationByHandle(h,FileEndOfFileInfo,&end,sizeof(end)) ? 0 : -host_error(GetLastError());
}
static int64_t do_ftruncate(int fd,int64_t length) {
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    return h ? set_size(h,length) : -e;
}
static int64_t do_truncate(const char *guest,int64_t length) {
    if (game_path(guest)) return -EROFS;
    char path[1024]; wchar_t wide[1100];
    int e=translate(guest,path,sizeof(path));
    if (!e) e=wide_path(path,wide,sizeof(wide)/sizeof(*wide));
    if (e) return -e;
    HANDLE h=CreateFileW(wide,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h==INVALID_HANDLE_VALUE) return -host_error(GetLastError());
    int64_t r=set_size(h,length);
    CloseHandle(h);
    return r;
}
static int64_t do_fsync(int fd) {
    if (fd>=0 && fd<3) return 0;
    File *f; int e=0;
    HANDLE h=file_handle(fd,&f,&e);
    if (!h) return f && f->dir ? 0 : -e;
    return FlushFileBuffers(h) ? 0 : -host_error(GetLastError());
}
static int64_t do_access(const char *guest,int mode) {
    char path[1024]; wchar_t wide[1100];
    int e=translate(guest,path,sizeof(path));
    if (!e) e=wide_path(path,wide,sizeof(wide)/sizeof(*wide));
    if (e) return -e;
    DWORD attributes=GetFileAttributesW(wide);
    if (attributes==INVALID_FILE_ATTRIBUTES) return -host_error(GetLastError());
    if ((mode & 2) && (attributes & FILE_ATTRIBUTE_READONLY) && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return -EACCES;
    return 0;
}
#else
static int64_t path_op(const char *guest,int op,int mode) {
    if (game_path(guest)) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    int r= op==0 ? mkdir(path,mode ? mode : 0755) : op==1 ? rmdir(path) : unlink(path);
    return r ? -errno : 0;
}
static int64_t do_rename(const char *from,const char *to) {
    if (game_path(from) || game_path(to)) return -EROFS;
    char a[1024],b[1024];
    int e=translate(from,a,sizeof(a));
    if (!e) e=translate(to,b,sizeof(b));
    if (e) return -e;
    return rename(a,b) ? -errno : 0;
}
static int64_t do_ftruncate(int fd,int64_t length) {
    int h=host_fd(fd);
    if (h<0) return -EBADF;
    return ftruncate(h,length) ? -errno : 0;
}
static int64_t do_truncate(const char *guest,int64_t length) {
    if (game_path(guest)) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    return truncate(path,length) ? -errno : 0;
}
static int64_t do_fsync(int fd) { int h=host_fd(fd); if (h<0) return -EBADF; return fsync(h) ? -errno : 0; }
static int64_t do_access(const char *guest,int mode) {
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    return access(path,mode&7) ? -errno : 0;
}
#endif

/* Convention adapters: sceKernel* -> Orbis error codes, POSIX -> -1 + errno. */
static int64_t sce(int64_t r) { return r<0 ? ERR(runtime_guest_errno((int)-r)) : r; }
static int64_t posix(int64_t r) { if (r<0) { *runtime_errno()=runtime_guest_errno((int)-r); return -1; } return r; }
#define PAIR(name,params,args) \
    static ABI int64_t sce_##name params { return sce(do_##name args); } \
    static ABI int64_t posix_##name params { return posix(do_##name args); }
PAIR(open,(const char *p,int f,int m),(p,f,m))
PAIR(close,(int fd),(fd))
PAIR(read,(int fd,void *b,uint64_t n),(fd,b,n))
PAIR(pread,(int fd,void *b,uint64_t n,int64_t o),(fd,b,n,o))
PAIR(write,(int fd,const void *b,uint64_t n),(fd,b,n))
PAIR(pwrite,(int fd,const void *b,uint64_t n,int64_t o),(fd,b,n,o))
PAIR(lseek,(int fd,int64_t o,int w),(fd,o,w))
PAIR(fstat,(int fd,GuestStat *s),(fd,s))
PAIR(stat,(const char *p,GuestStat *s),(p,s))
PAIR(getdents,(int fd,char *b,uint64_t n,int64_t *base),(fd,b,n,base))
PAIR(rename,(const char *a,const char *b),(a,b))
PAIR(ftruncate,(int fd,int64_t l),(fd,l))
PAIR(truncate,(const char *p,int64_t l),(p,l))
PAIR(fsync,(int fd),(fd))
static ABI int64_t posix_access(const char *p,int m) { return posix(do_access(p,m)); }
static ABI int64_t sce_mkdir(const char *p,int m) { return sce(path_op(p,0,m)); }
static ABI int64_t posix_mkdir(const char *p,int m) { return posix(path_op(p,0,m)); }
static ABI int64_t sce_rmdir(const char *p) { return sce(path_op(p,1,0)); }
static ABI int64_t posix_rmdir(const char *p) { return posix(path_op(p,1,0)); }
static ABI int64_t sce_unlink(const char *p) { return sce(path_op(p,2,0)); }
static ABI int64_t posix_unlink(const char *p) { return posix(path_op(p,2,0)); }
static ABI int32_t sce_check_reachability(const char *p) {
    GuestStat s; return (int32_t)sce(do_stat(p,&s));
}

static const RuntimeExport exports[]={
    {"sceKernelOpen",sce_open}, {"open",posix_open}, {"_open",posix_open},
    {"sceKernelClose",sce_close}, {"close",posix_close}, {"_close",posix_close},
    {"sceKernelRead",sce_read}, {"read",posix_read}, {"_read",posix_read},
    {"sceKernelPread",sce_pread}, {"pread",posix_pread},
    {"sceKernelWrite",sce_write}, {"write",posix_write}, {"_write",posix_write},
    {"sceKernelPwrite",sce_pwrite}, {"pwrite",posix_pwrite},
    {"sceKernelLseek",sce_lseek}, {"lseek",posix_lseek},
    {"sceKernelFstat",sce_fstat}, {"fstat",posix_fstat},
    {"sceKernelStat",sce_stat}, {"stat",posix_stat},
    {"sceKernelGetdirentries",sce_getdents}, {"getdirentries",posix_getdents},
    {"sceKernelRename",sce_rename}, {"rename",posix_rename},
    {"sceKernelFtruncate",sce_ftruncate}, {"ftruncate",posix_ftruncate},
    {"sceKernelTruncate",sce_truncate}, {"truncate",posix_truncate},
    {"sceKernelFsync",sce_fsync}, {"fsync",posix_fsync},
    {"access",posix_access}, {"sceKernelCheckReachability",sce_check_reachability},
    {"sceKernelMkdir",sce_mkdir}, {"mkdir",posix_mkdir},
    {"sceKernelRmdir",sce_rmdir}, {"rmdir",posix_rmdir},
    {"sceKernelUnlink",sce_unlink}, {"unlink",posix_unlink},
};
uintptr_t runtime_file_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
/* Host-side helpers for other modules (e.g. SaveData, Fios). */
int64_t runtime_file_open(const char *p,int f,int m) { return do_open(p,f,m); }
int64_t runtime_file_close(int fd) { return do_close(fd); }
int64_t runtime_file_read(int fd,void *b,uint64_t n) { return do_read(fd,b,n); }
int64_t runtime_file_pread(int fd,void *b,uint64_t n,int64_t o) { return do_pread(fd,b,n,o); }
int64_t runtime_file_write(int fd,const void *b,uint64_t n) { return do_write(fd,b,n); }
int64_t runtime_file_lseek(int fd,int64_t o,int w) { return do_lseek(fd,o,w); }
int64_t runtime_file_stat(const char *p,void *s) { return do_stat(p,s); }
int64_t runtime_file_fstat(int fd,void *s) { return do_fstat(fd,s); }
int64_t runtime_file_getdents(int fd,char *b,uint64_t n,int64_t *base) { return do_getdents(fd,b,n,base); }
int runtime_file_translate(const char *guest,char *out,size_t size) { return translate(guest,out,size); }
void runtime_file_report(void) {
    printf("Runtime: files opened=%zu, reads=%zu (%llu bytes), writes=%zu, not found=%zu\n",
           opens,reads,(unsigned long long)bytes_read,writes,missing);
}
