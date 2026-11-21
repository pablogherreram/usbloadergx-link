/*
 * USB Loader GX Link SD-only FTP service.
 * FTP protocol handling adapted conceptually from ftpii by Joseph Jordan.
 * This implementation is substantially rewritten for embedding, one client,
 * PASV-only operation, mandatory authentication and a fixed sd:/ root.
 * It deliberately contains no NAND, ISFS, USB, DVD or executable-loader API.
 */
#include <gccore.h>
#include <network.h>
#include <ogc/lwp.h>

#include "FTPServer.h"
#include "FTPPath.h"
#include "network/networkops.h"
#include "settings/CSettings.h"
#include "gecko.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
static lwp_t g_thread = LWP_THREAD_NULL;
static volatile bool g_stop = false;
static volatile FTPServer::Status g_status = FTPServer::Disabled;
static s32 g_listener = -1;
static s32 g_control = -1;
static s32 g_passive = -1;
static const u32 kStackSize = 64 * 1024;
static u8 g_stack[kStackSize] ATTRIBUTE_ALIGN(32);

struct Session {
    bool userOk, loggedIn;
    char cwd[768];
    char renameFrom[1024];
    s32 pasv;
    Session() : userOk(false), loggedIn(false), pasv(-1) { strcpy(cwd, "/"); renameFrom[0] = 0; }
};

// libogc forwards Wii socket I/O to IOS. Keep each IPC transfer at the
// documented 32 KiB ceiling and use aligned MEM1 buffers.
static const size_t kTransferBufferSize = 32 * 1024;
static const size_t kSocketChunkSize = 32 * 1024;
static const int kSocketBufferSize = 32 * 1024;
static const u16 kPassivePortFirst = 2122;
static const u16 kPassivePortLast = 2131;
static u16 g_nextPassivePort = kPassivePortFirst;
static u8 g_transferBuffer[kTransferBufferSize] ATTRIBUTE_ALIGN(32);
static u8 g_fileBuffer[64 * 1024] ATTRIBUTE_ALIGN(32);

static void closeSocket(s32 &s) {
    if (s >= 0) {
        net_shutdown(s, 2);
        net_close(s);
        s = -1;
    }
}

static void tuneDataSocket(s32 s) {
    if (s < 0) return;
    int bufferSize = kSocketBufferSize;
    net_setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));
    net_setsockopt(s, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
}

static bool sendAll(s32 s, const void *data, size_t len) {
    const u8 *p = static_cast<const u8 *>(data);
    while (len && !g_stop) {
        size_t request = len > kSocketChunkSize ? kSocketChunkSize : len;
        s32 n = net_send(s, p, request, 0);
        if (n > 0) {
            p += n;
            len -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            usleep(1000);
            continue;
        }
        return false;
    }
    return len == 0;
}
static bool reply(s32 s, int code, const char *text) {
    char line[640]; int n = snprintf(line, sizeof(line), "%d %s\r\n", code, text ? text : "");
    return n > 0 && sendAll(s, line, (size_t)n);
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    char *e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e; *e = 0; return s;
}
static void upper(char *s) { while (*s) { if (*s >= 'a' && *s <= 'z') *s -= 32; ++s; } }
static bool resolve(Session &ss, const char *arg, char *physical, size_t psize, char *virt = 0, size_t vsize = 0) {
    return FTPPath::Resolve(ss.cwd, arg, physical, psize, virt, vsize);
}
static bool isPhysicalRoot(const char *path) { return path && !strcmp(path, "sd:/"); }
static bool requireLogin(Session &ss, s32 c) { if (ss.loggedIn) return true; reply(c, 530, "Please login with USER and PASS."); return false; }
static bool requireWrite(Session &ss, s32 c) { if (!requireLogin(ss, c)) return false; if (Settings.FTPWriteAccess) return true; reply(c, 550, "Write access is disabled in USB Loader GX Link."); return false; }

