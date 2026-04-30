# Liquid Glass Private Testing Archive

Private testing was used to find the adaptive behavior for the address results dropdown.

Findings:

- `NSGlassEffectView` public styles are only `Regular` and `Clear`.
- Forcing `tintColor` or `appearance` is not desirable; it fights system rendering.
- Private setters like `_scrimState`, `_variant`, and `_adaptiveAppearance` accepted values but did not solve automatic light/dark switching.
- The useful discovery was private `NSAdaptiveAppearanceView`.

Current implementation is documented in `docs/liquid-glass-adaptive.md`.
