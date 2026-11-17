# rc1 validation

Baseline source: `381913d6d843b1a49a17e405d7ab0473d75cf270`.
Network protocol: 41. Recorded hardware session: 17 November 2026.

The owner played six joined online games in a 1,457-second session, then
closed normally. No freeze, crash, or halt was recorded in that session.

| Map | Gameplay duration | Mean FPS | 1% low FPS |
| --- | ---: | ---: | ---: |
| Blood Gulch | 124 s | 50.2 | 24.8 |
| Longest | 177 s | 58.2 | 30.1 |
| Prisoner | 288 s | 58.3 | 30.1 |
| Longest | 289 s | 58.3 | 31.0 |
| Beaver Creek | 293 s | 59.3 | 46.0 |
| Prisoner | 168 s | 58.5 | 32.0 |

Across the run, the recorded five-second windows averaged 55.9 FPS; 81%
were at least 57 FPS and 6% were below 45 FPS. Window averages and per-map
gameplay summaries use different intervals, including transitions.

These results support using rc1 as this repository's starting point. They
do not qualify the entire campaign, every map, split screen, or prolonged
co-op. The newer protocol 44 merge had join failures and was not selected.
The owner reported that release7 would not allow a join and then power-cycled
the console; no new release7 log was available during repository preparation.

## Archived rc1 application checksums

These identify the existing tested application files, not a newly rebuilt
binary or a complete playable release bundle.

```text
408fb2b22d0ba3da7b2b3780781fbe6f1aeadff74ae69ade9675587a272a0d30  halo.wuhb
515d65a734aa5d5cc9b93030f6a8492f39bfcb519981e27cf1ce8ac823ecd635  halo.elf
```

Local evidence used for this summary: the rc1b trial logs and corrected
six-join report retained by the maintainer. Raw logs are not included here.
