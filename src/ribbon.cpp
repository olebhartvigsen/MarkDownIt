#include "ribbon.h"
#include "app.h"

// Module-level globals.
IUIFramework* g_pRibbonFramework = NULL;
IUIApplication* g_pRibbonApplication = NULL;
UINT g_ribbonHeight = 0;

//
// Framework init/destroy.
//

bool InitRibbon(HWND hWnd, AppWindow* app)
{
    HRESULT hr = CoCreateInstance(CLSID_UIRibbonFramework, NULL,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pRibbonFramework));
    if (FAILED(hr) || !g_pRibbonFramework)
        return false;

    hr = CRibbonApplication::CreateInstance(app, &g_pRibbonApplication);
    if (FAILED(hr) || !g_pRibbonApplication)
        return false;

    hr = g_pRibbonFramework->Initialize(hWnd, g_pRibbonApplication);
    if (FAILED(hr))
        return false;

    // MARKDOWNIT_RIBBON is the resource name in the .rc file.
    hr = g_pRibbonFramework->LoadUI(GetModuleHandle(NULL), L"MARKDOWNIT_RIBBON");
    if (FAILED(hr))
        return false;

    return true;
}

void DestroyRibbon()
{
    if (g_pRibbonFramework)
    {
        g_pRibbonFramework->Destroy();
        g_pRibbonFramework->Release();
        g_pRibbonFramework = NULL;
    }
    if (g_pRibbonApplication)
    {
        g_pRibbonApplication->Release();
        g_pRibbonApplication = NULL;
    }
    g_ribbonHeight = 0;
}

void UpdateRibbonWrapState(bool wrapped)
{
    if (!g_pRibbonFramework) return;
    // Set the toggle state of the Wrap command.
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_BOOL;
    var.boolVal = wrapped ? VARIANT_TRUE : VARIANT_FALSE;
    g_pRibbonFramework->SetUICommandProperty(IDC_CMD_WRAP,
        UI_PKEY_BooleanValue, var);
    PropVariantClear(&var);
}

//
// CRibbonApplication: IUIApplication implementation.
//

HRESULT CRibbonApplication::CreateInstance(AppWindow* app, IUIApplication** ppApp)
{
    if (!ppApp) return E_POINTER;
    *ppApp = NULL;
    CRibbonApplication* p = new CRibbonApplication(app);
    if (!p) return E_OUTOFMEMORY;
    *ppApp = static_cast<IUIApplication*>(p);
    return S_OK;
}

CRibbonApplication::~CRibbonApplication()
{
    if (m_pCommandHandler)
    {
        m_pCommandHandler->Release();
        m_pCommandHandler = NULL;
    }
}

STDMETHODIMP_(ULONG) CRibbonApplication::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CRibbonApplication::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) delete this;
    return cRef;
}

