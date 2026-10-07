/* System services for an offline, single-user console:
 *   - one local user (id 1) who is logged in but not signed in to PSN;
 *   - network cable unplugged: NetCtl disconnected, sockets unavailable;
 *   - common dialogs complete immediately (no UI is drawn yet);
 *   - the whole package is installed (PlayGo reports every chunk local).
 * Every entry here is an explicit contract; unknown functions still stop. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>

#define USER_ID 1
#define ORBIS_OK 0
#define USER_INVALID_ARGUMENT ((int32_t)0x80960005)
#define USER_NO_EVENT ((int32_t)0x80960007)
#define SYSTEM_NO_EVENT ((int32_t)0x80A10004)
#define SYSTEM_PARAMETER ((int32_t)0x80A10003)
#define NET_CTL_NOT_CONNECTED ((int32_t)0x80412108)
#define NET_CTL_INVALID_ADDR ((int32_t)0x80412107)
#define NP_SIGNED_OUT ((int32_t)0x80550006)
#define NP_INVALID_ARGUMENT ((int32_t)0x80550003)
#define NET_ENETUNREACH ((int32_t)0x80410133)
#define NET_EINVAL ((int32_t)0x80410116)
#define HTTP_NETWORK ((int32_t)0x80431063)
#define DIALOG_NOT_INITIALIZED ((int32_t)0x80B80003)
#define PLAYGO_BAD_HANDLE ((int32_t)0x80B20009)
#define PLAYGO_BAD_POINTER ((int32_t)0x80B2000A)
#define PLAYGO_BAD_SIZE ((int32_t)0x80B2000B)
#define PLAYGO_BAD_CHUNK ((int32_t)0x80B2000C)
#define AUDIO_IN_NOT_OPENED ((int32_t)0x80260109)
#define TROPHY_INVALID ((int32_t)0x80551604)

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int next_id=1;
static int new_id(void) { pthread_mutex_lock(&lock); int id=next_id++; pthread_mutex_unlock(&lock); return id; }
static void note(const char *what) { printf("Runtime: %s\n",what); }

/* ---- UserService ---- */
static int login_event_pending=1;
static ABI int32_t user_initialize(const void *params) { (void)params; note("UserService initialized (user 1 logged in)"); return 0; }
static ABI int32_t user_terminate(void) { return 0; }
static ABI int32_t user_initial(int32_t *id) { if (!id) return USER_INVALID_ARGUMENT; *id=USER_ID; return 0; }
static ABI int32_t user_list(int32_t *ids) {
    if (!ids) return USER_INVALID_ARGUMENT;
    ids[0]=USER_ID; ids[1]=ids[2]=ids[3]=-1; return 0;
}
static ABI int32_t user_name(int32_t id,char *name,uint64_t size) {
    if (id!=USER_ID || !name) return USER_INVALID_ARGUMENT;
    const char *value=getenv("BB_USER_NAME") ? getenv("BB_USER_NAME") : "Hunter";
    if (strlen(value)+1>size) return (int32_t)0x8096000a; /* BUFFER_TOO_SHORT */
    strcpy(name,value); return 0;
}
static ABI int32_t user_event(int32_t *event) {
    if (!event) return USER_INVALID_ARGUMENT;
    pthread_mutex_lock(&lock);
    int pending=login_event_pending; login_event_pending=0;
    pthread_mutex_unlock(&lock);
    if (!pending) return USER_NO_EVENT;
    event[0]=0; event[1]=USER_ID; /* LOGIN */
    return 0;
}

/* ---- SystemService ---- */
static int language(void) { const char *v=getenv("BB_LANGUAGE"); return v ? atoi(v) : 1; }
static ABI int32_t system_param(int32_t id,int32_t *value) {
    if (!value) return SYSTEM_PARAMETER;
    switch (id) {
    case 1: *value=language(); break;           /* language (1 = English US, 8 = Russian) */
    case 2: *value=1; break;                    /* date format DD/MM/YYYY */
    case 3: *value=1; break;                    /* 24-hour clock */
#ifdef _WIN32
    case 4: *value=(int32_t)(bb_utc_offset(time(NULL))/60); break;
#else
    case 4: { time_t now=time(NULL); struct tm t; localtime_r(&now,&t); *value=(int32_t)(t.tm_gmtoff/60); break; }
#endif
    case 5: *value=0; break;                    /* summer time */
    case 7: *value=0; break;                    /* parental level off */
    case 1000: *value=1; break;                 /* enter button = cross */
    default: fprintf(stderr,"STOP: unsupported system parameter %d\n",id); exit(21);
    }
    return 0;
}
static ABI int32_t system_status(unsigned char *status) {
    if (!status) return SYSTEM_PARAMETER;
    memset(status,0,12); /* event_num=0, no overlay, foreground, normal CPU mode */
    return 0;
}
static ABI int32_t system_event(void *event) { (void)event; return SYSTEM_NO_EVENT; }
static ABI int32_t hide_splash(void) { note("SystemService: splash screen hidden"); return 0; }
static ABI int32_t launch_browser(void) { note("SystemService: web browser request ignored (offline)"); return 0; }

/* ---- NetCtl / Net / Http / Ssl: no network ---- */
static int32_t net_errno;
static ABI int32_t net_init(void) { return 0; }
static ABI int32_t net_term(void) { return 0; }
static ABI int32_t *net_errno_loc(void) { return &net_errno; }
static ABI int32_t net_pool_create(const char *name,int size,int flags) { (void)name; (void)size; (void)flags; return new_id(); }
static ABI int32_t net_pool_destroy(int id) { (void)id; return 0; }
static ABI int32_t net_unreachable(void) { net_errno=51; return NET_ENETUNREACH; }
static ABI int32_t net_epoll_create(const char *name,int flags) { (void)name; (void)flags; return new_id(); }
static ABI int32_t net_epoll_destroy(int id) { (void)id; return 0; }
static ABI int32_t net_resolver_create(const char *name,int pool,int flags) { (void)name; (void)pool; (void)flags; return new_id(); }
static ABI int32_t net_resolver_destroy(int id) { (void)id; return 0; }
static ABI uint16_t net_htons(uint16_t v) { return htons(v); }
static ABI uint16_t net_ntohs(uint16_t v) { return ntohs(v); }
static ABI uint32_t net_htonl(uint32_t v) { return htonl(v); }
static ABI uint32_t net_ntohl(uint32_t v) { return ntohl(v); }
static ABI int32_t net_pton(int af,const char *src,void *dst) {
    if (af!=2) { net_errno=47; return NET_EINVAL; }
    return inet_pton(AF_INET,src,dst);
}
static ABI const char *net_ntop(int af,const void *src,char *dst,uint32_t size) {
    if (af!=2) { net_errno=47; return NULL; }
    return inet_ntop(AF_INET,src,dst,size);
}
/* BB_ONLINE=1 (co-op work, observation): the console is connected and signed in to PSN, and
 * every HTTP request and matchmaking call is logged ("Online: ..."). Nothing leaves the PC:
 * transfers still fail as unplugged. */
