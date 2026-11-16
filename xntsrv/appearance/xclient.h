/*
 * A very small X11 client: just enough of the core protocol to publish
 * settings.  It speaks the protocol directly over a socket, so the helper
 * needs no X library.  Portable, so that it can be tested from a Linux guest.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET XSocket;
const XSocket kNoSocket = INVALID_SOCKET;
#else
typedef int XSocket;
const XSocket kNoSocket = -1;
#endif

class XClient {
public:
    typedef uint32_t Atom;
    typedef uint32_t Window;

    ~XClient();

    /* Take over a connected socket and do the X connection handshake with a
     * MIT-MAGIC-COOKIE-1 cookie (empty for none).  On failure, Error() says why. */
    bool Open(XSocket connected, const std::string &cookie);
    void Close();
    bool IsOpen() const { return socket_ != kNoSocket; }
    XSocket Socket() const { return socket_; }
    const std::string &Error() const { return error_; }

    Window Root() const { return root_; }
    unsigned RootWidth() const { return rootWidth_; }
    unsigned RootHeight() const { return rootHeight_; }

    Atom InternAtom(const std::string &name);
    Window GetSelectionOwner(Atom selection);
    /* An invisible window, to own a selection and carry a property. */
    Window CreateHiddenWindow();
    void SetSelectionOwner(Atom selection, Window owner);
    void ChangeProperty(Window window, Atom property, Atom type, const std::string &bytes);
    /* The property's bytes; empty when it is not set. */
    std::string GetProperty(Window window, Atom property);
    /* Announce to everyone watching the root window that `owner` now manages `selection`. */
    void AnnounceManager(Atom manager, Atom selection, Window owner);

    /* Send everything, wait for the server to have processed it. */
    bool Sync();

    /* Read whatever the server has sent.  Returns false when the connection
     * has closed.  *lostSelection is set if `owner` was told it no longer
     * owns a selection (another settings manager took over). */
    bool ReadEvents(Window owner, bool *lostSelection);

private:
    bool Send(const std::vector<uint8_t> &request);
    bool ReadExact(uint8_t *buffer, size_t length);
    /* Read up to and including the reply to the last request sent. */
    bool ReadReply(std::vector<uint8_t> *reply);
    void HandleEvent(const uint8_t *event);

    XSocket socket_ = kNoSocket;
    std::string error_;
    Window root_ = 0;
    unsigned rootWidth_ = 0, rootHeight_ = 0;
    uint32_t idBase_ = 0, idMask_ = 0, nextId_ = 0;
    uint16_t sequence_ = 0;
    Window watchedOwner_ = 0;
    bool lostSelection_ = false;
};

/* Build the XSETTINGS property: a list of named integer and string settings. */
class XSettings {
public:
    void SetInt(const std::string &name, int32_t value);
    void SetString(const std::string &name, const std::string &value);
    /* The encoded property.  `serial` must grow each time the contents change. */
    std::string Encode(uint32_t serial) const;

private:
    struct Entry {
        std::string name;
        bool isString;
        int32_t number;
        std::string text;
    };
    std::vector<Entry> entries_;
};

/* Return `existing` X resources (lines of "name: value") with the given names
 * set to the given values and everything else left as it was. */
std::string MergeResources(const std::string &existing,
                           const std::vector<std::pair<std::string, std::string>> &values);

/* The first MIT-MAGIC-COOKIE-1 cookie in the contents of an Xauthority file. */
std::string CookieFromXauthority(const std::string &fileContents);
