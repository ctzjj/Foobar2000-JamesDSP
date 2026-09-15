#pragma once

// Minimal ATL compatibility header for building without full ATL
// This provides CComPtr which is used by foobar2000 SDK

#ifndef __ATLCOMCLI_H__
#define __ATLCOMCLI_H__

#include <unknwn.h>

template <class T>
class CComPtr {
public:
    T* p;

    CComPtr() : p(NULL) {}
    CComPtr(T* lp) : p(lp) { if (p) p->AddRef(); }
    CComPtr(const CComPtr<T>& lp) : p(lp.p) { if (p) p->AddRef(); }
    ~CComPtr() { if (p) p->Release(); }

    void Release() {
        T* pTemp = p;
        if (pTemp) {
            p = NULL;
            pTemp->Release();
        }
    }

    operator T*() const { return p; }
    T& operator*() const { return *p; }
    T** operator&() { Release(); return &p; }
    T* operator->() const { return p; }
    T* operator=(T* lp) {
        if (lp) lp->AddRef();
        if (p) p->Release();
        p = lp;
        return p;
    }
    T* operator=(const CComPtr<T>& lp) { return operator=(lp.p); }
    bool operator!() const { return (p == NULL); }
    bool operator==(T* pT) const { return p == pT; }
};

#endif // __ATLCOMCLI_H__