static int online(void) {
    static int value=-1;
    if (value<0) { const char *e=getenv("BB_ONLINE"); value=e && e[0]=='1'; }
    return value;
}
static const char *online_name(void) {
    const char *name=getenv("BB_USER_NAME");
    return name && *name ? name : "Hunter";
}
static ABI int32_t netctl_state(int32_t *state) {
    if (!state) return NET_CTL_INVALID_ADDR;
    *state=online() ? 3 : 0; /* IPOBTAINED : DISCONNECTED */
    if (online()) puts("Online: NetCtlGetState -> 3");
    return 0;
}
static ABI int32_t netctl_info(int code,void *info) {
    if (!online()) { (void)code; (void)info; return NET_CTL_NOT_CONNECTED; }
    if (!info) return NET_CTL_INVALID_ADDR;
    memset(info,0,256);
    if (code==14) strcpy(info,"192.168.0.2"); /* IP_ADDRESS */
    else if (code==15) strcpy(info,"255.255.255.0"); /* NETMASK */
    else if (code==16) strcpy(info,"192.168.0.1"); /* DEFAULT_ROUTE */
    else if (code==17 || code==18) strcpy(info,"192.168.0.1"); /* DNS */
    else if (code==20) *(uint32_t *)info=1500; /* MTU */
    printf("Online: NetCtlGetInfo %d\n",code);
    return 0;
}
static ABI int32_t netctl_register(void *cb,void *arg,int32_t *cid) { (void)cb; (void)arg; if (!cid) return NET_CTL_INVALID_ADDR; *cid=new_id(); return 0; }
static ABI int32_t netctl_check(void) { return 0; }
static ABI int32_t netctl_unregister(int cid) { (void)cid; return 0; }
static ABI int32_t netctl_nat(uint32_t *info) {
    if (!info) return NET_CTL_INVALID_ADDR;
    info[1]=0; info[2]=3; info[3]=0; /* stun failed, NAT type 3, no mapped address */
    return 0;
}
static ABI int32_t lib_init_id(void) { return new_id(); }
static ABI int32_t ok_void(void) { return 0; }
static ABI int32_t http_fail(void) { return HTTP_NETWORK; }
/* Objects are created so setup code proceeds; any transfer fails as unplugged. */
static ABI int32_t http_template(int32_t ctx,const char *agent,int32_t version,int32_t proxy) {
    (void)proxy;
    if (online()) printf("Online: HttpCreateTemplate ctx %d agent \"%s\" http %d\n",ctx,agent ? agent : "",version);
    return new_id();
}
static ABI int32_t http_connection(int32_t tmpl,const char *url,int32_t keepalive) {
    const int32_t id=new_id();
    if (online()) printf("Online: HttpCreateConnectionWithURL %d -> %d: %s (keepalive %d)\n",tmpl,id,url ? url : "",keepalive);
    return id;
}
static ABI int32_t http_request(int32_t conn,int32_t method,const char *url,uint64_t length) {
    const int32_t id=new_id();
    static const char *methods[]={"GET","POST","HEAD","OPTIONS","PUT","DELETE","TRACE","CONNECT"};
    if (online()) printf("Online: HttpCreateRequestWithURL %d -> %d: %s %s (content length %llu)\n",conn,id,
                         method>=0 && method<8 ? methods[method] : "?",url ? url : "",(unsigned long long)length);
    return id;
}
static ABI int32_t http_header(int32_t id,const char *name,const char *value,uint32_t mode) {
    if (online()) printf("Online: HttpAddRequestHeader %d: %s: %s (mode %u)\n",id,name ? name : "",value ? value : "",mode);
    return 0;
}
static ABI int32_t http_send(int32_t id,const void *data,size_t size) {
    if (online()) {
        printf("Online: HttpSendRequest %d, %zu bytes:",id,size);
        const unsigned char *b=data;
        for (size_t i=0;b && i<size && i<4096;++i) {
            if (b[i]>=32 && b[i]<127) putchar(b[i]); else printf("\\x%02x",b[i]);
        }
        putchar('\n');
    }
    return HTTP_NETWORK;
}
static ABI int32_t http_epoll(int32_t ctx,void **handle) {
    (void)ctx;
    if (!handle) return (int32_t)0x80431077; /* HTTP INVALID_VALUE */
    *handle=(void *)(uintptr_t)(0x100+new_id()); return 0;
}
static ABI int32_t http_wait(void *handle,void *events,int32_t max,int64_t timeout) {
    (void)handle; (void)events; (void)max;
    if (timeout>0) { struct timespec t={timeout/1000000,(timeout%1000000)*1000}; nanosleep(&t,NULL); }
    return 0; /* no events: nothing is in flight */
}

/* ---- NP (PSN): signed out ---- */
static ABI int32_t np_state(int32_t user,int32_t *state) {
    if (!state) return NP_INVALID_ARGUMENT;
    (void)user; *state=online() ? 2 : 1; /* SIGNED_IN : SIGNED_OUT */
    if (online()) puts("Online: NpGetState -> 2");
    return 0;
}
static ABI int32_t np_signed_out(void) { return NP_SIGNED_OUT; }
/* OrbisNpOnlineId: char data[16], term, dummy[3]; OrbisNpId: the online id, opt[8], reserved[8]. */
static ABI int32_t np_online_id(int32_t user,char *id) {
    (void)user;
    if (!online()) return NP_SIGNED_OUT;
    if (!id) return NP_INVALID_ARGUMENT;
    memset(id,0,20);
    strncpy(id,online_name(),16);
    printf("Online: NpGetOnlineId -> %s\n",id);
    return 0;
}
static ABI int32_t np_np_id(int32_t user,char *id) {
    (void)user;
    if (!online()) return NP_SIGNED_OUT;
    if (!id) return NP_INVALID_ARGUMENT;
    memset(id,0,36);
    strncpy(id,online_name(),16);
    printf("Online: NpGetNpId -> %s\n",id);
    return 0;
}
/* Availability, PS Plus, parental controls: fine while online (the results are read through
 * the async request, np_poll). */
static ABI int32_t np_check(void) { return online() ? 0 : NP_SIGNED_OUT; }
static ABI int32_t np_plus(int32_t req,const void *param,uint8_t *result) {
    (void)req; (void)param;
    if (!online()) return NP_SIGNED_OUT;
    if (result) *result=1; /* authorized */
    puts("Online: NpCheckPlus");
    return 0;
}
/* OrbisNpAuthGetAuthorizationCodeParameter: size, online id pointer?, client id, scope...;
 * OrbisNpAuthorizationCode: char code[128], padding. The server will accept any code. */
