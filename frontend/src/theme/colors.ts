// src/theme/colors.ts
// THE palette — single source of truth. Edit values here to retheme the whole app.
export const colors = {
  bg: {
    primary:    '#0e1f38',   // Main page background — rich dark navy blue
    secondary:  '#132640',   // Cards, side panels
    tertiary:   '#1a3260',   // Input fields, hover surfaces
    overlay:    '#0a1628',   // Modal backdrops, dropzones
  },
  accent: {
    primary:    '#2563eb',   // Primary buttons, active states, links
    hover:      '#1d4ed8',   // Button hover
    light:      '#3b82f6',   // Highlights, focus rings, progress bars
    subtle:     '#1e3a8a',   // Subtle tinted backgrounds (e.g. selected row)
  },
  text: {
    primary:    '#e8f0fe',   // Main body text
    secondary:  '#94a3b8',   // Muted / helper text
    accent:     '#60a5fa',   // Links, labels, accented values
    inverse:    '#0e1f38',   // Text on light/accent backgrounds
  },
  border: {
    default:    '#1e3a5f',   // Default border
    focus:      '#2563eb',   // Focused input border
    subtle:     '#132640',   // Very subtle dividers
  },
  status: {
    success:    '#10b981',   // Green — upload complete, auth success
    error:      '#ef4444',   // Red — errors, failures
    warning:    '#f59e0b',   // Amber — warnings
    info:       '#3b82f6',   // Blue — info banners
  },
  mono:         '#64748b',   // For monospace IDs, hash values, code
}
