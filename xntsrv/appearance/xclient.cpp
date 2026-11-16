/*
 * See xclient.h.  Requests are built by hand in the server's byte order,
 * which is declared little-endian at connection time.
 *
 * SPDX-License-Identifier: MIT
 */
#include "xclient.h"

#include <cstring>

#ifdef _WIN32
static void CloseSocket(XSocket s) { closesocket(s); }
#else
#include <sys/socket.h>
#include <unistd.h>
static void CloseSocket(XSocket s) { close(s); }
#endif

namespace {

void Put16(std::vector<uint8_t> *out, uint16_t v)
{
    out->push_back((uint8_t)(v & 0xff));
    out->push_back((uint8_t)(v >> 8));
}

void Put32(std::vector<uint8_t> *out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out->push_back((uint8_t)(v >> (8 * i)));
}

void PutPadded(std::vector<uint8_t> *out, const std::string &bytes)
{
    out->insert(out->end(), bytes.begin(), bytes.end());
    while (out->size() % 4)
        out->push_back(0);
}

uint16_t Get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t Get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

size_t Pad4(size_t n) { return (n + 3) & ~(size_t)3; }

/* Fill in a request's length field (in 4-byte units) once it is complete. */
void Finish(std::vector<uint8_t> *request)
{
    uint16_t units = (uint16_t)(request->size() / 4);
    (*request)[2] = (uint8_t)(units & 0xff);
    (*request)[3] = (uint8_t)(units >> 8);
}

const uint8_t kSelectionClear = 29;

} /* namespace */

XClient::~XClient() { Close(); }

void XClient::Close()
{
    if (socket_ != kNoSocket)
        CloseSocket(socket_);
    socket_ = kNoSocket;
}

bool XClient::Send(const std::vector<uint8_t> &request)
{
    size_t done = 0;
    while (socket_ != kNoSocket && done < request.size()) {
        int n = (int)send(socket_, (const char *)request.data() + done, (int)(request.size() - done), 0);
        if (n <= 0) {
            error_ = "the connection to the X server was lost";
            Close();
            return false;
        }
        done += (size_t)n;
    }
    return socket_ != kNoSocket;
}

bool XClient::ReadExact(uint8_t *buffer, size_t length)
{
    size_t done = 0;
    while (socket_ != kNoSocket && done < length) {
        int n = (int)recv(socket_, (char *)buffer + done, (int)(length - done), 0);
        if (n <= 0) {
            error_ = "the connection to the X server was lost";
            Close();
            return false;
        }
        done += (size_t)n;
    }
    return socket_ != kNoSocket;
}

bool XClient::Open(XSocket connected, const std::string &cookie)
{
    socket_ = connected;
    const std::string authName = cookie.empty() ? "" : "MIT-MAGIC-COOKIE-1";

    std::vector<uint8_t> setup;
    setup.push_back('l'); /* least significant byte first */
    setup.push_back(0);
    Put16(&setup, 11); /* protocol version 11.0 */
    Put16(&setup, 0);
    Put16(&setup, (uint16_t)authName.size());
    Put16(&setup, (uint16_t)cookie.size());
    Put16(&setup, 0);
    PutPadded(&setup, authName);
    PutPadded(&setup, cookie);
    if (!Send(setup))
        return false;

    uint8_t head[8];
    if (!ReadExact(head, sizeof head))
        return false;
    std::vector<uint8_t> body((size_t)Get16(head + 6) * 4);
    if (!body.empty() && !ReadExact(body.data(), body.size()))
        return false;
    if (head[0] != 1) {
        size_t n = head[1] < body.size() ? head[1] : body.size();
        error_ = "the X server refused the connection: " + std::string((const char *)body.data(), n);
        Close();
        return false;
    }

    /* Fixed part, then the vendor string, the pixmap formats, and the screens. */
    if (body.size() < 32) {
        error_ = "the X server sent a malformed greeting";
        Close();
        return false;
    }
    idBase_ = Get32(body.data() + 4);
    idMask_ = Get32(body.data() + 8);
    size_t vendorLength = Get16(body.data() + 16);
    size_t formats = body[21];
    size_t screen = 32 + Pad4(vendorLength) + 8 * formats;
    if (body[20] < 1 || body.size() < screen + 24) {
        error_ = "the X server has no screen";
        Close();
        return false;
    }
    root_ = Get32(body.data() + screen);
    rootWidth_ = Get16(body.data() + screen + 20);
    rootHeight_ = Get16(body.data() + screen + 22);
    return true;
}

