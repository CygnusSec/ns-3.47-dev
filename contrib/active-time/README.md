# Active-time measurement

This module owns the passive MAC/TCP observation pipeline used to calculate
`Tactive` and `RBR`. It is deliberately independent of the optional CATRA
controllers, so Scenario 1 records the same measurements in both CATRA-enabled
and CATRA-disabled builds.

The existing `Catra*` C++ type names are retained for source compatibility.
They measure channel activity only; they do not calculate or apply `CW'`.
