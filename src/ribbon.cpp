#include "ribbon.h"
#include "crash_trace.h"
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
    {
        // Release both COM objects so no zombie framework keeps
        // receiving state pushes from UpdateRibbon* helpers.
        g_pRibbonFramework->Release();
        g_pRibbonFramework = NULL;
        g_pRibbonApplication->Release();
        g_pRibbonApplication = NULL;
        return false;
    }

    // MARKDOWNIT_RIBBON is the resource name in the .rc file.
    hr = g_pRibbonFramework->LoadUI(GetModuleHandle(NULL), L"MARKDOWNIT_RIBBON");
    if (FAILED(hr))
    {
        g_pRibbonFramework->Destroy();
        g_pRibbonFramework->Release();
        g_pRibbonFramework = NULL;
        g_pRibbonApplication->Release();
        g_pRibbonApplication = NULL;
        return false;
    }

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

void SetRibbonToggle(UINT cmdId, bool on)
{
    if (!g_pRibbonFramework) return;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_BOOL;
    var.boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
    g_pRibbonFramework->SetUICommandProperty(cmdId,
        UI_PKEY_BooleanValue, var);
    PropVariantClear(&var);
}

void UpdateRibbonFormatState(const FormatState& state)
{
    if (!g_pRibbonFramework) return;
    SetRibbonToggle(IDC_CMD_BOLD, state.bold);
    SetRibbonToggle(IDC_CMD_ITALIC, state.italic);
    SetRibbonToggle(IDC_CMD_CODE, state.code);
    SetRibbonToggle(IDC_CMD_STRIKE, state.strike);
    SetRibbonToggle(IDC_CMD_BULLETS, state.inBullets);
    SetRibbonToggle(IDC_CMD_NUMBERING, state.inNumbering);
    SetRibbonToggle(IDC_CMD_QUOTE, state.inQuote);
    // H1/H2/H3 are not toggle buttons; they are action buttons.
    // We use the pressed state to show "current heading level"
    // via BooleanValue on toggle-style commands only.
    SetRibbonToggle(IDC_CMD_H1, state.headingLevel == 1);
    SetRibbonToggle(IDC_CMD_H2, state.headingLevel == 2);
    SetRibbonToggle(IDC_CMD_H3, state.headingLevel == 3);
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

// The nine table commands. Shared by the enabled-state handler and by the
// invalidation lists so a new table command cannot be wired into one and
// forgotten in the other.
static bool IsTableCommand(UINT nCmdID) {
    switch (nCmdID) {
    case IDC_CMD_INSERT_TABLE:
    case IDC_CMD_TABLE_TOOLS:
    case IDC_CMD_ADD_ROW:
    case IDC_CMD_ADD_ROW_ABOVE:
    case IDC_CMD_REMOVE_ROW:
    case IDC_CMD_ADD_COLUMN:
    case IDC_CMD_ADD_COLUMN_LEFT:
    case IDC_CMD_REMOVE_COLUMN:
    case IDC_CMD_REMOVE_TABLE:
    case IDC_CMD_SPLIT_CELL:
    case IDC_CMD_ALIGN_LEFT:
    case IDC_CMD_ALIGN_CENTER:
    case IDC_CMD_ALIGN_RIGHT:
        return true;
    default:
        return false;
    }
}

// Allocate a VT_LPWSTR value with CoTaskMemAlloc. The Ribbon framework frees
// UI_PKEY_Label strings through PropVariantClear, which calls CoTaskMemFree;
// SysAllocString-backed memory freed that way is an allocator mismatch with a
// 4-byte OLEAUT header offset and corrupts the heap (0xC0000374 in ntdll,
// WER 2026-09-14 09:25:05). The SDK's UIInitPropertyToString helper is not
// present in modern Windows SDK headers, so the allocation is done here.
static HRESULT SetCmdLabel(const wchar_t* label, PROPVARIANT* out)
{
    if (!out || !label) return E_POINTER;
    size_t len = wcslen(label);
    PWSTR copy = static_cast<PWSTR>(
        CoTaskMemAlloc((len + 1) * sizeof(wchar_t)));
    if (!copy) return E_OUTOFMEMORY;
    wmemcpy(copy, label, len + 1);
    out->vt = VT_LPWSTR;
    out->pwszVal = copy;
    return S_OK;
}

STDMETHODIMP CRibbonCommandHandler::UpdateProperty(
    UINT nCmdID, REFPROPERTYKEY key,
    const PROPVARIANT* ppropvarCurrentValue,
    PROPVARIANT* ppropvarNewValue)
{
    UNREFERENCED_PARAMETER(ppropvarCurrentValue);
    diag::TraceFmt("UpdateProperty enter cmd=%u", nCmdID);

    // The Ribbon framework queries the initial toggle state of the
    // Wrap ToggleButton via UI_PKEY_BooleanValue. Return the renderer's
    // current wrap state (true by default).
    if (nCmdID == IDC_CMD_WRAP && ppropvarNewValue)
    {
        if (IsEqualPropertyKey(key, UI_PKEY_BooleanValue))
        {
            diag::TraceFmt("UpdateProperty wrap bool");
            ppropvarNewValue->vt = VT_BOOL;
            ppropvarNewValue->boolVal =
                (m_pApp && m_pApp->IsWrapEnabled()) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
    }

    if (nCmdID == IDC_CMD_EDIT && ppropvarNewValue)
    {
        if (IsEqualPropertyKey(key, UI_PKEY_BooleanValue))
        {
            diag::TraceFmt("UpdateProperty edit bool");
            ppropvarNewValue->vt = VT_BOOL;
            ppropvarNewValue->boolVal =
                (m_pApp && m_pApp->IsEditing()) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
    }

    if (nCmdID == IDC_CMD_SOURCE && ppropvarNewValue)
    {
        if (IsEqualPropertyKey(key, UI_PKEY_BooleanValue))
        {
            diag::TraceFmt("UpdateProperty source bool");
            ppropvarNewValue->vt = VT_BOOL;
            ppropvarNewValue->boolVal =
                (m_pApp && m_pApp->IsSourceView()) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
    }

    // Settings buttons (Fil menu): dynamic labels reflect current state.
    // cmdAssocMd shows "Associate .md files" or "Unassociate .md files".
    // Width entries carry a checkmark prefix on the active width, the
    // native menu idiom for a chosen item: UICC's content model for a
    // MenuGroup allows no toggle or checkbox controls, so the mark
    // rides on the label text.
    if (ppropvarNewValue && IsEqualPropertyKey(key, UI_PKEY_Label))
    {
        diag::TraceFmt("UpdateProperty label branch cmd=%u", nCmdID);
        if (nCmdID == IDC_CMD_ASSOC_MD)
        {
            diag::TraceFmt("UpdateProperty assoc label, IsMdRegistered begin");
            bool on = (m_pApp && m_pApp->IsMdRegistered());
            diag::TraceFmt("UpdateProperty assoc label done on=%d", (int)on);
            const wchar_t* lbl = on
                ? L"Unassociate .md files"
                : L"Associate .md files";
            diag::TraceFmt("assoc label SetCmdLabel begin");
            HRESULT hrAssoc = SetCmdLabel(lbl, ppropvarNewValue);
            diag::TraceFmt("assoc label alloc done hr=0x%08X",
                           (unsigned)hrAssoc);
            return hrAssoc;
        }
        static const UINT widthCmds[4] = {
            IDC_CMD_WIDTH_STD, IDC_CMD_WIDTH_960,
            IDC_CMD_WIDTH_1600, IDC_CMD_WIDTH_FULL
        };
        static const wchar_t* const widthLabels4[4] = {
            L"Standard", L"960 px", L"1600 px", L"Full width"
        };
        for (int i = 0; i < 4; ++i) {
            if (nCmdID == widthCmds[i]) {
                int mode = m_pApp ? m_pApp->GetContentWidthMode() : 0;
                // buf lives in the loop body: lbl must not outlive it.
                // The earlier version built buf inside "if (mode == i)",
                // so lbl pointed into a dead scope by the time the
                // trace calls and SetCmdLabel ran, and the mark the
                // user was supposed to see never survived the trip.
                wchar_t buf[40];
                buf[0] = (wchar_t)0x2713;  // check mark
                buf[1] = L' ';
                wcsncpy_s(buf + 2, 38, widthLabels4[i], _TRUNCATE);
                const wchar_t* lbl =
                    (mode == i) ? buf : widthLabels4[i];
                diag::TraceFmt("UpdateProperty width label i=%d mode=%d",
                               i, mode);
                HRESULT hrWidth = SetCmdLabel(lbl, ppropvarNewValue);
                diag::TraceFmt("UpdateProperty width label done i=%d hr=0x%08X",
                               i, (unsigned)hrWidth);
                return hrWidth;
            }
        }
    }

    // Enable/disable format buttons based on edit mode.
    if (ppropvarNewValue && IsEqualPropertyKey(key, UI_PKEY_Enabled))
    {
        diag::TraceFmt("UpdateProperty enabled branch cmd=%u", nCmdID);

        // Zoom is available in both view and edit mode, so it must not go
        // through the editing gate below. At either end of the range the
        // button that cannot act is disabled, which the zoom guide asks
        // for; a live-looking button that silently does nothing reads as a
        // broken control.
        if (m_pApp &&
            (nCmdID == IDC_CMD_ZOOMIN || nCmdID == IDC_CMD_ZOOMOUT)) {
            ppropvarNewValue->vt = VT_BOOL;
            const bool on = (nCmdID == IDC_CMD_ZOOMIN)
                ? m_pApp->CanZoomIn()
                : m_pApp->CanZoomOut();
            ppropvarNewValue->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }

        // The File menu realizes its item views lazily at the first
        // open, so the startup Label invalidation (timer 6 in
        // InvalidateSettingsButtons) is dropped: no view exists to
        // mark dirty yet. This Enabled query is the witness that THIS
        // command's view exists right now, while the popup builds.
        // Seize the moment: set the marked label as the command's
        // current value, then invalidate the property (after the set,
        // a set onto a pending invalidation fails) so the label pass
        // of the same build re-asks it too. Whichever route the
        // framework honors, the checkmark rides on the active width
        // from the first menu open. A failed set is retried on the
        // next open through the pushedWidthMode tracking.
        {
            static const UINT wCmds[4] = {
                IDC_CMD_WIDTH_STD, IDC_CMD_WIDTH_960,
                IDC_CMD_WIDTH_1600, IDC_CMD_WIDTH_FULL
            };
            static const wchar_t* const wLbls[4] = {
                L"Standard", L"960 px", L"1600 px", L"Full width"
            };
            static int pushedWidthMode[4] = { -2, -2, -2, -2 };
            const int mode = m_pApp ? m_pApp->GetContentWidthMode() : 0;
            for (int i = 0; i < 4; ++i) {
                if (nCmdID == wCmds[i] && pushedWidthMode[i] != mode) {
                    wchar_t buf[40];
                    buf[0] = (wchar_t)0x2713;  // check mark
                    buf[1] = L' ';
                    wcsncpy_s(buf + 2, 38, wLbls[i], _TRUNCATE);
                    const wchar_t* lbl = (mode == i) ? buf : wLbls[i];
                    const size_t len = wcslen(lbl);
                    wchar_t* copy = static_cast<wchar_t*>(
                        CoTaskMemAlloc((len + 1) * sizeof(wchar_t)));
                    if (copy) {
                        wcscpy_s(copy, len + 1, lbl);
                        PROPVARIANT var;
                        PropVariantInit(&var);
                        var.vt = VT_LPWSTR;
                        var.pwszVal = copy;
                        diag::TraceFmt(
                            "width label push start i=%d mode=%d", i, mode);
                        HRESULT hrSet = g_pRibbonFramework
                            ? g_pRibbonFramework->SetUICommandProperty(
                                  wCmds[i], UI_PKEY_Label, var)
                            : E_FAIL;
                        diag::TraceFmt(
                            "width label push done i=%d hr=0x%08X",
                            i, (unsigned)hrSet);
                        if (g_pRibbonFramework) {
                            g_pRibbonFramework->InvalidateUICommand(
                                wCmds[i], UI_INVALIDATIONS_PROPERTY,
                                &UI_PKEY_Label);
                        }
                        PropVariantClear(&var);
                        if (SUCCEEDED(hrSet)) pushedWidthMode[i] = mode;
                    }
                    break;
                }
            }
        }

        static const UINT fmtCmds[] = {
            IDC_CMD_BOLD, IDC_CMD_ITALIC, IDC_CMD_CODE, IDC_CMD_STRIKE,
            IDC_CMD_H1, IDC_CMD_H2, IDC_CMD_H3,
            IDC_CMD_BULLETS, IDC_CMD_NUMBERING, IDC_CMD_QUOTE,
            IDC_CMD_LINK, IDC_CMD_CLEARFORMAT,
            IDC_CMD_INDENT, IDC_CMD_OUTDENT,
            IDC_CMD_UNDO, IDC_CMD_REDO
        };
        for (auto cmd : fmtCmds) {
            if (nCmdID == cmd) {
                ppropvarNewValue->vt = VT_BOOL;
                bool editing = (m_pApp && m_pApp->IsEditing());
                if (cmd == IDC_CMD_UNDO)
                    ppropvarNewValue->boolVal = (editing && m_pApp && m_pApp->CanUndo()) ? VARIANT_TRUE : VARIANT_FALSE;
                else if (cmd == IDC_CMD_REDO)
                    ppropvarNewValue->boolVal = (editing && m_pApp && m_pApp->CanRedo()) ? VARIANT_TRUE : VARIANT_FALSE;
                else
                    ppropvarNewValue->boolVal = editing ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
        }

        // Table commands mutate the document, so they follow edit mode.
        // Within edit mode they are gated on caret context, then narrowed
        // further by what the command can actually do here: the header and
        // delimiter rows are structural, and the last column cannot be
        // removed. Without this the ribbon offers a live button that
        // silently does nothing.
        if (IsTableCommand(nCmdID) && m_pApp && m_pApp->IsEditing()) {
            if (!m_pApp->CaretInTable()) {
                ppropvarNewValue->vt = VT_BOOL;
                ppropvarNewValue->boolVal = VARIANT_FALSE;
                return S_OK;
            }
            const TableCapabilities caps = m_pApp->CaretTableCapabilities();
            bool on = true;
            switch (nCmdID) {
            case IDC_CMD_ADD_ROW:         on = caps.addRowBelow;   break;
            case IDC_CMD_ADD_ROW_ABOVE:   on = caps.addRowAbove;   break;
            case IDC_CMD_REMOVE_ROW:      on = caps.removeRow;     break;
            case IDC_CMD_ADD_COLUMN:      on = caps.addColumnRight; break;
            case IDC_CMD_ADD_COLUMN_LEFT: on = caps.addColumnLeft;  break;
            case IDC_CMD_REMOVE_COLUMN:   on = caps.removeColumn;   break;
            case IDC_CMD_REMOVE_TABLE:    on = true;                break;
            case IDC_CMD_SPLIT_CELL:      on = caps.splitCell;      break;
            // Alignment rewrites the delimiter row of any table the caret is
            // in, so it is available wherever the caret is.
            case IDC_CMD_ALIGN_LEFT:
            case IDC_CMD_ALIGN_CENTER:
            case IDC_CMD_ALIGN_RIGHT:     on = true;                break;
            // The DropDownButton's own enabled state is derived by the
            // framework from its children. Inside a table Delete Table is
            // always available, so the menu always has at least one live
            // item and the button itself stays enabled.
            case IDC_CMD_TABLE_TOOLS:     on = true;                break;
            default: break;
            }
            ppropvarNewValue->vt = VT_BOOL;
            ppropvarNewValue->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (nCmdID == IDC_CMD_INSERT_TABLE) {
            ppropvarNewValue->vt = VT_BOOL;
            // Disabled inside a cell: a table may never contain a table.
            const bool on = m_pApp && m_pApp->IsEditing() &&
                            !m_pApp->CaretInTable();
            ppropvarNewValue->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
    }

    // Formatting toggle buttons: query the caret's format state.
    if (ppropvarNewValue && IsEqualPropertyKey(key, UI_PKEY_BooleanValue))
    {
        diag::TraceFmt("UpdateProperty boolean branch cmd=%u", nCmdID);
        bool on = false;
        if (m_pApp)
        {
            diag::TraceFmt("UpdateProperty GetFormatState begin cmd=%u", nCmdID);
            FormatState fs = m_pApp->GetFormatState();
            diag::TraceFmt("UpdateProperty GetFormatState done cmd=%u", nCmdID);
            switch (nCmdID)
            {
            case IDC_CMD_MARK:
                on = m_pApp && m_pApp->SelectionHasMarker();  break;
            case IDC_CMD_BOLD:     on = fs.bold;      break;
            case IDC_CMD_ITALIC:   on = fs.italic;    break;
            case IDC_CMD_CODE:     on = fs.code;      break;
            case IDC_CMD_STRIKE:   on = fs.strike;    break;
            case IDC_CMD_BULLETS:  on = fs.inBullets;  break;
            case IDC_CMD_NUMBERING:on = fs.inNumbering;break;
            case IDC_CMD_QUOTE:    on = fs.inQuote;   break;
            case IDC_CMD_H1:       on = (fs.headingLevel == 1); break;
            case IDC_CMD_H2:       on = (fs.headingLevel == 2); break;
            case IDC_CMD_H3:       on = (fs.headingLevel == 3); break;
            default: return E_NOTIMPL;
            }
        }
        ppropvarNewValue->vt = VT_BOOL;
        ppropvarNewValue->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
        diag::TraceFmt("boolean branch return");
        return S_OK;
    }

    diag::TraceFmt("UpdateProperty E_NOTIMPL return");
    return E_NOTIMPL;
}

STDMETHODIMP CRibbonCommandHandler::Execute(
    UINT nCmdID, UI_EXECUTIONVERB verb,
    const PROPERTYKEY* key,
    const PROPVARIANT* ppropvarValue,
    IUISimplePropertySet* pCommandExecutionProperties)
{
    diag::Trace("Execute enter");
    UNREFERENCED_PARAMETER(key);
    UNREFERENCED_PARAMETER(ppropvarValue);
    UNREFERENCED_PARAMETER(pCommandExecutionProperties);

    if (verb != UI_EXECUTIONVERB_EXECUTE || !m_pApp)
        return S_OK;

    // Dispatch to AppWindow action methods based on command ID.
    switch (nCmdID)
    {
    case IDC_CMD_NEW:     m_pApp->NewDocument();       break;
    case IDC_CMD_OPEN:    m_pApp->OpenFileDialog();    break;
    case IDC_CMD_RELOAD:  m_pApp->Reload();            break;
    case IDC_CMD_SAVE:    m_pApp->Save();              break;
    case IDC_CMD_SAVEAS:  m_pApp->SaveAs();            break;
    case IDC_CMD_IMPORT_DOCX: m_pApp->ImportWordDocx(); break;
    case IDC_CMD_EXPORT_DOCX: m_pApp->ExportWordDocx(); break;
    case IDC_CMD_EXPORT_PDF:  m_pApp->ExportPdf();      break;
    case IDC_CMD_WRAP:    m_pApp->ToggleWrap();        break;
    case IDC_CMD_MARK:
        diag::Trace("MARKEXEC execute");
        m_pApp->ToggleMarkSelection();
        break;
    case IDC_CMD_ZOOMIN:  m_pApp->ZoomIn();            break;
    case IDC_CMD_ZOOMOUT: m_pApp->ZoomOut();           break;
    case IDC_CMD_ABOUT:   m_pApp->ShowAbout();         break;
    case IDC_CMD_BOLD:    m_pApp->ToggleBold();        break;
    case IDC_CMD_ITALIC: m_pApp->ToggleItalic();      break;
    case IDC_CMD_STRIKE:  m_pApp->ToggleStrike();      break;
    case IDC_CMD_CODE:    m_pApp->ToggleCode();        break;
    case IDC_CMD_LINK:    m_pApp->InsertLinkCmd();     break;
    case IDC_CMD_INSERT_TABLE: m_pApp->InsertTableCmd(); break;
    case IDC_CMD_ADD_ROW:     m_pApp->AddTableRow();     break;
    case IDC_CMD_ADD_ROW_ABOVE: m_pApp->AddTableRow(false); break;
    case IDC_CMD_REMOVE_ROW:  m_pApp->RemoveTableRow(); break;
    case IDC_CMD_ADD_COLUMN:  m_pApp->AddTableColumn(); break;
    case IDC_CMD_ADD_COLUMN_LEFT: m_pApp->AddTableColumn(false); break;
    case IDC_CMD_REMOVE_COLUMN: m_pApp->RemoveTableColumn(); break;
    case IDC_CMD_ALIGN_LEFT:
        m_pApp->SetTableColumnAlign(TableAlignMark::Left); break;
    case IDC_CMD_ALIGN_CENTER:
        m_pApp->SetTableColumnAlign(TableAlignMark::Center); break;
    case IDC_CMD_ALIGN_RIGHT:
        m_pApp->SetTableColumnAlign(TableAlignMark::Right); break;
    case IDC_CMD_REMOVE_TABLE: m_pApp->RemoveTable(); break;
    case IDC_CMD_SPLIT_CELL:   m_pApp->SplitTableCell(); break;
    case IDC_CMD_CLEARFORMAT: m_pApp->ClearFormat();  break;
    case IDC_CMD_H1:      m_pApp->SetHeading(1);       break;
    case IDC_CMD_H2:      m_pApp->SetHeading(2);       break;
    case IDC_CMD_H3:      m_pApp->SetHeading(3);       break;
    case IDC_CMD_BULLETS: m_pApp->ToggleBullets();     break;
    case IDC_CMD_NUMBERING: m_pApp->ToggleNumbering();break;
    case IDC_CMD_QUOTE:   m_pApp->ToggleQuote();       break;
    case IDC_CMD_INDENT:  m_pApp->Indent();            break;
    case IDC_CMD_OUTDENT: m_pApp->Outdent();           break;
    case IDC_CMD_EDIT:    m_pApp->ToggleEdit();        break;
    case IDC_CMD_SOURCE: m_pApp->ToggleSourceView();  break;
    case IDC_CMD_UNDO:    m_pApp->UndoAction();       break;
    case IDC_CMD_REDO:    m_pApp->RedoAction();       break;
    case IDC_CMD_FIND:    m_pApp->ShowFindReplace(false); break;
    case IDC_CMD_REPLACE: m_pApp->ShowFindReplace(true);  break;
    case IDC_CMD_ASSOC_MD:    m_pApp->ToggleMdAssociation();         break;
    case IDC_CMD_WIDTH_STD:   diag::Trace("Execute width std");   m_pApp->SetContentWidthMode(0);        break;
    case IDC_CMD_WIDTH_960:   diag::Trace("Execute width 960");   m_pApp->SetContentWidthMode(1);        break;
    case IDC_CMD_WIDTH_1600:  diag::Trace("Execute width 1600");  m_pApp->SetContentWidthMode(2);        break;
    case IDC_CMD_WIDTH_FULL:  diag::Trace("Execute width full");  m_pApp->SetContentWidthMode(3);        break;
    }
    diag::Trace("Execute return");
    return S_OK;
}