void XClient::HandleEvent(const uint8_t *event)
{
    /* Bit 7 marks an event sent by another client; the code is the rest. */
    if ((event[0] & 0x7f) == kSelectionClear && Get32(event + 8) == watchedOwner_ && watchedOwner_)
        lostSelection_ = true;
}

bool XClient::ReadReply(std::vector<uint8_t> *reply)
{
    for (;;) {
        uint8_t packet[32];
        if (!ReadExact(packet, sizeof packet))
            return false;
        if (packet[0] == 1) { /* a reply; it may carry more data */
            reply->assign(packet, packet + 32);
            size_t extra = (size_t)Get32(packet + 4) * 4;
            if (extra) {
                reply->resize(32 + extra);
                if (!ReadExact(reply->data() + 32, extra))
                    return false;
            }
            if (Get16(packet + 2) == sequence_)
                return true;
        } else if (packet[0] == 0) { /* an error */
            if (Get16(packet + 2) == sequence_) {
                error_ = "the X server rejected a request (error " + std::to_string(packet[1]) + ")";
                reply->clear();
                return false;
            }
        } else {
            HandleEvent(packet);
        }
    }
}

XClient::Atom XClient::InternAtom(const std::string &name)
{
    std::vector<uint8_t> r = { 16, 0, 0, 0 };
    Put16(&r, (uint16_t)name.size());
    Put16(&r, 0);
    PutPadded(&r, name);
    Finish(&r);
    sequence_++;
    std::vector<uint8_t> reply;
    if (!Send(r) || !ReadReply(&reply))
        return 0;
    return Get32(reply.data() + 8);
}

XClient::Window XClient::GetSelectionOwner(Atom selection)
{
    std::vector<uint8_t> r = { 23, 0, 0, 0 };
    Put32(&r, selection);
    Finish(&r);
    sequence_++;
    std::vector<uint8_t> reply;
    if (!Send(r) || !ReadReply(&reply))
        return 0;
    return Get32(reply.data() + 8);
}

XClient::Window XClient::CreateHiddenWindow()
{
    Window id = idBase_ | (nextId_++ & idMask_);
    std::vector<uint8_t> r = { 1, 0, 0, 0 }; /* depth 0: same as the parent */
    Put32(&r, id);
    Put32(&r, root_);
    Put16(&r, (uint16_t)-10); /* off screen, and never mapped anyway */
    Put16(&r, (uint16_t)-10);
    Put16(&r, 1);
    Put16(&r, 1);
    Put16(&r, 0);
    Put16(&r, 2); /* InputOnly */
    Put32(&r, 0); /* visual: same as the parent */
    Put32(&r, 0); /* no attributes */
    Finish(&r);
    sequence_++;
    Send(r);
    return id;
}

void XClient::SetSelectionOwner(Atom selection, Window owner)
{
    std::vector<uint8_t> r = { 22, 0, 0, 0 };
    Put32(&r, owner);
    Put32(&r, selection);
    Put32(&r, 0); /* CurrentTime */
    Finish(&r);
    sequence_++;
    Send(r);
}

void XClient::ChangeProperty(Window window, Atom property, Atom type, const std::string &bytes)
{
    std::vector<uint8_t> r = { 18, 0 /* replace */, 0, 0 };
    Put32(&r, window);
    Put32(&r, property);
    Put32(&r, type);
    r.push_back(8); /* the data is bytes */
    r.push_back(0);
    r.push_back(0);
    r.push_back(0);
    Put32(&r, (uint32_t)bytes.size());
    PutPadded(&r, bytes);
    Finish(&r);
    sequence_++;
    Send(r);
}

std::string XClient::GetProperty(Window window, Atom property)
{
    std::vector<uint8_t> r = { 20, 0 /* do not delete */, 0, 0 };
    Put32(&r, window);
    Put32(&r, property);
    Put32(&r, 0);       /* any type */
    Put32(&r, 0);       /* from the start */
    Put32(&r, 1 << 20); /* up to 4 MB */
    Finish(&r);
    sequence_++;
    std::vector<uint8_t> reply;
    if (!Send(r) || !ReadReply(&reply) || reply[1] != 8)
        return "";
    size_t length = Get32(reply.data() + 16);
    if (reply.size() < 32 + length)
        return "";
    return std::string((const char *)reply.data() + 32, length);
}

