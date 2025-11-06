/*------------------------------------------------------------------------
名称：DragDropWrapper.cpp
说明：在本地实现 DoDragDrop 操作以避免托管 CCW（用于 AOT 发布）。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-06
备注：本文件提供 DoDragDropFilesNative 函数，接受以换行分隔的宽字符路径列表，
      在本地创建 IDataObject/IDropSource 并调用系统 DoDragDrop，避免托管侧创建 COM
      Callable Wrappers (CCW)。
------------------------------------------------------------------------*/

#include "Common.h"
#include <windows.h>
#include <ole2.h>
#include <objidl.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <memory>

using std::vector;
using std::wstring;

// 如果目标环境的头文件中未定义 DROPFILES，则在本地声明一个兼容结构，避免编译错误。
#ifndef DROPFILES
typedef struct _DROPFILES {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
} DROPFILES, *LPDROPFILES;
#endif

// 简单的 IEnumFORMATETC 实现，仅返回一个 FORMATETC
class FormatEtcEnum : public IEnumFORMATETC {
private:
    LONG _ref;
    FORMATETC _fmt;
    bool _hasReturned;
public:
    FormatEtcEnum(const FORMATETC &fmt) : _ref(1), _fmt(fmt), _hasReturned(false) {
        // 确保内部 FORMATETC 的 ptd 字段为 nullptr，避免未初始化指针在后续被使用
        _fmt.ptd = nullptr;
    }
    virtual ~FormatEtcEnum() {}

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppvObject) override {
        if(ppvObject == nullptr) return E_POINTER;
        if(IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumFORMATETC)) {
            *ppvObject = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&_ref); }
    ULONG __stdcall Release() override { LONG v = InterlockedDecrement(&_ref); if(v==0) delete this; return v; }

    // IEnumFORMATETC
    HRESULT __stdcall Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) override {
        if(celt == 0 || rgelt == nullptr) return E_INVALIDARG;
        ULONG fetched = 0;
        if(!_hasReturned && celt >= 1) {
            rgelt[0] = _fmt;
            fetched = 1;
            _hasReturned = true;
        }
        if(pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    HRESULT __stdcall Skip(ULONG celt) override { return E_NOTIMPL; }
    HRESULT __stdcall Reset() override { _hasReturned = false; return S_OK; }
    HRESULT __stdcall Clone(IEnumFORMATETC **ppenum) override {
        if(!ppenum) return E_POINTER;
        *ppenum = new FormatEtcEnum(_fmt);
        return S_OK;
    }
};

// 简单的 IDataObject 实现，仅支持 CF_HDROP / TYMED_HGLOBAL
class SimpleDataObjectNative : public IDataObject {
private:
    LONG _ref;
    HGLOBAL _hSource; // 原始缓冲区（仅作数据源），析构时释放
    FORMATETC _fe;

public:
    SimpleDataObjectNative(HGLOBAL hSource) : _ref(1), _hSource(hSource) {
        // 清零 FORMATETC，确保所有未显式设置的字段（尤其是 ptd）为安全的空值，防止 DoDragDrop 内部使用未初始化指针
        ZeroMemory(&_fe, sizeof(_fe));
        _fe.cfFormat = CF_HDROP;
        _fe.dwAspect = DVASPECT_CONTENT;
        _fe.lindex = -1;
        _fe.tymed = TYMED_HGLOBAL;
        _fe.ptd = nullptr;
    }

    virtual ~SimpleDataObjectNative() {
        if(_hSource) {
            GlobalFree(_hSource);
            _hSource = nullptr;
        }
    }

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppvObject) override {
        if(ppvObject == nullptr) return E_POINTER;
        if(IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject)) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override { return InterlockedIncrement(&_ref); }
    ULONG __stdcall Release() override { LONG v = InterlockedDecrement(&_ref); if(v==0) delete this; return v; }

    // IDataObject minimal 实现
    HRESULT __stdcall GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override {
        if(!pformatetcIn || !pmedium) return E_INVALIDARG;
        if(pformatetcIn->cfFormat != CF_HDROP) return DV_E_FORMATETC;
        if(!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;

        SIZE_T size = GlobalSize(_hSource);
        if(size == 0) return E_FAIL;

        HGLOBAL hOut = GlobalAlloc(GMEM_MOVEABLE, size);
        if(!hOut) return E_OUTOFMEMORY;

        void* src = GlobalLock(_hSource);
        void* dst = GlobalLock(hOut);
        if(!src || !dst) {
            if(src) GlobalUnlock(_hSource);
            if(dst) GlobalUnlock(hOut);
            GlobalFree(hOut);
            return E_FAIL;
        }
        memcpy(dst, src, size);
        GlobalUnlock(_hSource);
        GlobalUnlock(hOut);

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hOut;
        pmedium->pUnkForRelease = nullptr; // 接收方负责释放
        return S_OK;
    }

    HRESULT __stdcall GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) override { return E_NOTIMPL; }
    HRESULT __stdcall QueryGetData(FORMATETC *pformatetc) override {
        if(!pformatetc) return E_INVALIDARG;
        if(pformatetc->cfFormat == CF_HDROP) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC *pformatectIn, FORMATETC *pformatetcOut) override {
        if(!pformatetcOut || !pformatectIn) return E_INVALIDARG;
        *pformatetcOut = *pformatectIn;
        return S_OK;
    }
    HRESULT __stdcall SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override { return E_NOTIMPL; }
    HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override {
        if(!ppenumFormatEtc) return E_POINTER;
        if(dwDirection == DATADIR_GET) {
            *ppenumFormatEtc = new FormatEtcEnum(_fe);
            return S_OK;
        }
        return E_NOTIMPL;
    }
    HRESULT __stdcall DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall DUnadvise(DWORD dwConnection) override { return E_NOTIMPL; }
    HRESULT __stdcall EnumDAdvise(IEnumSTATDATA **ppenumAdvise) override { return E_NOTIMPL; }
};

