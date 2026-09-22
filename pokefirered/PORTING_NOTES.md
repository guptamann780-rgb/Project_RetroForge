# SDL2/host port — known issues

## data/maps/CeladonCity_GameCorner/scripts.inc — NOT YET FIXED
5 call sites use `goto_if_ge VAR, (EXPR) - N, label` with an arithmetic
macro argument. Host GNU `as` fails with "too many positional arguments"
even on a minimal 3-line repro — confirmed it's plain arithmetic
operators inside an unquoted macro arg (not parens specifically:
`(2)` alone is fine, `2 - 3` alone already fails). Real GBA build
(agbcc toolchain) is unaffected — this is host-`as`-specific.
Effect if left unfixed: this one map's coin-purchase NPCs will likely
be broken/unreachable in the host port. Rest of game unaffected.
Next step: check GAS docs for macro argument literal-parsing mode,
or just precompute the constant (MAX_COINS+1-500 etc.) by hand and
pass a plain number instead of an expression, as a workaround.
