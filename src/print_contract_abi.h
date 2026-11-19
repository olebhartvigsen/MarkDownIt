// The hand-declared ABI of the Windows print contract. The pieces the SDK
// still ships come from it; the pieces it stopped shipping live here, each
// with the source of its GUID in a comment. Plan:
// .hermes/plans/2026-11-19_233000-modern-windows-print-integration.md
//
// Why hand-declared at all: the contract is a set of classic COM
// interfaces between the app and the print pipeline. The Win8-era SDK
// headers that declared them (PrintPreview.h) are gone from current
// toolkits, while the interfaces themselves are still the contract of
// every Windows 10 and 11 print dialog. The shapes below are verified:
//
// - IPrintDocumentSource, IPrintDocumentPageSource and
//   IPrintPreviewPageCollection, GUIDs and method order, from the working
//   unpackaged reference smourier/XpsPrintSamples (2024),
//   CustomPrintDocumentAot/Model/BasePrintDocument.cs and Interop/.
// - IPrintPreviewDxgiPackageTarget, vtable order SetJobPageCount,
//   DrawPage, InvalidatePreview, from the Rust windows projection
//   (docs.rs, Win32::Graphics::Printing) and the ComponentOne/SharpDX
//   line of bindings; the modern IID and the Win8.0 fallback IID from
//   Chromium's win8/metro_driver/print_document_source.cc, which ships
//   both because the IID changed across SDK and OS generations.
// - IPrintDocumentPackageTarget and its status event stay in the SDK
//   (documenttarget.h); nothing here redeclares them.

#ifndef MARKDOWNIT_SRC_PRINT_CONTRACT_ABI_H_
#define MARKDOWNIT_SRC_PRINT_CONTRACT_ABI_H_

#include <inspectable.h>
#include <unknwn.h>

#include <cstdint>

// Only pointers of these are used here; the full headers are heavy.
struct IDXGISurface;
struct IPrintDocumentPackageTarget;

namespace print_contract {

// IPrintDocumentSource {dedc0c30-f1eb-47df-aae6-ed5427511f01}: the marker
// the app hands to PrintTaskSourceRequestedArgs.SetSource. It carries no
// methods: the pipeline interrogates the object for the two interfaces
// below. Declared IUnknown-style with no slots beyond identity, which is
// what a classic COM object answers.
struct __declspec(uuid("dedc0c30-f1eb-47df-aae6-ed5427511f01"))
IPrintDocumentSource : public IUnknown {};

// IPrintDocumentPageSource {a96bb1db-172e-4667-82b5-ad97a252318f}: the
// pipeline's entry into the app's content. GetPreviewPageCollection
// hands the preview sink, MakeDocument the final print sink. The layout
// is IUnknown plus the two methods, the empirically working shape of the
// reference: the print pipeline calls these vtables as classic COM, not
// through the WinRT IInspectable layout.
struct __declspec(uuid("a96bb1db-172e-4667-82b5-ad97a252318f"))
IPrintDocumentPageSource : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(
        IPrintDocumentPackageTarget* printDocumentPackageTarget,
        void** printPreviewPageCollection) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeDocument(
        IInspectable* printTaskOptions,
        IPrintDocumentPackageTarget* printDocumentPackageTarget) = 0;
};

// IPrintPreviewPageCollection {0b31cc62-d7ec-4747-9d6e-f2537d870f2b}:
// the preview's page feed. Paginate arrives whenever the paper, the
// orientation or the tray changes (Word repaginates on the same events);
// MakePage asks for one page's image. printTaskOptions is the WinRT
// PrintTaskOptions as an IInspectable, and may be null in a MakePage
// context.
struct __declspec(uuid("0b31cc62-d7ec-4747-9d6e-f2537d870f2b"))
IPrintPreviewPageCollection : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Paginate(
        uint32_t currentJobPage,
        IInspectable* printTaskOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakePage(
        uint32_t desiredJobPage,
        float width,
        float height) = 0;
};

// MakePage's "a new preview pass" page number: not a page index.
constexpr uint32_t kJobPageApplicationDefined = 0xFFFFFFFFu;

// IPrintPreviewDxgiPackageTarget
// {1a6dd0ad-1e2a-4e99-a5ba-91f17818290e}: the preview sink. Obtained
// from IPrintDocumentPackageTarget::GetPackageTarget with its own IID as
// the target type; DrawPage hands the rendered surface for a page to the
// dialog. The IID is generation II of the interface; the Win8.0 original
// {fb2a33c0-8c35-465f-bed5-9f3689517752} is kept below as the fallback,
// exactly as Chromium's metro print driver did.
struct __declspec(uuid("1a6dd0ad-1e2a-4e99-a5ba-91f17818290e"))
IPrintPreviewDxgiPackageTarget : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetJobPageCount(
        int32_t countType,
        uint32_t count) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPage(
        uint32_t jobPageNumber,
        IDXGISurface* pageImage,
        float dpiX,
        float dpiY) = 0;
    virtual HRESULT STDMETHODCALLTYPE InvalidatePreview() = 0;
};

// The Win8.0 generation of the preview target: the vtables are the same,
// the identity GUID changed in Win8.1.
struct __declspec(uuid("fb2a33c0-8c35-465f-bed5-9f3689517752"))
IPrintPreviewDxgiPackageTargetWin8 : public IUnknown {};

// PageCountType of SetJobPageCount. The enum has these two members in
// declaration order; Final is 0, Intermediate 1, the MIDL default
// numbering the bindings of every generation carry.
enum PageCountType {
    kFinalPageCount = 0,
    kIntermediatePageCount = 1,
};

// The GUIDs as initialized constants, for GetPackageTarget's type
// argument where an IID-typed value is needed.
// {1a6dd0ad-1e2a-4e99-a5ba-91f17818290e}
constexpr GUID kPrintPreviewDxgiPackageTargetType = {
    0x1a6dd0ad, 0x1e2a, 0x4e99,
    {0xa5, 0xba, 0x91, 0xf1, 0x78, 0x18, 0x29, 0x0e}};
// {fb2a33c0-8c35-465f-bed59f3689517752}
constexpr GUID kPrintPreviewDxgiPackageTargetTypeWin8 = {
    0xfb2a33c0, 0x8c35, 0x465f,
    {0xbe, 0xd5, 0x9f, 0x36, 0x89, 0x51, 0x77, 0x52}};

// ERROR_PRINT_CANCELLED, as Chromium and the reference treat it: a job
// the user dismissed is not an error, but it reaches the app as this
// HRESULT and must be swallowed quietly.
constexpr HRESULT kErrorPrintCancelled = static_cast<HRESULT>(
    0x80070000uL | 122uL);

}  // namespace print_contract

#endif  // MARKDOWNIT_SRC_PRINT_CONTRACT_ABI_H_