// 简单的 IDropSource 实现
class DropSourceNative : public IDropSource {
private:
    LONG _ref;
public:
    DropSourceNative() : _ref(1) {}
    virtual ~DropSourceNative() {}

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppvObject) override {
        if(ppvObject == nullptr) return E_POINTER;
        if(IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDropSource)) {
            *ppvObject = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&_ref); }
    ULONG __stdcall Release() override { LONG v = InterlockedDecrement(&_ref); if(v==0) delete this; return v; }

    // IDropSource
    HRESULT __stdcall QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if(fEscapePressed) return DRAGDROP_S_CANCEL;
        // 若左键已释放，则视为放下 (MK_LBUTTON == 0x0001)
        if((grfKeyState & 0x0001) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT __stdcall GiveFeedback(DWORD dwEffect) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// 将一段以换行分隔的宽字符路径字符串转换为 HGLOBAL (DROPFILES)
static HGLOBAL BuildDropFilesHGlobal(const wchar_t* filesCsv) {
    if(!filesCsv) return nullptr;
    // 分割文件路径
    vector<wstring> paths;
    const wchar_t* p = filesCsv;
    const wchar_t* start = p;
    while(*p) {
        if(*p == L'\n' || *p == L'\r') {
            if(p > start) paths.emplace_back(start, p - start);
            // 跳过连续的换行符
            while(*p == L'\n' || *p == L'\r') ++p;
            start = p;
            continue;
        }
        ++p;
    }
    if(p > start) paths.emplace_back(start, p - start);

    if(paths.empty()) return nullptr;

    // 计算需要的宽字符总数（每个路径后一个 NUL，最后多一个 NUL）
    size_t totalChars = 0;
    for(const auto &s : paths) totalChars += s.size() + 1;
    totalChars += 1; // 最后额外的 NUL

    SIZE_T sizeInBytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeInBytes);
    if(!hGlobal) return nullptr;

    DROPFILES* df = (DROPFILES*)GlobalLock(hGlobal);
    if(!df) {
        GlobalFree(hGlobal);
        return nullptr;
    }
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = 0; df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE;

    wchar_t* dest = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
    for(const auto &s : paths) {
        for(size_t i = 0; i < s.size(); ++i) dest[i] = s[i];
        dest[s.size()] = L'\0';
        dest += (s.size() + 1);
    }
    *dest = L'\0';

    GlobalUnlock(hGlobal);
    return hGlobal;
}

extern "C" {
    /**
     * DoDragDropFilesNative
     * 参数：filesCsv - 以 '\n' 分隔的宽字符文件路径列表（UTF-16）。
     * 返回：1 表示成功（放下），0 表示失败/取消。
     */
    DllExport int __stdcall DoDragDropFilesNative(const wchar_t* filesCsv) {
        if(!filesCsv) return 0;

        HRESULT hr = OleInitialize(NULL);
        bool initialized = SUCCEEDED(hr);

        HGLOBAL hDrop = BuildDropFilesHGlobal(filesCsv);
        if(!hDrop) {
            if(initialized) OleUninitialize();
            return 0;
        }

        // IDataObject 实例
        SimpleDataObjectNative* dataObj = new SimpleDataObjectNative(hDrop);
        // DropSource 实例
        DropSourceNative* dropSrc = new DropSourceNative();

        DWORD effect = 0;
        HRESULT res = DoDragDrop(static_cast<IDataObject*>(dataObj), static_cast<IDropSource*>(dropSrc), DROPEFFECT_COPY, &effect);

        // 释放引用（对象会在 Release 时删除）
        dataObj->Release();
        dropSrc->Release();

        if(initialized) OleUninitialize();

        return (res == S_OK || res == DRAGDROP_S_DROP) ? 1 : 0;
    }
}
