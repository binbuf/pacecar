# Task 14: Polish, Optimization & Release Prep

## Priority: P2
## Depends on: 13-integration
## Blocks: None

## Description

Final polish pass: performance optimization, memory footprint verification, visual refinements, and release build configuration.

## Acceptance Criteria

- [ ] **Memory footprint**:
  - Verify < 15 MB working set at idle (design target)
  - Profile with Windows Task Manager or `cargo instruments`
  - Fix any leaks or excessive allocations
- [ ] **Performance**:
  - UI repaints only when new data arrives (not 60fps idle)
  - `ctx.request_repaint_after(Duration::from_millis(polling_interval))` used
  - No heap allocations in the render hot path
  - Sparkline ring buffers are fixed-size, pre-allocated
- [ ] **Visual polish**:
  - Verify appearance on Windows 11 with light and dark system themes
  - Ensure transparency looks correct over various backgrounds
  - Test at different DPI scales (100%, 125%, 150%, 200%)
  - Smooth gauge/sparkline rendering without artifacts
- [ ] **Release build**:
  - Verify `opt-level = "s"`, `lto = true`, `strip = true` are effective
  - Check final binary size
  - Test release build for correctness (optimized builds can behave differently)
- [ ] **Error resilience**:
  - No panics in production paths
  - All `unwrap()` calls audited and replaced with proper error handling
  - Logging for diagnostic purposes (consider `tracing` or `log` crate)
- [ ] **Windows-specific**:
  - No console window on startup (use `#![windows_subsystem = "windows"]`)
  - App icon set on the executable

## Testing

- [ ] Run full test suite in release mode
- [ ] Manual soak test: run for 30+ minutes, verify no memory growth
- [ ] Manual test: all features work in release build

## Notes

- `#![windows_subsystem = "windows"]` prevents the console window but also hides panic messages — consider a panic hook that shows a message box or writes to a log file
- Binary size can be further reduced with `cargo-bloat` analysis
- Consider creating a simple installer or portable zip for distribution