static ABI int32_t np_auth_code(int32_t req,const uint64_t *param,char *code,int32_t *issuer) {
    (void)req;
    if (!online()) return NP_SIGNED_OUT;
    if (param) {
        const char *client=(const char *)param[2], *scope=(const char *)param[3];
        printf("Online: NpAuthGetAuthorizationCode client %.40s scope %.80s\n",
               client ? client : "?",scope ? scope : "?");
    }
    if (code) { memset(code,0,136); strcpy(code,"BBPORTCOOP"); }
    if (issuer) *issuer=1;
    return 0;
}
static ABI int32_t np_register(void *cb,void *arg) { (void)cb; (void)arg; return new_id(); }
static ABI void np_register_void(void *cb,void *arg) { (void)cb; (void)arg; }
static ABI int32_t np_request(const void *param) { (void)param; return new_id(); }
static ABI int32_t np_request_ctx(int32_t ctx,const void *param) { (void)ctx; (void)param; return new_id(); }
static ABI int32_t np_poll(int32_t request,int32_t *result) {
    (void)request;
    if (result) *result=online() ? 0 : NP_SIGNED_OUT;
    return 0; /* finished */
}
/* Matchmaking, signaling, web API and score calls: logged with their first arguments while
 * online (still refused: no server yet). */
#define NP_TRACE(name) \
    static ABI int32_t trace_##name(uint64_t a,uint64_t b,uint64_t c,uint64_t d) { \
        if (online()) printf("Online: " #name "(%#llx, %#llx, %#llx, %#llx)\n",(unsigned long long)a, \
                             (unsigned long long)b,(unsigned long long)c,(unsigned long long)d); \
        return NP_SIGNED_OUT; \
    }
NP_TRACE(sceNpMatching2CreateContext) NP_TRACE(sceNpMatching2ContextStart)
NP_TRACE(sceNpMatching2CreateJoinRoom) NP_TRACE(sceNpMatching2JoinRoom) NP_TRACE(sceNpMatching2LeaveRoom)
NP_TRACE(sceNpMatching2SearchRoom) NP_TRACE(sceNpMatching2GetServerId) NP_TRACE(sceNpMatching2GetWorldInfoList)
NP_TRACE(sceNpMatching2GetLobbyInfoList) NP_TRACE(sceNpMatching2JoinLobby) NP_TRACE(sceNpMatching2LeaveLobby)
NP_TRACE(sceNpMatching2GrantRoomOwner) NP_TRACE(sceNpMatching2KickoutRoomMember)
NP_TRACE(sceNpMatching2SetRoomDataExternal) NP_TRACE(sceNpMatching2SetRoomDataInternal)
NP_TRACE(sceNpMatching2SetRoomMemberDataInternal) NP_TRACE(sceNpMatching2SignalingGetConnectionStatus)
NP_TRACE(sceNpMatching2SignalingGetPingInfo) NP_TRACE(sceNpSignalingCreateContext)
NP_TRACE(sceNpSignalingActivateConnection) NP_TRACE(sceNpSignalingGetConnectionStatus)
NP_TRACE(sceNpWebApiCreateContext) NP_TRACE(sceNpWebApiCreateRequest) NP_TRACE(sceNpWebApiSendRequest)
NP_TRACE(sceNpScoreGetGameData) NP_TRACE(sceNpScoreRecordGameData) NP_TRACE(sceNpScoreGetRankingByRange)
NP_TRACE(sceNpScoreRecordScore) NP_TRACE(sceNpLookupNpId) NP_TRACE(sceNpGetGamePresenceStatus)
static ABI int32_t np_compare(const void *a,const void *b) {
    if (!a || !b) return NP_INVALID_ARGUMENT;
    return memcmp(a,b,16) ? (int32_t)0x80550609 : 0; /* NP_UTIL NOT_MATCH */
}

/* ---- Voice chat: ports exist, carry no audio ---- */
static ABI int32_t voice_port(void *param,uint32_t *port) { (void)param; if (!port) return (int32_t)0x8029000b; *port=(uint32_t)new_id(); return 0; }
static ABI int32_t voice_read(uint32_t port,void *data,uint32_t *size) { (void)port; (void)data; if (size) *size=0; return 0; }
static ABI int32_t voice_write(uint32_t port,const void *data,uint32_t *size) { (void)port; (void)data; (void)size; return 0; }
static ABI int32_t voice_info(uint32_t port,uint32_t *info) {
    (void)port;
    if (!info) return (int32_t)0x8029000b;
    memset(info,0,40); /* type, state=unconnected, no bytes available */
    return 0;
}

/* ---- Common dialogs: nothing is displayed; an opened dialog finishes. ---- */
typedef struct { const char *name; int initialized, status; } Dialog;
static Dialog dialogs[]={{"CommonDialog",0,0},{"MsgDialog",0,0},{"SaveDataDialog",0,0},
                         {"NpProfileDialog",0,0},{"NpCommerceDialog",0,0},{"ImeDialog",0,0}};
static int common_initialized;
static ABI int32_t common_init(void) { common_initialized=1; return 0; }
static int32_t dialog_init(int i) {
    if (dialogs[i].initialized) return (int32_t)0x80B80004;
    dialogs[i].initialized=1; dialogs[i].status=1; return 0;
}
static int32_t dialog_open(int i) {
    if (!dialogs[i].initialized) return DIALOG_NOT_INITIALIZED;
    printf("Runtime: %s opened; completed immediately (no dialog UI yet)\n",dialogs[i].name);
    dialogs[i].status=3; return 0;
}
static int32_t dialog_status(int i) { return dialogs[i].status; }
static int32_t dialog_term(int i) {
    if (!dialogs[i].initialized) return DIALOG_NOT_INITIALIZED;
    dialogs[i].initialized=0; dialogs[i].status=0; return 0;
}
#define DIALOG(tag,i) \
    static ABI int32_t tag##_init(void) { return dialog_init(i); } \
    static ABI int32_t tag##_open(const void *p) { (void)p; return dialog_open(i); } \
    static ABI int32_t tag##_status(void) { return dialog_status(i); } \
    static ABI int32_t tag##_term(void) { return dialog_term(i); }
DIALOG(msg,1) DIALOG(save,2) DIALOG(profile,3) DIALOG(commerce,4)
static ABI int32_t profile_result(void *result) { if (result) memset(result,0,4); return 0; }
/* ImeDialog: text typed on the keyboard into the game window (title bar shows it).
 * OrbisImeDialogParam: user, type, languages(8), enter label, method, filter,
 * option, max length, char16 buffer, position, alignment, placeholder, title. */
