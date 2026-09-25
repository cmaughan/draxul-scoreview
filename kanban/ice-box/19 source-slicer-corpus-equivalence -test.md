# SourceSlicer corpus equivalence

**Type:** test

## Gap

The Grieg regressions do not span the MusicXML state transitions a random rolling
window may cross.

## Work

- [ ] Build a small license-safe corpus covering attributes, voices, repeats, pickups,
      meter, key, clef, transposition, grace notes, tuplets, and multiple staves.
- [ ] Compare seeded sliced windows with monolithic engraving after normalizing time.
- [ ] Compare bar timing/indexing with the semantic importer where their vocabularies
      overlap and print reproducible seeds/ranges on failure.
- [ ] Keep a small always-on subset and gate the expensive Verovio corpus through the
      product's slow-test mechanism.
- [ ] Preserve the Grieg measure-53 case as a named regression.
