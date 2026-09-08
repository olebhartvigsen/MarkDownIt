// Shared oracle helper: render one diagram type headless with the dom-shim
// and return the SVG string. Used by pie_oracle.mjs and the sequence probe.
// dom-shim.mjs has no exports — it performs its work at import time via
// side effects, so a bare side-effect import is the correct wiring.
//
// Font config: the user's real Chrome render (Mermaid Live / docs) runs
// with themeVariables.fontSize=16 and sans-serif throughout — measured
// and verified in tests/markdown-mermaid/mermaid-diagram-*.svg exports.
// The oracle must use the SAME config or text bboxes (and therefore
// layout heights/pitches) diverge from the parity target.
import './dom-shim.mjs';

export const ORACLE_FONTS = {
  themeVariables: { fontSize: '16px' },
  fontFamily: '"Segoe UI", Arial, sans-serif',
  // Sequence reads per-role fonts from its own config, not themeVariables:
  sequence: {
    messageFontSize: 16, noteFontSize: 16, actorFontSize: 16,
    messageFontFamily: '"Segoe UI", Arial, sans-serif',
    noteFontFamily: '"Segoe UI", Arial, sans-serif',
    actorFontFamily: '"Segoe UI", Arial, sans-serif',
  },
};

export async function renderDiagram(type, source, configOverrides = {}) {
  const mermaid = (await import('mermaid')).default;
  const globalFonts = { ...ORACLE_FONTS, sequence: undefined };
  mermaid.initialize({
    startOnLoad: false,
    ...globalFonts,
    // per-type fonts live inside the type config itself:
    [type]: {
      useMaxWidth: false,
      ...ORACLE_FONTS[type],
      ...configOverrides[type],
    },
    themeCSS: '',
  });
  const { svg } = await mermaid.render('g', source);
  return { svg };
}