static s32 openPassive(s32 c, Session &ss, bool extended) {
    if (ss.pasv >= 0) {
        closeSocket(ss.pasv);
        g_passive = -1;
    }

    s32 listener = -1;
    u16 selectedPort = 0;
    const unsigned portCount = kPassivePortLast - kPassivePortFirst + 1;

    // Rotate through a small passive range. This avoids immediately reusing a
    // TCP endpoint that IOS may still hold in TIME_WAIT after the prior RETR,
    // while SO_REUSEADDR handles ordinary quick reconnects.
    for (unsigned attempt = 0; attempt < portCount; ++attempt) {
        u16 port = g_nextPassivePort;
        ++g_nextPassivePort;
        if (g_nextPassivePort > kPassivePortLast)
            g_nextPassivePort = kPassivePortFirst;

        s32 candidate = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (candidate < 0)
            continue;

        int reuse = 1;
        net_setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (net_bind(candidate, (sockaddr *)&address, sizeof(address)) == 0 &&
            net_listen(candidate, 1) == 0) {
            listener = candidate;
            selectedPort = port;
            break;
        }
        net_close(candidate);
    }

    if (listener < 0) {
        reply(c, 425, "Cannot enter passive mode.");
        return -1;
    }

    ss.pasv = listener;
    g_passive = listener;

    char msg[128];
    if (extended) {
        snprintf(msg, sizeof(msg), "Entering Extended Passive Mode (|||%u|).",
                 (unsigned)selectedPort);
        reply(c, 229, msg);
    } else {
        unsigned ip1 = 127, ip2 = 0, ip3 = 0, ip4 = 1;
        sscanf(GetNetworkIP(), "%u.%u.%u.%u", &ip1, &ip2, &ip3, &ip4);
        snprintf(msg, sizeof(msg),
                 "Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
                 ip1, ip2, ip3, ip4, selectedPort / 256, selectedPort % 256);
        reply(c, 227, msg);
    }
    return listener;
}

