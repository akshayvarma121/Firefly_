/** @type {import('tailwindcss').Config} */

/* 
 * TAILWIND CONFIGURATION - INSTRUMENT PANEL AESTHETIC
 * 
 * RULES & CONSTRAINTS:
 * 1. Tokens: Exactly the 7 color tokens defined below. No others.
 * 2. Border Radius: Maximum 2px border radius everywhere.
 * 3. Shadows: No box-shadow utilities used anywhere in the app (removed from theme).
 * 4. Borders: 1px solid borders using the 'border' token for all panel divisions.
 * 5. Effects: No gradients, blur, or glow utilities (removed from theme).
 * 
 * COLOR TOKENS:
 * - background: #111310 (Global app background)
 * - panel: #1B1D18 (Surface/container background)
 * - border: #33362E (All strokes, dividers, panel outlines)
 * - text-primary: #E8E6DE (Standard text, headings)
 * - text-muted: #8C8B80 (Secondary text, inactive states)
 * - signal: #E8A33D (Accent/Active state indicator, DO NOT use for decoration)
 * - trace-secondary: #6B8F71 (Secondary chart trace/dual-objective line)
 */

export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    colors: {
      background: '#111310',
      panel: '#1B1D18',
      border: '#33362E',
      text: {
        primary: '#E8E6DE',
        muted: '#8C8B80',
      },
      signal: '#E8A33D',
      trace: {
        secondary: '#6B8F71',
      },
      transparent: 'transparent',
      current: 'currentColor',
    },
    fontFamily: {
      sans: ['"IBM Plex Sans"', 'sans-serif'],
      mono: ['"IBM Plex Mono"', 'monospace'],
    },
    borderRadius: {
      none: '0',
      sm: '1px',
      DEFAULT: '2px',
      md: '2px',
      lg: '2px',
      xl: '2px',
      '2xl': '2px',
      '3xl': '2px',
      full: '2px', // overridden to prevent rounded cards
    },
    boxShadow: {
      none: 'none',
    },
    backgroundImage: {
      none: 'none',
    },
    blur: {
      none: '0',
    },
    dropShadow: {
      none: 'none',
    },
    extend: {},
  },
  plugins: [],
  corePlugins: {
    boxShadow: false,
    boxShadowColor: false,
    backgroundImage: false,
    gradientColorStops: false,
    blur: false,
    dropShadow: false,
  }
}