typedef struct {
    int32_t user; uint32_t type; uint64_t languages; uint32_t enter_label, input_method;
    void *filter; uint32_t option, max_length; uint16_t *buffer;
    float x, y; uint32_t halign, valign; const uint16_t *placeholder, *title; int8_t reserved[16];
} ImeParam;
static struct { int running, finished, end_status; uint16_t *buffer; uint32_t max_length; } ime;
static size_t utf16_to_utf8(const uint16_t *in, size_t limit, char *out, size_t size) {
    size_t n=0;
    for (size_t i=0; in && i<limit && in[i] && n+4<size; ++i) {
        uint32_t c=in[i];
        if (c>=0xD800 && c<0xDC00 && i+1<limit && in[i+1]>=0xDC00 && in[i+1]<0xE000) { c=0x10000+((c-0xD800)<<10)+(in[i+1]-0xDC00); ++i; }
        if (c<0x80) out[n++]=(char)c;
        else if (c<0x800) { out[n++]=(char)(0xC0|c>>6); out[n++]=(char)(0x80|(c&63)); }
        else if (c<0x10000) { out[n++]=(char)(0xE0|c>>12); out[n++]=(char)(0x80|((c>>6)&63)); out[n++]=(char)(0x80|(c&63)); }
        else { out[n++]=(char)(0xF0|c>>18); out[n++]=(char)(0x80|((c>>12)&63)); out[n++]=(char)(0x80|((c>>6)&63)); out[n++]=(char)(0x80|(c&63)); }
    }
    out[n]=0; return n;
}
static void utf8_to_utf16(const char *in, uint16_t *out, uint32_t max) {
    uint32_t n=0;
    for (const unsigned char *p=(const unsigned char *)in; *p && n<max;) {
        uint32_t c; int extra;
        if (*p<0x80) { c=*p; extra=0; } else if ((*p&0xE0)==0xC0) { c=*p&31; extra=1; }
        else if ((*p&0xF0)==0xE0) { c=*p&15; extra=2; } else { c=*p&7; extra=3; }
        ++p;
        for (int k=0;k<extra && (*p&0xC0)==0x80;++k) c=(c<<6)|(*p++&63);
        if (c>=0x10000) { if (n+2>max) break; c-=0x10000; out[n++]=(uint16_t)(0xD800+(c>>10)); out[n++]=(uint16_t)(0xDC00+(c&1023)); }
        else out[n++]=(uint16_t)c;
    }
    out[n<max ? n : max]=0;
}
static void ime_complete(int end_status, const char *text) {
    if (!end_status && ime.buffer && ime.max_length) utf8_to_utf16(text,ime.buffer,ime.max_length);
    ime.end_status=end_status; ime.finished=1; ime.running=0;
    printf("Runtime: ImeDialog %s%s%s\n",end_status ? "cancelled" : "text: ",end_status ? "" : text,"");
}
static ABI int32_t ime_init(const ImeParam *param, const void *extended) {
    (void)extended;
    if (!param || !param->buffer || !param->max_length) return (int32_t)0x80BC0004; /* IME INVALID_ADDRESS */
    if (ime.running) return (int32_t)0x80BC0003;                                  /* BUSY */
    memset(&ime,0,sizeof(ime));
    ime.buffer=param->buffer; ime.max_length=param->max_length; ime.running=1;
    char initial[512], prompt[256];
    utf16_to_utf8(param->buffer,param->max_length,initial,sizeof(initial));
    utf16_to_utf8(param->title,128,prompt,sizeof(prompt));
    const char *preset=getenv("BB_IME_TEXT");
    if (preset) { ime_complete(0,preset); return 0; }
    if (!bbgpu_text_input_begin(initial,prompt[0] ? prompt : "Text")) {
        const char *name=getenv("BB_USER_NAME");
        ime_complete(0,name ? name : initial[0] ? initial : "Hunter");
    } else printf("Runtime: ImeDialog opened: type in the game window, Enter to confirm, Esc to cancel\n");
    return 0;
}
static ABI int32_t ime_status(void) {
    if (ime.running) {
        char text[512];
        int state=bbgpu_text_input_poll(text,sizeof(text));
        if (state) ime_complete(state==1 ? 0 : 1,text);
    }
    return ime.running ? 1 : ime.finished ? 2 : 0; /* Running / Finished / None */
}
static ABI int32_t ime_result(uint32_t *result) {
    if (!ime.finished) return (int32_t)0x80BC0101; /* DIALOG NOT_FINISHED */
    if (result) *result=(uint32_t)ime.end_status;
    return 0;
}
static ABI int32_t ime_term(void) { memset(&ime,0,sizeof(ime)); return 0; }

/* ---- Trophies: accepted locally, recorded in the log ---- */
static ABI int32_t trophy_context(int32_t *ctx,int32_t user,uint32_t label,uint64_t options) {
    (void)user; (void)label; (void)options;
    if (!ctx) return TROPHY_INVALID;
    *ctx=new_id(); return 0;
}
static ABI int32_t trophy_handle(int32_t *handle) { if (!handle) return TROPHY_INVALID; *handle=new_id(); return 0; }
static ABI int32_t trophy_register(int32_t ctx,int32_t handle,uint64_t options) { (void)ctx; (void)handle; (void)options; return 0; }
static ABI int32_t trophy_unlock(int32_t ctx,int32_t handle,int32_t id,int32_t *platinum) {
    (void)ctx; (void)handle;
    printf("Runtime: trophy %d unlocked\n",id);
    if (platinum) *platinum=-1;
    return 0;
}
static ABI int32_t trophy_game_info(int32_t ctx,int32_t handle,void *details,void *data) {
    (void)ctx; (void)handle;
    if (details) { uint64_t size; memcpy(&size,details,8); memset((char *)details+8,0,size>8 && size<4096 ? size-8 : 0); }
    if (data) { uint64_t size; memcpy(&size,data,8); memset((char *)data+8,0,size>8 && size<4096 ? size-8 : 0); }
    return 0;
}
static ABI int32_t trophy_info(int32_t ctx,int32_t handle,int32_t id,void *details,void *data) {
    (void)id; return trophy_game_info(ctx,handle,details,data);
}