STDMETHODIMP CRibbonApplication::QueryInterface(REFIID iid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (iid == __uuidof(IUnknown))
        *ppv = static_cast<IUnknown*>(this);
    else if (iid == __uuidof(IUIApplication))
        *ppv = static_cast<IUIApplication*>(this);
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP CRibbonApplication::OnCreateUICommand(
    UINT nCmdID, UI_COMMANDTYPE typeID,
    IUICommandHandler** ppCommandHandler)
{
    // The Application Menu button ("Fil" on Danish Windows) is always
    // created by the Ribbon Framework, even without ApplicationMenu in XML.
    // Return E_NOTIMPL to prevent handler creation. This suppresses
    // the button via the COM API.
    if (typeID == (UI_COMMANDTYPE)0)  // UI_COMMANDTYPE_APPLICATIONMENU
    {
        *ppCommandHandler = NULL;
        return E_NOTIMPL;
    }

    UNREFERENCED_PARAMETER(nCmdID);

    if (!m_pCommandHandler)
    {
        HRESULT hr = CRibbonCommandHandler::CreateInstance(m_pApp, &m_pCommandHandler);
        if (FAILED(hr)) return hr;
    }
    return m_pCommandHandler->QueryInterface(IID_PPV_ARGS(ppCommandHandler));
}

STDMETHODIMP CRibbonApplication::OnViewChanged(
    UINT viewId, UI_VIEWTYPE typeId,
    IUnknown* pView, UI_VIEWVERB verb, INT uReasonCode)
{
    UNREFERENCED_PARAMETER(uReasonCode);
    UNREFERENCED_PARAMETER(viewId);

    if (UI_VIEWTYPE_RIBBON != typeId)
        return E_NOTIMPL;

    switch (verb)
    {
    case UI_VIEWVERB_CREATE:
        return S_OK;

    case UI_VIEWVERB_SIZE:
    {
        // The ribbon was resized. Get its new height so the app can
        // offset its content area below the ribbon.
        IUIRibbon* pRibbon = NULL;
        HRESULT hr = pView->QueryInterface(IID_PPV_ARGS(&pRibbon));
        if (SUCCEEDED(hr))
        {
            hr = pRibbon->GetHeight(&g_ribbonHeight);
            pRibbon->Release();
            // Notify AppWindow to re-layout.
            if (m_pApp)
                m_pApp->OnRibbonHeightChanged();
        }
        return S_OK;
    }

    case UI_VIEWVERB_DESTROY:
        return S_OK;
    }
    return E_NOTIMPL;
}

STDMETHODIMP CRibbonApplication::OnDestroyUICommand(
    UINT32 nCmdID, UI_COMMANDTYPE typeID,
    IUICommandHandler* commandHandler)
{
    UNREFERENCED_PARAMETER(nCmdID);
    UNREFERENCED_PARAMETER(typeID);
    UNREFERENCED_PARAMETER(commandHandler);
    return E_NOTIMPL;
}

//
// CRibbonCommandHandler: IUICommandHandler implementation.
//

HRESULT CRibbonCommandHandler::CreateInstance(AppWindow* app, IUICommandHandler** ppHandler)
{
    if (!ppHandler) return E_POINTER;
    *ppHandler = NULL;
    CRibbonCommandHandler* p = new CRibbonCommandHandler(app);
    if (!p) return E_OUTOFMEMORY;
    *ppHandler = static_cast<IUICommandHandler*>(p);
    return S_OK;
}

STDMETHODIMP_(ULONG) CRibbonCommandHandler::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CRibbonCommandHandler::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) delete this;
    return cRef;
}

STDMETHODIMP CRibbonCommandHandler::QueryInterface(REFIID iid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (iid == __uuidof(IUnknown))
        *ppv = static_cast<IUnknown*>(this);
    else if (iid == __uuidof(IUICommandHandler))
        *ppv = static_cast<IUICommandHandler*>(this);
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP CRibbonCommandHandler::UpdateProperty(
    UINT nCmdID, REFPROPERTYKEY key,
    const PROPVARIANT* ppropvarCurrentValue,
    PROPVARIANT* ppropvarNewValue)
{
    UNREFERENCED_PARAMETER(nCmdID);
    UNREFERENCED_PARAMETER(key);
    UNREFERENCED_PARAMETER(ppropvarCurrentValue);
    UNREFERENCED_PARAMETER(ppropvarNewValue);
    return E_NOTIMPL;
}

STDMETHODIMP CRibbonCommandHandler::Execute(
    UINT nCmdID, UI_EXECUTIONVERB verb,
    const PROPERTYKEY* key,
    const PROPVARIANT* ppropvarValue,
    IUISimplePropertySet* pCommandExecutionProperties)
{
    UNREFERENCED_PARAMETER(key);
    UNREFERENCED_PARAMETER(ppropvarValue);
    UNREFERENCED_PARAMETER(pCommandExecutionProperties);

    if (verb != UI_EXECUTIONVERB_EXECUTE || !m_pApp)
        return S_OK;

    // Dispatch to AppWindow action methods based on command ID.
    switch (nCmdID)
    {
    case IDC_CMD_OPEN:    m_pApp->OpenFileDialog();    break;
    case IDC_CMD_RELOAD:  m_pApp->Reload();            break;
    case IDC_CMD_WRAP:    m_pApp->ToggleWrap();        break;
    case IDC_CMD_ZOOMIN:  m_pApp->ZoomIn();            break;
    case IDC_CMD_ZOOMOUT: m_pApp->ZoomOut();           break;
    case IDC_CMD_ABOUT:   m_pApp->ShowAbout();         break;
    }
    return S_OK;
}
