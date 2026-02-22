# Task 10c: Metric Panel Widget

## Priority: P1
## Depends on: 10a-gauge-widget, 10b-sparkline-widget
## Blocks: 13-integration

## Description

Implement the composite metric panel widget in `ui/panel.rs` that combines a visualization (gauge or sparkline) with a numeric value and label into a single cohesive panel.

## Acceptance Criteria

- [ ] `MetricPanel` widget:
  ```rust
  struct MetricPanel {
      label: &str,              // "CPU", "RAM", etc.
      primary_value: String,    // "42%", "10/16 GB", "72C"
      secondary_value: Option<String>, // "3.8 GHz", upload/download speeds
      gauge_value: Option<f32>, // 0.0–1.0 for gauge mode
      sparkline_history: Option<&[f32]>, // for sparkline mode
      color: Color32,           // accent color
      visualization: Visualization, // Gauges or Sparklines
  }
  ```
- [ ] **Gauge mode** layout:
  ```
  ┌──────────┐
  │  [gauge]  │
  │   42%     │
  │   CPU     │
  │  3.8GHz   │
  └──────────┘
  ```
- [ ] **Sparkline mode** layout:
  ```
  ┌──────────┐
  │[sparkline]│
  │   42%     │
  │   CPU     │
  │  3.8GHz   │
  └──────────┘
  ```
- [ ] Panel styling:
  - Rounded corners
  - Subtle border with accent color
  - Semi-transparent background
  - Consistent sizing across all panels
- [ ] Implements `egui::Widget` trait
- [ ] Adapts to different metric types:
  - CPU: gauge + percentage + frequency
  - RAM: gauge + percentage + used/total GB
  - GPU: gauge + percentage + temperature
  - Network: no gauge, upload + download speeds with arrows
  - Disk: no gauge, read + write speeds

## Testing

- [ ] Unit test: correct layout branch selected based on visualization mode
- [ ] Snapshot test (insta): panel layout structure for each metric type

## Notes

- Network and Disk panels don't have a natural 0–100% gauge; consider using sparklines for speed history or just show numeric values with arrows
- Use `egui::Frame` for the panel container with rounded corners
- Panel should have a fixed minimum size to maintain grid alignment
