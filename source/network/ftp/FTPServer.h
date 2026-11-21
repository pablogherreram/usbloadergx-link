#ifndef GX_LINK_FTP_SERVER_H
#define GX_LINK_FTP_SERVER_H

namespace FTPServer {
    enum Status { Disabled, Starting, Online, Error };
    bool Start();
    void Stop();
    void Restart();
    Status GetStatus();
    const char *GetStatusText();
}

#endif
