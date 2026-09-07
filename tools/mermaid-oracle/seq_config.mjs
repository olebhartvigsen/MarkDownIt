// Dump mermaid's DEFAULT sequence config + the config.type.d.ts SurveySequenceConfig defaults
import mermaid from 'mermaid';
const cfg = mermaid.mermaidAPI.getConfig();
console.log(JSON.stringify(cfg.sequence, null, 2));
