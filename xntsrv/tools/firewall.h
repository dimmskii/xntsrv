/*
 * The Windows Firewall rules XNTsrv needs.
 *
 * Server rule: the X server needs TCP for its own window manager and for the
 * XNTsrv services, all of which use 127.0.0.1.  The server, built from the
 * VcXsrv tree, still opens its port on every interface, so inbound
 * connections to it are blocked until it binds to loopback only (TODO.md,
 * item 4).  Loopback traffic is
 * not subject to the firewall and keeps working.
 *
 * TCP service rule: lets the XNTsrv TCP service, when someone starts it,
 * accept X connections (and audio, where it is turned on) from directly
 * attached networks only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <objbase.h>
#include <oleauto.h>
#include <netfw.h>

struct FirewallRule {
    const wchar_t *name;
    const wchar_t *program; /* file name inside the install folder */
    bool allow;             /* false: block all inbound to the program */
    const wchar_t *description;
};

static const FirewallRule kFirewallRules[] = {
    { L"XNTsrv server (block inbound)", L"xntsrv-server.exe", false,
      L"Keeps the XNTsrv X server reachable from this computer only. "
      L"Connections from WSL and other machines go through the XNTsrv services." },
    { L"XNTsrv TCP service (local subnet)", L"xntsrv-service.exe", true,
      L"Lets the XNTsrv TCP service accept X and audio connections from directly attached networks. "
      L"It has no effect unless that service is started." },
};

/* Remove every XNTsrv rule, then add them again for the programs in `dir`
 * unless that is empty. */
static inline HRESULT SetFirewallRules(const std::wstring &dir)
{
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INetFwPolicy2 *policy = NULL;
    INetFwRules *rules = NULL;

    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2), (void **)&policy);
    if (SUCCEEDED(hr))
        hr = policy->get_Rules(&rules);

    for (const FirewallRule &spec : kFirewallRules) {
        INetFwRule *rule = NULL;
        BSTR name = SysAllocString(spec.name);

        if (SUCCEEDED(hr))
            rules->Remove(name); /* succeeds whether or not the rule existed */
        if (SUCCEEDED(hr) && !dir.empty())
            hr = CoCreateInstance(__uuidof(NetFwRule), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwRule), (void **)&rule);
        if (SUCCEEDED(hr) && rule) {
            BSTR app = SysAllocString((dir + L"\\" + spec.program).c_str());
            BSTR text = SysAllocString(spec.description);
            rule->put_Name(name);
            rule->put_Description(text);
            rule->put_ApplicationName(app);
            rule->put_Direction(NET_FW_RULE_DIR_IN);
            rule->put_Profiles(NET_FW_PROFILE2_ALL);
            if (spec.allow) {
                std::wstring ports = std::to_wstring(xnt::kXBasePort) + L"," + std::to_wstring(xnt::kAudioPort);
                BSTR port = SysAllocString(ports.c_str());
                BSTR remote = SysAllocString(L"LocalSubnet");
                rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
                rule->put_LocalPorts(port);
                rule->put_RemoteAddresses(remote);
                rule->put_Action(NET_FW_ACTION_ALLOW);
                SysFreeString(port);
                SysFreeString(remote);
            } else {
                rule->put_Action(NET_FW_ACTION_BLOCK);
            }
            rule->put_Enabled(VARIANT_TRUE);
            hr = rules->Add(rule);
            SysFreeString(app);
            SysFreeString(text);
        }
        if (rule)
            rule->Release();
        SysFreeString(name);
    }

    if (rules)
        rules->Release();
    if (policy)
        policy->Release();
    if (SUCCEEDED(init))
        CoUninitialize();
    return hr;
}