/* ---- PlayGo: fully installed package ---- */
static int playgo_handle, playgo_chunks=-1;
static ABI int32_t playgo_init(const void *params) {
    (void)params;
    int fd=(int)runtime_file_open("/app0/sce_sys/playgo-chunk.dat",0,0);
    unsigned char header[12];
    playgo_chunks=1;
    if (fd>=0) {
        if (runtime_file_read(fd,header,sizeof(header))==sizeof(header)) playgo_chunks=header[10]|header[11]<<8;
        runtime_file_close(fd);
    }
    printf("Runtime: PlayGo initialized; %d chunks, all installed locally\n",playgo_chunks);
    return 0;
}
static ABI int32_t playgo_open(int32_t *handle,const void *param) {
    (void)param;
    if (!handle) return PLAYGO_BAD_POINTER;
    playgo_handle=1; *handle=1; return 0;
}
static ABI int32_t playgo_chunk_ids(int32_t handle,uint16_t *ids,uint32_t count,uint32_t *out) {
    if (handle!=playgo_handle || !handle) return PLAYGO_BAD_HANDLE;
    if (!out) return PLAYGO_BAD_POINTER;
    if (ids && !count) return PLAYGO_BAD_SIZE;
    if (!ids) { *out=(uint32_t)playgo_chunks; return 0; }
    uint32_t n=count<(uint32_t)playgo_chunks ? count : (uint32_t)playgo_chunks;
    for (uint32_t i=0;i<n;++i) ids[i]=(uint16_t)i;
    *out=n; return 0;
}
static ABI int32_t playgo_locus(int32_t handle,const uint16_t *ids,uint32_t count,int8_t *loci) {
    if (handle!=playgo_handle || !handle) return PLAYGO_BAD_HANDLE;
    if (!ids || !loci) return PLAYGO_BAD_POINTER;
    if (!count) return PLAYGO_BAD_SIZE;
    for (uint32_t i=0;i<count;++i) {
        if (ids[i]>=playgo_chunks) return PLAYGO_BAD_CHUNK;
        loci[i]=3; /* LocalFast */
    }
    return 0;
}
static ABI int32_t playgo_speed(int32_t handle,int32_t speed) { (void)speed; return handle==playgo_handle && handle ? 0 : PLAYGO_BAD_HANDLE; }

/* ---- DiscMap: the game is fully installed, no disc bitmap exists ---- */
#define DISC_MAP_NO_BITMAP ((int32_t)0x81100004)
static ABI int32_t discmap_on_hdd(const char *path,int64_t offset,int64_t size,int32_t *result) {
    (void)path; (void)offset; (void)size; (void)result; return DISC_MAP_NO_BITMAP;
}
static ABI int32_t discmap_8a82(const char *path,int64_t offset,int64_t size,int32_t *flags,int32_t *r1,int32_t *r2) {
    (void)path; (void)offset; (void)size; (void)flags; (void)r1; (void)r2; return DISC_MAP_NO_BITMAP;
}

/* ---- Devices that are absent: mouse, microphone, voice chat ---- */
/* A mouse port opens but never reports a connected device. */
static ABI int32_t mouse_open(int32_t user,int32_t type,int32_t index,const void *param) { (void)user; (void)type; (void)index; (void)param; return new_id(); }
static ABI int32_t mouse_read(int32_t handle,unsigned char *data,int32_t count) {
    (void)handle;
    if (!data || count<1) return (int32_t)0x80DF0001;
    memset(data,0,40); return 1; /* timestamp 0, connected=false */
}
static ABI int32_t mouse_close(int32_t handle) { (void)handle; return 0; }
static ABI int32_t audio_in_open(void) { return AUDIO_IN_NOT_OPENED; }


/* BB_ONLINE: the network imports that share a stub, each logged under its own name. */
#define NET_WRAP(name, fn) \
    static ABI uint64_t wrap_##name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) { \
        if (online()) printf("Online: " #name "(%#llx, %#llx, %#llx, %#llx)\n",(unsigned long long)a, \
                             (unsigned long long)b,(unsigned long long)c,(unsigned long long)d); \
        return ((uint64_t (ABI *)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))(uintptr_t)fn)(a,b,c,d,e,f); \
    }
NET_WRAP(sceNetPoolCreate, net_pool_create)
NET_WRAP(sceNetEpollCreate, net_epoll_create)
NET_WRAP(sceNetResolverCreate, net_resolver_create)
NET_WRAP(sceNetSocket, net_unreachable)
NET_WRAP(sceNetConnect, net_unreachable)
NET_WRAP(sceNetBind, net_unreachable)
NET_WRAP(sceNetListen, net_unreachable)
NET_WRAP(sceNetAccept, net_unreachable)
NET_WRAP(sceNetSend, net_unreachable)
NET_WRAP(sceNetSendto, net_unreachable)
NET_WRAP(sceNetRecv, net_unreachable)
NET_WRAP(sceNetRecvfrom, net_unreachable)
NET_WRAP(sceNetSetsockopt, net_unreachable)
NET_WRAP(sceNetGetsockopt, net_unreachable)
NET_WRAP(sceNetGetsockname, net_unreachable)
NET_WRAP(sceNetShutdown, net_unreachable)
NET_WRAP(sceNetSocketClose, net_unreachable)
NET_WRAP(sceNetSocketAbort, net_unreachable)
NET_WRAP(sceNetEpollControl, net_unreachable)
NET_WRAP(sceNetEpollWait, net_unreachable)
NET_WRAP(sceNetEpollAbort, net_unreachable)
NET_WRAP(sceNetResolverStartNtoa, net_unreachable)
NET_WRAP(sceNetResolverStartAton, net_unreachable)
NET_WRAP(sceNetCtlRegisterCallback, netctl_register)
NET_WRAP(sceNetCtlCheckCallback, netctl_check)
NET_WRAP(sceSslInit, lib_init_id)
NET_WRAP(sceSslTerm, ok_void)
NET_WRAP(sceHttpInit, lib_init_id)
NET_WRAP(sceHttpTerm, ok_void)
NET_WRAP(sceHttpDeleteTemplate, ok_void)
NET_WRAP(sceHttpCreateEpoll, http_epoll)
NET_WRAP(sceHttpSetNonblock, ok_void)
NET_WRAP(sceHttpSetConnectTimeOut, ok_void)
NET_WRAP(sceHttpsEnableOption, ok_void)
NET_WRAP(sceHttpsDisableOption, ok_void)
NET_WRAP(sceHttpSetRequestContentLength, ok_void)
NET_WRAP(sceHttpDeleteConnection, ok_void)
NET_WRAP(sceHttpDeleteRequest, ok_void)
NET_WRAP(sceHttpAbortWaitRequest, ok_void)
NET_WRAP(sceHttpDestroyEpoll, ok_void)
NET_WRAP(sceHttpSetEpoll, ok_void)
NET_WRAP(sceHttpUnsetEpoll, ok_void)
NET_WRAP(sceHttpWaitRequest, http_wait)
NET_WRAP(sceHttpGetStatusCode, http_fail)
NET_WRAP(sceHttpGetResponseContentLength, http_fail)
NET_WRAP(sceHttpReadData, http_fail)
NET_WRAP(sceNpRegisterStateCallback, np_register)
NET_WRAP(sceNpUnregisterStateCallback, ok_void)
NET_WRAP(sceNpRegisterGamePresenceCallback, np_register_void)
NET_WRAP(sceNpRegisterPlusEventCallback, np_register)
NET_WRAP(sceNpUnregisterPlusEventCallback, ok_void)
NET_WRAP(sceNpCheckCallback, ok_void)
NET_WRAP(sceNpSetNpTitleId, ok_void)
NET_WRAP(sceNpNotifyPlusFeature, ok_void)
NET_WRAP(sceNpSetContentRestriction, ok_void)
NET_WRAP(sceNpCreateAsyncRequest, np_request)
NET_WRAP(sceNpDeleteRequest, ok_void)
NET_WRAP(sceNpAbortRequest, ok_void)
NET_WRAP(sceNpPollAsync, np_poll)
NET_WRAP(sceNpCheckNpAvailability, np_check)
NET_WRAP(sceNpGetParentalControlInfo, np_check)
NET_WRAP(sceNpAuthCreateAsyncRequest, np_request)
NET_WRAP(sceNpAuthDeleteRequest, ok_void)
NET_WRAP(sceNpAuthPollAsync, np_poll)
NET_WRAP(sceNpLookupCreateTitleCtx, np_request)
NET_WRAP(sceNpLookupDeleteTitleCtx, ok_void)
NET_WRAP(sceNpLookupCreateAsyncRequest, np_request_ctx)
NET_WRAP(sceNpLookupDeleteRequest, ok_void)
NET_WRAP(sceNpLookupAbortRequest, ok_void)
NET_WRAP(sceNpLookupPollAsync, np_poll)
NET_WRAP(sceNpScoreCreateNpTitleCtx, np_request_ctx)
NET_WRAP(sceNpScoreDeleteNpTitleCtx, ok_void)
NET_WRAP(sceNpScoreCreateRequest, np_request)
NET_WRAP(sceNpScoreDeleteRequest, ok_void)
NET_WRAP(sceNpScoreAbortRequest, ok_void)
NET_WRAP(sceNpWebApiInitialize, lib_init_id)
NET_WRAP(sceNpWebApiTerminate, ok_void)
NET_WRAP(sceNpWebApiDeleteRequest, ok_void)
NET_WRAP(sceNpWebApiAbortRequest, ok_void)
NET_WRAP(sceNpWebApiDeleteContext, ok_void)
NET_WRAP(sceNpWebApiDeletePushEventFilter, ok_void)
NET_WRAP(sceNpWebApiUnregisterPushEventCallback, ok_void)
NET_WRAP(sceNpMatching2ContextStop, ok_void)
NET_WRAP(sceNpMatching2DestroyContext, ok_void)
NET_WRAP(sceNpMatching2RegisterContextCallback, ok_void)
NET_WRAP(sceNpMatching2RegisterLobbyEventCallback, ok_void)
NET_WRAP(sceNpMatching2RegisterRoomEventCallback, ok_void)
NET_WRAP(sceNpMatching2RegisterSignalingCallback, ok_void)
NET_WRAP(sceNpMatching2SetDefaultRequestOptParam, ok_void)
NET_WRAP(sceNpSignalingDeleteContext, ok_void)
NET_WRAP(sceNpSignalingDeactivateConnection, ok_void)
NET_WRAP(sceNpScoreSetPlayerCharacterId, ok_void)
NET_WRAP(sceNpMatching2Initialize, ok_void)
NET_WRAP(sceNpMatching2Terminate, ok_void)
NET_WRAP(sceNpSignalingInitialize, ok_void)
NET_WRAP(sceNpSignalingTerminate, ok_void)

