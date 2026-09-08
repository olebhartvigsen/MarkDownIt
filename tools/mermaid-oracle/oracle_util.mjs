// Shared oracle helper: render one diagram type headless with the dom-shim
// and return the SVG string. Used by every *_oracle.mjs.
//
// The class/pie/flowchart oracles must run under the historical dom-shim
// callibration (len*4 text bounds) their C++ engines were tuned against;
// the sequence oracle requires the real-font shim. Two shim files:
//   dom-shim.mjs      historical (class/pie/flowchart/dump)
//   dom-shim-seq.mjs  real font metrics via text_metrics.mjs
// An oracle opts in by setting globalThis.__ORACLE_REAL_FONTS = true BEFORE
// rendering (sequence_oracle.mjs does; dom-shim-seq.mjs is the copy whose
// getBBox honours the flag).
export const ORACLE_FONTS_GLOBAL = {
  fontFamily: '"Segoe UI", Arial, sans-serif',
  themeVariables: { fontSize: '16px' },
};
const ORACLE_FONTS_SEQ = {
  sequence: {
    messageFontSize: 16, noteFontSize: 16, actorFontSize: 16,
    messageFontFamily: '"Segoe UI", Arial, sans-serif',
    noteFontFamily: '"Segoe UI", Arial, sans-serif',
    actorFontFamily: '"Segoe UI", Arial, sans-serif',
  },
};

export async function renderDiagram(type, source, configOverrides = {}) {
  const mermaid = (await import('mermaid')).default;
  mermaid.initialize({
    startOnLoad: false,
    // Sequence is calibrated against the user's 16px sans-serif render;
    // other diagram types use mermaid defaults exactly like the old oracle.
    ...(type === 'sequence' ? ORACLE_FONTS_GLOBAL : {}),
    // per-type fonts live inside the type config itself:
    [type]: {
      useMaxWidth: false,
      ...(type === 'sequence' ? ORACLE_FONTS_SEQ.sequence : null),
      ...configOverrides[type],
    },
    themeCSS: '',
  });
  const { svg } = await mermaid.render('g', source);
  return { svg };
}