static s32 acceptData(Session &ss, s32 c) {
    if (ss.pasv < 0) {
        reply(c, 425, "Use PASV or EPSV first.");
        return -1;
    }
    sockaddr_in a;
    socklen_t len = sizeof(a);
    s32 d = net_accept(ss.pasv, (sockaddr*)&a, &len);
    closeSocket(ss.pasv);
    g_passive = -1;
    if (d >= 0) tuneDataSocket(d);
    return d;
}
static void formatListLine(char *out, size_t size, const char *name, const struct stat &st) {
    char date[32]="Jan 01  1980"; struct tm *tmv=localtime(&st.st_mtime); if(tmv) strftime(date,sizeof(date),"%b %d %H:%M",tmv);
    snprintf(out,size,"%crw-r--r-- 1 wii wii %10lu %s %s\r\n", S_ISDIR(st.st_mode)?'d':'-', (unsigned long)st.st_size,date,name);
}
static bool listPath(Session &ss, s32 c, const char *arg, bool namesOnly) {
    char path[1024]; if(!resolve(ss,arg&&*arg?arg:".",path,sizeof(path))) { reply(c,550,"Invalid path."); return false; }
    DIR *dir=opendir(path); if(!dir) { reply(c,550,"Directory unavailable."); return false; }
    reply(c,150,"Opening passive data connection for directory list."); s32 d=acceptData(ss,c); if(d<0){closedir(dir);return false;}
    struct dirent *e; char line[1400]; while((e=readdir(dir)) && !g_stop){ if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue; if(namesOnly) snprintf(line,sizeof(line),"%s\r\n",e->d_name); else { char child[1200]; snprintf(child,sizeof(child),"%s%s%s",path,(path[strlen(path)-1]=='/'?"":"/"),e->d_name); struct stat st; if(stat(child,&st)!=0)continue; formatListLine(line,sizeof(line),e->d_name,st);} if(!sendAll(d,line,strlen(line)))break; }
    closedir(dir); closeSocket(d); reply(c,226,"Transfer complete."); return true;
}
static bool transferFile(Session &ss,s32 c,const char *arg,bool upload,bool append){
    char path[1024]; if(!resolve(ss,arg,path,sizeof(path)) || (upload && isPhysicalRoot(path))){reply(c,550,"Invalid path.");return false;}
    FILE *f=fopen(path,upload?(append?"ab":"wb"):"rb"); if(!f){reply(c,550,"Cannot open file.");return false;}
    setvbuf(f, (char *)g_fileBuffer, _IOFBF, sizeof(g_fileBuffer));
    reply(c,150,"Opening passive data connection.");
    s32 d=acceptData(ss,c);
    if(d<0){fclose(f);return false;}
    u8 *buf = g_transferBuffer;
    bool ok=true;
    if(upload){
        while(!g_stop){
            s32 n=net_recv(d,buf,kTransferBufferSize,0);
            if(n==0) break;
            if(n<0){
                if(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR){usleep(1000);continue;}
                ok=false;
                break;
            }
            if(fwrite(buf,1,(size_t)n,f)!=(size_t)n){ok=false;break;}
        }
        if(fflush(f)!=0) ok=false;
    }
    else {
        size_t n;
        while(!g_stop && (n=fread(buf,1,kTransferBufferSize,f))>0){
            if(!sendAll(d,buf,n)){ok=false;break;}
        }
        if(ferror(f)) ok=false;
    }
    fclose(f); closeSocket(d); reply(c,ok?226:426,ok?"Transfer complete.":"Transfer aborted."); return ok;
}
static void handleClient(s32 c) {
    Session ss; reply(c,220,"USB Loader GX Link SD-only FTP ready."); char input[1024]; size_t used=0;
    while(!g_stop){ s32 n=net_recv(c,input+used,sizeof(input)-used-1,0); if(n<=0)break; used+=n; input[used]=0; char *line;
        while((line=strstr(input,"\n"))){ *line=0; char commandLine[1024]; strncpy(commandLine,input,sizeof(commandLine)-1);commandLine[sizeof(commandLine)-1]=0; size_t consumed=(line-input)+1; memmove(input,input+consumed,used-consumed);used-=consumed;input[used]=0;
            char *arg=strchr(commandLine,' '); if(arg){*arg++=0;arg=trim(arg);} else arg=(char*)""; trim(commandLine); upper(commandLine);
            if(!strcmp(commandLine,"USER")){ss.userOk=!strcmp(arg,"wii");ss.loggedIn=false;reply(c,ss.userOk?331:530,ss.userOk?"Password required.":"Unknown user.");}
            else if(!strcmp(commandLine,"PASS")){ss.loggedIn=ss.userOk&&Settings.FTPPassword[0]&&!strcmp(arg,Settings.FTPPassword);reply(c,ss.loggedIn?230:530,ss.loggedIn?"Logged in.":"Login incorrect.");}
            else if(!strcmp(commandLine,"QUIT")){reply(c,221,"Goodbye.");return;}
            else if(!strcmp(commandLine,"NOOP"))reply(c,200,"OK.");
            else if(!strcmp(commandLine,"SYST"))reply(c,215,"UNIX Type: L8");
            else if(!strcmp(commandLine,"FEAT")){const char *features="211-Features\r\n EPSV\r\n PASV\r\n SIZE\r\n UTF8\r\n211 End\r\n";sendAll(c,features,strlen(features));}
            else if(!strcmp(commandLine,"OPTS"))reply(c,200,"UTF8 enabled.");
            else if(!strcmp(commandLine,"TYPE")){if(requireLogin(ss,c))reply(c,200,"Type set.");}
            else if(!strcmp(commandLine,"PWD")){if(requireLogin(ss,c)){char m[900];snprintf(m,sizeof(m),"\"%s\" is current directory.",ss.cwd);reply(c,257,m);}}
            else if(!strcmp(commandLine,"CWD")||!strcmp(commandLine,"CDUP")){if(requireLogin(ss,c)){char p[1024],v[768];const char *target=!strcmp(commandLine,"CDUP")?"..":arg;struct stat st;if(resolve(ss,target,p,sizeof(p),v,sizeof(v))&&stat(p,&st)==0&&S_ISDIR(st.st_mode)){strcpy(ss.cwd,v);reply(c,250,"Directory changed.");}else reply(c,550,"Directory unavailable.");}}
            else if(!strcmp(commandLine,"EPSV")){if(requireLogin(ss,c))openPassive(c,ss,true);}
            else if(!strcmp(commandLine,"PASV")){if(requireLogin(ss,c))openPassive(c,ss,false);}
            else if(!strcmp(commandLine,"LIST")){if(requireLogin(ss,c))listPath(ss,c,arg,false);}
            else if(!strcmp(commandLine,"NLST")){if(requireLogin(ss,c))listPath(ss,c,arg,true);}
            else if(!strcmp(commandLine,"SIZE")){if(requireLogin(ss,c)){char p[1024],m[64];struct stat st;if(resolve(ss,arg,p,sizeof(p))&&stat(p,&st)==0&&S_ISREG(st.st_mode)){snprintf(m,sizeof(m),"%lu",(unsigned long)st.st_size);reply(c,213,m);}else reply(c,550,"File unavailable.");}}
            else if(!strcmp(commandLine,"RETR")){if(requireLogin(ss,c))transferFile(ss,c,arg,false,false);}
            else if(!strcmp(commandLine,"STOR")||!strcmp(commandLine,"APPE")){if(requireWrite(ss,c))transferFile(ss,c,arg,true,!strcmp(commandLine,"APPE"));}
            else if(!strcmp(commandLine,"DELE")){if(requireWrite(ss,c)){char p[1024];reply(c,resolve(ss,arg,p,sizeof(p))&&!isPhysicalRoot(p)&&remove(p)==0?250:550,"Delete operation completed.");}}
            else if(!strcmp(commandLine,"MKD")){if(requireWrite(ss,c)){char p[1024],v[768],m[900];if(resolve(ss,arg,p,sizeof(p),v,sizeof(v))&&!isPhysicalRoot(p)&&mkdir(p,0777)==0){snprintf(m,sizeof(m),"\"%s\" created.",v);reply(c,257,m);}else reply(c,550,"Create directory failed.");}}
            else if(!strcmp(commandLine,"RMD")){if(requireWrite(ss,c)){char p[1024];reply(c,resolve(ss,arg,p,sizeof(p))&&!isPhysicalRoot(p)&&rmdir(p)==0?250:550,"Remove directory operation completed.");}}
            else if(!strcmp(commandLine,"RNFR")){if(requireWrite(ss,c)){char p[1024];struct stat st;if(resolve(ss,arg,p,sizeof(p))&&!isPhysicalRoot(p)&&stat(p,&st)==0){strcpy(ss.renameFrom,p);reply(c,350,"Ready for RNTO.");}else reply(c,550,"Source unavailable.");}}
            else if(!strcmp(commandLine,"RNTO")){if(requireWrite(ss,c)){char p[1024];if(ss.renameFrom[0]&&resolve(ss,arg,p,sizeof(p))&&!isPhysicalRoot(p)&&rename(ss.renameFrom,p)==0)reply(c,250,"Rename completed.");else reply(c,550,"Rename failed.");ss.renameFrom[0]=0;}}
            else if(!strcmp(commandLine,"SITE")||!strcmp(commandLine,"PORT")||!strcmp(commandLine,"EPRT"))reply(c,502,"Command disabled by SD-only security policy.");
            else reply(c,502,"Command not implemented.");
        }
        if(used==sizeof(input)-1){reply(c,500,"Command too long.");used=0;}
    }
}
static void *serverThread(void *) {
    g_status=FTPServer::Starting; Initialize_Network(5); if(g_stop){g_status=FTPServer::Disabled;return 0;} if(!IsNetworkInit()){g_status=FTPServer::Error;return 0;}
    s32 s=net_socket(AF_INET,SOCK_STREAM,IPPROTO_IP); if(s<0){g_status=FTPServer::Error;return 0;} g_listener=s; int one=1; net_setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)); sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons((u16)Settings.FTPPort);
    if(net_bind(s,(sockaddr*)&a,sizeof(a))<0||net_listen(s,1)<0){closeSocket(g_listener);g_status=FTPServer::Error;return 0;} g_status=FTPServer::Online; gprintf("FTP: SD-only server online at %s:%d\n",GetNetworkIP(),Settings.FTPPort);
    while(!g_stop){sockaddr_in ca;socklen_t len=sizeof(ca);s32 c=net_accept(s,(sockaddr*)&ca,&len);if(c<0){if(g_stop)break;usleep(100000);continue;}g_control=c;handleClient(c);closeSocket(g_control);} closeSocket(g_listener);g_status=FTPServer::Disabled;return 0;
}
}

bool FTPServer::Start(){if(!Settings.FTPEnabled||!Settings.FTPPassword[0])return false;if(g_thread!=LWP_THREAD_NULL)return true;g_stop=false;g_status=Starting;if(LWP_CreateThread(&g_thread,serverThread,0,g_stack,kStackSize,40)!=0){g_thread=LWP_THREAD_NULL;g_status=Error;return false;}return true;}
void FTPServer::Stop(){g_stop=true;closeSocket(g_passive);closeSocket(g_control);closeSocket(g_listener);if(g_thread!=LWP_THREAD_NULL){LWP_JoinThread(g_thread,0);g_thread=LWP_THREAD_NULL;}g_status=Disabled;}
void FTPServer::Restart(){Stop();if(Settings.FTPEnabled)Start();}
FTPServer::Status FTPServer::GetStatus(){return g_status;}
const char *FTPServer::GetStatusText(){switch(g_status){case Starting:return "Starting";case Online:return "Online";case Error:return "Error";default:return "Disabled";}}
