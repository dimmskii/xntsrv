/*
 * The Windows Firewall rule that keeps the X server off the network.
 *
 * The server needs TCP for its own window manager and for the XNTSrv services,
 * all of which use 127.0.0.1.  Until XNTSrv builds a server that binds to
 * loopback only, the stock one opens its port on every interface, so inbound
 * connections to it are blocked here.  Loopback traffic is not subject to the
 * firewall and keeps working.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <objbase.h>
#include <oleauto.h>
#include <netfw.h>

/* Remove the rule, then add it again for `program` unless that is empty. */
static inline HRESULT SetFirewallRule(const std::wstring &program)
{
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INetFwPolicy2 *policy = NULL;
    INetFwRules *rules = NULL;
    INetFwRule *rule = NULL;
    BSTR name = SysAllocString(xnt::kFirewallRule);

    HRESULT hr = CoCreateInstance(__uuidof(NetFwPolicy2), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2), (void **)&policy);
    if (SUCCEEDED(hr))
        hr = policy->get_Rules(&rules);
    if (SUCCEEDED(hr))
        rules->Remove(name); /* succeeds whether or not the rule existed */

    if (SUCCEEDED(hr) && !program.empty()) {
        hr = CoCreateInstance(__uuidof(NetFwRule), NULL, CLSCTX_INPROC_SERVER,
                              __uuidof(INetFwRule), (void **)&rule);
        if (SUCCEEDED(hr)) {
            BSTR app = SysAllocString(program.c_str());
            BSTR text = SysAllocString(L"Keeps the XNTSrv X server reachable from this computer only. "
                                       L"Connections from WSL and other machines go through the XNTSrv services.");
            rule->put_Name(name);
            rule->put_Description(text);
            rule->put_ApplicationName(app);
            rule->put_Direction(NET_FW_RULE_DIR_IN);
            rule->put_Action(NET_FW_ACTION_BLOCK);
            rule->put_Profiles(NET_FW_PROFILE2_ALL);
            rule->put_Enabled(VARIANT_TRUE);
            hr = rules->Add(rule);
            SysFreeString(app);
            SysFreeString(text);
        }
    }

    if (rule)
        rule->Release();
    if (rules)
        rules->Release();
    if (policy)
        policy->Release();
    SysFreeString(name);
    if (SUCCEEDED(init))
        CoUninitialize();
    return hr;
}
