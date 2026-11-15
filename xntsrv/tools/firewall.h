/*
 * The Windows Firewall rules XNTSrv needs.
 *
 * Server rule: the X server needs TCP for its own window manager and for the
 * XNTSrv services, all of which use 127.0.0.1.  Until XNTSrv builds a server
 * that binds to loopback only, the stock one opens its port on every
 * interface, so inbound connections to it are blocked.  Loopback traffic is
 * not subject to the firewall and keeps working.
 *
 * TCP service rule: lets the XNTSrv TCP service, when someone starts it,
 * accept X connections from directly attached networks only.
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
    { L"XNTSrv server (block inbound)", L"xntsrv-server.exe", false,
      L"Keeps the XNTSrv X server reachable from this computer only. "
      L"Connections from WSL and other machines go through the XNTSrv services." },
    { L"XNTSrv TCP service (local subnet)", L"xntsrv-service.exe", true,
      L"Lets the XNTSrv TCP service accept X connections from directly attached networks. "
      L"It has no effect unless that service is started." },
};

/* Remove every XNTSrv rule, then add them again for the programs in `dir`
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
                BSTR port = SysAllocString(std::to_wstring(xnt::kXBasePort).c_str());
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
