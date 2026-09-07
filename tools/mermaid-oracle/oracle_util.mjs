// Shared oracle helper: render one diagram type headless with the dom-shim
// and return the SVG string. Used by pie_oracle.mjs and the sequence probe.
// dom-shim.mjs has no exports — it performs its work at import time via
// side effects, so a bare side-effect import is the correct wiring.
import './dom-shim.mjs';

export async function renderDiagram(type, source, configOverrides = {}) {
  const mermaid = (await import('mermaid')).default;
  mermaid.initialize({
    startOnLoad: false,
    [type]: { useMaxWidth: false, ...configOverrides[type] },
    themeCSS: '',
  });
  const { svg } = await mermaid.render('g', source);
  return { svg };
}
