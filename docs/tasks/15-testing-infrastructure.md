# Task 15: Testing Infrastructure

## Priority: P1
## Depends on: 01-project-setup
## Blocks: None (supports all other tasks)

## Description

Set up the testing infrastructure, patterns, and CI-ready test harness so all other tasks can write tests against a consistent framework.

## Acceptance Criteria

- [ ] **Test dependencies** configured in `Cargo.toml`:
  - `mockall` for trait-based mocking
  - `insta` for snapshot testing
  - `proptest` for property-based testing
- [ ] **Trait abstractions** for mockable system interfaces:
  - `SystemInfoProvider` trait wrapping `sysinfo::System` calls
  - `GpuProvider` trait wrapping NVML calls
  - Mock implementations generated via `mockall::automock`
- [ ] **Test helpers** module (`src/test_helpers.rs` or `tests/common/mod.rs`):
  - Factory functions for creating test `MetricsSnapshot` instances
  - Factory functions for creating test `Config` instances
  - Helper to create a mock `MetricsCollector`
- [ ] **Snapshot testing** setup:
  - `insta` configured with snapshot directory
  - Example snapshot test for default config
- [ ] **Property testing** examples:
  - Example `proptest` test for a metric conversion function
  - Strategies defined for generating valid metric values
- [ ] **CI readiness**:
  - All tests pass with `cargo test`
  - Tests don't depend on hardware (GPU, specific network interfaces)
  - Tests don't require elevated permissions

## Testing

- [ ] `cargo test` runs all tests successfully on a clean checkout
- [ ] Mock-based tests demonstrate the pattern for other tasks to follow

## Notes

- This task establishes patterns — other task implementers should follow these patterns
- Keep mocking minimal; only mock external system boundaries (sysinfo, NVML)
- Snapshot files should be committed to version control
- Consider `cargo-nextest` for faster parallel test execution