void XClient::AnnounceManager(Atom manager, Atom selection, Window owner)
{
    std::vector<uint8_t> r = { 25, 0 /* do not propagate */, 0, 0 };
    Put32(&r, root_);
    Put32(&r, 1u << 17); /* to clients that asked for StructureNotify on the root */
    /* The event itself: a 32-byte ClientMessage. */
    r.push_back(33);
    r.push_back(32);
    Put16(&r, 0);
    Put32(&r, root_);
    Put32(&r, manager);
    Put32(&r, 0); /* timestamp: CurrentTime */
    Put32(&r, selection);
    Put32(&r, owner);
    Put32(&r, 0);
    Put32(&r, 0);
    Finish(&r);
    sequence_++;
    Send(r);
}

bool XClient::Sync()
{
    /* Any request with a reply forces everything before it to be processed;
     * asking who owns "nothing" is the cheapest. */
    std::vector<uint8_t> r = { 23, 0, 0, 0 };
    Put32(&r, 1); /* the PRIMARY atom */
    Finish(&r);
    sequence_++;
    std::vector<uint8_t> reply;
    return Send(r) && ReadReply(&reply);
}

bool XClient::ReadEvents(Window owner, bool *lostSelection)
{
    watchedOwner_ = owner;
    /* A round trip picks up everything queued before it, without blocking on
     * a connection that has nothing to say. */
    bool alive = Sync();
    *lostSelection = lostSelection_;
    return alive;
}

/* ---- XSETTINGS --------------------------------------------------------- */

void XSettings::SetInt(const std::string &name, int32_t value)
{
    entries_.push_back({ name, false, value, "" });
}

void XSettings::SetString(const std::string &name, const std::string &value)
{
    entries_.push_back({ name, true, 0, value });
}

std::string XSettings::Encode(uint32_t serial) const
{
    std::vector<uint8_t> out;
    out.push_back(0); /* least significant byte first */
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    Put32(&out, serial);
    Put32(&out, (uint32_t)entries_.size());
    for (const Entry &e : entries_) {
        out.push_back(e.isString ? 1 : 0);
        out.push_back(0);
        Put16(&out, (uint16_t)e.name.size());
        PutPadded(&out, e.name);
        Put32(&out, serial); /* when this setting last changed */
        if (e.isString) {
            Put32(&out, (uint32_t)e.text.size());
            PutPadded(&out, e.text);
        } else {
            Put32(&out, (uint32_t)e.number);
        }
    }
    return std::string(out.begin(), out.end());
}

/* ---- X resources ------------------------------------------------------- */

std::string MergeResources(const std::string &existing,
                           const std::vector<std::pair<std::string, std::string>> &values)
{
    std::string out;
    size_t start = 0;

    /* Keep every line that does not set one of our names. */
    while (start < existing.size()) {
        size_t end = existing.find('\n', start);
        if (end == std::string::npos)
            end = existing.size();
        std::string line = existing.substr(start, end - start);
        start = end + 1;

        size_t colon = line.find(':');
        std::string name = line.substr(0, colon == std::string::npos ? 0 : colon);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        bool ours = false;
        for (const auto &value : values)
            ours = ours || name == value.first;
        if (!ours && !line.empty())
            out += line + "\n";
    }
    for (const auto &value : values)
        out += value.first + ":\t" + value.second + "\n";
    return out;
}

/* ---- Xauthority -------------------------------------------------------- */

std::string CookieFromXauthority(const std::string &file)
{
    /* Each entry: a 2-byte family, then four counted fields (address, display
     * number, protocol name, data), each with a 2-byte big-endian length. */
    size_t at = 0;
    while (at + 2 <= file.size()) {
        at += 2;
        std::string field[4];
        for (int i = 0; i < 4; i++) {
            if (at + 2 > file.size())
                return "";
            size_t length = ((size_t)(uint8_t)file[at] << 8) | (uint8_t)file[at + 1];
            at += 2;
            if (at + length > file.size())
                return "";
            field[i] = file.substr(at, length);
            at += length;
        }
        if (field[2] == "MIT-MAGIC-COOKIE-1")
            return field[3];
    }
    return "";
}
