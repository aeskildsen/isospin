---
summary: Test UGen of the isospin build checks
categories: UGens>Tests
---

## Description

A hand-written test UGen for the isospin build checks. It multiplies its input by a gain.

## Class methods

### *ar

| Argument | Text |
|---|---|
| `in` | The input. |
| `gain` | The gain. |

Returns: one channel.

## Examples

```supercollider
{ IsoFixture.ar(SinOsc.ar(440), 0.1) }.play;
```