static const RuntimeExport exports[]={
    {"sceUserServiceInitialize",user_initialize}, {"sceUserServiceTerminate",user_terminate},
    {"sceUserServiceGetInitialUser",user_initial}, {"sceUserServiceGetLoginUserIdList",user_list},
    {"sceUserServiceGetUserName",user_name}, {"sceUserServiceGetEvent",user_event},
    {"sceSystemServiceParamGetInt",system_param}, {"sceSystemServiceGetStatus",system_status},
    {"sceSystemServiceReceiveEvent",system_event}, {"sceSystemServiceHideSplashScreen",hide_splash},
    {"sceSystemServiceLaunchWebBrowser",launch_browser},
    {"sceNetInit",net_init}, {"sceNetTerm",net_term}, {"sceNetErrnoLoc",net_errno_loc},
    {"sceNetPoolCreate",wrap_sceNetPoolCreate}, {"sceNetPoolDestroy",net_pool_destroy},
    {"sceNetEpollCreate",wrap_sceNetEpollCreate}, {"sceNetEpollDestroy",net_epoll_destroy},
    {"sceNetResolverCreate",wrap_sceNetResolverCreate}, {"sceNetResolverDestroy",net_resolver_destroy},
    {"sceNetHtons",net_htons}, {"sceNetNtohs",net_ntohs}, {"sceNetHtonl",net_htonl}, {"sceNetNtohl",net_ntohl},
    {"sceNetInetPton",net_pton}, {"sceNetInetNtop",net_ntop},
    {"sceNetSocket",wrap_sceNetSocket}, {"sceNetConnect",wrap_sceNetConnect}, {"sceNetBind",wrap_sceNetBind},
    {"sceNetListen",wrap_sceNetListen}, {"sceNetAccept",wrap_sceNetAccept}, {"sceNetSend",wrap_sceNetSend},
    {"sceNetSendto",wrap_sceNetSendto}, {"sceNetRecv",wrap_sceNetRecv}, {"sceNetRecvfrom",wrap_sceNetRecvfrom},
    {"sceNetSetsockopt",wrap_sceNetSetsockopt}, {"sceNetGetsockopt",wrap_sceNetGetsockopt},
    {"sceNetGetsockname",wrap_sceNetGetsockname}, {"sceNetShutdown",wrap_sceNetShutdown},
    {"sceNetSocketClose",wrap_sceNetSocketClose}, {"sceNetSocketAbort",wrap_sceNetSocketAbort},
    {"sceNetEpollControl",wrap_sceNetEpollControl}, {"sceNetEpollWait",wrap_sceNetEpollWait}, {"sceNetEpollAbort",wrap_sceNetEpollAbort},
    {"sceNetResolverStartNtoa",wrap_sceNetResolverStartNtoa}, {"sceNetResolverStartAton",wrap_sceNetResolverStartAton},
    {"sceNetCtlGetState",netctl_state}, {"sceNetCtlGetInfo",netctl_info},
    {"sceNetCtlRegisterCallback",wrap_sceNetCtlRegisterCallback}, {"sceNetCtlCheckCallback",wrap_sceNetCtlCheckCallback},
    {"sceNetCtlUnregisterCallback",netctl_unregister}, {"sceNetCtlGetNatInfo",netctl_nat},
    {"sceSslInit",wrap_sceSslInit}, {"sceSslTerm",wrap_sceSslTerm},
    {"sceHttpInit",wrap_sceHttpInit}, {"sceHttpTerm",wrap_sceHttpTerm},
    {"sceHttpCreateTemplate",http_template}, {"sceHttpDeleteTemplate",wrap_sceHttpDeleteTemplate},
    {"sceHttpCreateConnectionWithURL",http_connection}, {"sceHttpCreateRequestWithURL",http_request},
    {"sceHttpSendRequest",http_send}, {"sceHttpCreateEpoll",wrap_sceHttpCreateEpoll},
    {"sceHttpSetNonblock",wrap_sceHttpSetNonblock}, {"sceHttpSetConnectTimeOut",wrap_sceHttpSetConnectTimeOut},
    {"sceHttpsEnableOption",wrap_sceHttpsEnableOption}, {"sceHttpsDisableOption",wrap_sceHttpsDisableOption},
    {"sceHttpAddRequestHeader",http_header}, {"sceHttpSetRequestContentLength",wrap_sceHttpSetRequestContentLength},
    {"sceHttpDeleteConnection",wrap_sceHttpDeleteConnection}, {"sceHttpDeleteRequest",wrap_sceHttpDeleteRequest},
    {"sceHttpAbortWaitRequest",wrap_sceHttpAbortWaitRequest}, {"sceHttpDestroyEpoll",wrap_sceHttpDestroyEpoll},
    {"sceHttpSetEpoll",wrap_sceHttpSetEpoll}, {"sceHttpUnsetEpoll",wrap_sceHttpUnsetEpoll}, {"sceHttpWaitRequest",wrap_sceHttpWaitRequest},
    {"sceHttpGetStatusCode",wrap_sceHttpGetStatusCode}, {"sceHttpGetResponseContentLength",wrap_sceHttpGetResponseContentLength},
    {"sceHttpReadData",wrap_sceHttpReadData},
    {"sceNpGetState",np_state}, {"sceNpGetOnlineId",np_online_id}, {"sceNpGetNpId",np_np_id},
    {"sceNpRegisterStateCallback",wrap_sceNpRegisterStateCallback}, {"sceNpUnregisterStateCallback",wrap_sceNpUnregisterStateCallback},
    {"sceNpRegisterGamePresenceCallback",wrap_sceNpRegisterGamePresenceCallback}, {"sceNpRegisterPlusEventCallback",wrap_sceNpRegisterPlusEventCallback},
    {"sceNpUnregisterPlusEventCallback",wrap_sceNpUnregisterPlusEventCallback}, {"sceNpCheckCallback",wrap_sceNpCheckCallback},
    {"sceNpSetNpTitleId",wrap_sceNpSetNpTitleId}, {"sceNpNotifyPlusFeature",wrap_sceNpNotifyPlusFeature}, {"sceNpSetContentRestriction",wrap_sceNpSetContentRestriction},
    {"sceNpCreateAsyncRequest",wrap_sceNpCreateAsyncRequest}, {"sceNpDeleteRequest",wrap_sceNpDeleteRequest}, {"sceNpAbortRequest",wrap_sceNpAbortRequest},
    {"sceNpPollAsync",wrap_sceNpPollAsync}, {"sceNpCheckNpAvailability",wrap_sceNpCheckNpAvailability},
    {"sceNpGetParentalControlInfo",wrap_sceNpGetParentalControlInfo}, {"sceNpCheckPlus",np_plus},
    {"sceNpGetGamePresenceStatus",trace_sceNpGetGamePresenceStatus},
    {"sceNpCmpNpId",np_compare}, {"sceNpCmpOnlineId",np_compare},
    {"sceNpAuthCreateAsyncRequest",wrap_sceNpAuthCreateAsyncRequest}, {"sceNpAuthDeleteRequest",wrap_sceNpAuthDeleteRequest},
    {"sceNpAuthPollAsync",wrap_sceNpAuthPollAsync}, {"sceNpAuthGetAuthorizationCode",np_auth_code},
    {"sceNpLookupCreateTitleCtx",wrap_sceNpLookupCreateTitleCtx}, {"sceNpLookupDeleteTitleCtx",wrap_sceNpLookupDeleteTitleCtx},
    {"sceNpLookupCreateAsyncRequest",wrap_sceNpLookupCreateAsyncRequest}, {"sceNpLookupDeleteRequest",wrap_sceNpLookupDeleteRequest},
    {"sceNpLookupAbortRequest",wrap_sceNpLookupAbortRequest}, {"sceNpLookupPollAsync",wrap_sceNpLookupPollAsync}, {"sceNpLookupNpId",trace_sceNpLookupNpId},
    {"sceNpScoreCreateNpTitleCtx",wrap_sceNpScoreCreateNpTitleCtx}, {"sceNpScoreDeleteNpTitleCtx",wrap_sceNpScoreDeleteNpTitleCtx},
    {"sceNpScoreCreateRequest",wrap_sceNpScoreCreateRequest}, {"sceNpScoreDeleteRequest",wrap_sceNpScoreDeleteRequest}, {"sceNpScoreAbortRequest",wrap_sceNpScoreAbortRequest},
    {"sceNpWebApiInitialize",wrap_sceNpWebApiInitialize}, {"sceNpWebApiTerminate",wrap_sceNpWebApiTerminate},
    {"sceNpWebApiCreateContext",trace_sceNpWebApiCreateContext},
    {"sceNpWebApiCreateRequest",trace_sceNpWebApiCreateRequest}, {"sceNpWebApiSendRequest",trace_sceNpWebApiSendRequest},
    {"sceNpWebApiDeleteRequest",wrap_sceNpWebApiDeleteRequest}, {"sceNpWebApiAbortRequest",wrap_sceNpWebApiAbortRequest},
    {"sceNpWebApiDeleteContext",wrap_sceNpWebApiDeleteContext}, {"sceNpWebApiReadData",np_signed_out},
    {"sceNpWebApiGetHttpStatusCode",np_signed_out}, {"sceNpWebApiGetHttpResponseHeaderValue",np_signed_out},
    {"sceNpWebApiGetHttpResponseHeaderValueLength",np_signed_out},
    {"sceNpWebApiCreatePushEventFilter",np_signed_out}, {"sceNpWebApiDeletePushEventFilter",wrap_sceNpWebApiDeletePushEventFilter},
    {"sceNpWebApiRegisterPushEventCallback",np_signed_out}, {"sceNpWebApiUnregisterPushEventCallback",wrap_sceNpWebApiUnregisterPushEventCallback},
    {"sceNpWebApiUtilityParseNpId",np_signed_out},
    {"sceNpMatching2ContextStart",trace_sceNpMatching2ContextStart}, {"sceNpMatching2ContextStop",wrap_sceNpMatching2ContextStop},
    {"sceNpMatching2DestroyContext",wrap_sceNpMatching2DestroyContext},
    {"sceNpMatching2RegisterContextCallback",wrap_sceNpMatching2RegisterContextCallback}, {"sceNpMatching2RegisterLobbyEventCallback",wrap_sceNpMatching2RegisterLobbyEventCallback},
    {"sceNpMatching2RegisterRoomEventCallback",wrap_sceNpMatching2RegisterRoomEventCallback}, {"sceNpMatching2RegisterSignalingCallback",wrap_sceNpMatching2RegisterSignalingCallback},
    {"sceNpMatching2SetDefaultRequestOptParam",wrap_sceNpMatching2SetDefaultRequestOptParam},
    {"sceNpMatching2CreateJoinRoom",trace_sceNpMatching2CreateJoinRoom}, {"sceNpMatching2JoinRoom",trace_sceNpMatching2JoinRoom},
    {"sceNpMatching2LeaveRoom",trace_sceNpMatching2LeaveRoom}, {"sceNpMatching2SearchRoom",trace_sceNpMatching2SearchRoom},
    {"sceNpMatching2GetServerId",trace_sceNpMatching2GetServerId}, {"sceNpMatching2GetWorldInfoList",trace_sceNpMatching2GetWorldInfoList},
    {"sceNpMatching2GetLobbyInfoList",trace_sceNpMatching2GetLobbyInfoList}, {"sceNpMatching2JoinLobby",trace_sceNpMatching2JoinLobby},
    {"sceNpMatching2LeaveLobby",trace_sceNpMatching2LeaveLobby}, {"sceNpMatching2GrantRoomOwner",trace_sceNpMatching2GrantRoomOwner},
    {"sceNpMatching2KickoutRoomMember",trace_sceNpMatching2KickoutRoomMember}, {"sceNpMatching2SetRoomDataExternal",trace_sceNpMatching2SetRoomDataExternal},
    {"sceNpMatching2SetRoomDataInternal",trace_sceNpMatching2SetRoomDataInternal}, {"sceNpMatching2SetRoomMemberDataInternal",trace_sceNpMatching2SetRoomMemberDataInternal},
    {"sceNpMatching2SignalingGetConnectionStatus",trace_sceNpMatching2SignalingGetConnectionStatus}, {"sceNpMatching2SignalingGetPingInfo",trace_sceNpMatching2SignalingGetPingInfo},
    {"sceNpSignalingDeleteContext",wrap_sceNpSignalingDeleteContext}, {"sceNpSignalingActivateConnection",trace_sceNpSignalingActivateConnection},
    {"sceNpSignalingDeactivateConnection",wrap_sceNpSignalingDeactivateConnection}, {"sceNpSignalingGetConnectionStatus",trace_sceNpSignalingGetConnectionStatus},
    {"sceNpScoreCensorComment",np_signed_out}, {"sceNpScoreSanitizeComment",np_signed_out},
    {"sceNpScoreGetBoardInfo",np_signed_out}, {"sceNpScoreGetGameData",trace_sceNpScoreGetGameData},
    {"sceNpScoreGetRankingByNpIdPcId",np_signed_out}, {"sceNpScoreGetRankingByRange",trace_sceNpScoreGetRankingByRange},
    {"sceNpScoreRecordGameData",trace_sceNpScoreRecordGameData}, {"sceNpScoreRecordScore",trace_sceNpScoreRecordScore},
    {"sceNpScoreSetPlayerCharacterId",wrap_sceNpScoreSetPlayerCharacterId},
    {"sceVoiceCreatePort",voice_port}, {"sceVoiceDeletePort",ok_void},
    {"sceVoiceConnectIPortToOPort",ok_void}, {"sceVoiceDisconnectIPortFromOPort",ok_void},
    {"sceVoiceStart",ok_void}, {"sceVoiceStop",ok_void}, {"sceVoiceGetPortInfo",voice_info},
    {"sceVoiceReadFromOPort",voice_read}, {"sceVoiceWriteToIPort",voice_write},
    {"sceAudioInInput",audio_in_open}, {"sceAudioInClose",audio_in_open},
    {"sceNpMatching2Initialize",wrap_sceNpMatching2Initialize}, {"sceNpMatching2Terminate",wrap_sceNpMatching2Terminate},
    {"sceNpMatching2CreateContext",trace_sceNpMatching2CreateContext},
    {"sceNpSignalingInitialize",wrap_sceNpSignalingInitialize}, {"sceNpSignalingTerminate",wrap_sceNpSignalingTerminate},
    {"sceNpSignalingCreateContext",trace_sceNpSignalingCreateContext},
    {"sceCommonDialogInitialize",common_init},
    {"sceMsgDialogInitialize",msg_init}, {"sceMsgDialogOpen",msg_open},
    {"sceMsgDialogUpdateStatus",msg_status}, {"sceMsgDialogTerminate",msg_term},
    {"sceSaveDataDialogInitialize",save_init}, {"sceSaveDataDialogOpen",save_open},
    {"sceSaveDataDialogUpdateStatus",save_status}, {"sceSaveDataDialogTerminate",save_term},
    {"sceNpProfileDialogInitialize",profile_init}, {"sceNpProfileDialogOpen",profile_open},
    {"sceNpProfileDialogUpdateStatus",profile_status}, {"sceNpProfileDialogTerminate",profile_term},
    {"sceNpProfileDialogGetResult",profile_result},
    {"sceNpCommerceDialogInitialize",commerce_init}, {"sceNpCommerceDialogOpen",commerce_open},
    {"sceNpCommerceDialogUpdateStatus",commerce_status}, {"sceNpCommerceDialogTerminate",commerce_term},
    {"sceImeDialogInit",ime_init}, {"sceImeDialogGetStatus",ime_status}, {"sceImeDialogGetResult",ime_result},
    {"sceImeDialogTerm",ime_term}, {"sceImeDialogAbort",ime_term},
    {"sceNpTrophyCreateContext",trophy_context}, {"sceNpTrophyCreateHandle",trophy_handle},
    {"sceNpTrophyRegisterContext",trophy_register}, {"sceNpTrophyUnlockTrophy",trophy_unlock},
    {"sceNpTrophyGetGameInfo",trophy_game_info}, {"sceNpTrophyGetTrophyInfo",trophy_info},
    {"scePlayGoInitialize",playgo_init}, {"scePlayGoOpen",playgo_open}, {"scePlayGoGetChunkId",playgo_chunk_ids},
    {"scePlayGoGetLocus",playgo_locus}, {"scePlayGoSetInstallSpeed",playgo_speed},
    {"sceMouseInit",ok_void}, {"sceMouseOpen",mouse_open}, {"sceMouseRead",mouse_read}, {"sceMouseClose",mouse_close},
    {"sceAudioInOpen",audio_in_open},
    {"sceDiscMapIsRequestOnHDD",discmap_on_hdd}, {"sceDiscMap_8A828CAEE7EDD5E9",discmap_8a82},
    {"sceVoiceInit",ok_void}, {"sceVoiceEnd",ok_void},
};
uintptr_t runtime_services_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
